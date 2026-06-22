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

    // Convert the entire currentFrame to grayscale once (reuse m_currentFrameGray buffer)
    if (currentFrame.channels() == 1)
    {
        m_currentFrameGray = currentFrame;
    }
    else if (currentFrame.channels() == 4)
    {
        cv::cvtColor(currentFrame, m_currentFrameGray, cv::COLOR_BGRA2GRAY);
    }
    else
    {
        cv::cvtColor(currentFrame, m_currentFrameGray, cv::COLOR_BGR2GRAY);
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
        rect = rect & cv::Rect(0, 0, m_currentFrameGray.cols, m_currentFrameGray.rows);

        if (rect.width <= 0 || rect.height <= 0)
        {
            totalDiff = 999.0;
            break;
        }

        // Zero-copy view from the grayscale frame
        cv::Mat gray = m_currentFrameGray(rect);

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

    // Convert the entire currentFrame to grayscale once (reuse m_currentFrameGray buffer)
    if (currentFrame.channels() == 1)
    {
        m_currentFrameGray = currentFrame;
    }
    else if (currentFrame.channels() == 4)
    {
        cv::cvtColor(currentFrame, m_currentFrameGray, cv::COLOR_BGRA2GRAY);
    }
    else
    {
        cv::cvtColor(currentFrame, m_currentFrameGray, cv::COLOR_BGR2GRAY);
    }

    for (const auto& poly : boxes)
    {
        if (poly.empty())
        {
            m_lastGrays.push_back(cv::Mat());
            continue;
        }

        cv::Rect rect = cv::boundingRect(poly);
        rect = rect & cv::Rect(0, 0, m_currentFrameGray.cols, m_currentFrameGray.rows);

        if (rect.width <= 0 || rect.height <= 0)
        {
            m_lastGrays.push_back(cv::Mat());
            continue;
        }

        // Crop and clone to store long-term
        cv::Mat gray = m_currentFrameGray(rect).clone();
        m_lastGrays.push_back(std::move(gray));
    }
}

void BoxDiffDetector::Reset()
{
    m_lastBoxes.clear();
    m_lastGrays.clear();
    m_currentFrameGray.release();
    m_lastDiff = 0.0;
}
