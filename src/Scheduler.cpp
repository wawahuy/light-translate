#include "src/Scheduler.h"
#include "src/capture/CaptureEngine.h"
#include "src/network/TextTranslateProvider.h"
#include "src/utils/StringUtils.h"
#include "src/ui/OverlayWindow.h"
#include <opencv2/imgproc.hpp>

// Các hằng số WaitableTimer có thể chưa có trong MinGW-w64 cũ
#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
#  define CREATE_WAITABLE_TIMER_MANUAL_RESET    0x00000001UL
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#  define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002UL
#endif

// ── Constructor / Destructor ──────────────────────────────────────────────────

Scheduler::Scheduler() = default;

Scheduler::~Scheduler()
{
    Stop();
}

// ── Component binding ─────────────────────────────────────────────────────────

void Scheduler::SetComponents(CaptureEngine*   capture,
                               TextTranslateProvider* client,
                               OverlayWindow*   overlay)
{
    m_capture = capture;
    m_client  = client;
    m_overlay = overlay;
}

// ── Start / Stop ──────────────────────────────────────────────────────────────

bool Scheduler::Start(int intervalMs)
{
    if (m_running.load()) return true;

    wchar_t cwd[MAX_PATH] = { 0 };
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring baseDir = cwd;
    for (auto& c : baseDir)
    {
        if (c == L'\\') c = L'/';
    }
    if (!baseDir.empty() && baseDir.back() != L'/')
    {
        baseDir += L'/';
    }

    m_ocrDetModelDir = baseDir + L"models/PP-OCRv5_mobile_det_infer";
    m_ocrRecModelDir = baseDir + L"models/PP-OCRv5_mobile_rec_infer";
    m_lastOCRText.clear();
    m_translationHistory.clear();
    m_lastTranslateTime = 0;
    m_lastSeenTime = 0;

    m_intervalMs.store(intervalMs);
    m_shouldStop.store(false);
    m_paused.store(false);
    m_boxDiffDetector.Reset();

    // Manual-reset stop event (signalled on Stop())
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // Auto-reset trigger event (for TriggerOnce / Hotkey mode)
    m_hTriggerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (intervalMs > 0)
    {
        // Auto mode: use waitable timer
        m_hTimer = CreateWaitableTimerExW(
            nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
        if (!m_hTimer)
            m_hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

        if (!m_hTimer || !m_hStopEvent) return false;

        // Arm the timer: first fire after intervalMs, then period = intervalMs
        LARGE_INTEGER li{};
        li.QuadPart = -static_cast<LONGLONG>(intervalMs) * 10000LL;  // 100-ns units, relative
        SetWaitableTimer(m_hTimer, &li, intervalMs, nullptr, nullptr, FALSE);
        if (OnStatus) OnStatus(L"Scheduler starting with interval " + std::to_wstring(intervalMs) + L" ms...");
    }
    else
    {
        // Hotkey / manual mode: no timer, wait for TriggerOnce()
        m_hTimer = nullptr;
        if (OnStatus) OnStatus(L"Scheduler starting in manual trigger mode...");
    }

    m_running.store(true);

    m_networkThread   = std::thread(&Scheduler::NetworkProc,   this);
    m_schedulerThread = std::thread(&Scheduler::SchedulerProc, this);

    return true;
}

void Scheduler::Stop()
{
    if (!m_running.load()) return;

    m_shouldStop.store(true);

    // Wake up scheduler thread
    if (m_hStopEvent)    SetEvent(m_hStopEvent);
    if (m_hTriggerEvent) SetEvent(m_hTriggerEvent);

    // Wake up network thread
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
    m_ocrEngine.Reset();
    m_boxDiffDetector.Reset();
}

void Scheduler::SetIntervalMs(int ms)
{
    m_intervalMs.store(ms);
    if (m_running.load() && m_hTimer)
    {
        LARGE_INTEGER li{};
        li.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
        // Re-arm with the new period
        SetWaitableTimer(m_hTimer, &li, ms, nullptr, nullptr, FALSE);
    }
}

void Scheduler::TriggerOnce()
{
    if (m_hTriggerEvent)
        SetEvent(m_hTriggerEvent);
}

void Scheduler::SetPaused(bool paused)
{
    m_paused.store(paused);
    if (OnStatus) OnStatus(paused ? L"Scheduler paused." : L"Scheduler resumed.");
}

// ── Scheduler thread ──────────────────────────────────────────────────────────

void Scheduler::SchedulerProc()
{
    if (OnStatus) OnStatus(L"Scheduler started.");

    // Build the wait handle array depending on mode
    HANDLE handles[3];
    DWORD  handleCount;

    if (m_hTimer)
    {
        // Auto mode: timer + trigger + stop
        handles[0] = m_hTimer;
        handles[1] = m_hTriggerEvent;
        handles[2] = m_hStopEvent;
        handleCount = 3;
    }
    else
    {
        // Manual mode: trigger + stop only
        handles[0] = m_hTriggerEvent;
        handles[1] = m_hStopEvent;
        handleCount = 2;
    }

    while (!m_shouldStop.load())
    {
        DWORD res = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);

        // Check which handle fired
        DWORD idx = res - WAIT_OBJECT_0;
        if (idx >= handleCount) break;  // error

        // Stop event is always the last handle
        if (handles[idx] == m_hStopEvent) break;

        if (m_shouldStop.load()) break;

        if (m_paused.load())
        {
            continue;
        }

        // Drop this tick if network is still busy
        if (m_networkBusy.load())
        {
            if (OnStatus) OnStatus(L"Skipped (network busy)");
            continue;
        }

        // Get latest captured frame
        if (!m_capture) continue;
        Frame frame;
        if (!m_capture->GetLatestFrame(frame)) continue;
        if (frame.data.empty())               continue;

        // Convert BGRA Mat to BGR Mat for PaddleOCR
        cv::Mat currentMat(frame.height, frame.width, CV_8UC4, frame.data.data());
        cv::Mat bgrMat;
        cv::cvtColor(currentMat, bgrMat, cv::COLOR_BGRA2BGR);

        // Push to single-slot queue (replaces any pending frame)
        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_pending = PendingFrame{ std::move(bgrMat) };
        }
        m_queueCV.notify_one();

        if (OnStatus) OnStatus(L"Frame queued for processing...");
    }
}

