#include "src/ocr/PaddleOcrEngine.h"
#include "src/utils/StringUtils.h"
#include "src/paddleocr/modules/text_detection/predictor.h"
#include "src/paddleocr/modules/text_recognition/predictor.h"
#include "src/paddleocr/common/processors.h"
#include "src/paddleocr/base/base_pipeline.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

PaddleOcrEngine::PaddleOcrEngine(const std::wstring& detModelDir, const std::wstring& recModelDir)
    : m_detModelDir(detModelDir), m_recModelDir(recModelDir)
{
}

PaddleOcrEngine::~PaddleOcrEngine() = default;
PaddleOcrEngine::PaddleOcrEngine(PaddleOcrEngine&&) noexcept = default;
PaddleOcrEngine& PaddleOcrEngine::operator=(PaddleOcrEngine&&) noexcept = default;

bool PaddleOcrEngine::Initialize()
{
    if (m_initialized) return true;

    if (!CheckOcrModelExists(m_detModelDir) || !CheckOcrModelExists(m_recModelDir))
        return false;

    // Text Detection setup
    TextDetPredictorParams detParams;
    detParams.model_name     = "PP-OCRv5_mobile_det";
    detParams.model_dir      = WideToUtf8(m_detModelDir);
    detParams.device         = "cpu";
    detParams.limit_side_len = 960;
    detParams.limit_type     = "max";
    detParams.max_side_limit = 4000;
    detParams.thresh         = 0.3f;
    detParams.box_thresh     = 0.6f;
    detParams.unclip_ratio   = 2.0f;

    m_textDetModel = std::unique_ptr<BasePredictor>(
        new TextDetPredictor(detParams));

    // Text Recognition setup
    TextRecPredictorParams recParams;
    recParams.model_name = "PP-OCRv5_mobile_rec";
    recParams.model_dir  = WideToUtf8(m_recModelDir);
    recParams.device     = "cpu";

    m_textRecModel = std::unique_ptr<BasePredictor>(
        new TextRecPredictor(recParams));

    // CropByPolys for extracting text sub-images
    m_cropByPolys = std::make_unique<CropByPolys>("quad");

    m_initialized = true;
    return true;
}

OcrResult PaddleOcrEngine::Recognize(const cv::Mat& bgrFrame)
{
    DetectionResult det = Detect(bgrFrame);
    if (det.empty()) return {};
    return Recognize(det);
}

DetectionResult PaddleOcrEngine::Detect(const cv::Mat& bgrFrame)
{
    DetectionResult result;
    if (!m_initialized) return result;

    // Run text detection model
    std::vector<cv::Mat> detInput = { bgrFrame.clone() };
    m_textDetModel->Predict(detInput);
    std::vector<TextDetPredictorResult> detResults =
        static_cast<TextDetPredictor*>(m_textDetModel.get())->PredictorResult();

    if (detResults.empty() || detResults[0].dt_polys.empty())
        return result;  // No text detected

    // Sort detected polygons (top-to-bottom, left-to-right)
    auto dt_polys = ComponentsProcessor::SortQuadBoxes(detResults[0].dt_polys);

    // Crop text regions
    auto cropResult = (*m_cropByPolys)(bgrFrame, dt_polys);
    if (!cropResult.ok())
        return result;  // Crop failed

    result.croppedTexts = cropResult.value();
    result.boxes = dt_polys;

    // Build grayscale crops for diff detection
    result.regionGrays.reserve(result.croppedTexts.size());
    for (auto& crop : result.croppedTexts)
    {
        cv::Mat gray;
        if (crop.channels() == 1)
            gray = crop.clone();
        else
            cv::cvtColor(crop, gray, cv::COLOR_BGR2GRAY);
        result.regionGrays.push_back(gray);
    }

    return result;
}

OcrResult PaddleOcrEngine::Recognize(const DetectionResult& detection)
{
    OcrResult result;
    if (!m_initialized || detection.empty()) return result;

    const auto& croppedTexts = detection.croppedTexts;

    // Sort crops by aspect ratio for batching efficiency
    std::vector<std::pair<int, float>> sortInfo;
    for (int m = 0; m < (int)croppedTexts.size(); ++m)
    {
        float ratio = (float)croppedTexts[m].cols / (float)croppedTexts[m].rows;
        sortInfo.push_back({m, ratio});
    }
    std::sort(sortInfo.begin(), sortInfo.end(),
        [](const std::pair<int, float>& a, const std::pair<int, float>& b)
        {
            return a.second < b.second;
        });

    std::vector<cv::Mat> sortedCrops;
    sortedCrops.reserve(croppedTexts.size());
    for (auto& info : sortInfo)
        sortedCrops.push_back(croppedTexts[info.first]);

    m_textRecModel->Predict(sortedCrops);
    auto recResults = static_cast<TextRecPredictor*>(m_textRecModel.get())->PredictorResult();

    // Unsort recognition results back to original order
    std::vector<TextRecPredictorResult> orderedResults(croppedTexts.size());
    for (size_t m = 0; m < recResults.size(); ++m)
    {
        int origIdx = sortInfo[m].first;
        orderedResults[origIdx] = recResults[m];
    }

    // Collect recognized text
    result.texts.reserve(orderedResults.size());
    for (auto& rec : orderedResults)
        result.texts.push_back(rec.rec_text);

    return result;
}

void PaddleOcrEngine::Reset()
{
    m_textDetModel.reset();
    m_textRecModel.reset();
    m_cropByPolys.reset();
    m_initialized = false;
}
