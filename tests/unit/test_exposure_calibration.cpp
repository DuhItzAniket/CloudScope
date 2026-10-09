#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/decode.hpp>
#include <cloudscope/capture/exposure.hpp>
#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/sim/sim_camera.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using Catch::Matchers::WithinAbs;

namespace {

// A sky with bright cloud tops and the Sun: luma `cloud` for the clouds, `sky` elsewhere, the Sun clipped.
cv::Mat scene(int cloud, int sky)
{
    cv::Mat image(240, 320, CV_8UC1, cv::Scalar(sky));
    cv::ellipse(image, cv::Point(100, 150), cv::Size(70, 35), 0, 0, 360, cv::Scalar(cloud), cv::FILLED);
    cv::circle(image, cv::Point(250, 60), 14, cv::Scalar(255), cv::FILLED);
    return image;
}

}  // namespace

TEST_CASE("metering leaves the Sun out and finds the cloud tops", "[capture][exposure]")
{
    const cv::Mat image = scene(200, 90);
    const FrameStatistics stats = compute_statistics(image);
    REQUIRE(stats.sun.found);
    CHECK(sun_aware_percentile(image, stats.sun, 0.999, 2.5) == 200.0);  // the Sun's 255 is gone
    CHECK(sun_aware_percentile(image, SunBlob{}, 0.999, 2.5) == 255.0);  // without exclusion the Sun dominates
    CHECK(clipped_outside_sun(image, stats.sun, 2.5) == 0.0);
    CHECK(clipped_outside_sun(image, SunBlob{}, 2.5) > 0.0);
}

TEST_CASE("the sky controller brightens dark skies, darkens clipping ones and holds within the dead band",
          "[capture][exposure]")
{
    SkyExposureSettings settings;
    settings.max_exposure_ms = 50.0;
    settings.max_gain = 30.0;
    const SkyExposureController controller(settings);

    const ExposureDecision dark = controller.update(scene(120, 40), 4.0, 0.0);
    CHECK(dark.changed);
    CHECK(dark.exposure_ms > 4.0);
    CHECK(dark.exposure_ms <= 8.0);  // at most one step of 2x
    CHECK(dark.gain == 0.0);

    const ExposureDecision clipping = controller.update(scene(255, 180), 4.0, 0.0);
    CHECK(clipping.changed);
    CHECK_THAT(clipping.exposure_ms, WithinAbs(2.0, 1e-9));  // all clipped: a full step down

    const ExposureDecision fine = controller.update(scene(232, 100), 4.0, 0.0);
    CHECK_FALSE(fine.changed);
    CHECK(fine.reason == "within the dead band");

    // Out of exposure range the remaining factor goes to gain, and gain is given back before exposure is cut.
    const ExposureDecision long_exposure = controller.update(scene(60, 20), 40.0, 0.0);
    CHECK_THAT(long_exposure.exposure_ms, WithinAbs(50.0, 1e-9));
    CHECK(long_exposure.gain > 0.0);
    const ExposureDecision give_back = controller.update(scene(255, 200), 50.0, 20.0);
    CHECK(give_back.gain < 20.0);
    CHECK_THAT(give_back.exposure_ms, WithinAbs(50.0, 1e-9));

    CHECK(bracket_exposures(8.0, {-2.0, 0.0, 2.0}, 1.0, 20.0) == std::vector<double>{2.0, 8.0, 20.0});
}

