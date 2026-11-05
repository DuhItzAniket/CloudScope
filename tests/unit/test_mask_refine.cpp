#include <cloudscope/ai/mask_refine.hpp>

#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>

using cloudscope::ai::apply_glare_rule;
using cloudscope::ai::find_sun;
using cloudscope::ai::GeometryInputs;
using cloudscope::ai::guided_filter;
using cloudscope::ai::luminance_of;
using cloudscope::ai::MaskRefineSettings;
using cloudscope::ai::ray_field;
using cloudscope::ai::refine_classes;

namespace {

// A zenith-pointing equidistant camera filling the square: the zenith angle grows linearly with the distance from
// the centre and reaches 90 degrees on the inscribed circle; outside it the ray map is invalid. The Sun-aligned
// frame is irrelevant to these tests (every rule is rotation-free), so the x axis is the image's.
GeometryInputs hemisphere_geometry(int g, double sun_zenith_deg)
{
    GeometryInputs geometry;
    const auto plane = static_cast<std::size_t>(g) * static_cast<std::size_t>(g);
    geometry.ray_map.assign(std::size_t{4} * plane, 0.0F);
    for (int r = 0; r < g; ++r) {
        for (int c = 0; c < g; ++c) {
            const double u = ((c + 0.5) / g - 0.5) * 2.0;
            const double v = ((r + 0.5) / g - 0.5) * 2.0;
            const double rho = std::hypot(u, v);
            if (rho > 1.0) {
                continue;
            }
            const double zenith = rho * std::numbers::pi / 2.0;
            const double azimuth = std::atan2(v, u);
            const auto at = static_cast<std::size_t>(r) * static_cast<std::size_t>(g) + static_cast<std::size_t>(c);
            geometry.ray_map[at] = static_cast<float>(std::sin(zenith) * std::cos(azimuth));
            geometry.ray_map[plane + at] = static_cast<float>(std::sin(zenith) * std::sin(azimuth));
            geometry.ray_map[2 * plane + at] = static_cast<float>(std::cos(zenith));
            geometry.ray_map[3 * plane + at] = 1.0F;
        }
    }
    const double z = sun_zenith_deg * std::numbers::pi / 180.0;
    geometry.meta = {static_cast<float>(std::cos(z)), static_cast<float>(std::sin(z)), 1.0F};
    geometry.valid = true;
    return geometry;
}

int first_edge(const cv::Mat& classes, int row)
{
    for (int x = 1; x < classes.cols; ++x) {
        if (classes.at<std::uint8_t>(row, x) != classes.at<std::uint8_t>(row, x - 1)) {
            return x;
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("the guided filter leaves flat regions alone and the refined edge follows the picture, not the grid",
          "[ai][mask]")
{
    const cv::Size size(512, 512);
    cv::Mat flat(size, CV_32FC1, cv::Scalar(0.4F));
    const cv::Mat same = guided_filter(flat, flat, 8, 1.0e-3F);
    CHECK(std::abs(same.at<float>(100, 100) - 0.4F) < 1.0e-4F);

    cv::Mat guide(size, CV_32FC1, cv::Scalar(0.2F));
    guide(cv::Rect(262, 0, 250, 512)).setTo(0.8F);  // the picture's edge at column 262
    cv::Mat p0(128, 128, CV_32FC1, cv::Scalar(0.0F));
    p0(cv::Rect(0, 0, 64, 128)).setTo(1.0F);  // the coarse mask's edge at column 64, i.e. 256 once upsampled
    cv::Mat p1;
    cv::subtract(cv::Scalar(1.0F), p0, p1);
    const cv::Mat classes = refine_classes({p0, p1}, guide, MaskRefineSettings{});
    REQUIRE(classes.size() == size);
    CHECK(classes.at<std::uint8_t>(256, 10) == 0);
    CHECK(classes.at<std::uint8_t>(256, 500) == 1);
    const int edge = first_edge(classes, 256);
    INFO("refined edge at column " << edge);
    CHECK(edge >= 260);  // off the grid's 256 ...
    CHECK(edge <= 264);  // ... and onto the picture's 262

    // a plain bilinear upsampling (no guide information) keeps the edge on the grid
    cv::Mat uniform(size, CV_32FC1, cv::Scalar(0.5F));
    const int grid_edge = first_edge(refine_classes({p0, p1}, uniform, MaskRefineSettings{}), 256);
    CHECK(grid_edge >= 254);
    CHECK(grid_edge <= 258);
}

TEST_CASE("the ray field interpolates unit directions and knows when the geometry is unknown", "[ai][mask]")
{
    const int g = 32;
    const cv::Size size(512, 512);
    const auto rays = ray_field(hemisphere_geometry(g, 30.0), g, g, size);
    REQUIRE(rays.ok);
    CHECK(rays.valid.at<std::uint8_t>(256, 256) == 1);
    CHECK(rays.valid.at<std::uint8_t>(2, 2) == 0);  // outside the inscribed circle
    const cv::Vec3f up = rays.directions.at<cv::Vec3f>(256, 256);
    CHECK(std::abs(cv::norm(up) - 1.0) < 1.0e-3);
    CHECK(up[2] > 0.99F);
    CHECK_FALSE(ray_field(GeometryInputs{}, g, g, size).ok);
    GeometryInputs wrong_size;
    wrong_size.valid = true;
    wrong_size.ray_map.assign(7, 1.0F);
    CHECK_FALSE(ray_field(wrong_size, g, g, size).ok);
}

TEST_CASE("the glare rule finds the Sun on the ring of its zenith angle and turns saturated cloud there into glare",
          "[ai][mask]")
{
    const int g = 32;
    const cv::Size size(512, 512);
    const GeometryInputs geometry = hemisphere_geometry(g, 30.0);
    const auto rays = ray_field(geometry, g, g, size);
    REQUIRE(rays.ok);
    // the Sun at a zenith angle of 30 degrees along +x: a third of the way from the centre to the rim
    const cv::Point sun_true(256 + 85, 256);
    const cv::Point cumulus(100, 400);  // a bright cumulus, 74 degrees from the zenith and far from the Sun
    cv::Mat rgb(size, CV_8UC3, cv::Scalar(90, 120, 160));
    cv::circle(rgb, sun_true, 20, cv::Scalar(255, 255, 255), cv::FILLED);
    cv::circle(rgb, cumulus, 20, cv::Scalar(255, 255, 255), cv::FILLED);
    const cv::Mat lum = luminance_of(rgb);
    CHECK(lum.at<float>(sun_true) == 1.0F);
    CHECK(std::abs(lum.at<float>(10, 10) - 160.0F / 255.0F) < 1.0e-5F);

    const MaskRefineSettings settings;
    const auto sun = find_sun(lum, rays, geometry, settings);
    REQUIRE(sun.has_value());
    CHECK(std::abs(sun->x - sun_true.x) <= 24);
    CHECK(std::abs(sun->y - sun_true.y) <= 24);

    cv::Mat sky(size, CV_8UC1, cv::Scalar(2));  // the model calls everything cloud
    const int changed = apply_glare_rule(sky, lum, rays, *sun, settings);
    CHECK(changed > 500);
    CHECK(sky.at<std::uint8_t>(sun_true) == 3);
    CHECK(sky.at<std::uint8_t>(cumulus) == 2);              // beyond 25 degrees: stays what the model said
    CHECK(sky.at<std::uint8_t>(cv::Point(256, 256)) == 2);  // not saturated: stays cloud

    // no saturated plateau on the Sun's ring: no Sun, nothing changes
    cv::Mat dull(size, CV_8UC3, cv::Scalar(90, 120, 160));
    cv::circle(dull, cumulus, 20, cv::Scalar(255, 255, 255), cv::FILLED);
    CHECK_FALSE(find_sun(luminance_of(dull), rays, geometry, settings).has_value());
    // unknown geometry: no Sun
    CHECK_FALSE(find_sun(lum, ray_field(GeometryInputs{}, g, g, size), GeometryInputs{}, settings).has_value());
}
