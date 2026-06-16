#pragma once
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <vector>
#include <functional>
#include <string>
#include <opencv2/core.hpp>
#include "src/ocr/IOcrEngine.h"
#include "src/ocr/BoxDiffDetector.h"
#include "src/network/ITranslateProvider.h"

// Forward declarations
class CaptureEngine;
class OverlayWindow;

// Coordinates the capture -> OCR -> translate -> overlay pipeline.
class TranslationPipeline
{
public:
    TranslationPipeline();
    ~TranslationPipeline();

    // Bind system components.
    void SetComponents(CaptureEngine* capture,
                       ITranslateProvider* client,
                       OverlayWindow* overlay);

    // Set OCR Engine configuration.
    void SetOcrConfig(OcrType type, const std::wstring& detDir = L"", const std::wstring& recDir = L"");

    // Start pipeline threads.
    bool Start(int intervalMs);

    // Stop pipeline threads.
    void Stop();

    // Change capture interval (Auto mode).
    void SetIntervalMs(int ms);

    // Trigger one manual capture (Hotkey/manual mode).
    void TriggerOnce();

    // Set scale percentage for capture region.
    void SetScaleRoi(int scaleRoi);

    // Pause or resume pipeline.
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused.load(); }
    bool IsRunning() const { return m_running.load(); }

    // Status callback for logging and UI updates.
    std::function<void(const std::wstring&)> OnStatus;

private:
    void SchedulerProc();
    void NetworkProc();

    // Internal helpers to reduce NetworkProc nesting and complexity
    void ProcessPendingFrame(cv::Mat frameMat);
    bool InitializeOcrEngine();
    bool PerformOcr(const cv::Mat& frameMat, OcrResult& outOcrResult);
    void TranslateAndShow(const OcrResult& ocrResult);

    CaptureEngine*       m_capture = nullptr;
    ITranslateProvider*  m_client = nullptr;
    OverlayWindow*       m_overlay = nullptr;

    std::thread          m_schedulerThread;
    std::thread          m_networkThread;
    std::atomic<bool>    m_running{ false };
    std::atomic<bool>    m_shouldStop{ false };
    std::atomic<bool>    m_paused{ false };

    // Waitable timer & events
    HANDLE               m_hTimer = nullptr;
    HANDLE               m_hStopEvent = nullptr;
    HANDLE               m_hTriggerEvent = nullptr;
    std::atomic<int>     m_intervalMs{ 1000 };
    std::atomic<int>     m_scaleRoi{ 100 };

    // Single-slot frame queue
    struct PendingFrame { cv::Mat frameMat; };
    std::optional<PendingFrame> m_pending;
    std::mutex                  m_queueMutex;
    std::condition_variable     m_queueCV;

    std::atomic<bool>    m_networkBusy{ false };

    // OCR configuration & engine
    OcrType              m_ocrType = OcrType::WindowsOCR;
    std::wstring         m_ocrDetModelDir;
    std::wstring         m_ocrRecModelDir;
    std::unique_ptr<IOcrEngine> m_ocrEngine;
    BoxDiffDetector      m_boxDiffDetector;

    // History and caching
    std::wstring         m_lastOCRText;
    std::vector<std::wstring> m_translationHistory;
    ULONGLONG            m_lastTranslateTime = 0;
    ULONGLONG            m_lastSeenTime = 0;
};
