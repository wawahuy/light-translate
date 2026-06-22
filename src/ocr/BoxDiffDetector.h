#pragma once
#include <vector>
#include <memory>
#include <opencv2/core.hpp>

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

    // Update the saved boxes by cropping the current frame and converting to grayscale internally.
    // @param currentFrame The current BGR frame
    // @param boxes        The new box coordinates (polygons)
    void Update(const cv::Mat& currentFrame,
                const std::vector<std::vector<cv::Point2f>>& boxes);

    // Clear saved state so the next DetectChange always reports true.
    void Reset();

    // Get the average diff value from the last DetectChange call (for logging).
    double LastDiffValue() const { return m_lastDiff; }

    // Check if we currently have saved boxes
    bool HasSavedBoxes() const { return !m_lastBoxes.empty(); }

private:
    std::vector<std::vector<cv::Point2f>> m_lastBoxes;
    std::vector<cv::Mat>                  m_lastGrays;
    double                                m_lastDiff = 0.0;
    cv::Mat                               m_currentFrameGray; // Cache for full-screen grayscale frame
};
