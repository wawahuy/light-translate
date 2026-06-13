#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <vector>
#include <functional>
#include <string>
#include <cstdint>

// Forward declarations
class CaptureEngine;
class TextTranslateProvider;
class OverlayWindow;

/// Coordinates the capture -> hash -> encode -> HTTP -> overlay pipeline.
///
/// Thread model
/// -------------
///  Scheduler thread  - woken by a WaitableTimer at the configured interval.
///                      Reads the latest frame, checks xxHash, encodes JPEG,
///                      and pushes to the single-slot frame queue.
///
///  Network thread    - blocks on the queue; sends HTTP POST, updates overlay.
///
/// CPU-saving features
/// --------------------
///  - WaitableTimer (never polls with Sleep).
///  - xxHash dirty-frame detection (skip if screen unchanged).
///  - Single-slot queue: if network is busy the new frame is dropped, not queued.
///  - Network busy flag prevents scheduling new requests while one is running.
class Scheduler
{
public:
    Scheduler();
    ~Scheduler();

    void SetComponents(CaptureEngine* capture,
                       TextTranslateProvider* client,
                       OverlayWindow* overlay);

    /// Start both threads.  intervalMs = milliseconds between capture attempts.
    bool Start(int intervalMs);

    /// Stop both threads gracefully.
    void Stop();

    /// Change the timer period without restarting (thread-safe).
    void SetIntervalMs(int ms);

    bool IsRunning() const { return m_running.load(); }

    /// Status callback - invoked from the scheduler/network thread.
    /// Marshal to UI thread with PostMessage if you update UI controls.
    std::function<void(const std::wstring&)> OnStatus;

private:
    std::wstring MockOCR(const std::vector<uint8_t>& jpegData);
    void SchedulerProc();
    void NetworkProc();

    CaptureEngine*   m_capture  = nullptr;
    TextTranslateProvider* m_client   = nullptr;
    OverlayWindow*   m_overlay  = nullptr;

    std::thread       m_schedulerThread;
    std::thread       m_networkThread;
    std::atomic<bool> m_running   { false };
    std::atomic<bool> m_shouldStop{ false };

    // -- Waitable timer --------------------------------------------------------
    HANDLE            m_hTimer     = nullptr;
    HANDLE            m_hStopEvent = nullptr;       // signals threads to exit
    std::atomic<int>  m_intervalMs { 1000 };

    // -- Single-slot frame queue -----------------------------------------------
    struct PendingFrame { std::vector<uint8_t> jpeg; };
    std::optional<PendingFrame> m_pending;
    std::mutex                  m_queueMutex;
    std::condition_variable     m_queueCV;

    std::atomic<bool>  m_networkBusy{ false };
    uint64_t           m_lastHash = 0;          // dirty-frame detection
};