TEST_CASE("the sky controller clips fewer cloud pixels than the simulated camera's own automatic exposure",
          "[capture][exposure][sim]")
{
    // The synthetic sky has an exposure model and its own automatic exposure (P018). Run both for a while and
    // compare the clipped fraction outside the Sun.
    const auto source = sim::make_synthetic_sky_source({.seed = 3, .cloud_fraction = 0.6, .sun_visible = true});
    const hal::CameraCapabilities& caps = source->capabilities();
    const hal::CameraMode mode = caps.modes.front();
    REQUIRE(outcome(source->prepare(mode)) == "ok");
    std::vector<hal::ControlSetting> settings;
    settings.reserve(caps.controls.size());
    for (const hal::ControlInfo& info : caps.controls) {
        settings.push_back({.value = info.default_value, .automatic = false});
    }
    const auto index_of = [&](hal::CameraControl control) {
        for (std::size_t i = 0; i < caps.controls.size(); ++i) {
            if (caps.controls[i].control == control) {
                return i;
            }
        }
        return caps.controls.size();
    };
    const std::size_t exposure = index_of(hal::CameraControl::Exposure);
    REQUIRE(exposure < caps.controls.size());
    Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
    const auto render = [&](std::uint64_t index) {
        REQUIRE(outcome(source->render(
                    {.index = index, .stream_time_s = static_cast<double>(index) * 0.1, .settings = settings},
                    frame)) == "ok");
        frame.info() = {.sequence = index,
                        .captured = {},
                        .width = mode.width,
                        .height = mode.height,
                        .format = mode.format,
                        .stride = static_cast<std::size_t>(mode.width) * bytes_per_pixel(mode.format),
                        .simulated = true};
        const auto image = decode_gray8(frame);
        REQUIRE(outcome(image) == "ok");
        return *image;
    };

    // The camera's automatic exposure, settled.
    settings[exposure].automatic = true;
    cv::Mat auto_image;
    for (std::uint64_t i = 0; i < 30; ++i) {
        auto_image = render(i);
    }
    const SunBlob auto_sun = compute_statistics(auto_image).sun;
    const double auto_clipped = clipped_outside_sun(auto_image, auto_sun, 2.5);

    // The sky controller, from the same start, settled.
    settings[exposure] = {.value = caps.controls[exposure].default_value, .automatic = false};
    SkyExposureSettings sky_settings;
    sky_settings.min_exposure_ms = caps.controls[exposure].minimum;
    sky_settings.max_exposure_ms = caps.controls[exposure].maximum;
    const SkyExposureController controller(sky_settings);
    cv::Mat sky_image;
    for (std::uint64_t i = 100; i < 140; ++i) {
        sky_image = render(i);
        const ExposureDecision decision = controller.update(sky_image, settings[exposure].value, 0.0);
        settings[exposure].value = hal::nearest_setting(caps.controls[exposure], decision.exposure_ms);
    }
    const FrameStatistics sky_stats = compute_statistics(sky_image);
    const double sky_clipped = clipped_outside_sun(sky_image, sky_stats.sun, 2.5);
    CAPTURE(auto_clipped, sky_clipped, settings[exposure].value);
    CHECK(sky_clipped <= auto_clipped);
    CHECK(sky_clipped < 0.01);
    CHECK(sun_aware_percentile(sky_image, sky_stats.sun, 0.99, 2.5) >= 150.0);  // not driven dark either
}

TEST_CASE("exposure fusion keeps highlights and shadows from a bracket", "[capture][exposure]")
{
    // A scene with a dark and a bright half: the short exposure keeps the bright half, the long one the dark half.
    cv::Mat radiance(64, 96, CV_32FC3);
    radiance(cv::Rect(0, 0, 48, 64)).setTo(cv::Scalar(0.05, 0.05, 0.05));
    radiance(cv::Rect(48, 0, 48, 64)).setTo(cv::Scalar(0.9, 0.9, 0.9));
    cv::Mat tint(64, 96, CV_32FC3, cv::Scalar(1.0, 0.9, 0.8));
    cv::multiply(radiance, tint, radiance);
    std::vector<cv::Mat> bracket;
    for (const double gain : {1.0, 4.0, 16.0}) {
        cv::Mat exposed;
        radiance.convertTo(exposed, CV_8UC3, 255.0 * gain);
        bracket.push_back(exposed);
    }
    const cv::Mat fused = merge_mertens(bracket);
    REQUIRE(fused.type() == CV_8UC3);
    REQUIRE(fused.size() == radiance.size());
    const cv::Scalar dark_half = cv::mean(fused(cv::Rect(0, 0, 44, 64)));
    const cv::Scalar bright_half = cv::mean(fused(cv::Rect(52, 0, 44, 64)));
    CHECK(dark_half[0] > 30.0);     // lifted out of the shadows (0.05 * 255 = 13 in the shortest exposure)
    CHECK(bright_half[0] < 250.0);  // not clipped (the long exposures clip it)
    CHECK(bright_half[0] > dark_half[0]);
    CHECK(cv::norm(merge_mertens({bracket[0]}), bracket[0], cv::NORM_INF) == 0.0);
    CHECK(merge_mertens({}).empty());
}

