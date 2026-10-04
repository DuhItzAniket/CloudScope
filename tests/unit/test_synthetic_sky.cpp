#include <cloudscope/sim/synthetic_sky.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace cloudscope;
using cloudscope::sim::make_synthetic_sky;
using cloudscope::sim::SyntheticSky;
using cloudscope::sim::SyntheticSkySpec;
using Catch::Matchers::WithinAbs;

namespace {

SyntheticSky make(const SyntheticSkySpec& spec)
{
    auto sky = make_synthetic_sky(spec);
    REQUIRE(sky.has_value());
    return *sky;
}

bool identical(const cv::Mat& a, const cv::Mat& b)
{
    return a.size() == b.size() && a.type() == b.type() && cv::norm(a, b, cv::NORM_INF) == 0.0;
}

// Pixels whose three channels are all 255.
cv::Mat saturated(const cv::Mat& bgr)
{
    cv::Mat result;
    cv::inRange(bgr, cv::Scalar(255, 255, 255), cv::Scalar(255, 255, 255), result);
    return result;
}

}  // namespace

TEST_CASE("a synthetic sky has the requested size and the documented formats", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.width = 320;
    spec.height = 200;
    const SyntheticSky sky = make(spec);

    CHECK(sky.image.size() == cv::Size(320, 200));
    CHECK(sky.image.type() == CV_8UC3);
    CHECK(sky.cloud_mask.size() == cv::Size(320, 200));
    CHECK(sky.cloud_mask.type() == CV_8UC1);
    // The mask holds only 0 and 255.
    CHECK(cv::countNonZero(sky.cloud_mask == 255) + cv::countNonZero(sky.cloud_mask == 0) == 320 * 200);
}

TEST_CASE("the same spec gives the same image and another seed gives another image", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.seed = 42;
    const SyntheticSky first = make(spec);
    const SyntheticSky again = make(spec);
    CHECK(identical(first.image, again.image));
    CHECK(identical(first.cloud_mask, again.cloud_mask));
    CHECK(first.saturated_pixels == again.saturated_pixels);

    spec.seed = 43;
    const SyntheticSky other = make(spec);
    CHECK_FALSE(identical(first.image, other.image));
    CHECK_FALSE(identical(first.cloud_mask, other.cloud_mask));
}

TEST_CASE("the cloud mask covers the requested fraction", "[sim][synthetic_sky]")
{
    const double requested = GENERATE(0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0);
    CAPTURE(requested);
    SyntheticSkySpec spec;
    spec.sun_visible = false;
    spec.cloud_fraction = requested;
    spec.seed = 7;
    const SyntheticSky sky = make(spec);

    CHECK_THAT(sky.cloud_fraction, WithinAbs(requested, 0.002));
    const double counted = static_cast<double>(cv::countNonZero(sky.cloud_mask)) / (640.0 * 480.0);
    CHECK_THAT(sky.cloud_fraction, WithinAbs(counted, 1e-12));  // the reported truth is the mask's own count
}

TEST_CASE("cloud pixels are brighter and less blue than clear sky", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.sun_visible = false;
    spec.cloud_fraction = 0.5;
    const SyntheticSky sky = make(spec);

    cv::Mat grey;
    cv::cvtColor(sky.image, grey, cv::COLOR_BGR2GRAY);
    const double cloud_brightness = cv::mean(grey, sky.cloud_mask)[0];
    const double clear_brightness = cv::mean(grey, ~sky.cloud_mask)[0];
    CHECK(cloud_brightness > clear_brightness + 25.0);

    // Clear sky is blue (blue channel well above red); cloud is grey (channels close together).
    const cv::Scalar clear_colour = cv::mean(sky.image, ~sky.cloud_mask);
    const cv::Scalar cloud_colour = cv::mean(sky.image, sky.cloud_mask);
    CHECK(clear_colour[0] - clear_colour[2] > 60.0);
    CHECK(cloud_colour[0] - cloud_colour[2] < clear_colour[0] - clear_colour[2]);
}

TEST_CASE("the Sun is a saturated disc at the stated position and nothing else saturates", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.width = 800;
    spec.height = 600;
    spec.sun_x = 0.25;
    spec.sun_y = 0.6;
    spec.sun_radius_px = 20.0;
    spec.cloud_fraction = 0.8;   // bright clouds everywhere: still no saturation outside the disc
    spec.noise_sigma = 6.0;
    const SyntheticSky sky = make(spec);

    CHECK(sky.sun_visible);
    CHECK_THAT(sky.sun_centre.x, WithinAbs(0.25 * 799.0, 1e-9));
    CHECK_THAT(sky.sun_centre.y, WithinAbs(0.6 * 599.0, 1e-9));
    CHECK(sky.sun_radius_px == 20.0);

    const cv::Mat saturated_mask = saturated(sky.image);
    CHECK(cv::countNonZero(saturated_mask) == sky.saturated_pixels);
    // A rasterised disc of radius 20 has close to pi * r^2 pixels.
    CHECK_THAT(static_cast<double>(sky.saturated_pixels), WithinAbs(std::numbers::pi * 400.0, 0.03 * std::numbers::pi * 400.0));

    // Every saturated pixel is inside the disc; the centroid is the Sun centre.
    std::vector<cv::Point> points;
    cv::findNonZero(saturated_mask, points);
    double sum_x = 0.0;
    double sum_y = 0.0;
    double farthest = 0.0;
    for (const cv::Point& point : points) {
        sum_x += point.x;
        sum_y += point.y;
        farthest = std::max(farthest, std::hypot(point.x - sky.sun_centre.x, point.y - sky.sun_centre.y));
    }
    CHECK(farthest <= 20.0);
    CHECK_THAT(sum_x / static_cast<double>(points.size()), WithinAbs(sky.sun_centre.x, 0.5));
    CHECK_THAT(sum_y / static_cast<double>(points.size()), WithinAbs(sky.sun_centre.y, 0.5));

    // The disc is not counted as cloud, and there is a glow: just outside the disc the sky is brighter
    // than far away from it.
    CHECK(cv::countNonZero(sky.cloud_mask & saturated_mask) == 0);
}

