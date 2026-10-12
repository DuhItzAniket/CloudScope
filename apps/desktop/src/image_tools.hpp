// Image tools (P038, FR-DSP-04, FR-DSP-05): the focus measure, the pixel inspector, region-of-interest statistics
// and the loupe. Pure functions on 8-bit OpenCV images (BGR or grey), thread-safe.
#pragma once

#include <QtCore/QPoint>
#include <QtGui/QImage>
#include <opencv2/core.hpp>

#include <array>
#include <optional>

namespace cloudscope::desktop {

struct PixelValue {
    bool valid = false;  // false when the point lies outside the image
    int x = 0;
    int y = 0;
    int blue = 0;
    int green = 0;
    int red = 0;
    int luma = 0;  // BT.601 of the three channels; the value itself for grey
};

struct RoiStatistics {
    int pixels = 0;
    std::array<double, 3> mean{};     // blue, green, red (all equal for grey)
    std::array<double, 3> std_dev{};  // population standard deviation
    std::array<int, 3> minimum{};
    std::array<int, 3> maximum{};
    double luma_mean = 0.0;
    double focus = 0.0;  // variance of the Laplacian of the luma: higher is sharper
};

// The region clipped to the image; empty if nothing is left.
[[nodiscard]] cv::Rect clamp_roi(const cv::Rect& roi, const cv::Size& image_size);

// Variance of the Laplacian of the luma inside `roi` (the whole image when `roi` is empty). 0 for an empty region.
[[nodiscard]] double focus_measure(const cv::Mat& image, const cv::Rect& roi = {});

[[nodiscard]] PixelValue pixel_value(const cv::Mat& image, int x, int y);

// Statistics of the region (clipped to the image). Empty if nothing of it lies on the image.
[[nodiscard]] std::optional<RoiStatistics> roi_statistics(const cv::Mat& image, const cv::Rect& roi);

// The loupe: the (2 * radius + 1) square around `centre`, magnified `magnification` times with no interpolation, so
// that each source pixel is a visible block. Pixels outside the image are black. An empty image gives a null QImage.
[[nodiscard]] QImage loupe_image(const cv::Mat& image, QPoint centre, int radius_px, int magnification);

// An 8-bit BGR image as a QImage of its own (the pixel data is copied; the Mat may go).
[[nodiscard]] QImage to_qimage(const cv::Mat& image);

}  // namespace cloudscope::desktop
