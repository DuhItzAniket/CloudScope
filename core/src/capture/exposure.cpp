#include "cloudscope/capture/exposure.hpp"

#include <fmt/format.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cloudscope {

namespace {

cv::Mat metering_mask(const cv::Size& size, const SunBlob& sun, double sun_margin)
{
    cv::Mat mask(size, CV_8UC1, cv::Scalar(255));
    if (sun.found) {
        const int radius = static_cast<int>(std::ceil(sun.radius_px * sun_margin));
        cv::circle(mask, cv::Point(static_cast<int>(std::lround(sun.x)), static_cast<int>(std::lround(sun.y))), radius,
                   cv::Scalar(0), cv::FILLED);
    }
    return mask;
}

}  // namespace

double sun_aware_percentile(const cv::Mat& luma, const SunBlob& sun, double fraction, double sun_margin)
{
    CV_Assert(luma.type() == CV_8UC1);
    const cv::Mat mask = metering_mask(luma.size(), sun, sun_margin);
    std::array<std::uint64_t, 256> histogram{};
    std::uint64_t counted = 0;
    for (int y = 0; y < luma.rows; ++y) {
        const auto* row = luma.ptr<std::uint8_t>(y);
        const auto* allowed = mask.ptr<std::uint8_t>(y);
        for (int x = 0; x < luma.cols; ++x) {
            if (allowed[x] != 0) {
                ++histogram[row[x]];
                ++counted;
            }
        }
    }
    if (counted == 0) {
        return 0.0;
    }
    const double wanted = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(counted);
    double seen = 0.0;
    for (std::size_t level = 0; level < histogram.size(); ++level) {
        seen += static_cast<double>(histogram[level]);
        if (seen >= wanted) {
            return static_cast<double>(level);
        }
    }
    return 255.0;
}

double clipped_outside_sun(const cv::Mat& luma, const SunBlob& sun, double sun_margin, int clip_level)
{
    CV_Assert(luma.type() == CV_8UC1);
    const cv::Mat mask = metering_mask(luma.size(), sun, sun_margin);
    cv::Mat clipped;
    cv::threshold(luma, clipped, clip_level - 1, 255, cv::THRESH_BINARY);
    const int metered = cv::countNonZero(mask);
    if (metered == 0) {
        return 0.0;
    }
    cv::Mat both;
    cv::bitwise_and(clipped, mask, both);
    return static_cast<double>(cv::countNonZero(both)) / static_cast<double>(metered);
}

ExposureDecision SkyExposureController::update(const cv::Mat& image, double exposure_ms, double gain) const
{
    const cv::Mat luma = luma_of(image);
    const FrameStatistics stats = compute_statistics(luma);
    const double level = sun_aware_percentile(luma, stats.sun, settings_.target_percentile, settings_.sun_margin);
    ExposureDecision decision{
        .exposure_ms = exposure_ms, .gain = gain, .metered_level = level, .changed = false, .reason = ""};
    const double error = settings_.target_level - level;
    if (std::abs(error) <= settings_.dead_band) {
        decision.reason = "within the dead band";
        return decision;
    }
    // Light is proportional to exposure (and roughly to gain): the ratio that would move the level to the
    // target, limited per step. A level of 0 (all dark) or 255 (all clipped) means "far away": take the full step.
    double ratio = settings_.target_level / std::max(level, 1.0);
    if (level >= 254.0) {
        ratio = 1.0 / settings_.max_step_ratio;
    }
    ratio = std::clamp(ratio, 1.0 / settings_.max_step_ratio, settings_.max_step_ratio);

    double exposure = exposure_ms * ratio;
    double new_gain = gain;
    if (exposure > settings_.max_exposure_ms) {
        // Out of exposure: the remaining factor goes to gain, if gain is in use.
        const double remaining = exposure / settings_.max_exposure_ms;
        exposure = settings_.max_exposure_ms;
        if (settings_.max_gain > settings_.min_gain) {
            new_gain = std::clamp(gain + 10.0 * std::log2(remaining), settings_.min_gain, settings_.max_gain);
        }
    } else if (exposure < settings_.min_exposure_ms) {
        exposure = settings_.min_exposure_ms;
    } else if (gain > settings_.min_gain && ratio > 1.0 && settings_.max_gain > settings_.min_gain) {
        // Brightening with gain in use: nothing to do, exposure takes it.
    } else if (gain > settings_.min_gain && ratio < 1.0 && settings_.max_gain > settings_.min_gain) {
        // Darkening: give gain back first, it only adds noise.
        new_gain = std::clamp(gain + 10.0 * std::log2(ratio), settings_.min_gain, settings_.max_gain);
        exposure = exposure_ms;
    }
    decision.exposure_ms = exposure;
    decision.gain = new_gain;
    decision.changed = std::abs(exposure - exposure_ms) > 1e-9 || std::abs(new_gain - gain) > 1e-9;
    decision.reason = fmt::format("percentile {:.0f} is {} the target {:.0f}: ratio {:.2f}", level,
                                  error > 0 ? "below" : "above", settings_.target_level, ratio);
    if (!decision.changed) {
        decision.reason += " (at a limit)";
    }
    return decision;
}

std::vector<double> bracket_exposures(double base_ms, const std::vector<double>& stops, double min_ms, double max_ms)
{
    std::vector<double> out;
    out.reserve(stops.size());
    for (const double stop : stops) {
        out.push_back(std::clamp(base_ms * std::pow(2.0, stop), min_ms, max_ms));
    }
    return out;
}

