#include <cloudscope/ai/mask_refine.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace cloudscope::ai {

namespace {

constexpr int kCloud = 2;
constexpr int kGlare = 3;
static_assert(kSkyParseClasses[kCloud] == std::string_view("cloud"));
static_assert(kSkyParseClasses[kGlare] == std::string_view("sun_glare"));
constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

cv::Mat box(const cv::Mat& src, int radius)
{
    cv::Mat out;
    const int k = 2 * radius + 1;
    cv::boxFilter(src, out, CV_32F, cv::Size(k, k), cv::Point(-1, -1), true, cv::BORDER_REFLECT);
    return out;
}

float zenith_deg(const cv::Vec3f& direction)
{
    return std::acos(std::clamp(direction[2], -1.0F, 1.0F)) * kDegrees;
}

}  // namespace

// The guide's own statistics (mean and variance over the window), shared by every map filtered with it.
struct GuideStats {
    cv::Mat mean;
    cv::Mat var_eps;  // variance + eps
    int radius = 0;
};

GuideStats guide_stats(const cv::Mat& guide, int radius, float eps)
{
    GuideStats stats;
    stats.radius = radius;
    stats.mean = box(guide, radius);
    stats.var_eps = box(guide.mul(guide), radius) - stats.mean.mul(stats.mean) + static_cast<double>(eps);
    return stats;
}

cv::Mat guided(const GuideStats& stats, const cv::Mat& guide, const cv::Mat& src)
{
    const cv::Mat mean_p = box(src, stats.radius);
    const cv::Mat corr_ip = box(guide.mul(src), stats.radius);
    const cv::Mat cov_ip = corr_ip - stats.mean.mul(mean_p);
    cv::Mat a;
    cv::divide(cov_ip, stats.var_eps, a);
    const cv::Mat b = mean_p - a.mul(stats.mean);
    cv::Mat out = box(a, stats.radius).mul(guide) + box(b, stats.radius);
    return out;
}

cv::Mat guided_filter(const cv::Mat& guide, const cv::Mat& src, int radius, float eps)
{
    CV_Assert(guide.type() == CV_32FC1 && src.type() == CV_32FC1 && guide.size() == src.size());
    return guided(guide_stats(guide, radius, eps), guide, src);
}

cv::Mat refine_classes(const std::vector<cv::Mat>& probabilities, const cv::Mat& guide,
                       const MaskRefineSettings& settings)
{
    CV_Assert(!probabilities.empty() && guide.type() == CV_32FC1);
    const GuideStats stats = guide_stats(guide, settings.guide_radius_px, settings.guide_eps);
    std::vector<cv::Mat> refined;
    refined.reserve(probabilities.size());
    for (const cv::Mat& p : probabilities) {
        cv::Mat up;
        cv::resize(p, up, guide.size(), 0.0, 0.0, cv::INTER_LINEAR);
        for (int pass = 0; pass < std::max(settings.guide_passes, 1); ++pass) {
            up = guided(stats, guide, up);
        }
        refined.push_back(up);
    }
    cv::Mat classes(guide.size(), CV_8UC1, cv::Scalar(0));
    std::vector<const float*> rows(refined.size(), nullptr);
    for (int y = 0; y < guide.rows; ++y) {
        for (std::size_t c = 0; c < refined.size(); ++c) {
            rows[c] = refined[c].ptr<float>(y);
        }
        auto* out = classes.ptr<std::uint8_t>(y);
        for (int x = 0; x < guide.cols; ++x) {
            std::size_t best = 0;
            float best_value = rows[0][x];
            for (std::size_t c = 1; c < refined.size(); ++c) {
                if (rows[c][x] > best_value) {
                    best_value = rows[c][x];
                    best = c;
                }
            }
            out[x] = static_cast<std::uint8_t>(best);
        }
    }
    return classes;
}

