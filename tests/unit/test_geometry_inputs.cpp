// P067: the ray map and the Sun metadata match STRATIA's reference implementation on the shared vectors, and
// unknown geometry gives the all-zero tensor.

#include "test_support.hpp"

#include <cloudscope/ai/geometry_inputs.hpp>
#include <cloudscope/calibration/intrinsics.hpp>
#include <cloudscope/geometry/rotation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cmath>
#include <fstream>
#include <numbers>

using namespace cloudscope;
using namespace cloudscope::ai;
using Catch::Matchers::WithinAbs;
using cloudscope::test::data_path;

namespace {

constexpr double kVectorTolerance = 2e-5;  // the vectors are stored with six decimals

Quaternion cloudscope_pose(double azimuth_deg, double elevation_deg, double roll_deg)
{
    // As the desktop's SkyProjection composes it: the roll in the camera frame first, then the pointing.
    const Quaternion level = camera_orientation({.azimuth = Degrees(azimuth_deg), .elevation = Degrees(elevation_deg)});
    const Quaternion roll = from_axis_angle({.x = 0.0, .y = 0.0, .z = 1.0}, to_radians(Degrees(roll_deg)));
    return multiply(level, roll);
}

CameraModel nominal_b0268()
{
    const auto model = load_camera_model(data_path("stratia/b0268_nominal.camera.json"));
    REQUIRE(model.has_value());
    return *model;
}

}  // namespace

TEST_CASE("the full-frame resize maps output pixels to source pixels with the pixel-centre convention",
          "[ai][geometry]")
{
    const FullFrameAffine a = full_frame_affine(4656, 3496, 512, 512);
    const cv::Point2d first = a({0.0, 0.0});
    CHECK_THAT(first.x, WithinAbs(0.5 * 4656.0 / 512.0 - 0.5, 1e-9));
    const cv::Point2d last = a({511.0, 511.0});
    CHECK_THAT(last.x, WithinAbs(4656.0 - 0.5 * 4656.0 / 512.0 - 0.5, 1e-9));
    CHECK_THAT(last.y, WithinAbs(3496.0 - 0.5 * 3496.0 / 512.0 - 0.5, 1e-9));
}

TEST_CASE("the Sun-aligned frame has x toward the Sun's azimuth and z up", "[ai][geometry]")
{
    const Vector3 toward_sun =
        sun_aligned({.x = std::sin(0.7), .y = std::cos(0.7), .z = 0.0}, 0.7 * 180.0 / std::numbers::pi);
    CHECK_THAT(toward_sun.x, WithinAbs(1.0, 1e-12));
    CHECK_THAT(toward_sun.y, WithinAbs(0.0, 1e-12));
    const Vector3 up = sun_aligned({.x = 0.0, .y = 0.0, .z = 1.0}, 123.0);
    CHECK_THAT(up.z, WithinAbs(1.0, 1e-12));
    CHECK_THAT(up.x, WithinAbs(0.0, 1e-12));
    SunPosition sun;
    sun.apparent_elevation_deg = 50.0;
    const auto meta = meta_vector(&sun);
    CHECK_THAT(meta[0], WithinAbs(std::cos(40.0 * std::numbers::pi / 180.0), 1e-6));
    CHECK_THAT(meta[1], WithinAbs(std::sin(40.0 * std::numbers::pi / 180.0), 1e-6));
    CHECK(meta[2] == 1.0F);
    CHECK(meta_vector(nullptr) == std::array<float, 3>{0.0F, 0.0F, 0.0F});
}

