#include "src/Scheduler.h"
#include "src/capture/CaptureEngine.h"

// Các hằng số WaitableTimer có thể chưa có trong MinGW-w64 cũ
#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
#  define CREATE_WAITABLE_TIMER_MANUAL_RESET    0x00000001UL
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#  define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002UL
#endif
#include "src/network/TextTranslateProvider.h"
#include "overlay/OverlayWindow.h"
#include "utils/ImageEncoder.h"
#include "utils/Hash.h"

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

    m_intervalMs.store(intervalMs);
    m_shouldStop.store(false);
    m_lastHash = 0;

    // Manual-reset stop event (signalled on Stop())
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // High-resolution waitable timer (falls back to standard on older Windows)
    // m_hTimer = CreateWaitableTimerExW(
    //     nullptr, nullptr,
    //     CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
    //     TIMER_ALL_ACCESS);
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
    if (m_hStopEvent) SetEvent(m_hStopEvent);

    // Wake up network thread
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_pending.reset();
    }
    m_queueCV.notify_all();

    if (m_schedulerThread.joinable()) m_schedulerThread.join();
    if (m_networkThread.joinable())   m_networkThread.join();

    if (m_hTimer)     { CloseHandle(m_hTimer);     m_hTimer     = nullptr; }
    if (m_hStopEvent) { CloseHandle(m_hStopEvent); m_hStopEvent = nullptr; }

    m_running.store(false);
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

// ── Scheduler thread ──────────────────────────────────────────────────────────

void Scheduler::SchedulerProc()
{
    const HANDLE handles[2] = { m_hTimer, m_hStopEvent };

    if (OnStatus) OnStatus(L"Scheduler started.");
    while (!m_shouldStop.load())
    {
        // Wait for timer OR stop event
        DWORD res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (res != WAIT_OBJECT_0)
            break;  // stop event fired, or error

        if (m_shouldStop.load()) break;

        // Waitable timer is manual-reset – reset it immediately so the period
        // continues from this point rather than the arming point.
        // ResetEvent(m_hTimer);

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

        // // Skip if frame is identical to last one (dirty-frame detection)
        // uint64_t hash = XXHash64(frame.data.data(), frame.data.size());
        // if (hash == m_lastHash)
        // {
        //     if (OnStatus) OnStatus(L"Skipped (frame unchanged)");
        //     continue;
        // }
        // m_lastHash = hash;

        // Encode ROI to JPEG at quality 75 %
        std::vector<uint8_t> jpeg;
        if (!EncodeBGRAtoJPEG(frame.data.data(), frame.width, frame.height, 75, jpeg))
        {
            if (OnStatus) OnStatus(L"JPEG encoding failed");
            continue;
        }
        // if (OnStatus) OnStatus(L"Frame captured and encoded (" +
        //     std::to_wstring(jpeg.size() / 1024) + L" KB)");

        // Push to single-slot queue (replaces any pending frame)
        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_pending = PendingFrame{ std::move(jpeg) };
        }
        m_queueCV.notify_one();

        if (OnStatus) OnStatus(L"Frame queued for translation...");
    }
}

std::wstring Scheduler::MockOCR(const std::vector<uint8_t>& jpegData)
{
    // Placeholder function for OCR
    // In the future, this will use PaddleOCR to perform OCR on the image.
    // For now, return a mock string to translate.
    return L"Hello world! This is a test message to translate.";
}

// -- Network thread ------------------------------------------------------------

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

        if (m_client)
        {
            std::wstring ocrText = MockOCR(pending.jpeg);
            std::wstring result = m_client->Translate(ocrText);
            if (!result.empty())
            {
                // Update overlay (SetText is thread-safe – uses PostMessage internally)
                if (m_overlay) m_overlay->SetText(result);
                if (OnStatus)  OnStatus(L"OK: " + result);
            }
            else
            {
                if (OnStatus)
                    OnStatus(L"API error: " + m_client->GetLastError());
            }
        }

        m_networkBusy.store(false);
    }
}
