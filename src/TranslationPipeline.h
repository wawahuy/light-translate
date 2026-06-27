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
#include "src/AppConfig.h"

#include "src/capture/ICaptureEngine.h"

// Forward declarations
class ITranslationOutput;

// Coordinates the capture -> OCR -> translate -> overlay pipeline.
class TranslationPipeline
{
public:
    TranslationPipeline();
    ~TranslationPipeline();

    // Bind system components.
    void SetComponents(ICaptureEngine* capture,
                       ITranslateProvider* client,
                       ITranslationOutput* overlay);

    // Set OCR Engine configuration.
    void SetOcrConfig(OcrType type, const std::wstring& detDir = L"", const std::wstring& recDir = L"", const std::wstring& langTag = L"");

    void SetDisplayMode(DisplayMode mode) { m_displayMode = mode; }

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

    // Set ROI configuration.
    void SetRoiConfig(bool active, int timeoutMs, RECT roiRect);

    // Pause or resume pipeline.
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused.load(); }
    bool IsRunning() const { return m_running.load(); }

    // Status callback for logging and UI updates.
    std::function<void(const std::wstring&)> OnStatus;

private:
    void SchedulerProc();
    void OcrProc();
    void NetworkProc();

    // Internal helpers
    bool InitializeOcrEngine();
    bool PerformOcr(const cv::Mat& frameMat, OcrResult& outOcrResult);

    ICaptureEngine*      m_capture = nullptr;
    ITranslateProvider*  m_client = nullptr;
    ITranslationOutput*  m_overlay = nullptr;

    std::thread          m_schedulerThread;
    std::thread          m_ocrThread;
    std::thread          m_networkThread;
    std::atomic<bool>    m_running{ false };
    std::atomic<bool>    m_shouldStop{ false };
    std::atomic<bool>    m_paused{ false };

    // Scheduler synchronization
    std::mutex              m_schedulerMutex;
    std::condition_variable m_schedulerCV;
    bool                    m_schedulerTriggered = false;

    std::atomic<int>     m_intervalMs{ 1000 };
    std::atomic<int>     m_scaleRoi{ 100 };
    DisplayMode          m_displayMode = DisplayMode::InPlace;

    // OCR Queue (Single-slot)
    struct PendingFrame {
        Frame frame;
        cv::Mat frameMat;
    };
    std::optional<PendingFrame> m_pendingOcr;
    std::mutex                  m_ocrMutex;
    std::condition_variable     m_ocrCV;
    std::atomic<bool>           m_ocrBusy{ false };

    // Translation Queue (Single-slot)
    struct PendingTranslate {
        std::wstring text;
        std::vector<std::vector<cv::Point2f>> boxes;
    };
    std::optional<PendingTranslate> m_pendingTranslate;
    std::mutex                      m_translateMutex;
    std::condition_variable         m_translateCV;

    // OCR configuration & engine
    OcrType              m_ocrType = OcrType::PaddleOCR;
    std::wstring         m_ocrDetModelDir;
    std::wstring         m_ocrRecModelDir;
    std::wstring         m_ocrLangTag = L"Default";
    std::unique_ptr<IOcrEngine> m_ocrEngine;
    BoxDiffDetector      m_boxDiffDetector;

    // History and caching
    std::wstring         m_lastOCRText;
    std::wstring         m_lastTranslationResult;
    std::vector<std::wstring> m_translationHistory;
    std::vector<std::vector<cv::Point2f>> m_lastBoxes;
    ULONGLONG            m_lastTranslateTime = 0;
    ULONGLONG            m_lastSeenTime = 0;

    // ROI Idle Text Detection fields
    bool                 m_roiActive = false;
    int                  m_roiTimeoutMs = 3000;
    RECT                 m_roiRect = { 0, 0, 0, 0 };
    bool                 m_roiDetectionActive = false;
    ULONGLONG            m_lastTextSeenTime = 0;
};
