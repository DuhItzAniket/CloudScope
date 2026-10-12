// Display stretch and preview-only image treatments (P036, FR-DSP-11): levels and gamma applied to the picture on
// screen and never to recorded data, automatic levels from the histogram, per-channel histograms and the RGB parade
// for the scopes, the over-exposure highlight, and the red rendering of the night-vision theme.
//
// All functions are pure and thread-safe; they return new images and leave their inputs untouched.
#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace cloudscope::desktop {

using Histogram = std::array<std::uint32_t, 256>;
using Lut = std::array<std::uint8_t, 256>;
using ChannelHistograms = std::array<Histogram, 3>;  // blue, green, red

struct StretchSettings {
    bool enabled = false;
    bool automatic = true;     // levels from the luma histogram's percentiles instead of black/white
    double black = 0.0;        // input level mapped to 0, as a fraction 0..1 of the range
    double white = 1.0;        // input level mapped to 255
    double gamma = 1.0;        // > 1 brightens the mid-tones
    double auto_low = 0.001;   // percentile for the automatic black point
    double auto_high = 0.999;  // percentile for the automatic white point
    bool highlight_overexposed = false;
    int clip_threshold = 250;  // luma at or above which a pixel is painted as over-exposed

    friend bool operator==(const StretchSettings&, const StretchSettings&) = default;
};

struct Levels {
    double black = 0.0;  // fractions 0..1
    double white = 1.0;
};

// The levels that put `low_fraction` of the pixels below black and `high_fraction` below white; never inverted
// (white stays at least one level above black). A flat (all-zero) histogram gives the identity.
[[nodiscard]] Levels auto_levels(const Histogram& histogram, double low_fraction, double high_fraction);

// A look-up table for levels and gamma: identity for black 0, white 1, gamma 1.
[[nodiscard]] Lut make_lut(double black, double white, double gamma);

// The table the settings ask for, given the luma histogram of the picture (used when `automatic` is set).
// The identity table when the stretch is disabled.
[[nodiscard]] Lut stretch_lut(const StretchSettings& settings, const Histogram& luma_histogram);

[[nodiscard]] bool is_identity(const Lut& lut);

// The image with the table applied to every channel (a copy). 8-bit images only; others are returned unchanged.
[[nodiscard]] cv::Mat apply_lut(const cv::Mat& image, const Lut& lut);

// Histograms of the blue, green and red channel of an 8-bit BGR image (of the single channel, three times, for grey).
[[nodiscard]] ChannelHistograms channel_histograms(const cv::Mat& image);

// The RGB parade: for each of `columns` vertical strips of the picture, the distribution of each channel's values
// in `rows` bins (row 0 = darkest). Values are pixel counts normalised to 0..1 per strip. CV_32FC1 matrices of
// size rows x columns, one per channel (blue, green, red).
[[nodiscard]] std::array<cv::Mat, 3> rgb_parade(const cv::Mat& image, int columns = 256, int rows = 64);

// Paints every pixel whose luma is at or above `clip_threshold` in `colour`, in place. Returns how many.
int highlight_overexposed(cv::Mat& preview_bgr, const cv::Mat& luma, int clip_threshold, const cv::Vec3b& colour);

// The night-vision rendering: the luma in the red channel, nothing in green and blue (a copy).
[[nodiscard]] cv::Mat night_vision(const cv::Mat& image);

// Fractions of pixels at the two ends of the range: clipped black (level 0) and clipped white (level 255).
struct ClippingFractions {
    double black = 0.0;
    double white = 0.0;
};
[[nodiscard]] ClippingFractions clipping_fractions(const Histogram& histogram);

}  // namespace cloudscope::desktop