TEST_CASE("the glow around the Sun brightens the sky near the disc", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.cloud_fraction = 0.0;
    spec.noise_sigma = 0.0;
    spec.sun_x = 0.5;
    spec.sun_y = 0.5;
    spec.sun_radius_px = 10.0;
    const SyntheticSky with_sun = make(spec);
    spec.sun_visible = false;
    const SyntheticSky without_sun = make(spec);

    const int cy = static_cast<int>(std::lround(with_sun.sun_centre.y));
    const int cx = static_cast<int>(std::lround(with_sun.sun_centre.x));
    const auto red = [](const SyntheticSky& sky, int y, int x) { return sky.image.at<cv::Vec3b>(y, x)[2]; };
    CHECK(red(with_sun, cy, cx + 13) > red(without_sun, cy, cx + 13) + 60);    // close to the disc: strong glow
    CHECK(red(with_sun, cy, cx + 40) > red(without_sun, cy, cx + 40));         // further out: weaker
    CHECK(red(with_sun, cy, cx + 13) > red(with_sun, cy, cx + 40));
    CHECK(red(with_sun, cy, cx + 100) == red(without_sun, cy, cx + 100));      // beyond six radii: none
}

TEST_CASE("without the Sun no pixel is saturated", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.sun_visible = false;
    spec.cloud_fraction = 1.0;
    spec.noise_sigma = 20.0;
    const SyntheticSky sky = make(spec);
    CHECK_FALSE(sky.sun_visible);
    CHECK(sky.saturated_pixels == 0);
    double brightest = 0.0;
    cv::minMaxLoc(sky.image.reshape(1), nullptr, &brightest);
    CHECK(brightest <= 250.0);
}

TEST_CASE("a Sun partly or fully outside the frame is handled", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.sun_radius_px = 30.0;
    spec.sun_x = 0.0;   // centre on the left edge: about half the disc is visible
    spec.sun_y = 0.5;
    const SyntheticSky half = make(spec);
    const double full_disc = std::numbers::pi * 900.0;
    CHECK(half.saturated_pixels > 0.45 * full_disc);
    CHECK(half.saturated_pixels < 0.60 * full_disc);

    spec.sun_x = -1.0;  // far outside
    const SyntheticSky outside = make(spec);
    CHECK(outside.sun_visible);            // it exists, it is just not in the picture
    CHECK(outside.saturated_pixels == 0);
}

TEST_CASE("noise changes pixel values by about the requested amount", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.sun_visible = false;
    spec.cloud_fraction = 0.0;  // a smooth gradient: all variation between the two images is noise
    spec.noise_sigma = 0.0;
    const SyntheticSky clean = make(spec);
    spec.noise_sigma = 4.0;
    const SyntheticSky noisy = make(spec);

    cv::Mat difference;
    cv::subtract(noisy.image, clean.image, difference, cv::noArray(), CV_32F);
    cv::Scalar mean;
    cv::Scalar deviation;
    cv::meanStdDev(difference.reshape(1), mean, deviation);
    CHECK_THAT(mean[0], WithinAbs(0.0, 0.1));
    CHECK_THAT(deviation[0], WithinAbs(4.0, 0.3));
    CHECK(identical(clean.cloud_mask, noisy.cloud_mask));  // noise does not move the truth
}

TEST_CASE("a full-resolution B0268 frame can be generated", "[sim][synthetic_sky]")
{
    SyntheticSkySpec spec;
    spec.width = 4656;
    spec.height = 3496;
    spec.sun_radius_px = 60.0;
    const SyntheticSky sky = make(spec);
    CHECK(sky.image.total() == 4656ULL * 3496ULL);
    CHECK_THAT(sky.cloud_fraction, WithinAbs(0.4, 0.003));
    CHECK(sky.saturated_pixels > 0);
}

TEST_CASE("an invalid spec is refused", "[sim][synthetic_sky]")
{
    const auto mutate = GENERATE(table<int, const char*>({
        {0, "width and height"},
        {1, "width and height"},
        {2, "cloud_fraction"},
        {3, "cloud_fraction"},
        {4, "sun_radius_px"},
        {5, "noise_sigma"},
        {6, "sun_x and sun_y"},
        {7, "cloud_fraction"},
    }));
    SyntheticSkySpec spec;
    switch (std::get<0>(mutate)) {
    case 0:
        spec.width = 8;
        break;
    case 1:
        spec.height = 9000;
        break;
    case 2:
        spec.cloud_fraction = -0.1;
        break;
    case 3:
        spec.cloud_fraction = 1.5;
        break;
    case 4:
        spec.sun_radius_px = 0.0;
        break;
    case 5:
        spec.noise_sigma = 25.0;
        break;
    case 6:
        spec.sun_x = std::nan("");
        break;
    default:
        spec.cloud_fraction = std::nan("");
        break;
    }
    const auto sky = make_synthetic_sky(spec);
    REQUIRE_FALSE(sky.has_value());
    CHECK(sky.error().code == ErrorCode::InvalidArgument);
    CHECK(sky.error().message.find(std::get<1>(mutate)) != std::string::npos);
}
