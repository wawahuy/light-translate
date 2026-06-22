#include "src/TranslationPipeline.h"
#include "src/capture/ICaptureEngine.h"
#include "src/network/ITranslateProvider.h"
#include "src/utils/StringUtils.h"
#include "src/ui/ITranslationOutput.h"
#include "src/ocr/OcrFactory.h"
#include <opencv2/imgproc.hpp>
#include <chrono>

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

void TranslationPipeline::SetComponents(ICaptureEngine* capture,
                                         ITranslateProvider* client,
                                         ITranslationOutput* overlay)
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

void TranslationPipeline::SetRoiConfig(bool active, int timeoutMs, RECT roiRect)
{
    m_roiActive = active;
    m_roiTimeoutMs = timeoutMs;
    m_roiRect = roiRect;
}

bool TranslationPipeline::Start(int intervalMs)
{
    if (m_running.load()) return true;

    m_lastOCRText.clear();
    m_lastTranslationResult.clear();
    m_translationHistory.clear();
    m_lastTranslateTime = 0;
    m_lastSeenTime = 0;
    m_lastTextSeenTime = GetTickCount64();
    m_roiDetectionActive = false;

    m_intervalMs.store(intervalMs);
    m_shouldStop.store(false);
    m_paused.store(false);
    m_schedulerTriggered = false;
    m_boxDiffDetector.Reset();

    if (intervalMs > 0)
    {
        if (OnStatus) OnStatus(L"Pipeline starting with interval " + std::to_wstring(intervalMs) + L" ms...");
    }
    else
    {
        if (OnStatus) OnStatus(L"Pipeline starting in manual trigger mode...");
    }

    m_running.store(true);

    if (m_displayMode == DisplayMode::InPlace && m_capture && m_overlay)
    {
        RECT capRect = m_capture->GetCaptureRect();
        m_overlay->SetPosition(capRect.left, capRect.top);
        m_overlay->SetSize(capRect.right - capRect.left, capRect.bottom - capRect.top);
    }

    m_ocrBusy.store(false);
    m_networkThread   = std::thread(&TranslationPipeline::NetworkProc,   this);
    m_ocrThread       = std::thread(&TranslationPipeline::OcrProc,       this);
    m_schedulerThread = std::thread(&TranslationPipeline::SchedulerProc, this);

    return true;
}

void TranslationPipeline::Stop()
{
    if (!m_running.load()) return;

    m_shouldStop.store(true);

    // Wake up capture worker thread
    {
        std::lock_guard<std::mutex> lk(m_schedulerMutex);
        m_schedulerTriggered = true;
    }
    m_schedulerCV.notify_all();

    // Wake up OCR thread
    {
        std::lock_guard<std::mutex> lk(m_ocrMutex);
        m_pendingOcr.reset();
    }
    m_ocrCV.notify_all();

    // Wake up Network thread
    {
        std::lock_guard<std::mutex> lk(m_translateMutex);
        m_pendingTranslate.reset();
    }
    m_translateCV.notify_all();

    if (m_schedulerThread.joinable()) m_schedulerThread.join();
    if (m_ocrThread.joinable())       m_ocrThread.join();
    if (m_networkThread.joinable())   m_networkThread.join();

    m_running.store(false);
    
    if (m_ocrEngine)
    {
        m_ocrEngine->Reset();
        m_ocrEngine.reset();
    }
    m_roiDetectionActive = false;
    m_lastTextSeenTime = 0;
    m_boxDiffDetector.Reset();
}

void TranslationPipeline::SetIntervalMs(int ms)
{
    m_intervalMs.store(ms);
    {
        std::lock_guard<std::mutex> lk(m_schedulerMutex);
    }
    m_schedulerCV.notify_all();
}

void TranslationPipeline::SetScaleRoi(int scaleRoi)
{
    m_scaleRoi.store(scaleRoi);
}

void TranslationPipeline::TriggerOnce()
{
    {
        std::lock_guard<std::mutex> lk(m_schedulerMutex);
        m_schedulerTriggered = true;
    }
    m_schedulerCV.notify_all();
}

void TranslationPipeline::SetPaused(bool paused)
{
    m_paused.store(paused);
    if (OnStatus) OnStatus(paused ? L"Pipeline paused." : L"Pipeline resumed.");
}

