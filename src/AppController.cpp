#include "src/AppController.h"
#include "src/capture/CaptureEngineFactory.h"
#include "src/network/TranslateProviderFactory.h"
#include "src/ocr/OcrFactory.h"
#include "src/utils/StringUtils.h"

AppController::AppController() = default;

AppController::~AppController()
{
    Stop();
}

bool AppController::LoadSettings(const std::wstring& iniPath)
{
    return m_config.Load(iniPath);
}

void AppController::SaveSettings(const std::wstring& iniPath)
{
    m_config.Save(iniPath);
}

bool AppController::Start(HWND hwndOwner, ITranslationOutput* overlay)
{
    if (m_running) return true;

    m_lastError.clear();

    // 1. Initialize Capture Engine
    m_capture = CaptureEngineFactory::CreateEngine(CaptureType::DXGI);
    if (!m_capture)
    {
        m_lastError = L"Failed to create Capture Engine.";
        return false;
    }

    if (!m_capture->Initialize(m_config.monitorIndex))
    {
        m_lastError = L"CaptureEngine initialization failed: " + m_capture->GetLastError();
        m_capture.reset();
        return false;
    }

    m_capture->SetCaptureRect(m_config.captureRect);
    if (!m_capture->Start())
    {
        m_lastError = L"Failed to start Capture Engine.";
        m_capture->Shutdown();
        m_capture.reset();
        return false;
    }

    // 2. Initialize translation provider
    m_client = TranslateProviderFactory::CreateProvider(m_config.providerType);
    m_clientType = m_config.providerType;
    if (m_client)
    {
        m_client->SetApiKey(m_config.apiKey);
        m_client->SetApiModel(m_config.apiModel);
        m_client->SetTargetLanguage(m_config.targetLanguage);
    }

    // 3. Connect pipeline components
    m_pipeline.SetComponents(m_capture.get(), m_client.get(), overlay);
    m_pipeline.SetDisplayMode(m_config.displayMode);
    m_pipeline.SetRoiConfig(m_config.roiActive, m_config.roiTimeoutMs, m_config.roiRect);
    m_pipeline.OnStatus = OnStatus;

    // 4. Resolve OCR model paths and configure OCR type
    {
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
        std::wstring detModelDir = baseDir + L"models/PP-OCRv5_mobile_det_infer";
        std::wstring recModelDir = baseDir + L"models/PP-OCRv5_mobile_rec_infer";
        m_pipeline.SetOcrConfig(m_config.ocrType, detModelDir, recModelDir);
    }

    m_pipeline.SetScaleRoi(m_config.scaleRoi);

    // 5. Start pipeline thread
    if (m_config.captureMode == CaptureMode::Auto)
    {
        m_pipeline.Start(m_config.GetIntervalMs());
    }
    else
    {
        m_pipeline.Start(0); // manual hotkey mode
    }

    m_running = true;
    return true;
}

void AppController::Stop()
{
    if (!m_running) return;

    m_pipeline.Stop();

    if (m_capture)
    {
        m_capture->Stop();
        m_capture->Shutdown();
        m_capture.reset();
    }

    m_client.reset();
    m_clientType = static_cast<TranslateProvider>(-1);
    m_regionClient.reset();
    m_regionClientType = static_cast<TranslateProvider>(-1);
    m_running = false;
}

void AppController::TriggerOnce()
{
    m_pipeline.TriggerOnce();
}

void AppController::SetPaused(bool paused)
{
    m_pipeline.SetPaused(paused);
}

bool AppController::IsPaused() const
{
    return m_pipeline.IsPaused();
}

std::wstring AppController::PerformRegionCaptureAndTranslate(const cv::Mat& regionMat, std::wstring& outOcrText)
{
    outOcrText.clear();

    // Lazy-initialize OCR engine for Region translation
    if (!m_regionOcr || !m_regionOcr->IsInitialized())
    {
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
        std::wstring detModelDir = baseDir + L"models/PP-OCRv5_mobile_det_infer";
        std::wstring recModelDir = baseDir + L"models/PP-OCRv5_mobile_rec_infer";

        if (OnStatus) OnStatus(L"Initializing OCR modules for region selection...");
        m_regionOcr = OcrFactory::CreateEngine(m_config.ocrType, detModelDir, recModelDir);
        if (!m_regionOcr || !m_regionOcr->Initialize())
        {
            if (OnStatus) OnStatus(L"Region OCR Error: Failed to initialize models.");
            m_regionOcr.reset();
            return L"";
        }
        if (OnStatus) OnStatus(L"Region OCR modules initialized successfully.");
    }

    cv::Mat preparedFrame = m_regionOcr->PrepareFrame(regionMat);

    if (OnStatus) OnStatus(L"Performing OCR on selected region...");
    OcrResult ocrResult;
    if (m_regionOcr->SupportsTwoPhase())
    {
        DetectionResult detection = m_regionOcr->Detect(preparedFrame);
        if (detection.empty())
        {
            if (OnStatus) OnStatus(L"No text detected in selected region.");
            return L"";
        }
        ocrResult = m_regionOcr->Recognize(detection);
    }
    else
    {
        ocrResult = m_regionOcr->Recognize(preparedFrame);
    }

    outOcrText = Utf8ToWide(ocrResult.ConcatText());
    if (outOcrText.empty())
    {
        if (OnStatus) OnStatus(L"No text recognized in selected region.");
        return L"";
    }

    if (OnStatus) OnStatus(L"Translating: " + outOcrText);
    if (!m_regionClient || m_regionClientType != m_config.providerType)
    {
        m_regionClient = TranslateProviderFactory::CreateProvider(m_config.providerType);
        m_regionClientType = m_config.providerType;
    }
    if (!m_regionClient)
    {
        if (OnStatus) OnStatus(L"Translation API error: Invalid provider.");
        return L"";
    }
    m_regionClient->SetApiKey(m_config.apiKey);
    m_regionClient->SetApiModel(m_config.apiModel);
    m_regionClient->SetTargetLanguage(m_config.targetLanguage);

    std::wstring result = m_regionClient->Translate(outOcrText);
    if (result.empty())
    {
        if (OnStatus) OnStatus(L"Translation API error: " + m_regionClient->GetLastError());
        return L"";
    }

    if (OnStatus) OnStatus(L"OK: " + result);
    return result;
}
