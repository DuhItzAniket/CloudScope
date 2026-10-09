#include "cloudscope/capture/statistics.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace cloudscope {

double FrameStatistics::percentile(double fraction) const
{
    const auto total = static_cast<double>(static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height));
    if (total <= 0.0) {
        return 0.0;
    }
    const double wanted = std::clamp(fraction, 0.0, 1.0) * total;
    double seen = 0.0;
    for (std::size_t level = 0; level < histogram.size(); ++level) {
        seen += histogram[level];
        if (seen >= wanted) {
            return static_cast<double>(level);
        }
    }
    return 255.0;
}

cv::Mat luma_of(const cv::Mat& image)
{
    CV_Assert(image.depth() == CV_8U && (image.channels() == 1 || image.channels() == 3));
    if (image.channels() == 1) {
        return image.clone();
    }
    cv::Mat luma;
    cv::cvtColor(image, luma, cv::COLOR_BGR2GRAY);
    return luma;
}

cv::Mat saturation_map(const cv::Mat& image, int clip_threshold)
{
    cv::Mat mask;
    cv::threshold(luma_of(image), mask, clip_threshold - 1, 255, cv::THRESH_BINARY);
    return mask;
}

namespace {

// Robust noise estimate: the median absolute difference between horizontally adjacent pixels, scaled to a
// standard deviation (differences of two independent samples have sigma * sqrt(2); MAD / 0.6745 = sigma).
double estimate_noise(const cv::Mat& luma)
{
    if (luma.cols < 2 || luma.rows < 1) {
        return 0.0;
    }
    std::vector<int> differences;
    differences.reserve(static_cast<std::size_t>(luma.rows) * static_cast<std::size_t>(luma.cols - 1));
    for (int y = 0; y < luma.rows; ++y) {
        const auto* row = luma.ptr<std::uint8_t>(y);
        for (int x = 1; x < luma.cols; ++x) {
            differences.push_back(std::abs(static_cast<int>(row[x]) - static_cast<int>(row[x - 1])));
        }
    }
    const std::size_t middle = differences.size() / 2;
    std::nth_element(differences.begin(), differences.begin() + static_cast<std::ptrdiff_t>(middle), differences.end());
    return static_cast<double>(differences[middle]) / 0.6745 / std::numbers::sqrt2;
}

SunBlob find_sun(const cv::Mat& mask, const StatisticsOptions& options)
{
    SunBlob sun;
    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int count = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    int best = -1;
    int best_area = 0;
    for (int label = 1; label < count; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (area > best_area) {
            best_area = area;
            best = label;
        }
    }
    if (best < 0 || best_area < options.min_sun_area_px) {
        return sun;
    }
    const int box_w = stats.at<int>(best, cv::CC_STAT_WIDTH);
    const int box_h = stats.at<int>(best, cv::CC_STAT_HEIGHT);
    const double fill = static_cast<double>(best_area) / (static_cast<double>(box_w) * static_cast<double>(box_h));
    if (fill < options.min_sun_fill) {
        return sun;
    }
    sun.found = true;
    sun.x = centroids.at<double>(best, 0);
    sun.y = centroids.at<double>(best, 1);
    sun.area_px = best_area;
    sun.radius_px = std::sqrt(static_cast<double>(best_area) / std::numbers::pi);
    sun.fill = fill;
    return sun;
}

}  // namespace

FrameStatistics compute_statistics(const cv::Mat& image, const StatisticsOptions& options)
{
    const cv::Mat luma = luma_of(image);
    FrameStatistics out;
    out.width = luma.cols;
    out.height = luma.rows;
    const auto total = static_cast<double>(luma.total());
    if (total <= 0.0) {
        return out;
    }
    for (int y = 0; y < luma.rows; ++y) {
        const auto* row = luma.ptr<std::uint8_t>(y);
        for (int x = 0; x < luma.cols; ++x) {
            ++out.histogram[row[x]];
        }
    }
    double sum = 0.0;
    double sum_squares = 0.0;
    std::uint64_t clipped = 0;
    std::uint64_t dark = 0;
    for (std::size_t level = 0; level < out.histogram.size(); ++level) {
        const double n = out.histogram[level];
        const auto value = static_cast<double>(level);
        sum += n * value;
        sum_squares += n * value * value;
        if (static_cast<int>(level) >= options.clip_threshold) {
            clipped += out.histogram[level];
        }
        if (static_cast<int>(level) <= options.dark_threshold) {
            dark += out.histogram[level];
        }
    }
    out.mean = sum / total;
    out.std_dev = std::sqrt(std::max(sum_squares / total - out.mean * out.mean, 0.0));
    out.median = out.percentile(0.5);
    out.clipped_fraction = static_cast<double>(clipped) / total;
    out.dark_fraction = static_cast<double>(dark) / total;
    out.noise_sigma = estimate_noise(luma);
    cv::Mat laplacian;
    cv::Laplacian(luma, laplacian, CV_64F);
    cv::Scalar mean;
    cv::Scalar deviation;
    cv::meanStdDev(laplacian, mean, deviation);
    out.sharpness = deviation[0] * deviation[0];
    if (clipped > 0) {
        out.sun = find_sun(saturation_map(luma, options.clip_threshold), options);
    }
    return out;
}

}  // namespace cloudscope
