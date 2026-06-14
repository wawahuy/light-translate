#include "src/ocr/FrameDiffDetector.h"
#include <opencv2/imgproc.hpp>

bool FrameDiffDetector::HasChanged(const std::vector<cv::Mat>& currentGrays, double threshold)
{
    bool changed = true;

    if (!m_lastGrays.empty() && m_lastGrays.size() == currentGrays.size())
    {
        double totalDiff = 0.0;
        int    validCount = 0;

        for (size_t i = 0; i < currentGrays.size(); ++i)
        {
            // Only compare if the regions have the same size
            if (m_lastGrays[i].size() == currentGrays[i].size())
            {
                cv::Mat diff;
                cv::absdiff(currentGrays[i], m_lastGrays[i], diff);
                totalDiff += cv::mean(diff)[0];
                ++validCount;
            }
            else
            {
                // Size changed → text changed
                totalDiff = 999.0;
                break;
            }
        }

        double avgDiff = validCount > 0 ? totalDiff / validCount : 999.0;
        m_lastDiff = avgDiff;

        if (avgDiff < threshold)
            changed = false;
    }

    // Save current grays for next comparison
    m_lastGrays = currentGrays;

    return changed;
}

void FrameDiffDetector::Reset()
{
    m_lastGrays.clear();
    m_lastDiff = 0.0;
}
