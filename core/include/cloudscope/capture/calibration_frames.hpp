// Calibration frames (P026, FR-CAM-10): dark frames, flat fields and vignetting correction.
//
//   dark master   mean of frames taken with the lens covered: sensor offset and hot pixels, subtracted from pictures
//   flat master   mean of frames of an evenly lit surface: the camera's response across the field (vignetting, dust)
//   gain map      mean(flat - dark) / (flat - dark), multiplied into pictures to even the field
//   vignetting    a radial model g(r) = 1 + a r^2 + b r^4 fitted to the flat, for cameras without a flat of their own
//
// Masters are float images (CV_32FC1 or CV_32FC3) with the frame count and per-pixel noise next to them, and are
// written as 16-bit TIFF with a JSON sidecar. Uniformity is measured as the relative spread (p95 - p5) / median
// of a smoothed picture: lower is flatter.
#pragma once

#include "cloudscope/common/error.hpp"

#include <opencv2/core.hpp>

#include <filesystem>
#include <vector>

namespace cloudscope {

struct MasterFrame {
    cv::Mat mean;    // CV_32F, 1 or 3 channels, in the input's scale (0..255 or 0..65535)
    cv::Mat stddev;  // same shape: per-pixel standard deviation over the frames
    int frames = 0;
    int depth = CV_8U;  // the input depth, to scale back
};

// Mean and standard deviation of frames of the same size, type and depth (8- or 16-bit). InvalidArgument otherwise.
[[nodiscard]] Expected<MasterFrame> average_frames(const std::vector<cv::Mat>& frames);

// Gain map from a flat master and (optionally) a dark master: CV_32F, mean 1.0, clipped to [floor, ceiling].
[[nodiscard]] Expected<cv::Mat> gain_map(const MasterFrame& flat, const MasterFrame* dark = nullptr, double floor = 0.2,
                                         double ceiling = 5.0);

// (picture - dark) * gain, back in the picture's depth, saturated. Either correction may be absent (empty Mat).
[[nodiscard]] Expected<cv::Mat> apply_calibration(const cv::Mat& picture, const cv::Mat& dark_mean,
                                                  const cv::Mat& gain);

struct VignettingModel {
    double a = 0.0;  // coefficient of r^2 (r = distance from the centre over half the diagonal, 0..1)
    double b = 0.0;  // coefficient of r^4
    double centre_x = 0.0;
    double centre_y = 0.0;
    double rms_residual = 0.0;  // of the fit, relative to the centre brightness
};

// Fits g(r) = 1 + a r^2 + b r^4 to the luma of a flat (any 8/16-bit or float image) with least squares over radial
// bins, taking the picture's centre as the optical centre.
[[nodiscard]] Expected<VignettingModel> fit_vignetting(const cv::Mat& flat);

// The gain map (CV_32F, 1 channel) of a vignetting model for a picture size: 1 / g(r), normalised to a mean of 1.
[[nodiscard]] cv::Mat vignetting_gain(const VignettingModel& model, const cv::Size& size);

// Relative spread (p95 - p5) / median of the luma after a 15 x 15 box blur: 0 for a perfectly even field.
[[nodiscard]] double uniformity_spread(const cv::Mat& picture);

// 16-bit TIFF of the mean (scaled to 0..65535 from the input depth) plus `<file>.json` with frames, depth, noise.
[[nodiscard]] Expected<void> save_master(const MasterFrame& master, const std::filesystem::path& file);
[[nodiscard]] Expected<MasterFrame> load_master(const std::filesystem::path& file);

// Image files through std::filesystem paths (OpenCV's own file functions take narrow strings, which cannot name
// every folder on Windows). The format follows the extension (.tiff, .png, .jpg).
[[nodiscard]] Expected<void> write_image(const std::filesystem::path& file, const cv::Mat& image);
[[nodiscard]] Expected<cv::Mat> read_image(const std::filesystem::path& file);

}  // namespace cloudscope