TEST_CASE("dark and flat masters correct offset and vignetting, and the flat gets flatter", "[capture][calibration]")
{
    const cv::Size size(160, 120);
    // The truth: an even field under a vignetting of 1 - 0.4 r^2, a fixed-pattern dark offset, and shot noise.
    VignettingModel truth{.a = -0.4, .b = 0.0, .centre_x = (size.width - 1) / 2.0, .centre_y = (size.height - 1) / 2.0};
    const cv::Mat gain_truth = vignetting_gain(truth, size);  // 1 / g normalised
    cv::Mat shading(size, CV_32FC1);
    cv::divide(1.0, gain_truth, shading);
    shading /= cv::mean(shading)[0];
    cv::Mat dark_pattern(size, CV_32FC1);
    cv::randu(dark_pattern, 8.0, 16.0);
    cv::RNG rng(7);
    std::vector<cv::Mat> darks;
    std::vector<cv::Mat> flats;
    for (int i = 0; i < 16; ++i) {
        cv::Mat noise(size, CV_32FC1);
        rng.fill(noise, cv::RNG::NORMAL, 0.0, 2.0);
        cv::Mat dark8;
        cv::Mat(dark_pattern + noise).convertTo(dark8, CV_8UC1);
        darks.push_back(dark8);
        cv::Mat flat8;
        cv::Mat(dark_pattern + shading * 180.0 + noise).convertTo(flat8, CV_8UC1);
        flats.push_back(flat8);
    }
    const auto dark = average_frames(darks);
    const auto flat = average_frames(flats);
    REQUIRE(outcome(dark) == "ok");
    REQUIRE(outcome(flat) == "ok");
    CHECK(dark->frames == 16);
    CHECK_THAT(cv::mean(dark->mean)[0], WithinAbs(12.0, 1.0));
    CHECK_THAT(cv::mean(dark->stddev)[0], WithinAbs(2.0, 0.6));

    const auto gain = gain_map(*flat, &*dark);
    REQUIRE(outcome(gain) == "ok");
    CHECK_THAT(cv::mean(*gain)[0], WithinAbs(1.0, 0.05));

    // A picture of an even scene through the same camera comes out even after correction.
    cv::Mat picture8;
    cv::Mat(dark_pattern + shading * 120.0).convertTo(picture8, CV_8UC1);
    const double before = uniformity_spread(picture8);
    const auto corrected = apply_calibration(picture8, dark->mean, *gain);
    REQUIRE(outcome(corrected) == "ok");
    const double after = uniformity_spread(*corrected);
    CAPTURE(before, after);
    CHECK(before > 0.2);
    CHECK(after < before * 0.25);
    CHECK_THAT(cv::mean(*corrected)[0], WithinAbs(120.0, 3.0));

    const auto model =
        fit_vignetting(*corrected.and_then([&](const cv::Mat&) { return Expected<cv::Mat>(flats.front()); }));
    REQUIRE(outcome(model) == "ok");
    CHECK_THAT(model->a, WithinAbs(-0.4, 0.08));  // the dark offset pulls the fit a little; the shape is recovered
    CHECK(model->rms_residual < 0.05);
    const cv::Mat vignetting_corrected = vignetting_gain(*model, size);
    CHECK_THAT(cv::mean(vignetting_corrected)[0], WithinAbs(1.0, 1e-3));

    CHECK(outcome(average_frames({})) == "InvalidArgument");
    CHECK(outcome(apply_calibration(picture8, cv::Mat::zeros(10, 10, CV_32FC1), cv::Mat())) == "InvalidArgument");
}

TEST_CASE("masters round-trip through TIFF and sidecar", "[capture][calibration]")
{
    const TempWorkspace workspace;  // its path holds a non-ASCII character: image files must still work there
    cv::Mat a(20, 30, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::Mat b(20, 30, CV_8UC3, cv::Scalar(12, 22, 32));
    const auto master = average_frames({a, b});
    REQUIRE(outcome(master) == "ok");
    const std::filesystem::path file = workspace.path("dark.tiff");
    REQUIRE(outcome(save_master(*master, file)) == "ok");
    REQUIRE(std::filesystem::exists(file));
    REQUIRE(std::filesystem::exists(workspace.path("dark.tiff.json")));
    const auto loaded = load_master(file);
    REQUIRE(outcome(loaded) == "ok");
    CHECK(loaded->frames == 2);
    CHECK(loaded->depth == CV_8U);
    CHECK(loaded->mean.type() == CV_32FC3);
    CHECK(cv::norm(loaded->mean, master->mean, cv::NORM_INF) < 0.01);
    CHECK(outcome(load_master(workspace.path("missing.tiff"))) == "NotFound");
}
