#include "src/ocr/BoxDiffDetector.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

BoxDiffDetector::BoxDiffDetector() = default;

BoxDiffDetector::~BoxDiffDetector() = default;

bool BoxDiffDetector::DetectChange(const cv::Mat& currentFrame, double threshold)
{
    if (m_lastBoxes.empty() || m_lastGrays.empty() || m_lastBoxes.size() != m_lastGrays.size())
    {
        return true;
    }

    double totalDiff = 0.0;
    int validCount = 0;

    for (size_t i = 0; i < m_lastBoxes.size(); ++i)
    {
        const auto& poly = m_lastBoxes[i];
        if (poly.empty())
        {
            continue;
        }

        cv::Rect rect = cv::boundingRect(poly);
        rect = rect & cv::Rect(0, 0, currentFrame.cols, currentFrame.rows);

        if (rect.width <= 0 || rect.height <= 0)
        {
            totalDiff = 999.0;
            break;
        }

        cv::Mat crop = currentFrame(rect);
        cv::Mat gray;
        if (crop.channels() == 1)
        {
            gray = crop; // zero-copy view is fine here as it's temporary for DetectChange
        }
        else if (crop.channels() == 4)
        {
            cv::cvtColor(crop, gray, cv::COLOR_BGRA2GRAY);
        }
        else
        {
            cv::cvtColor(crop, gray, cv::COLOR_BGR2GRAY);
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

void BoxDiffDetector::Update(const cv::Mat& currentFrame,
                             const std::vector<std::vector<cv::Point2f>>& boxes)
{
    m_lastBoxes = boxes;
    m_lastGrays.clear();
    m_lastGrays.reserve(boxes.size());

    for (const auto& poly : boxes)
    {
        if (poly.empty())
        {
            m_lastGrays.push_back(cv::Mat());
            continue;
        }

        cv::Rect rect = cv::boundingRect(poly);
        rect = rect & cv::Rect(0, 0, currentFrame.cols, currentFrame.rows);

        if (rect.width <= 0 || rect.height <= 0)
        {
            m_lastGrays.push_back(cv::Mat());
            continue;
        }

        cv::Mat crop = currentFrame(rect);
        cv::Mat gray;
        if (crop.channels() == 1)
        {
            gray = crop.clone(); // deep copy since we store it long-term
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

void BoxDiffDetector::Reset()
{
    m_lastBoxes.clear();
    m_lastGrays.clear();
    m_lastDiff = 0.0;
}
