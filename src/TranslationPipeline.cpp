#include "src/TranslationPipeline.h"
#include "src/capture/CaptureEngine.h"
#include "src/network/ITranslateProvider.h"
#include "src/utils/StringUtils.h"
#include "src/ui/OverlayWindow.h"
#include "src/ocr/OcrFactory.h"
#include <opencv2/imgproc.hpp>

#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
#  define CREATE_WAITABLE_TIMER_MANUAL_RESET    0x00000001UL
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#  define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002UL
#endif

TranslationPipeline::TranslationPipeline() = default;

TranslationPipeline::~TranslationPipeline()
{
    Stop();
}

void TranslationPipeline::SetComponents(CaptureEngine* capture,
                                         ITranslateProvider* client,
                                         OverlayWindow* overlay)
{
    m_capture = capture;
    m_client  = client;
    m_overlay = overlay;
}

void TranslationPipeline::SetOcrConfig(OcrType type, const std::wstring& detDir, const std::wstring& recDir)
{
    m_ocrType = type;
    m_ocrDetModelDir = detDir;
    m_ocrRecModelDir = recDir;
}

bool TranslationPipeline::Start(int intervalMs)
{
    if (m_running.load()) return true;

    m_lastOCRText.clear();
    m_translationHistory.clear();
    m_lastTranslateTime = 0;
    m_lastSeenTime = 0;

    m_intervalMs.store(intervalMs);
    m_shouldStop.store(false);
    m_paused.store(false);
    m_boxDiffDetector.Reset();

    // Event used to signal thread stop
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // Event used to trigger manual capture cycles
    m_hTriggerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (intervalMs > 0)
    {
        // Continuous auto mode
        m_hTimer = CreateWaitableTimerExW(
            nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
        if (!m_hTimer)
            m_hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

        if (!m_hTimer || !m_hStopEvent) return false;

        LARGE_INTEGER li{};
        li.QuadPart = -static_cast<LONGLONG>(intervalMs) * 10000LL;
        SetWaitableTimer(m_hTimer, &li, intervalMs, nullptr, nullptr, FALSE);
        if (OnStatus) OnStatus(L"Pipeline starting with interval " + std::to_wstring(intervalMs) + L" ms...");
    }
    else
    {
        // Hotkey / manual trigger mode
        m_hTimer = nullptr;
        if (OnStatus) OnStatus(L"Pipeline starting in manual trigger mode...");
    }

    m_running.store(true);

    if (m_displayMode == DisplayMode::InPlace && m_capture && m_overlay)
    {
        RECT capRect = m_capture->GetCaptureRect();
        m_overlay->SetPosition(capRect.left, capRect.top);
        m_overlay->SetSize(capRect.right - capRect.left, capRect.bottom - capRect.top);
    }

    m_networkThread   = std::thread(&TranslationPipeline::NetworkProc,   this);
    m_schedulerThread = std::thread(&TranslationPipeline::SchedulerProc, this);

    return true;
}

void TranslationPipeline::Stop()
{
    if (!m_running.load()) return;

    m_shouldStop.store(true);

    // Wake up capture worker thread
    if (m_hStopEvent)    SetEvent(m_hStopEvent);
    if (m_hTriggerEvent) SetEvent(m_hTriggerEvent);

    // Wake up network queue consumer thread
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_pending.reset();
    }
    m_queueCV.notify_all();

    if (m_schedulerThread.joinable()) m_schedulerThread.join();
    if (m_networkThread.joinable())   m_networkThread.join();

    if (m_hTimer)        { CloseHandle(m_hTimer);        m_hTimer        = nullptr; }
    if (m_hStopEvent)    { CloseHandle(m_hStopEvent);    m_hStopEvent    = nullptr; }
    if (m_hTriggerEvent) { CloseHandle(m_hTriggerEvent); m_hTriggerEvent = nullptr; }

    m_running.store(false);
    
    if (m_ocrEngine)
    {
        m_ocrEngine->Reset();
        m_ocrEngine.reset();
    }
    m_boxDiffDetector.Reset();
}

void TranslationPipeline::SetIntervalMs(int ms)
{
    m_intervalMs.store(ms);
    if (m_running.load() && m_hTimer)
    {
        LARGE_INTEGER li{};
        li.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
        SetWaitableTimer(m_hTimer, &li, ms, nullptr, nullptr, FALSE);
    }
}

void TranslationPipeline::SetScaleRoi(int scaleRoi)
{
    m_scaleRoi.store(scaleRoi);
}

void TranslationPipeline::TriggerOnce()
{
    if (m_hTriggerEvent)
        SetEvent(m_hTriggerEvent);
}

void TranslationPipeline::SetPaused(bool paused)
{
    m_paused.store(paused);
    if (OnStatus) OnStatus(paused ? L"Pipeline paused." : L"Pipeline resumed.");
}

