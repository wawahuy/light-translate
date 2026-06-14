#pragma once
#include <string>
#include <vector>
#include <memory>
#include <opencv2/core.hpp>

// Forward declarations
class BasePredictor;
class CropByPolys;

/// Result of the detection + crop phase (before recognition).
struct DetectionResult
{
    std::vector<cv::Mat> croppedTexts;  ///< Cropped text region images (BGR)
    std::vector<cv::Mat> regionGrays;   ///< Grayscale of each crop (for diff detection)

    bool empty() const { return croppedTexts.empty(); }
};

/// Result of the full OCR pipeline (after recognition).
struct OcrResult
{
    std::vector<std::string> texts;  ///< Recognized text per region (original order)

    bool empty() const { return texts.empty(); }

    /// Concatenate all recognized texts into one UTF-8 string (space-separated).
    std::string ConcatText() const;
};

/// Encapsulates PaddleOCR text detection + recognition pipeline.
///
/// Two-phase usage (recommended — allows diff check between phases):
///   OcrEngine ocr;
///   ocr.Initialize(detDir, recDir);
///   DetectionResult det = ocr.Detect(bgrFrame);   // detect + crop
///   if (diffDetector.HasChanged(det.regionGrays))  // check diff
///       OcrResult rec = ocr.Recognize(det);        // recognize only if changed
class OcrEngine
{
public:
    OcrEngine();
    ~OcrEngine();

    // Non-copyable, movable
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;
    OcrEngine(OcrEngine&&) noexcept;
    OcrEngine& operator=(OcrEngine&&) noexcept;

    /// Lazy-initialize the detection + recognition models.
    /// Returns true on success. Safe to call multiple times (no-op after first success).
    bool Initialize(const std::wstring& detModelDir, const std::wstring& recModelDir);

    bool IsInitialized() const { return m_initialized; }

    /// Phase 1: Text detection + crop + grayscale conversion.
    /// Lightweight enough to run every frame.
    DetectionResult Detect(const cv::Mat& bgrFrame);

    /// Phase 2: Text recognition on previously cropped regions.
    /// Only call this after confirming the text regions have changed.
    OcrResult Recognize(const DetectionResult& detection);

    /// Release all loaded models and reset state.
    void Reset();

private:
    std::unique_ptr<BasePredictor> m_textDetModel;
    std::unique_ptr<BasePredictor> m_textRecModel;
    std::unique_ptr<CropByPolys>   m_cropByPolys;
    bool                           m_initialized = false;
};
