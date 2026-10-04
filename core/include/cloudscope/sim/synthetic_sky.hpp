// Synthetic sky images with known ground truth, for tests, benchmarks and the simulated camera.
//
// The picture is a blue sky gradient with noise-shaped clouds and, optionally, the Sun as a saturated disc
// with a glow. What makes it useful is the truth that comes with it: which pixels are cloud, where the Sun is,
// and exactly how many pixels are saturated.
//
// A synthetic image is never a measurement (NFR-DATA-03): whatever shows or stores one must label it simulated.
#pragma once

#include "cloudscope/common/error.hpp"

#include <opencv2/core.hpp>

#include <cstdint>

namespace cloudscope::sim {

struct SyntheticSkySpec {
    int width = 640;               // 16 to 8192
    int height = 480;              // 16 to 8192
    std::uint64_t seed = 1;        // the same spec always gives the same image
    double cloud_fraction = 0.4;   // requested share of cloud pixels, 0 to 1
    bool sun_visible = true;
    double sun_x = 0.7;            // Sun centre as a fraction of the width (0 = left edge); may lie outside 0..1
    double sun_y = 0.3;            // and of the height (0 = top edge)
    double sun_radius_px = 12.0;   // radius of the saturated disc, 1 to 2000
    double noise_sigma = 1.5;      // sensor noise: standard deviation in 8-bit grey levels, 0 to 20
};

struct SyntheticSky {
    cv::Mat image;                 // CV_8UC3, BGR
    cv::Mat cloud_mask;            // CV_8UC1: 255 = cloud, 0 = clear sky; the Sun disc counts as clear
    double cloud_fraction = 0.0;   // share of pixels that are 255 in cloud_mask
    bool sun_visible = false;
    cv::Point2d sun_centre;        // pixel coordinates, x to the right, y down
    double sun_radius_px = 0.0;
    int saturated_pixels = 0;      // pixels with all three channels at 255; these are exactly the Sun disc
};

// Fails with ErrorCode::InvalidArgument when a value in the spec is outside its range.
[[nodiscard]] Expected<SyntheticSky> make_synthetic_sky(const SyntheticSkySpec& spec);

}  // namespace cloudscope::sim
