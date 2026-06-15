#pragma once
#include <vector>
#include <memory>
#include <opencv2/core.hpp>

class CropByPolys;

/// Detects whether text inside the previously-detected bounding boxes has changed
/// between consecutive frames. If unchanged, we can skip running text detection.
class BoxDiffDetector
{
public:
    BoxDiffDetector();
    ~BoxDiffDetector();

    // Check if the regions of the new frame corresponding to the saved boxes have changed.
    // If there are no saved boxes, returns true.
    // @param currentFrame  The current BGR frame
    // @param threshold     Average pixel difference threshold (default 1.0)
    // @returns true if changed or no boxes saved, false if unchanged
    bool DetectChange(const cv::Mat& currentFrame, double threshold = 1.0);

    // Update the saved boxes and their grayscale crops.
    // @param boxes       The new box coordinates (polygons)
    // @param regionGrays The grayscale crops of those boxes
    void Update(const std::vector<std::vector<cv::Point2f>>& boxes,
                const std::vector<cv::Mat>& regionGrays);

    // Clear saved state so the next DetectChange always reports true.
    void Reset();

    // Get the average diff value from the last DetectChange call (for logging).
    double LastDiffValue() const { return m_lastDiff; }

    // Check if we currently have saved boxes
    bool HasSavedBoxes() const { return !m_lastBoxes.empty(); }

private:
    std::vector<std::vector<cv::Point2f>> m_lastBoxes;
    std::vector<cv::Mat>                  m_lastGrays;
    std::unique_ptr<CropByPolys>          m_cropByPolys;
    double                                m_lastDiff = 0.0;
};