void TranslationPipeline::SchedulerProc()
{
    if (OnStatus) OnStatus(L"Capture scheduler started.");

    while (!m_shouldStop.load())
    {
        {
            std::unique_lock<std::mutex> lk(m_schedulerMutex);
            int interval = m_intervalMs.load();
            if (interval > 0)
            {
                m_schedulerCV.wait_for(lk, std::chrono::milliseconds(interval), [&] {
                    return m_shouldStop.load() || m_schedulerTriggered;
                });
            }
            else
            {
                m_schedulerCV.wait(lk, [&] {
                    return m_shouldStop.load() || m_schedulerTriggered;
                });
            }

            if (m_shouldStop.load()) break;
            m_schedulerTriggered = false;
        }

        if (m_paused.load()) continue;

        // Drop the capture tick if OCR thread is currently busy
        if (m_ocrBusy.load())
        {
            if (OnStatus) OnStatus(L"Skipped capture tick (OCR busy)");
            continue;
        }

        if (!m_capture) continue;
        Frame frame;
        if (!m_capture->GetLatestFrame(frame)) continue;
        if (frame.data.empty())               continue;

        // Push frame to the single-slot queue (Zero-copy implementation)
        {
            std::lock_guard<std::mutex> lk(m_ocrMutex);
            PendingFrame pending;
            pending.frame = std::move(frame);

            int scalePct = m_scaleRoi.load();
            if (scalePct > 0 && scalePct != 100)
            {
                cv::Mat currentMat(pending.frame.height, pending.frame.width, CV_8UC4, pending.frame.data.data());
                double factor = scalePct / 100.0;
                cv::resize(currentMat, pending.frameMat, cv::Size(), factor, factor, cv::INTER_LINEAR);
            }
            else
            {
                pending.frameMat = cv::Mat(pending.frame.height, pending.frame.width, CV_8UC4, pending.frame.data.data());
            }
            m_pendingOcr = std::move(pending);
        }
        m_ocrCV.notify_one();

        if (OnStatus) OnStatus(L"Frame queued for OCR...");
    }
}