TEST_CASE("the ray map matches STRATIA's reference vectors for the nominal B0268", "[ai][geometry][vectors]")
{
    std::ifstream in(data_path("stratia/ray_map_reference.json"));
    REQUIRE(in);
    const nlohmann::json reference = nlohmann::json::parse(in);
    const CameraModel model = nominal_b0268();
    CHECK(model.width == 4656);
    const RayMapSpec spec{
        .height = reference.at("size"), .width = reference.at("size"), .patch = reference.at("patch")};
    for (const nlohmann::json& c : reference.at("cases")) {
        INFO(c.at("name").get<std::string>());
        const Quaternion pose = cloudscope_pose(c.at("azimuth_deg"), c.at("elevation_deg"), c.at("roll_deg"));
        SunPosition sun;
        sun.azimuth_deg = c.at("sun_azimuth_deg");
        sun.apparent_elevation_deg = c.at("sun_apparent_elevation_deg");
        sun.elevation_deg = sun.apparent_elevation_deg;
        const GeometryInputs inputs = geometry_inputs(&model, &pose, &sun, spec);
        REQUIRE(inputs.valid);
        REQUIRE(inputs.ray_map.size() == 4 * 32 * 32);
        const nlohmann::json& want = c.at("ray_map");
        std::size_t valid = 0;
        double worst = 0.0;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            for (std::size_t i = 0; i < 1024; ++i) {
                const double got = inputs.ray_map[channel * 1024 + i];
                const double expected = want[channel][i].get<double>();
                worst = std::max(worst, std::abs(got - expected));
                if (channel == 3 && got > 0.5) {
                    ++valid;
                }
            }
        }
        INFO("largest difference " << worst);
        CHECK(worst < kVectorTolerance);
        CHECK(valid == c.at("valid_count").get<std::size_t>());
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK_THAT(inputs.meta[k], WithinAbs(c.at("meta")[k].get<double>(), 1e-6));
        }
    }
}

TEST_CASE("unknown geometry gives the all-zero inputs; a mask and a horizon limit invalidate patches", "[ai][geometry]")
{
    const CameraModel model = nominal_b0268();
    const Quaternion pose = cloudscope_pose(0.0, 90.0, 0.0);
    SunPosition sun;
    sun.azimuth_deg = 100.0;
    sun.apparent_elevation_deg = 45.0;

    const GeometryInputs none = geometry_inputs(nullptr, &pose, &sun);
    CHECK_FALSE(none.valid);
    CHECK(none.ray_map.size() == 4 * 32 * 32);
    for (float v : none.ray_map) {
        CHECK(v == 0.0F);
    }
    CHECK(none.meta == std::array<float, 3>{0.0F, 0.0F, 0.0F});
    CHECK_FALSE(geometry_inputs(&model, nullptr, &sun).valid);
    CHECK_FALSE(geometry_inputs(&model, &pose, nullptr).valid);

    const GeometryInputs all = geometry_inputs(&model, &pose, &sun);
    REQUIRE(all.valid);
    std::size_t valid_all = 0;
    for (std::size_t i = 0; i < 1024; ++i) {
        valid_all += all.ray_map[3 * 1024 + i] > 0.5F ? 1 : 0;
        if (all.ray_map[3 * 1024 + i] > 0.5F) {
            const double n = std::hypot(all.ray_map[i], all.ray_map[1024 + i], all.ray_map[2048 + i]);
            CHECK_THAT(n, WithinAbs(1.0, 1e-5));  // unit directions
        }
    }
    CHECK(valid_all == 1024);  // a 105 degree fisheye looking up sees sky everywhere

    // a mask that hides the left half of the sensor
    cv::Mat mask(model.height, model.width, CV_8UC1, cv::Scalar(255));
    mask(cv::Rect(0, 0, model.width / 2, model.height)).setTo(0);
    const GeometryInputs masked = geometry_inputs(&model, &pose, &sun, {}, &mask);
    std::size_t valid_masked = 0;
    for (std::size_t i = 0; i < 1024; ++i) {
        valid_masked += masked.ray_map[3 * 1024 + i] > 0.5F ? 1 : 0;
    }
    CHECK(valid_masked == 512);
    CHECK(masked.ray_map[3 * 1024] == 0.0F);       // top-left patch: hidden
    CHECK(masked.ray_map[3 * 1024 + 31] == 1.0F);  // top-right patch: visible

    // a horizon limit removes the lowest directions of a tilted camera
    const Quaternion tilted = cloudscope_pose(180.0, 30.0, 0.0);
    const RayMapSpec limited{.min_elevation_deg = 10.0};
    const GeometryInputs low = geometry_inputs(&model, &tilted, &sun, {});
    const GeometryInputs cut = geometry_inputs(&model, &tilted, &sun, limited);
    std::size_t valid_low = 0;
    std::size_t valid_cut = 0;
    for (std::size_t i = 0; i < 1024; ++i) {
        valid_low += low.ray_map[3 * 1024 + i] > 0.5F ? 1 : 0;
        valid_cut += cut.ray_map[3 * 1024 + i] > 0.5F ? 1 : 0;
    }
    CHECK(valid_cut < valid_low);
    CHECK(valid_cut > 0);
}
