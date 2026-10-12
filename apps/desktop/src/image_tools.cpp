#include "image_tools.hpp"

#include <cloudscope/capture/statistics.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

bool usable(const cv::Mat& image)
{
    return !image.empty() && image.depth() == CV_8U && (image.channels() == 1 || image.channels() == 3);
}

}  // namespace

cv::Rect clamp_roi(const cv::Rect& roi, const cv::Size& image_size)
{
    const cv::Rect whole(0, 0, image_size.width, image_size.height);
    return roi & whole;
}

double focus_measure(const cv::Mat& image, const cv::Rect& roi)
{
    if (!usable(image)) {
        return 0.0;
    }
    const cv::Rect region = roi.empty() ? cv::Rect(0, 0, image.cols, image.rows) : clamp_roi(roi, image.size());
    if (region.width < 3 || region.height < 3) {
        return 0.0;
    }
    const cv::Mat luma = luma_of(image(region));
    cv::Mat laplacian;
    cv::Laplacian(luma, laplacian, CV_64F);
    cv::Scalar mean;
    cv::Scalar std_dev;
    cv::meanStdDev(laplacian, mean, std_dev);
    return std_dev[0] * std_dev[0];
}

PixelValue pixel_value(const cv::Mat& image, int x, int y)
{
    PixelValue value{.x = x, .y = y};
    if (!usable(image) || x < 0 || y < 0 || x >= image.cols || y >= image.rows) {
        return value;
    }
    value.valid = true;
    if (image.channels() == 1) {
        const int grey = image.at<std::uint8_t>(y, x);
        value.blue = value.green = value.red = value.luma = grey;
        return value;
    }
    const auto& bgr = image.at<cv::Vec3b>(y, x);
    value.blue = bgr[0];
    value.green = bgr[1];
    value.red = bgr[2];
    // BT.601 weights, the ones the frame statistics use.
    value.luma = static_cast<int>(std::lround(0.299 * value.red + 0.587 * value.green + 0.114 * value.blue));
    return value;
}

std::optional<RoiStatistics> roi_statistics(const cv::Mat& image, const cv::Rect& roi)
{
    if (!usable(image)) {
        return std::nullopt;
    }
    const cv::Rect region = clamp_roi(roi, image.size());
    if (region.empty()) {
        return std::nullopt;
    }
    const cv::Mat part = image(region);
    RoiStatistics stats;
    stats.pixels = region.width * region.height;
    cv::Scalar mean;
    cv::Scalar std_dev;
    cv::meanStdDev(part, mean, std_dev);
    const int channels = part.channels();
    for (int c = 0; c < 3; ++c) {
        const int source = channels == 1 ? 0 : c;
        stats.mean[static_cast<std::size_t>(c)] = mean[source];
        stats.std_dev[static_cast<std::size_t>(c)] = std_dev[source];
    }
    if (channels == 1) {
        double lo = 0.0;
        double hi = 0.0;
        cv::minMaxLoc(part, &lo, &hi);
        stats.minimum.fill(static_cast<int>(lo));
        stats.maximum.fill(static_cast<int>(hi));
        stats.luma_mean = mean[0];
    } else {
        std::array<cv::Mat, 3> planes;
        cv::split(part, planes.data());
        for (std::size_t c = 0; c < 3; ++c) {
            double lo = 0.0;
            double hi = 0.0;
            cv::minMaxLoc(planes[c], &lo, &hi);
            stats.minimum[c] = static_cast<int>(lo);
            stats.maximum[c] = static_cast<int>(hi);
        }
        stats.luma_mean = 0.299 * mean[2] + 0.587 * mean[1] + 0.114 * mean[0];
    }
    stats.focus = focus_measure(image, region);
    return stats;
}

QImage loupe_image(const cv::Mat& image, QPoint centre, int radius_px, int magnification)
{
    if (!usable(image)) {
        return {};
    }
    radius_px = std::clamp(radius_px, 1, 256);
    magnification = std::clamp(magnification, 1, 32);
    const int side = 2 * radius_px + 1;
    const cv::Mat patch = cv::Mat::zeros(side, side, CV_8UC3);
    const cv::Rect wanted(centre.x() - radius_px, centre.y() - radius_px, side, side);
    const cv::Rect available = clamp_roi(wanted, image.size());
    if (!available.empty()) {
        const cv::Rect target(available.x - wanted.x, available.y - wanted.y, available.width, available.height);
        if (image.channels() == 1) {
            cv::cvtColor(image(available), patch(target), cv::COLOR_GRAY2BGR);
        } else {
            image(available).copyTo(patch(target));
        }
    }
    cv::Mat magnified;
    cv::resize(patch, magnified, cv::Size(side * magnification, side * magnification), 0.0, 0.0, cv::INTER_NEAREST);
    return to_qimage(magnified);
}

QImage to_qimage(const cv::Mat& image)
{
    if (image.empty() || image.depth() != CV_8U) {
        return {};
    }
    if (image.channels() == 1) {
        return QImage(image.data, image.cols, image.rows, static_cast<qsizetype>(image.step), QImage::Format_Grayscale8)
            .copy();
    }
    if (image.channels() == 3) {
        return QImage(image.data, image.cols, image.rows, static_cast<qsizetype>(image.step), QImage::Format_BGR888)
            .copy();
    }
    return {};
}

}  // namespace cloudscope::desktop
