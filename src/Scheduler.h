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
#include <opencv2/core.hpp>
#include "src/ocr/OcrEngine.h"
#include "src/ocr/BoxDiffDetector.h"

// Forward declarations
class CaptureEngine;
class TextTranslateProvider;
class OverlayWindow;

/// Coordinates the capture -> OCR -> translate -> overlay pipeline.
///
/// Thread model
/// -------------
///  Scheduler thread  - woken by a WaitableTimer at the configured interval.
///                      Reads the latest frame, converts to BGR,
///                      and pushes to the single-slot frame queue.
///
///  Network thread    - blocks on the queue; runs OCR, translates, updates overlay.
///
/// CPU-saving features
/// --------------------
///  - WaitableTimer (never polls with Sleep).
///  - Text-region diff detection (skip if screen text unchanged).
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

    /// Trigger a single capture cycle (used in Hotkey mode).
    void TriggerOnce();

    /// Set the Scale ROI factor (in percentage)
    void SetScaleRoi(int scaleRoi);

    /// Pause/Resume the scheduler (used in Auto mode).
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused.load(); }

    bool IsRunning() const { return m_running.load(); }

    /// Status callback - invoked from the scheduler/network thread.
    /// Marshal to UI thread with PostMessage if you update UI controls.
    std::function<void(const std::wstring&)> OnStatus;

private:

    void SchedulerProc();
    void NetworkProc();

    CaptureEngine*   m_capture  = nullptr;
    TextTranslateProvider* m_client   = nullptr;
    OverlayWindow*   m_overlay  = nullptr;

    std::thread       m_schedulerThread;
    std::thread       m_networkThread;
    std::atomic<bool> m_running   { false };
    std::atomic<bool> m_shouldStop{ false };
    std::atomic<bool> m_paused    { false };

    // -- Waitable timer --------------------------------------------------------
    HANDLE            m_hTimer     = nullptr;
    HANDLE            m_hStopEvent = nullptr;       // signals threads to exit
    HANDLE            m_hTriggerEvent = nullptr;    // manual trigger event (Hotkey mode)
    std::atomic<int>  m_intervalMs { 1000 };
    std::atomic<int>  m_scaleRoi { 100 };

    // -- Single-slot frame queue -----------------------------------------------
    struct PendingFrame { cv::Mat frameMat; };
    std::optional<PendingFrame> m_pending;
    std::mutex                  m_queueMutex;
    std::condition_variable     m_queueCV;

    std::atomic<bool>  m_networkBusy{ false };

    // -- OCR pipeline ----------------------------------------------------------
    OcrEngine         m_ocrEngine;
    BoxDiffDetector   m_boxDiffDetector;
    std::wstring      m_ocrDetModelDir;
    std::wstring      m_ocrRecModelDir;
    std::wstring      m_lastOCRText;
    std::vector<std::wstring> m_translationHistory;
    ULONGLONG         m_lastTranslateTime = 0;
    ULONGLONG         m_lastSeenTime = 0;
};
