#include "display_stretch.hpp"

#include <cloudscope/capture/statistics.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cloudscope::desktop {

namespace {

constexpr int kLevels = 256;

// The lowest level at which more than `fraction` of the pixels lie at or below it; for fraction 1 (or above
// every pixel) the highest level that has pixels. Levels without pixels at the ends are never chosen.
double level_of_fraction(const Histogram& histogram, double fraction, std::uint64_t total)
{
    const auto wanted = static_cast<double>(total) * std::clamp(fraction, 0.0, 1.0);
    std::uint64_t seen = 0;
    int highest_used = 0;
    for (int level = 0; level < kLevels; ++level) {
        const std::uint32_t count = histogram[static_cast<std::size_t>(level)];
        if (count > 0) {
            highest_used = level;
        }
        seen += count;
        if (count > 0 && static_cast<double>(seen) > wanted) {
            return static_cast<double>(level);
        }
    }
    return static_cast<double>(highest_used);
}

}  // namespace

Levels auto_levels(const Histogram& histogram, double low_fraction, double high_fraction)
{
    const std::uint64_t total = std::accumulate(histogram.begin(), histogram.end(), std::uint64_t{0});
    if (total == 0) {
        return {};
    }
    const double low = level_of_fraction(histogram, low_fraction, total);
    double high = level_of_fraction(histogram, high_fraction, total);
    if (high <= low) {
        high = std::min(low + 1.0, static_cast<double>(kLevels - 1));
    }
    const double scale = 1.0 / (kLevels - 1);
    return {.black = low * scale, .white = high * scale};
}

Lut make_lut(double black, double white, double gamma)
{
    Lut lut{};
    const double lo = std::clamp(black, 0.0, 1.0);
    const double hi = std::clamp(white, lo + 1.0 / (kLevels - 1), 1.0);
    const double exponent = 1.0 / std::clamp(gamma, 0.05, 20.0);
    for (int level = 0; level < kLevels; ++level) {
        const double input = static_cast<double>(level) / (kLevels - 1);
        const double stretched = std::clamp((input - lo) / (hi - lo), 0.0, 1.0);
        const double output = std::pow(stretched, exponent);
        lut[static_cast<std::size_t>(level)] = static_cast<std::uint8_t>(std::lround(output * (kLevels - 1)));
    }
    return lut;
}

Lut stretch_lut(const StretchSettings& settings, const Histogram& luma_histogram)
{
    if (!settings.enabled) {
        return make_lut(0.0, 1.0, 1.0);
    }
    if (settings.automatic) {
        const Levels levels = auto_levels(luma_histogram, settings.auto_low, settings.auto_high);
        return make_lut(levels.black, levels.white, settings.gamma);
    }
    return make_lut(settings.black, settings.white, settings.gamma);
}

bool is_identity(const Lut& lut)
{
    for (std::size_t level = 0; level < lut.size(); ++level) {
        if (lut[level] != static_cast<std::uint8_t>(level)) {
            return false;
        }
    }
    return true;
}

cv::Mat apply_lut(const cv::Mat& image, const Lut& lut)
{
    if (image.empty() || image.depth() != CV_8U) {
        return image;
    }
    cv::Mat table(1, kLevels, CV_8UC1);
    std::ranges::copy(lut, table.ptr<std::uint8_t>(0));
    cv::Mat out;
    cv::LUT(image, table, out);
    return out;
}

ChannelHistograms channel_histograms(const cv::Mat& image)
{
    ChannelHistograms result{};
    if (image.empty() || image.depth() != CV_8U) {
        return result;
    }
    if (image.channels() == 1) {
        for (int y = 0; y < image.rows; ++y) {
            const auto* row = image.ptr<std::uint8_t>(y);
            for (int x = 0; x < image.cols; ++x) {
                ++result[0][row[x]];
            }
        }
        result[1] = result[0];
        result[2] = result[0];
        return result;
    }
    if (image.channels() != 3) {
        return result;
    }
    for (int y = 0; y < image.rows; ++y) {
        const auto* row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x) {
            ++result[0][row[x][0]];
            ++result[1][row[x][1]];
            ++result[2][row[x][2]];
        }
    }
    return result;
}

std::array<cv::Mat, 3> rgb_parade(const cv::Mat& image, int columns, int rows)
{
    std::array<cv::Mat, 3> parade;
    columns = std::max(columns, 1);
    rows = std::clamp(rows, 1, kLevels);
    for (cv::Mat& channel : parade) {
        channel = cv::Mat::zeros(rows, columns, CV_32FC1);
    }
    if (image.empty() || image.depth() != CV_8U || (image.channels() != 1 && image.channels() != 3)) {
        return parade;
    }
    // One strip per column of the parade: a reduced copy keeps the cost independent of the picture size.
    cv::Mat reduced;
    const int reduced_height = std::clamp(image.rows * columns / std::max(image.cols, 1), 1, 1024);
    cv::resize(image, reduced, cv::Size(columns, reduced_height), 0.0, 0.0, cv::INTER_AREA);
    cv::Mat bgr;
    if (reduced.channels() == 1) {
        cv::cvtColor(reduced, bgr, cv::COLOR_GRAY2BGR);
    } else {
        bgr = reduced;
    }
    const float per_pixel = 1.0F / static_cast<float>(reduced_height);
    for (int y = 0; y < bgr.rows; ++y) {
        const auto* row = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int bin = static_cast<int>(row[x][c]) * rows / kLevels;
                parade[static_cast<std::size_t>(c)].at<float>(bin, x) += per_pixel;
            }
        }
    }
    return parade;
}

int highlight_overexposed(cv::Mat& preview_bgr, const cv::Mat& luma, int clip_threshold, const cv::Vec3b& colour)
{
    if (preview_bgr.empty() || preview_bgr.type() != CV_8UC3 || luma.type() != CV_8UC1 ||
        luma.size() != preview_bgr.size()) {
        return 0;
    }
    int count = 0;
    const auto threshold = static_cast<std::uint8_t>(std::clamp(clip_threshold, 0, kLevels - 1));
    for (int y = 0; y < luma.rows; ++y) {
        const auto* in = luma.ptr<std::uint8_t>(y);
        auto* out = preview_bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < luma.cols; ++x) {
            if (in[x] >= threshold) {
                out[x] = colour;
                ++count;
            }
        }
    }
    return count;
}

cv::Mat night_vision(const cv::Mat& image)
{
    if (image.empty() || image.depth() != CV_8U) {
        return image;
    }
    const cv::Mat luma = luma_of(image);
    cv::Mat out(luma.size(), CV_8UC3);
    for (int y = 0; y < luma.rows; ++y) {
        const auto* in = luma.ptr<std::uint8_t>(y);
        auto* row = out.ptr<cv::Vec3b>(y);
        for (int x = 0; x < luma.cols; ++x) {
            row[x] = cv::Vec3b(0, 0, in[x]);
        }
    }
    return out;
}

ClippingFractions clipping_fractions(const Histogram& histogram)
{
    const std::uint64_t total = std::accumulate(histogram.begin(), histogram.end(), std::uint64_t{0});
    if (total == 0) {
        return {};
    }
    return {.black = static_cast<double>(histogram.front()) / static_cast<double>(total),
            .white = static_cast<double>(histogram.back()) / static_cast<double>(total)};
}

}  // namespace cloudscope::desktop
