#pragma once
#include <string>
#include <vector>
#include <opencv2/core.hpp>

// Supported OCR Engine types.
enum class OcrType : int
{
    PaddleOCR  = 0,
    WindowsOCR = 1
};

// Result of the detection + crop phase (before recognition).
struct DetectionResult
{
    std::vector<cv::Mat> croppedTexts;  // Cropped text region images (BGR)
    std::vector<cv::Mat> regionGrays;   // Grayscale of each crop (for diff detection)
    std::vector<std::vector<cv::Point2f>> boxes; // Polygons of detected text boxes (sorted)

    bool empty() const { return croppedTexts.empty(); }
};

// Result of the full OCR pipeline (after recognition).
struct OcrResult
{
    std::vector<std::string> texts;  // Recognized text per region
    std::vector<std::vector<cv::Point2f>> boxes; // Polygons of detected text boxes (sorted)

    bool empty() const { return texts.empty(); }

    // Concatenate all recognized texts into one UTF-8 string (space-separated).
    std::string ConcatText() const
    {
        std::string result;
        for (const auto& t : texts)
        {
            if (!result.empty())
                result += " ";
            result += t;
        }
        return result;
    }
};

// Interface for all OCR Engines.
class IOcrEngine
{
public:
    virtual ~IOcrEngine() = default;

    // Initialize the OCR engine.
    virtual bool Initialize() = 0;

    // Check if the engine is initialized.
    virtual bool IsInitialized() const = 0;

    // Single-phase OCR interface (run detection and recognition in one call).
    virtual OcrResult Recognize(const cv::Mat& bgrFrame) = 0;

    // Returns true if the engine supports separate detection and recognition phases.
    virtual bool SupportsTwoPhase() const { return false; }

    // Phase 1: Text detection + crop + grayscale conversion.
    virtual DetectionResult Detect(const cv::Mat& bgrFrame) { return {}; }

    // Phase 2: Text recognition on previously cropped regions.
    virtual OcrResult Recognize(const DetectionResult& detection) { return {}; }

    // Release all loaded resources/models.
    virtual void Reset() = 0;
};
