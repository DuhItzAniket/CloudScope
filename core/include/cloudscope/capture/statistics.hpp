// Frame statistics (P024, FR-CAM-09): what the sky auto-exposure, the operator and the sidecar want to know
// about a picture: brightness, clipping, noise, sharpness, and where the Sun is.
//
// Everything is computed on 8-bit luma (BGR frames are converted with BT.601 weights); the saturation map and
// the Sun blob come from the clipped pixels. All functions are pure and thread-safe.
#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>

namespace cloudscope {

struct SunBlob {
    bool found = false;
    double x = 0.0;          // centroid, pixels from the left edge
    double y = 0.0;          // centroid, pixels from the top edge
    double radius_px = 0.0;  // radius of a disc of the same area
    double area_px = 0.0;
    double fill = 0.0;  // area / bounding-box area: a disc is about 0.79, a ragged clipped cloud much less
};

struct FrameStatistics {
    int width = 0;
    int height = 0;
    std::array<std::uint32_t, 256> histogram{};  // of the luma
    double mean = 0.0;
    double median = 0.0;
    double std_dev = 0.0;
    double clipped_fraction = 0.0;  // luma >= clip threshold
    double dark_fraction = 0.0;     // luma <= dark threshold
    double noise_sigma = 0.0;       // estimated standard deviation of pixel noise, in luma levels
    double sharpness = 0.0;         // variance of the Laplacian (higher = sharper)
    SunBlob sun;

    // Luma level below which `fraction` (0..1) of the pixels lie, from the histogram.
    [[nodiscard]] double percentile(double fraction) const;
};

struct StatisticsOptions {
    int clip_threshold = 250;     // luma at or above which a pixel counts as clipped
    int dark_threshold = 5;       // luma at or below which a pixel counts as dark
    double min_sun_area_px = 20;  // smaller clipped blobs are not reported as the Sun
    double min_sun_fill = 0.45;   // blobs that fill their bounding box less than this are not a disc
};

// `image` is CV_8UC1 (luma) or CV_8UC3 (BGR).
[[nodiscard]] FrameStatistics compute_statistics(const cv::Mat& image, const StatisticsOptions& options = {});

// CV_8UC1 mask: 255 where the luma is at or above the threshold.
[[nodiscard]] cv::Mat saturation_map(const cv::Mat& image, int clip_threshold = 250);

// 8-bit luma of an 8-bit image (a copy for one-channel input).
[[nodiscard]] cv::Mat luma_of(const cv::Mat& image);

}  // namespace cloudscope