void TranslationPipeline::OcrProc()
{
    if (OnStatus) OnStatus(L"OCR worker thread started.");

    while (true)
    {
        PendingFrame pending;

        {
            std::unique_lock<std::mutex> lk(m_ocrMutex);
            m_ocrCV.wait(lk, [&]
            {
                return m_pendingOcr.has_value() || m_shouldStop.load();
            });

            if (m_shouldStop.load() && !m_pendingOcr.has_value())
                break;

            if (!m_pendingOcr.has_value()) continue;

            pending = std::move(*m_pendingOcr);
            m_pendingOcr.reset();
        }

        m_ocrBusy.store(true);

        if (InitializeOcrEngine())
        {
            cv::Mat preparedFrame = m_ocrEngine->PrepareFrame(pending.frameMat);
            OcrResult ocrResult;
            bool hasText = PerformOcr(preparedFrame, ocrResult);

            if (!hasText || ocrResult.empty())
            {
                m_boxDiffDetector.Reset();
                ULONGLONG now = GetTickCount64();
                ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;
                if (now - m_lastSeenTime > limit)
                {
                    m_translationHistory.clear();
                    m_lastOCRText.clear();
                    m_lastTranslationResult.clear();
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
            }
            else
            {
                m_lastSeenTime = GetTickCount64();
                std::wstring ocrText = Utf8ToWide(ocrResult.ConcatText());

                if (!ocrText.empty())
                {
                    if (ocrText == m_lastOCRText)
                    {
                        if (OnStatus) OnStatus(L"OCR text unchanged.");

                        // Update boxes position immediately if text is same but coordinates shifted
                        if (m_displayMode == DisplayMode::InPlace && m_overlay && !m_lastTranslationResult.empty())
                        {
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
                            m_overlay->SetInPlaceText(m_lastTranslationResult, overlayBoxes);
                        }
                    }
                    else
                    {
                        m_lastOCRText = ocrText;
                        if (OnStatus) OnStatus(L"New OCR text detected. Queueing translation...");

                        // Queue to translation thread
                        {
                            std::lock_guard<std::mutex> lk(m_translateMutex);
                            PendingTranslate req;
                            req.text = ocrText;
                            req.boxes = m_lastBoxes;
                            m_pendingTranslate = std::move(req);
                        }
                        m_translateCV.notify_one();
                    }
                }
            }
        }

        m_ocrBusy.store(false);
    }
}

void TranslationPipeline::NetworkProc()
{
    if (OnStatus) OnStatus(L"Translation network worker started.");

    while (true)
    {
        PendingTranslate req;

        {
            std::unique_lock<std::mutex> lk(m_translateMutex);
            m_translateCV.wait(lk, [&]
            {
                return m_pendingTranslate.has_value() || m_shouldStop.load();
            });

            if (m_shouldStop.load() && !m_pendingTranslate.has_value())
                break;

            if (!m_pendingTranslate.has_value()) continue;

            req = std::move(*m_pendingTranslate);
            m_pendingTranslate.reset();
        }

        if (!m_client) continue;

        if (OnStatus) OnStatus(L"Translating: " + req.text);

        try
        {
            std::wstring result = m_client->Translate(req.text);
            if (!result.empty())
            {
                m_lastTranslationResult = result;

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
                        overlayBoxes.reserve(req.boxes.size());
                        for (const auto& box : req.boxes)
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
                if (OnStatus) OnStatus(L"OK: " + result);
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
    // Handle ROI Detection transition if active
    if (m_roiActive)
    {
        if (m_roiDetectionActive)
        {
            double factor = 1.0;
            if (m_scaleRoi.load() > 0 && m_scaleRoi.load() != 100)
            {
                factor = m_scaleRoi.load() / 100.0;
            }

            int rx = static_cast<int>(m_roiRect.left * factor);
            int ry = static_cast<int>(m_roiRect.top * factor);
            int rw = static_cast<int>((m_roiRect.right - m_roiRect.left) * factor);
            int rh = static_cast<int>((m_roiRect.bottom - m_roiRect.top) * factor);

            cv::Rect cropRect(rx, ry, rw, rh);
            cropRect = cropRect & cv::Rect(0, 0, frameMat.cols, frameMat.rows);
            
            bool roiHasText = false;
            if (cropRect.width > 0 && cropRect.height > 0)
            {
                // Clone the cropped region to ensure a continuous memory layout for the OCR engines
                cv::Mat roiFrame = frameMat(cropRect).clone();
                if (m_ocrEngine->SupportsTwoPhase())
                {
                    DetectionResult detection = m_ocrEngine->Detect(roiFrame);
                    roiHasText = !detection.empty();
                }
                else
                {
                    OcrResult ocrResultRoi = m_ocrEngine->Recognize(roiFrame);
                    roiHasText = !ocrResultRoi.empty();
                }
            }
            
            if (roiHasText)
            {
                m_roiDetectionActive = false;
                m_lastTextSeenTime = GetTickCount64();
                if (OnStatus) OnStatus(L"Text detected in ROI. Deactivating ROI mode.");
                // Fall through to run full OCR on this tick
            }
            else
            {
                return false;
            }
        }
    }

    try
    {
        // 1. Check if box regions are unchanged (works for all engines)
        if (m_boxDiffDetector.HasSavedBoxes())
        {
            if (!m_boxDiffDetector.DetectChange(frameMat, 1.0))
            {
                m_lastSeenTime = GetTickCount64();
                if (m_roiActive)
                {
                    m_lastTextSeenTime = GetTickCount64();
                }
                if (OnStatus) OnStatus(L"Skipped (Box regions unchanged)");
                
                // Populate outOcrResult with cached boxes and text to prevent clearing screen
                outOcrResult.boxes = m_lastBoxes;
                outOcrResult.texts = { WideToUtf8(m_lastOCRText) };
                return true; 
            }
        }

        // 2. Perform recognition based on engine support
        bool hasText = false;
        if (m_ocrEngine->SupportsTwoPhase())
        {
            DetectionResult detection = m_ocrEngine->Detect(frameMat);
            if (!detection.empty())
            {
                m_boxDiffDetector.Update(frameMat, detection.boxes);
                m_lastBoxes = detection.boxes;
                outOcrResult = m_ocrEngine->Recognize(detection);
                hasText = !outOcrResult.empty();
            }
            else
            {
                m_lastBoxes.clear();
                m_boxDiffDetector.Reset();
            }
        }
        else
        {
            // Execute standard single-phase execution (e.g. Windows OCR)
            outOcrResult = m_ocrEngine->Recognize(frameMat);
            if (!outOcrResult.empty())
            {
                // Update BoxDiffDetector for single-phase engines by cropping the frame
                m_boxDiffDetector.Update(frameMat, outOcrResult.boxes);
                m_lastBoxes = outOcrResult.boxes;
                hasText = true;
            }
            else
            {
                m_lastBoxes.clear();
                m_boxDiffDetector.Reset();
            }
        }

        if (m_roiActive)
        {
            if (hasText)
            {
                m_lastTextSeenTime = GetTickCount64();
            }
            else
            {
                if (GetTickCount64() - m_lastTextSeenTime > static_cast<ULONGLONG>(m_roiTimeoutMs))
                {
                    m_roiDetectionActive = true;
                    m_boxDiffDetector.Reset();
                    if (OnStatus) OnStatus(L"Idle timeout exceeded. Activating ROI mode.");
                }
            }
        }

        return hasText;
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
}