void TranslationPipeline::SchedulerProc()
{
    if (OnStatus) OnStatus(L"Capture scheduler started.");

    HANDLE handles[3];
    DWORD  handleCount;

    if (m_hTimer)
    {
        handles[0] = m_hTimer;
        handles[1] = m_hTriggerEvent;
        handles[2] = m_hStopEvent;
        handleCount = 3;
    }
    else
    {
        handles[0] = m_hTriggerEvent;
        handles[1] = m_hStopEvent;
        handleCount = 2;
    }

    while (!m_shouldStop.load())
    {
        DWORD res = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);
        DWORD idx = res - WAIT_OBJECT_0;
        if (idx >= handleCount) break;

        if (handles[idx] == m_hStopEvent) break;
        if (m_shouldStop.load()) break;
        if (m_paused.load()) continue;

        // Drop the capture tick if network thread is currently busy
        if (m_networkBusy.load())
        {
            if (OnStatus) OnStatus(L"Skipped capture tick (network busy)");
            continue;
        }

        if (!m_capture) continue;
        Frame frame;
        if (!m_capture->GetLatestFrame(frame)) continue;
        if (frame.data.empty())               continue;

        // Wrap raw BGRA frame pixels in a cv::Mat and clone to own the buffer
        cv::Mat currentMat(frame.height, frame.width, CV_8UC4, frame.data.data());
        cv::Mat frameMat = currentMat.clone();

        // Crop/scale capture ROI if requested
        int scalePct = m_scaleRoi.load();
        if (scalePct > 0 && scalePct != 100)
        {
            double factor = scalePct / 100.0;
            cv::Mat scaledMat;
            cv::resize(frameMat, scaledMat, cv::Size(), factor, factor, cv::INTER_LINEAR);
            frameMat = std::move(scaledMat);
        }

        // Push frame to the single-slot queue
        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_pending = PendingFrame{ std::move(frameMat) };
        }
        m_queueCV.notify_one();

        if (OnStatus) OnStatus(L"Frame queued for processing...");
    }
}

void TranslationPipeline::NetworkProc()
{
    while (true)
    {
        PendingFrame pending;

        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCV.wait(lk, [&]
            {
                return m_pending.has_value() || m_shouldStop.load();
            });

            if (m_shouldStop.load() && !m_pending.has_value())
                break;

            if (!m_pending.has_value()) continue;

            pending = std::move(*m_pending);
            m_pending.reset();
        }

        m_networkBusy.store(true);
        ProcessPendingFrame(std::move(pending.frameMat));
        m_networkBusy.store(false);
    }
}

void TranslationPipeline::ProcessPendingFrame(cv::Mat frameMat)
{
    if (!InitializeOcrEngine())
    {
        return;
    }

    cv::Mat preparedFrame = m_ocrEngine->PrepareFrame(frameMat);

    OcrResult ocrResult;
    bool hasText = PerformOcr(preparedFrame, ocrResult);

    if (!hasText || ocrResult.empty())
    {
        // Cleanup overlay display if text is missing and timeout is exceeded
        m_boxDiffDetector.Reset();
        ULONGLONG now = GetTickCount64();
        ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;
        if (now - m_lastSeenTime > limit)
        {
            m_translationHistory.clear();
            m_lastOCRText.clear();
            m_lastBoxes.clear();
            if (m_overlay)
            {
                if (m_displayMode == DisplayMode::InPlace)
                    m_overlay->SetInPlaceText(L"", {});
                else
                    m_overlay->SetText(L"");
            }
            if (OnStatus) OnStatus(L"Screen cleared (no text detected + timeout)");
        }
        else
        {
            if (OnStatus) OnStatus(L"No text detected, keeping overlay (timeout pending)");
        }
        return;
    }

    TranslateAndShow(ocrResult);
}

bool TranslationPipeline::InitializeOcrEngine()
{
    if (m_ocrEngine && m_ocrEngine->IsInitialized())
    {
        return true;
    }

    if (!m_ocrEngine)
    {
        m_ocrEngine = OcrFactory::CreateEngine(m_ocrType, m_ocrDetModelDir, m_ocrRecModelDir);
    }

    if (OnStatus) OnStatus(L"Initializing OCR engine...");

    try
    {
        if (!m_ocrEngine->Initialize())
        {
            if (OnStatus) OnStatus(L"OCR Error: Initialization failed.");
            m_ocrEngine.reset();
            return false;
        }
        if (OnStatus) OnStatus(L"OCR engine initialized successfully.");
    }
    catch (const std::exception& e)
    {
        if (OnStatus) OnStatus(L"OCR Init Exception: " + Utf8ToWide(e.what()));
        m_ocrEngine.reset();
        return false;
    }
    catch (...)
    {
        if (OnStatus) OnStatus(L"OCR Init Unknown Exception occurred.");
        m_ocrEngine.reset();
        return false;
    }

    return true;
}

