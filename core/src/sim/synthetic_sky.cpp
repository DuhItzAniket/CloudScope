#include "cloudscope/sim/synthetic_sky.hpp"

#include <fmt/format.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cloudscope::sim {

namespace {

constexpr int kMinSize = 16;
constexpr int kMaxSize = 8192;
// Nothing but the Sun disc reaches 255: clouds, glow and noise are capped below it.
constexpr double kBrightestNonSun = 250.0;

Expected<void> check(const SyntheticSkySpec& spec)
{
    const auto bad = [](const char* what) { return fail(ErrorCode::InvalidArgument, fmt::format("synthetic sky: {}", what)); };
    if (spec.width < kMinSize || spec.width > kMaxSize || spec.height < kMinSize || spec.height > kMaxSize) {
        return bad("width and height must be between 16 and 8192");
    }
    if (!(spec.cloud_fraction >= 0.0 && spec.cloud_fraction <= 1.0)) {
        return bad("cloud_fraction must be between 0 and 1");
    }
    if (!(spec.sun_radius_px >= 1.0 && spec.sun_radius_px <= 2000.0)) {
        return bad("sun_radius_px must be between 1 and 2000");
    }
    if (!(spec.noise_sigma >= 0.0 && spec.noise_sigma <= 20.0)) {
        return bad("noise_sigma must be between 0 and 20");
    }
    if (!std::isfinite(spec.sun_x) || !std::isfinite(spec.sun_y)) {
        return bad("sun_x and sun_y must be finite");
    }
    return {};
}

// Smooth random field: three octaves of low-resolution noise, each enlarged with bicubic interpolation.
cv::Mat cloud_field(const cv::Size size, cv::RNG& rng)
{
    cv::Mat field = cv::Mat::zeros(size, CV_32FC1);
    const int longer = std::max(size.width, size.height);
    float amplitude = 1.0F;
    for (const int cells : {4, 9, 19}) {
        const cv::Size grid_size(std::max(2, cells * size.width / longer + 1),
                                 std::max(2, cells * size.height / longer + 1));
        cv::Mat grid(grid_size, CV_32FC1);
        rng.fill(grid, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::Mat enlarged;
        cv::resize(grid, enlarged, size, 0.0, 0.0, cv::INTER_CUBIC);
        field += amplitude * enlarged;
        amplitude *= 0.5F;
    }
    return field;
}

// The value below which a fraction (1 - cloud_fraction) of the field lies.
float cloud_threshold(const cv::Mat& field, double cloud_fraction)
{
    std::vector<float> values(field.begin<float>(), field.end<float>());
    if (cloud_fraction <= 0.0) {
        return *std::max_element(values.begin(), values.end()) + 1.0F;
    }
    const auto clear_count = static_cast<std::size_t>(std::llround((1.0 - cloud_fraction) * static_cast<double>(values.size())));
    if (clear_count == 0) {
        return *std::min_element(values.begin(), values.end()) - 1.0F;
    }
    const std::size_t index = std::min(clear_count, values.size() - 1);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

}  // namespace

Expected<SyntheticSky> make_synthetic_sky(const SyntheticSkySpec& spec)
{
    if (auto valid = check(spec); !valid) {
        return fail(valid.error());
    }
    const cv::Size size(spec.width, spec.height);
    cv::RNG rng(spec.seed);

    const cv::Mat field = cloud_field(size, rng);
    const float threshold = cloud_threshold(field, spec.cloud_fraction);

    SyntheticSky sky;
    sky.cloud_mask = cv::Mat(size, CV_8UC1);
    cv::Mat image(size, CV_32FC3);
    // Clouds fade in over this range of the field above the threshold, which gives them soft edges.
    constexpr float kEdgeWidth = 0.08F;
    for (int y = 0; y < size.height; ++y) {
        // Clear sky: deep blue at the top, paler towards the bottom (BGR).
        const float down = static_cast<float>(y) / static_cast<float>(size.height - 1);
        const cv::Vec3f clear(190.0F + 45.0F * down, 110.0F + 80.0F * down, 40.0F + 100.0F * down);
        const float* field_row = field.ptr<float>(y);
        auto* image_row = image.ptr<cv::Vec3f>(y);
        auto* mask_row = sky.cloud_mask.ptr<std::uint8_t>(y);
        for (int x = 0; x < size.width; ++x) {
            const float above = field_row[x] - threshold;
            const bool cloud = above >= 0.0F;
            mask_row[x] = cloud ? 255 : 0;
            const float cover = std::clamp(above / kEdgeWidth + 0.5F, 0.0F, 1.0F);
            // Thicker cloud (further above the threshold) is brighter, up to a cap below saturation.
            const float grey = std::min(175.0F + 220.0F * std::max(above, 0.0F), 240.0F);
            image_row[x] = clear * (1.0F - cover) + cv::Vec3f(grey, grey, grey) * cover;
        }
    }

    if (spec.noise_sigma > 0.0) {
        cv::Mat noise(size, CV_32FC3);
        rng.fill(noise, cv::RNG::NORMAL, 0.0, spec.noise_sigma);
        image += noise;
    }

    sky.sun_visible = spec.sun_visible;
    if (spec.sun_visible) {
        sky.sun_centre = cv::Point2d(spec.sun_x * (spec.width - 1), spec.sun_y * (spec.height - 1));
        sky.sun_radius_px = spec.sun_radius_px;
        const double glow_reach = 6.0 * spec.sun_radius_px;
        for (int y = 0; y < size.height; ++y) {
            auto* image_row = image.ptr<cv::Vec3f>(y);
            for (int x = 0; x < size.width; ++x) {
                const double distance = std::hypot(x - sky.sun_centre.x, y - sky.sun_centre.y);
                if (distance > spec.sun_radius_px && distance < glow_reach) {
                    const double falloff = 1.0 - (distance - spec.sun_radius_px) / (glow_reach - spec.sun_radius_px);
                    const auto glow = static_cast<float>(120.0 * falloff * falloff);
                    image_row[x] += cv::Vec3f(glow, glow, glow);
                }
            }
        }
    }

    cv::min(image, kBrightestNonSun, image);
    cv::max(image, 0.0, image);
    image.convertTo(sky.image, CV_8UC3);

    if (spec.sun_visible) {
        for (int y = 0; y < size.height; ++y) {
            auto* image_row = sky.image.ptr<cv::Vec3b>(y);
            auto* mask_row = sky.cloud_mask.ptr<std::uint8_t>(y);
            for (int x = 0; x < size.width; ++x) {
                if (std::hypot(x - sky.sun_centre.x, y - sky.sun_centre.y) <= spec.sun_radius_px) {
                    image_row[x] = cv::Vec3b(255, 255, 255);
                    mask_row[x] = 0;
                    ++sky.saturated_pixels;
                }
            }
        }
    }
    sky.cloud_fraction = static_cast<double>(cv::countNonZero(sky.cloud_mask)) / static_cast<double>(size.area());
    return sky;
}

}  // namespace cloudscope::sim
