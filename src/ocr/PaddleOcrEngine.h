#pragma once
#include "src/ocr/IOcrEngine.h"
#include <memory>
#include <string>

// Forward declarations
class BasePredictor;
class CropByPolys;

// PaddleOCR text detection + recognition implementation of IOcrEngine.
class PaddleOcrEngine : public IOcrEngine
{
public:
    PaddleOcrEngine(const std::wstring& detModelDir, const std::wstring& recModelDir);
    ~PaddleOcrEngine() override;

    // Non-copyable, movable
    PaddleOcrEngine(const PaddleOcrEngine&) = delete;
    PaddleOcrEngine& operator=(const PaddleOcrEngine&) = delete;
    PaddleOcrEngine(PaddleOcrEngine&&) noexcept;
    PaddleOcrEngine& operator=(PaddleOcrEngine&&) noexcept;

    // IOcrEngine implementation
    bool Initialize() override;
    bool IsInitialized() const override { return m_initialized; }
    OcrResult Recognize(const cv::Mat& bgrFrame) override;
    
    bool SupportsTwoPhase() const override { return true; }
    DetectionResult Detect(const cv::Mat& bgrFrame) override;
    OcrResult Recognize(const DetectionResult& detection) override;
    
    void Reset() override;

private:
    std::wstring m_detModelDir;
    std::wstring m_recModelDir;
    std::unique_ptr<BasePredictor> m_textDetModel;
    std::unique_ptr<BasePredictor> m_textRecModel;
    std::unique_ptr<CropByPolys>   m_cropByPolys;
    bool                           m_initialized = false;
};