// ── Network thread ────────────────────────────────────────────────────────────

void Scheduler::NetworkProc()
{
    while (true)
    {
        PendingFrame pending;

        // Block until a frame arrives or Stop() is called
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

        // ── Lazy-initialize OCR engine ────────────────────────────────────
        if (!m_ocrEngine.IsInitialized())
        {
            if (OnStatus) OnStatus(L"Initializing PaddleOCR modules...");
            try
            {
                if (!m_ocrEngine.Initialize(m_ocrDetModelDir, m_ocrRecModelDir))
                {
                    if (OnStatus) OnStatus(L"OCR Error: Model files not found in: " + m_ocrDetModelDir + L" or " + m_ocrRecModelDir);
                    m_networkBusy.store(false);
                    continue;
                }
                if (OnStatus) OnStatus(L"PaddleOCR modules initialized successfully.");
            }
            catch (const std::exception& e)
            {
                if (OnStatus) OnStatus(L"OCR Init Exception: " + Utf8ToWide(e.what()));
                m_networkBusy.store(false);
                continue;
            }
            catch (...)
            {
                if (OnStatus) OnStatus(L"OCR Init Unknown Exception occurred.");
                m_networkBusy.store(false);
                continue;
            }
        }

        // ── Detect + Diff + Recognize + Translate ────────────────────────
        try
        {
            bool shouldDetect = true;
            if (m_boxDiffDetector.HasSavedBoxes())
            {
                if (!m_boxDiffDetector.DetectChange(pending.frameMat, 1.0))
                {
                    shouldDetect = false;
                    m_lastSeenTime = GetTickCount64();
                    if (OnStatus) OnStatus(L"Skipped (Box regions unchanged)");
                }
            }

            if (shouldDetect)
            {
                // Phase 1: Detect text regions + crop
                DetectionResult detection = m_ocrEngine.Detect(pending.frameMat);

                // No text detected
                if (detection.empty())
                {
                    m_boxDiffDetector.Reset();
                    ULONGLONG now = GetTickCount64();
                    ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;
                    if (now - m_lastSeenTime > limit)
                    {
                        m_translationHistory.clear();
                        m_lastOCRText.clear();
                        if (m_overlay) m_overlay->SetText(L"");
                        if (OnStatus) OnStatus(L"Screen cleared (no text detected + timeout)");
                    }
                    else
                    {
                        if (OnStatus) OnStatus(L"No text detected, keeping overlay (timeout pending)");
                    }
                    m_networkBusy.store(false);
                    continue;
                }

                // Remember boxes and crops
                m_boxDiffDetector.Update(detection.boxes, detection.regionGrays);

                // Phase 2: Recognize text
                OcrResult ocrResult = m_ocrEngine.Recognize(detection);

                // Concatenate recognized text
                std::wstring ocrText = Utf8ToWide(ocrResult.ConcatText());

                // Check for empty text
                if (ocrText.empty())
                {
                    ULONGLONG now = GetTickCount64();
                    ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;
                    if (now - m_lastSeenTime > limit)
                    {
                        m_translationHistory.clear();
                        m_lastOCRText.clear();
                        if (m_overlay) m_overlay->SetText(L"");
                        if (OnStatus) OnStatus(L"Screen cleared (no text recognized + timeout)");
                    }
                    m_networkBusy.store(false);
                    continue;
                }

                // Update last seen timestamp since we successfully detected text
                m_lastSeenTime = GetTickCount64();

                // Translate if text changed
                if (ocrText == m_lastOCRText)
                {
                    if (OnStatus) OnStatus(L"Skipped (OCR text unchanged)");
                }
                else
                {
                    m_lastOCRText = ocrText;
                    if (m_client)
                    {
                        if (OnStatus) OnStatus(L"Translating: " + ocrText);
                        std::wstring result = m_client->Translate(ocrText);
                        if (!result.empty())
                        {
                            ULONGLONG now = GetTickCount64();
                            ULONGLONG limit = static_cast<ULONGLONG>(m_intervalMs.load()) + 1000;

                            // Only stack history if the previous translation was replaced too quickly
                            if (m_lastTranslateTime > 0 && (now - m_lastTranslateTime > limit))
                            {
                                m_translationHistory.clear();
                            }
                            m_lastTranslateTime = now;

                            // Add to history, keep max 3 lines
                            m_translationHistory.push_back(result);
                            if (m_translationHistory.size() > 3)
                            {
                                m_translationHistory.erase(m_translationHistory.begin());
                            }

                            // Join history with newlines
                            std::wstring joinedText;
                            for (size_t i = 0; i < m_translationHistory.size(); ++i)
                            {
                                if (i > 0) joinedText += L"\n";
                                joinedText += m_translationHistory[i];
                            }

                            if (m_overlay) m_overlay->SetText(joinedText);
                            if (OnStatus)  OnStatus(L"OK: " + result);
                        }
                        else
                        {
                            if (OnStatus) OnStatus(L"API error: " + m_client->GetLastError());
                        }
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            if (OnStatus) OnStatus(L"OCR/Translation Exception: " + Utf8ToWide(e.what()));
        }
        catch (...)
        {
            if (OnStatus) OnStatus(L"OCR/Translation Unknown Exception occurred.");
        }

        m_networkBusy.store(false);
    }
}