namespace {

cv::Mat well_exposedness(const cv::Mat& bgr32)
{
    // exp(-(c - 0.5)^2 / (2 * 0.2^2)) per channel, multiplied over channels.
    std::vector<cv::Mat> channels;
    cv::split(bgr32, channels);
    cv::Mat weight = cv::Mat::ones(bgr32.size(), CV_32F);
    for (const cv::Mat& channel : channels) {
        const cv::Mat centred = channel - 0.5f;
        cv::Mat squared;
        cv::multiply(centred, centred, squared);
        cv::Mat gaussian;
        cv::exp(squared * -12.5f, gaussian);  // 1 / (2 * 0.2^2) = 12.5
        cv::multiply(weight, gaussian, weight);
    }
    return weight;
}

cv::Mat contrast_weight(const cv::Mat& bgr32)
{
    cv::Mat grey;
    cv::cvtColor(bgr32, grey, cv::COLOR_BGR2GRAY);
    cv::Mat laplacian;
    cv::Laplacian(grey, laplacian, CV_32F);
    return cv::abs(laplacian);
}

cv::Mat saturation_weight(const cv::Mat& bgr32)
{
    std::vector<cv::Mat> channels;
    cv::split(bgr32, channels);
    const cv::Mat mean = (channels[0] + channels[1] + channels[2]) / 3.0f;
    cv::Mat variance = cv::Mat::zeros(bgr32.size(), CV_32F);
    for (const cv::Mat& channel : channels) {
        const cv::Mat d = channel - mean;
        cv::Mat squared;
        cv::multiply(d, d, squared);
        variance += squared;
    }
    cv::Mat out;
    cv::sqrt(variance / 3.0f, out);
    return out;
}

std::vector<cv::Mat> gaussian_pyramid(const cv::Mat& image, int levels)
{
    std::vector<cv::Mat> pyramid{image};
    for (int i = 1; i < levels; ++i) {
        cv::Mat down;
        cv::pyrDown(pyramid.back(), down);
        pyramid.push_back(down);
    }
    return pyramid;
}

std::vector<cv::Mat> laplacian_pyramid(const cv::Mat& image, int levels)
{
    std::vector<cv::Mat> gaussian = gaussian_pyramid(image, levels);
    std::vector<cv::Mat> laplacian;
    for (int i = 0; i + 1 < levels; ++i) {
        cv::Mat up;
        cv::pyrUp(gaussian[static_cast<std::size_t>(i) + 1], up, gaussian[static_cast<std::size_t>(i)].size());
        laplacian.push_back(gaussian[static_cast<std::size_t>(i)] - up);
    }
    laplacian.push_back(gaussian.back());
    return laplacian;
}

}  // namespace

cv::Mat merge_mertens(const std::vector<cv::Mat>& pictures, const FusionWeights& weights, int pyramid_levels)
{
    if (pictures.empty()) {
        return {};
    }
    if (pictures.size() == 1) {
        return pictures.front().clone();
    }
    const cv::Size size = pictures.front().size();
    if (pyramid_levels <= 0) {
        pyramid_levels = std::max(1, static_cast<int>(std::floor(std::log2(std::min(size.width, size.height)))) - 2);
    }
    std::vector<cv::Mat> images32;
    std::vector<cv::Mat> weight_maps;
    cv::Mat weight_sum = cv::Mat::zeros(size, CV_32F);
    for (const cv::Mat& picture : pictures) {
        CV_Assert(picture.type() == CV_8UC3 && picture.size() == size);
        cv::Mat image32;
        picture.convertTo(image32, CV_32FC3, 1.0 / 255.0);
        cv::Mat weight = cv::Mat::ones(size, CV_32F);
        if (weights.contrast > 0.0) {
            cv::Mat c;
            cv::pow(contrast_weight(image32) + 1e-6f, weights.contrast, c);
            cv::multiply(weight, c, weight);
        }
        if (weights.saturation > 0.0) {
            cv::Mat s;
            cv::pow(saturation_weight(image32) + 1e-6f, weights.saturation, s);
            cv::multiply(weight, s, weight);
        }
        if (weights.well_exposedness > 0.0) {
            cv::Mat e;
            cv::pow(well_exposedness(image32) + 1e-6f, weights.well_exposedness, e);
            cv::multiply(weight, e, weight);
        }
        weight_sum += weight;
        images32.push_back(image32);
        weight_maps.push_back(weight);
    }
    std::vector<cv::Mat> fused(static_cast<std::size_t>(pyramid_levels));
    for (std::size_t k = 0; k < images32.size(); ++k) {
        cv::Mat normalised;
        cv::divide(weight_maps[k], weight_sum, normalised);
        const std::vector<cv::Mat> weight_pyramid = gaussian_pyramid(normalised, pyramid_levels);
        const std::vector<cv::Mat> image_pyramid = laplacian_pyramid(images32[k], pyramid_levels);
        for (std::size_t level = 0; level < fused.size(); ++level) {
            cv::Mat weight3;
            cv::cvtColor(weight_pyramid[level], weight3, cv::COLOR_GRAY2BGR);
            cv::Mat weighted;
            cv::multiply(image_pyramid[level], weight3, weighted);
            if (fused[level].empty()) {
                fused[level] = weighted;
            } else {
                fused[level] += weighted;
            }
        }
    }
    cv::Mat result = fused.back();
    for (int level = pyramid_levels - 2; level >= 0; --level) {
        cv::Mat up;
        cv::pyrUp(result, up, fused[static_cast<std::size_t>(level)].size());
        result = up + fused[static_cast<std::size_t>(level)];
    }
    cv::Mat out;
    result.convertTo(out, CV_8UC3, 255.0);
    return out;
}

}  // namespace cloudscope
