#pragma once
#include <windows.h>
#include <string>
#include <memory>
#include <functional>
#include <opencv2/core.hpp>
#include "src/AppConfig.h"
#include "src/capture/ICaptureEngine.h"
#include "src/ui/ITranslationOutput.h"
#include "src/network/ITranslateProvider.h"
#include "src/ocr/IOcrEngine.h"
#include "src/TranslationPipeline.h"

class AppController
{
public:
    AppController();
    ~AppController();

    // Settings management
    bool LoadSettings(const std::wstring& iniPath);
    void SaveSettings(const std::wstring& iniPath);

    // Start/Stop the pipeline
    bool Start(HWND hwndOwner, ITranslationOutput* overlay);
    void Stop();

    // Trigger one manual capture frame
    void TriggerOnce();

    // Pause/Resume pipeline
    void SetPaused(bool paused);
    bool IsPaused() const;
    bool IsRunning() const { return m_running; }
    TranslationPipeline& GetPipeline() { return m_pipeline; }

    // Configuration Accessors
    AppConfig& GetConfig() { return m_config; }
    const AppConfig& GetConfig() const { return m_config; }

    // Process a region screen capture
    std::wstring PerformRegionCaptureAndTranslate(const cv::Mat& regionMat, std::wstring& outOcrText);

    // Status logger callback
    std::function<void(const std::wstring&)> OnStatus;

    // Get last error string
    std::wstring GetLastError() const { return m_lastError; }

    // Reset region OCR so it can reinitialize if type changes
    void ResetRegionOcr() { m_regionOcr.reset(); }

private:
    AppConfig                           m_config;
    std::unique_ptr<ICaptureEngine>     m_capture;
    std::unique_ptr<ITranslateProvider> m_client;
    std::unique_ptr<IOcrEngine>         m_regionOcr;
    std::unique_ptr<ITranslateProvider> m_regionClient;
    TranslateProvider                   m_regionClientType = static_cast<TranslateProvider>(-1);
    TranslationPipeline                 m_pipeline;
    bool                                m_running = false;
    std::wstring                        m_lastError;
    TranslateProvider                   m_clientType = static_cast<TranslateProvider>(-1);
};
