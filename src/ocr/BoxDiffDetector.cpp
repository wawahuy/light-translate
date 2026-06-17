#include "src/ocr/BoxDiffDetector.h"
#include "src/paddleocr/common/processors.h"
#include <opencv2/imgproc.hpp>

BoxDiffDetector::BoxDiffDetector()
{
    m_cropByPolys = std::make_unique<CropByPolys>("quad");
}

BoxDiffDetector::~BoxDiffDetector() = default;

bool BoxDiffDetector::DetectChange(const cv::Mat& currentFrame, double threshold)
{
    if (m_lastBoxes.empty() || m_lastGrays.empty())
    {
        return true;
    }

    // Crop the regions using saved boxes from the current frame
    auto cropResult = (*m_cropByPolys)(currentFrame, m_lastBoxes);
    if (!cropResult.ok())
    {
        // If cropping fails, assume changed so we re-detect
        return true;
    }

    const auto& croppedTexts = cropResult.value();
    if (croppedTexts.size() != m_lastGrays.size())
    {
        return true;
    }

    double totalDiff = 0.0;
    int validCount = 0;

    for (size_t i = 0; i < croppedTexts.size(); ++i)
    {
        cv::Mat gray;
        if (croppedTexts[i].channels() == 1)
        {
            gray = croppedTexts[i].clone();
        }
        else if (croppedTexts[i].channels() == 4)
        {
            cv::cvtColor(croppedTexts[i], gray, cv::COLOR_BGRA2GRAY);
        }
        else
        {
            cv::cvtColor(croppedTexts[i], gray, cv::COLOR_BGR2GRAY);
        }

        if (gray.size() == m_lastGrays[i].size())
        {
            cv::Mat diff;
            cv::absdiff(gray, m_lastGrays[i], diff);
            totalDiff += cv::mean(diff)[0];
            ++validCount;
        }
        else
        {
            // Size changed -> text changed/box size changed
            totalDiff = 999.0;
            break;
        }
    }

    double avgDiff = validCount > 0 ? totalDiff / validCount : 999.0;
    m_lastDiff = avgDiff;

    if (avgDiff < threshold)
    {
        return false; // Under threshold, meaning no significant change
    }

    return true; // Changed
}

void BoxDiffDetector::Update(const std::vector<std::vector<cv::Point2f>>& boxes,
                             const std::vector<cv::Mat>& regionGrays)
{
    m_lastBoxes = boxes;
    m_lastGrays = regionGrays;
}

void BoxDiffDetector::Update(const cv::Mat& currentFrame,
                             const std::vector<std::vector<cv::Point2f>>& boxes)
{
    m_lastBoxes = boxes;
    m_lastGrays.clear();

    auto cropResult = (*m_cropByPolys)(currentFrame, boxes);
    if (cropResult.ok())
    {
        const auto& croppedTexts = cropResult.value();
        m_lastGrays.reserve(croppedTexts.size());
        for (const auto& crop : croppedTexts)
        {
            cv::Mat gray;
            if (crop.channels() == 1)
            {
                gray = crop.clone();
            }
            else if (crop.channels() == 4)
            {
                cv::cvtColor(crop, gray, cv::COLOR_BGRA2GRAY);
            }
            else
            {
                cv::cvtColor(crop, gray, cv::COLOR_BGR2GRAY);
            }
            m_lastGrays.push_back(std::move(gray));
        }
    }
}

void BoxDiffDetector::Reset()
{
    m_lastBoxes.clear();
    m_lastGrays.clear();
    m_lastDiff = 0.0;
}
