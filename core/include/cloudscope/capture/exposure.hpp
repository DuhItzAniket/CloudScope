// Sky auto-exposure and high-dynamic-range capture (P025, FR-CAM-05, FR-CAM-06).
//
// A camera's own automatic exposure meters the whole frame; with the Sun in it the sky is driven dark, and
// without the Sun bright cloud tops clip. The sky controller instead
//   1. leaves the Sun's disc (and a margin around it) out of the metering: it is always clipped, that is fine;
//   2. puts a high percentile of the remaining pixels (the brightest cloud) just below clipping;
//   3. changes exposure by at most a factor per step and leaves a dead band, so the stream does not pump;
//   4. uses gain only when the exposure limit is reached (long exposures blur drifting cloud).
// Bracketing gives a set of exposures around the chosen one; `merge_mertens` fuses the decoded frames into one
// picture (exposure fusion after Mertens et al.: weights for contrast, saturation and well-exposedness, blended
// in a Laplacian pyramid), which keeps cloud highlights and shadows at once without a camera response curve.
#pragma once

#include "cloudscope/capture/statistics.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace cloudscope {

struct SkyExposureSettings {
    double target_percentile = 0.99;  // of the luma outside the Sun
    double target_level = 230.0;      // where that percentile should sit (just below clipping at 250)
    double dead_band = 8.0;           // no change while the percentile is within target +- dead_band
    double max_step_ratio = 2.0;      // exposure changes by at most this factor per update
    double sun_margin = 2.5;          // radius multiple of the Sun blob that is excluded from metering
    double min_exposure_ms = 0.05;
    double max_exposure_ms = 100.0;  // longer exposures blur drifting cloud and lower the frame rate
    double min_gain = 0.0;
    double max_gain = 0.0;  // 0: gain is not used
};

struct ExposureDecision {
    double exposure_ms = 0.0;
    double gain = 0.0;
    double metered_level = 0.0;  // the percentile that was metered
    bool changed = false;
    std::string reason;
};

// The luma level below which `fraction` of the pixels outside the Sun's disc lie.
[[nodiscard]] double sun_aware_percentile(const cv::Mat& luma, const SunBlob& sun, double fraction, double sun_margin);

class SkyExposureController {
public:
    explicit SkyExposureController(SkyExposureSettings settings = {}) : settings_(settings) {}

    // One control step from a decoded frame (8-bit, 1 or 3 channels) and the settings it was taken with.
    [[nodiscard]] ExposureDecision update(const cv::Mat& image, double exposure_ms, double gain) const;

    [[nodiscard]] const SkyExposureSettings& settings() const { return settings_; }

private:
    SkyExposureSettings settings_;
};

// Exposures for a bracket around `base_ms` in photographic stops (2 ** stop), clipped to the limits.
[[nodiscard]] std::vector<double> bracket_exposures(double base_ms, const std::vector<double>& stops, double min_ms,
                                                    double max_ms);

struct FusionWeights {
    double contrast = 1.0;
    double saturation = 1.0;
    double well_exposedness = 1.0;
};

// Exposure fusion of 8-bit BGR pictures of one scene taken at different exposures (same size). Returns 8-bit BGR.
// One picture is returned unchanged; an empty list gives an empty Mat.
[[nodiscard]] cv::Mat merge_mertens(const std::vector<cv::Mat>& pictures, const FusionWeights& weights = {},
                                    int pyramid_levels = 0);

// Fraction of pixels at or above `clip_level` outside the Sun's disc: the number the controller is judged by.
[[nodiscard]] double clipped_outside_sun(const cv::Mat& luma, const SunBlob& sun, double sun_margin,
                                         int clip_level = 250);

}  // namespace cloudscope
