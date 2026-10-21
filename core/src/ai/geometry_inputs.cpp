#include "cloudscope/ai/geometry_inputs.hpp"

#include <cmath>
#include <numbers>

namespace cloudscope::ai {

namespace {

constexpr double kMaskFraction = 0.5;  // a patch is inside the mask when at least this share of its footprint is

double radians(double degrees)
{
    return degrees * std::numbers::pi / 180.0;
}

}  // namespace

FullFrameAffine full_frame_affine(int source_width, int source_height, int out_width, int out_height)
{
    const double sx = static_cast<double>(source_width) / out_width;
    const double sy = static_cast<double>(source_height) / out_height;
    return {.sx = sx, .sy = sy, .tx = 0.5 * sx - 0.5, .ty = 0.5 * sy - 0.5};
}

Vector3 sun_aligned(const Vector3& enu, double sun_azimuth_deg)
{
    const double az = radians(sun_azimuth_deg);
    const Vector3 x{.x = std::sin(az), .y = std::cos(az), .z = 0.0};   // horizontal, toward the Sun's azimuth
    const Vector3 y{.x = -std::cos(az), .y = std::sin(az), .z = 0.0};  // z cross x
    return {.x = x.x * enu.x + x.y * enu.y, .y = y.x * enu.x + y.y * enu.y, .z = enu.z};
}

std::array<float, 3> meta_vector(const SunPosition* sun)
{
    if (sun == nullptr || !std::isfinite(sun->apparent_elevation_deg)) {
        return {0.0F, 0.0F, 0.0F};
    }
    const double zenith = radians(90.0 - sun->apparent_elevation_deg);
    return {static_cast<float>(std::cos(zenith)), static_cast<float>(std::sin(zenith)), 1.0F};
}

std::vector<float> ray_map(const CameraModel& model, const Quaternion& camera_to_enu, const SunPosition& sun,
                           const RayMapSpec& spec, const cv::Mat* mask)
{
    const int g_h = spec.height / spec.patch;
    const int g_w = spec.width / spec.patch;
    const std::size_t plane = static_cast<std::size_t>(g_h) * g_w;
    std::vector<float> out(4 * plane, 0.0F);
    const FullFrameAffine affine = full_frame_affine(model.width, model.height, spec.width, spec.height);
    const double half = (spec.patch - 1) / 2.0;
    const bool use_mask = mask != nullptr && !mask->empty();
    for (int r = 0; r < g_h; ++r) {
        for (int c = 0; c < g_w; ++c) {
            const cv::Point2d centre_out(c * spec.patch + half, r * spec.patch + half);
            const cv::Point2d src = affine(centre_out);
            const bool inside = std::isfinite(src.x) && std::isfinite(src.y) && src.x >= -0.5 &&
                                src.x <= model.width - 0.5 && src.y >= -0.5 && src.y <= model.height - 0.5;
            if (!inside) {
                continue;
            }
            if (use_mask) {
                int hits = 0;
                const int n = spec.mask_subgrid;
                for (int j = 0; j < n; ++j) {
                    for (int i = 0; i < n; ++i) {
                        const double du = (i + 0.5) / n * spec.patch - spec.patch / 2.0;
                        const double dv = (j + 0.5) / n * spec.patch - spec.patch / 2.0;
                        const cv::Point2d sample = affine({centre_out.x + du, centre_out.y + dv});
                        const int ix = static_cast<int>(std::lround(sample.x));
                        const int iy = static_cast<int>(std::lround(sample.y));
                        if (ix >= 0 && ix < mask->cols && iy >= 0 && iy < mask->rows &&
                            mask->at<std::uint8_t>(iy, ix) != 0) {
                            ++hits;
                        }
                    }
                }
                if (static_cast<double>(hits) / (n * n) < kMaskFraction) {
                    continue;
                }
            }
            const cv::Point3d ray = pixel_to_ray(model, src);
            if (!std::isfinite(ray.x) || !std::isfinite(ray.y) || !std::isfinite(ray.z)) {
                continue;
            }
            const Vector3 enu = rotate(camera_to_enu, {.x = ray.x, .y = ray.y, .z = ray.z});
            const Vector3 aligned = sun_aligned(enu, sun.azimuth_deg);
            const double elevation = std::asin(std::clamp(aligned.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
            if (!(elevation >= spec.min_elevation_deg)) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(r) * g_w + c;
            out[at] = static_cast<float>(aligned.x);
            out[plane + at] = static_cast<float>(aligned.y);
            out[2 * plane + at] = static_cast<float>(aligned.z);
            out[3 * plane + at] = 1.0F;
        }
    }
    return out;
}

GeometryInputs geometry_inputs(const CameraModel* model, const Quaternion* camera_to_enu, const SunPosition* sun,
                               const RayMapSpec& spec, const cv::Mat* mask)
{
    GeometryInputs inputs;
    const std::size_t count = static_cast<std::size_t>(4) * (spec.height / spec.patch) * (spec.width / spec.patch);
    if (model == nullptr || camera_to_enu == nullptr || sun == nullptr || model->width <= 0 || model->height <= 0 ||
        !std::isfinite(sun->azimuth_deg) || !std::isfinite(sun->apparent_elevation_deg)) {
        inputs.ray_map.assign(count, 0.0F);
        return inputs;
    }
    inputs.ray_map = ray_map(*model, *camera_to_enu, *sun, spec, mask);
    inputs.meta = meta_vector(sun);
    inputs.valid = true;
    return inputs;
}

}  // namespace cloudscope::ai
