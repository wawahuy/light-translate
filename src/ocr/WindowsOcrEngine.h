#pragma once
#include "src/ocr/IOcrEngine.h"
#include <winrt/Windows.Media.Ocr.h>

// Windows OCR engine implementation using C++/WinRT.
class WindowsOcrEngine : public IOcrEngine
{
public:
    WindowsOcrEngine();
    ~WindowsOcrEngine() override;

    // IOcrEngine overrides
    bool Initialize() override;
    bool IsInitialized() const override { return m_initialized; }
    OcrResult Recognize(const cv::Mat& bgrFrame) override;
    void Reset() override;

private:
    bool m_initialized = false;
    winrt::Windows::Media::Ocr::OcrEngine m_ocrEngine = nullptr;
};
