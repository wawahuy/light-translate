#pragma once
#include "src/ocr/IOcrEngine.h"
#include <memory>

// Native Windows OCR engine using Windows.Media.Ocr (C++/WinRT).
// Implements IOcrEngine. Uses Pimpl idiom to keep WinRT headers out of client files.
class WindowsOcrEngine : public IOcrEngine
{
public:
    WindowsOcrEngine();
    ~WindowsOcrEngine() override;

    // Non-copyable
    WindowsOcrEngine(const WindowsOcrEngine&) = delete;
    WindowsOcrEngine& operator=(const WindowsOcrEngine&) = delete;

    // IOcrEngine implementation
    bool Initialize() override;
    bool IsInitialized() const override;
    OcrResult Recognize(const cv::Mat& bgrFrame) override;
    void Reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
};
