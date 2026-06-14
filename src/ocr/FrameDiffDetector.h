#pragma once
#include <vector>
#include <opencv2/core.hpp>

/// Detects whether text regions have changed between consecutive frames
/// by comparing grayscale crops using cv::absdiff.
class FrameDiffDetector
{
public:
    /// Check if text regions changed compared to the last call.
    /// @param currentGrays  Grayscale images of each cropped text region
    /// @param threshold     Average pixel difference threshold (default 1.0)
    /// @returns true if the regions have changed significantly
    bool HasChanged(const std::vector<cv::Mat>& currentGrays, double threshold = 1.0);

    /// Returns the average diff value from the last HasChanged() call (for logging).
    double LastDiffValue() const { return m_lastDiff; }

    /// Clear saved state so the next call always reports "changed".
    void Reset();

private:
    std::vector<cv::Mat> m_lastGrays;
    double               m_lastDiff = 0.0;
};