RayField ray_field(const GeometryInputs& geometry, int grid_h, int grid_w, cv::Size size)
{
    RayField field;
    if (!geometry.valid || grid_h <= 0 || grid_w <= 0 || size.width <= 0 || size.height <= 0) {
        return field;
    }
    const std::size_t plane = static_cast<std::size_t>(grid_h) * static_cast<std::size_t>(grid_w);
    if (geometry.ray_map.size() != static_cast<std::size_t>(kRayMapChannels) * plane) {
        return field;
    }
    std::array<cv::Mat, 4> planes;
    for (std::size_t c = 0; c < 4; ++c) {
        cv::Mat coarse(grid_h, grid_w, CV_32FC1);
        std::copy_n(geometry.ray_map.data() + c * plane, plane, coarse.ptr<float>(0));
        cv::resize(coarse, planes[c], size, 0.0, 0.0, cv::INTER_LINEAR);
    }
    field.directions = cv::Mat(size, CV_32FC3, cv::Scalar(0.0, 0.0, 0.0));
    field.valid = cv::Mat(size, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < size.height; ++y) {
        const float* px = planes[0].ptr<float>(y);
        const float* py = planes[1].ptr<float>(y);
        const float* pz = planes[2].ptr<float>(y);
        const float* pv = planes[3].ptr<float>(y);
        auto* d = field.directions.ptr<cv::Vec3f>(y);
        auto* v = field.valid.ptr<std::uint8_t>(y);
        for (int x = 0; x < size.width; ++x) {
            const float n = std::sqrt(px[x] * px[x] + py[x] * py[x] + pz[x] * pz[x]);
            if (pv[x] > 0.5F && n > 1.0e-3F) {
                d[x] = cv::Vec3f(px[x] / n, py[x] / n, pz[x] / n);
                v[x] = 1;
            }
        }
    }
    field.ok = true;
    return field;
}

cv::Mat luminance_of(const cv::Mat& rgb)
{
    CV_Assert(rgb.type() == CV_8UC3);
    cv::Mat lum(rgb.size(), CV_32FC1);
    for (int y = 0; y < rgb.rows; ++y) {
        const auto* in = rgb.ptr<cv::Vec3b>(y);
        auto* out = lum.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const std::uint8_t brightest = std::max({in[x][0], in[x][1], in[x][2]});
            out[x] = static_cast<float>(brightest) / 255.0F;
        }
    }
    return lum;
}

std::optional<cv::Point> find_sun(const cv::Mat& luminance, const RayField& rays, const GeometryInputs& geometry,
                                  const MaskRefineSettings& settings)
{
    if (!rays.ok || !geometry.valid || geometry.meta[2] <= 0.0F || luminance.empty() ||
        luminance.size() != rays.directions.size()) {
        return std::nullopt;
    }
    const float sun_zenith = std::acos(std::clamp(geometry.meta[0], -1.0F, 1.0F)) * kDegrees;
    cv::Mat blurred;
    cv::blur(luminance, blurred, cv::Size(settings.sun_blur_px, settings.sun_blur_px), cv::Point(-1, -1),
             cv::BORDER_REFLECT);
    float best = -1.0F;
    cv::Point best_at(-1, -1);
    for (int y = 0; y < blurred.rows; ++y) {
        const float* b = blurred.ptr<float>(y);
        const auto* d = rays.directions.ptr<cv::Vec3f>(y);
        const auto* v = rays.valid.ptr<std::uint8_t>(y);
        for (int x = 0; x < blurred.cols; ++x) {
            if (v[x] == 0 || std::abs(zenith_deg(d[x]) - sun_zenith) > settings.sun_zenith_tolerance_deg) {
                continue;
            }
            if (b[x] > best) {
                best = b[x];
                best_at = cv::Point(x, y);
            }
        }
    }
    if (best_at.x < 0 || best < settings.glare_luminance) {
        return std::nullopt;
    }
    return best_at;
}

int apply_glare_rule(cv::Mat& sky_parse, const cv::Mat& luminance, const RayField& rays, cv::Point sun,
                     const MaskRefineSettings& settings)
{
    if (!rays.ok || sky_parse.empty() || sky_parse.type() != CV_8UC1 || sky_parse.size() != luminance.size() ||
        sky_parse.size() != rays.directions.size() || sun.x < 0 || sun.y < 0 || sun.x >= sky_parse.cols ||
        sun.y >= sky_parse.rows) {
        return 0;
    }
    const cv::Vec3f s = rays.directions.at<cv::Vec3f>(sun);
    const float cos_limit = std::cos(settings.glare_radius_deg / kDegrees);
    int changed = 0;
    for (int y = 0; y < sky_parse.rows; ++y) {
        auto* row = sky_parse.ptr<std::uint8_t>(y);
        const float* lum = luminance.ptr<float>(y);
        const auto* d = rays.directions.ptr<cv::Vec3f>(y);
        const auto* v = rays.valid.ptr<std::uint8_t>(y);
        for (int x = 0; x < sky_parse.cols; ++x) {
            if (row[x] != kCloud || v[x] == 0 || lum[x] < settings.glare_luminance) {
                continue;
            }
            if (d[x].dot(s) >= cos_limit) {
                row[x] = static_cast<std::uint8_t>(kGlare);
                ++changed;
            }
        }
    }
    return changed;
}

}  // namespace cloudscope::ai