bool TranslationPipeline::PerformOcr(const cv::Mat& frameMat, OcrResult& outOcrResult)
{
    try
    {
        // 1. Check if box regions are unchanged (works for all engines)
        if (m_boxDiffDetector.HasSavedBoxes())
        {
            if (!m_boxDiffDetector.DetectChange(frameMat, 1.0))
            {
                m_lastSeenTime = GetTickCount64();
                if (OnStatus) OnStatus(L"Skipped (Box regions unchanged)");
                
                // Populate outOcrResult with cached boxes and text to prevent clearing screen
                outOcrResult.boxes = m_lastBoxes;
                outOcrResult.texts = { WideToUtf8(m_lastOCRText) };
                return true; 
            }
        }

        // 2. Perform recognition based on engine support
        if (m_ocrEngine->SupportsTwoPhase())
        {
            DetectionResult detection = m_ocrEngine->Detect(frameMat);
            if (detection.empty())
            {
                m_lastBoxes.clear();
                m_boxDiffDetector.Reset();
                return false;
            }

            m_boxDiffDetector.Update(detection.boxes, detection.regionGrays);
            m_lastBoxes = detection.boxes;
            outOcrResult = m_ocrEngine->Recognize(detection);
        }
        else
        {
            // Execute standard single-phase execution (e.g. Windows OCR)
            outOcrResult = m_ocrEngine->Recognize(frameMat);
            if (outOcrResult.empty())
            {
                m_lastBoxes.clear();
                m_boxDiffDetector.Reset();
                return false;
            }

            // Update BoxDiffDetector for single-phase engines by cropping the frame
            m_boxDiffDetector.Update(frameMat, outOcrResult.boxes);
            m_lastBoxes = outOcrResult.boxes;
        }
    }
    catch (const std::exception& e)
    {
        if (OnStatus) OnStatus(L"OCR Processing Exception: " + Utf8ToWide(e.what()));
        return false;
    }
    catch (...)
    {
        if (OnStatus) OnStatus(L"OCR Processing Unknown Exception occurred.");
        return false;
    }

    return !outOcrResult.empty();
}

void TranslationPipeline::TranslateAndShow(const OcrResult& ocrResult)
{
    m_lastSeenTime = GetTickCount64();
    std::wstring ocrText = Utf8ToWide(ocrResult.ConcatText());

    if (ocrText.empty())
    {
        return;
    }

    if (ocrText == m_lastOCRText)
    {
        if (OnStatus) OnStatus(L"Skipped translation (OCR text unchanged)");
        return;
    }

    m_lastOCRText = ocrText;

    if (!m_client)
    {
        return;
    }

    if (OnStatus) OnStatus(L"Translating: " + ocrText);
    
    try
    {
        std::wstring result = m_client->Translate(ocrText);
        if (!result.empty())
        {
            ULONGLONG now = GetTickCount64();
            ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;

            if (m_lastTranslateTime > 0 && (now - m_lastTranslateTime > limit))
            {
                m_translationHistory.clear();
            }
            m_lastTranslateTime = now;

            m_translationHistory.push_back(result);
            if (m_translationHistory.size() > 3)
            {
                m_translationHistory.erase(m_translationHistory.begin());
            }

            std::wstring joinedText;
            for (size_t i = 0; i < m_translationHistory.size(); ++i)
            {
                if (i > 0) joinedText += L"\n";
                joinedText += m_translationHistory[i];
            }

            if (m_overlay)
            {
                if (m_displayMode == DisplayMode::InPlace)
                {
                    // Scale coordinates back to original frame ROI size
                    double factor = m_scaleRoi.load() / 100.0;
                    double invFactor = (factor > 0.0) ? (1.0 / factor) : 1.0;

                    std::vector<std::vector<Point2F>> overlayBoxes;
                    overlayBoxes.reserve(m_lastBoxes.size());
                    for (const auto& box : m_lastBoxes)
                    {
                        std::vector<Point2F> overlayBox;
                        overlayBox.reserve(box.size());
                        for (const auto& pt : box)
                        {
                            overlayBox.push_back({
                                static_cast<float>(pt.x * invFactor),
                                static_cast<float>(pt.y * invFactor)
                            });
                        }
                        overlayBoxes.push_back(std::move(overlayBox));
                    }
                    m_overlay->SetInPlaceText(result, overlayBoxes);
                }
                else
                {
                    m_overlay->SetText(joinedText);
                }
            }
            if (OnStatus)  OnStatus(L"OK: " + result);
        }
        else
        {
            if (OnStatus) OnStatus(L"API error: " + m_client->GetLastError());
        }
    }
    catch (const std::exception& e)
    {
        if (OnStatus) OnStatus(L"Translation Exception: " + Utf8ToWide(e.what()));
    }
    catch (...)
    {
        if (OnStatus) OnStatus(L"Translation Unknown Exception occurred.");
    }
}
