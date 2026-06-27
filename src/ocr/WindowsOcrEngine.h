#pragma once
#include "src/ocr/IOcrEngine.h"
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>

// Windows OCR engine implementation using C++/WinRT.
class WindowsOcrEngine : public IOcrEngine
{
public:
    WindowsOcrEngine(const std::wstring& langTag = L"");
    ~WindowsOcrEngine() override;

    // IOcrEngine overrides
    bool Initialize() override;
    bool IsInitialized() const override { return m_initialized; }
    cv::Mat PrepareFrame(const cv::Mat& bgraFrame) override;
    OcrResult Recognize(const cv::Mat& bgrFrame) override;
    void Reset() override;

private:
    bool m_initialized = false;
    winrt::Windows::Media::Ocr::OcrEngine m_ocrEngine = nullptr;
    winrt::Windows::Graphics::Imaging::SoftwareBitmap m_cachedSoftwareBitmap = nullptr;
    std::wstring m_langTag;
};
