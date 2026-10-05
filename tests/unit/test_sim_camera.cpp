#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/frame.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/sim/sim_camera.hpp>
#include <cloudscope/sim/sim_devices.hpp>
#include <cloudscope/sim/sim_rig.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using hal::CameraControl;
using hal::CameraMode;
using sim::SimCamera;
using sim::SimRig;
using sim::SimulationConfig;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);
constexpr CameraMode kSmallBgr{.width = 640, .height = 480, .format = PixelFormat::Bgr8, .fps = 30.0};
constexpr CameraMode kSmallGrey{.width = 640, .height = 480, .format = PixelFormat::Gray8, .fps = 30.0};

std::shared_ptr<SimRig> make_rig(const IClock& clock)
{
    return std::make_shared<SimRig>(clock, SimulationConfig{});
}

// A synthetic-sky camera. Not in real time unless asked for: a read never waits.
std::shared_ptr<SimCamera> sky_camera(std::shared_ptr<SimRig> rig, bool real_time = false,
                                      const sim::SyntheticSkyOptions& options = {})
{
    return std::make_shared<SimCamera>(
        std::move(rig), sim::sim_device_info(sim::kSkyCameraId, hal::DeviceKind::Camera, "Simulated sky camera"),
        sim::make_synthetic_sky_source(options), real_time);
}

std::shared_ptr<SimCamera> replay_camera(std::shared_ptr<SimRig> rig, const std::filesystem::path& folder,
                                         double fps = 2.0, bool real_time = false)
{
    auto source = sim::make_replay_source(folder, fps);
    REQUIRE(outcome(source) == "ok");
    return std::make_shared<SimCamera>(
        std::move(rig),
        sim::sim_device_info(sim::kReplayCameraId, hal::DeviceKind::Camera, "Simulated camera (replay)"),
        std::move(*source), real_time);
}

void start(SimCamera& camera, const CameraMode& mode)
{
    REQUIRE(outcome(camera.open()) == "ok");
    REQUIRE(outcome(camera.set_mode(mode)) == "ok");
    REQUIRE(outcome(camera.start()) == "ok");
}

void set(SimCamera& camera, CameraControl control, double value)
{
    REQUIRE(outcome(camera.set_control(control, {.value = value, .automatic = false})) == "ok");
}

Frame frame_for(const CameraMode& mode)
{
    return Frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
}

// The next frame of a camera streaming BGR8, as an image of its own.
cv::Mat grab(SimCamera& camera, Frame& frame)
{
    REQUIRE(outcome(camera.read_frame(frame, 0ms)) == "ok");
    return cv::Mat(frame.info().height, frame.info().width, CV_8UC3, frame.buffer().data()).clone();
}

// Mean over all pixels and channels.
double level(const cv::Mat& image)
{
    const cv::Scalar mean = cv::mean(image);
    double sum = 0.0;
    for (int channel = 0; channel < image.channels(); ++channel) {
        sum += mean[channel];
    }
    return sum / image.channels();
}

// Mean absolute difference between two pictures of the same size.
double difference(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat delta;
    cv::absdiff(a, b, delta);
    return level(delta);
}

std::string file_bytes(const std::filesystem::path& file)
{
    return read_file(file);
}

cv::Mat decode(const std::string& bytes)
{
    std::string copy = bytes;
    return cv::imdecode(cv::Mat(1, static_cast<int>(copy.size()), CV_8UC1, copy.data()),
                        cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
}

// Writes a picture file into a workspace (through our own file code: the folder name is not ASCII).
// `encoding` is ".jpg" or ".png".
void write_picture(const TempWorkspace& workspace, const std::string& name, const std::string& encoding,
                   const cv::Mat& picture)
{
    std::vector<unsigned char> encoded;
    REQUIRE(cv::imencode(encoding, picture, encoded));
    std::string bytes(encoded.size(), '\0');
    std::memcpy(bytes.data(), encoded.data(), encoded.size());
    workspace.write(name, bytes);
}

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

// ------------------------------------------------------------------------------------- contracts

TEST_CASE("the simulated sky camera obeys the camera contract", "[sim][camera][contract]")
{
    const ManualClock clock(kStart);
    check_camera_contract([&clock] { return sky_camera(make_rig(clock)); });
}

TEST_CASE("the replay camera obeys the camera contract", "[sim][camera][contract][replay]")
{
    const ManualClock clock(kStart);
    check_camera_contract([&clock] { return replay_camera(make_rig(clock), data_dir() / "sky"); });
}

// ------------------------------------------------------------------------- what the sky camera shows

TEST_CASE("the sky camera offers modes up to 4K in six pixel formats and four controls", "[sim][camera]")
{
    const ManualClock clock(kStart);
    const auto camera = sky_camera(make_rig(clock));
    REQUIRE(camera->open());
    const auto capabilities = camera->capabilities();
    REQUIRE(capabilities);
    CHECK(capabilities->modes.size() == 11);
    CHECK(capabilities->modes.front() == kSmallBgr);  // the mode after open()
    CHECK(camera->mode().value() == kSmallBgr);
    CHECK(capabilities->modes.back() ==
          CameraMode{.width = 3840, .height = 2160, .format = PixelFormat::Bgr8, .fps = 15.0});

    REQUIRE(capabilities->controls.size() == 4);
    const hal::ControlInfo& exposure = capabilities->controls[0];
    CHECK(to_string(exposure.control) == "exposure");
    CHECK(exposure.unit == "ms");
    CHECK(exposure.supports_auto);
    CHECK(exposure.calibrated);
    CHECK(exposure.default_value == 10.0);
    CHECK(to_string(capabilities->controls[1].control) == "gain");
    CHECK(capabilities->controls[1].unit == "dB");
    CHECK(to_string(capabilities->controls[2].control) == "white_balance");
    CHECK(to_string(capabilities->controls[3].control) == "brightness");
    CHECK_FALSE(capabilities->controls[3].calibrated);  // the driver's own scale, like on a webcam
    CHECK(camera->info().simulated);
    CHECK(camera->info().id == "sim:camera:sky");
}

TEST_CASE("exposure and gain scale the picture, and the Sun stays saturated whatever the exposure",
          "[sim][camera][controls]")
{
    const ManualClock clock(kStart);
    const auto camera = sky_camera(make_rig(clock));
    start(*camera, kSmallBgr);
    Frame frame = frame_for(kSmallBgr);

    set(*camera, CameraControl::Exposure, 2.5);
    const double quarter = level(grab(*camera, frame));
    set(*camera, CameraControl::Exposure, 5.0);
    const double half = level(grab(*camera, frame));
    CHECK(quarter > 20.0);
    CHECK_THAT(half / quarter, WithinAbs(2.0, 0.1));  // twice the exposure, twice the light

    set(*camera, CameraControl::Exposure, 2.5);
    set(*camera, CameraControl::Gain, 6.0);  // 6 dB is a factor of two
    CHECK_THAT(level(grab(*camera, frame)) / half, WithinAbs(1.0, 0.05));

    // Ten times the reference exposure: nearly everything clips.
    set(*camera, CameraControl::Gain, 0.0);
    set(*camera, CameraControl::Exposure, 100.0);
    CHECK(level(grab(*camera, frame)) > 250.0);

    // At the shortest exposure the sky is black, and the Sun's disc is still saturated.
    set(*camera, CameraControl::Exposure, 0.05);
    const cv::Mat dark = grab(*camera, frame);
    CHECK(level(dark) < 5.0);
    const auto& sun = dark.at<cv::Vec3b>(static_cast<int>(0.3 * 479), static_cast<int>(0.7 * 639));
    CHECK(sun == cv::Vec3b(255, 255, 255));
    cv::Mat saturated;
    cv::inRange(dark, cv::Scalar(255, 255, 255), cv::Scalar(255, 255, 255), saturated);
    CHECK_THAT(cv::countNonZero(saturated), WithinAbs(452.0, 30.0));  // a disc of radius 12: pi * 12 * 12
}

TEST_CASE("white balance tilts the colours and brightness shifts the levels", "[sim][camera][controls]")
{
    const ManualClock clock(kStart);
    const auto camera = sky_camera(make_rig(clock));
    start(*camera, kSmallBgr);
    Frame frame = frame_for(kSmallBgr);
    set(*camera, CameraControl::Exposure, 4.0);  // dark enough that nothing but the Sun clips

    const auto red_over_blue = [&] {
        const cv::Scalar mean = cv::mean(grab(*camera, frame));
        return mean[2] / mean[0];
    };
    set(*camera, CameraControl::WhiteBalance, 3500.0);
    const double cool = red_over_blue();
    set(*camera, CameraControl::WhiteBalance, 8000.0);
    const double warm = red_over_blue();
    CHECK(warm > 1.5 * cool);  // a higher setting makes the picture warmer

    set(*camera, CameraControl::WhiteBalance, 5500.0);
    const double before = level(grab(*camera, frame));
    set(*camera, CameraControl::Brightness, 30.0);
    CHECK_THAT(level(grab(*camera, frame)) - before, WithinAbs(30.0, 1.5));
}

TEST_CASE("sensor noise differs from frame to frame and grows with gain", "[sim][camera][controls]")
{
    const ManualClock clock(kStart);  // the clock stands still: the clouds do not move between frames
    const auto camera = sky_camera(make_rig(clock));
    start(*camera, kSmallBgr);
    Frame frame = frame_for(kSmallBgr);

    const auto frame_to_frame = [&] {
        cv::Mat first;
        cv::Mat second;
        grab(*camera, frame).convertTo(first, CV_16S);
        grab(*camera, frame).convertTo(second, CV_16S);
        cv::Scalar mean;
        cv::Scalar deviation;
        cv::meanStdDev(first - second, mean, deviation);
        return (deviation[0] + deviation[1] + deviation[2]) / 3.0;
    };
    set(*camera, CameraControl::Exposure, 5.0);
    const double quiet = frame_to_frame();
    CHECK(quiet > 1.2);  // 1.5 grey levels per frame: about 2.1 between two frames
    CHECK(quiet < 3.5);

    // 18 dB more gain with an eighth of the exposure: about the same brightness, eight times the noise.
    set(*camera, CameraControl::Gain, 18.0);
    set(*camera, CameraControl::Exposure, 5.0 / 7.94);
    CHECK(frame_to_frame() > 4.0 * quiet);
}

TEST_CASE("automatic exposure steers the picture to a medium level and reports the exposure it chose",
          "[sim][camera][controls]")
{
    const ManualClock clock(kStart);
    for (const double start_ms : {0.5, 200.0}) {
        CAPTURE(start_ms);
        const auto camera = sky_camera(make_rig(clock));
        start(*camera, kSmallBgr);
        Frame frame = frame_for(kSmallBgr);

        set(*camera, CameraControl::Exposure, start_ms);
        const double before = level(grab(*camera, frame));
        CHECK((before < 40.0 || before > 240.0));  // far too dark or far too bright

        const auto automatic = camera->set_control(CameraControl::Exposure, {.value = 0.0, .automatic = true});
        REQUIRE(outcome(automatic) == "ok");
        CHECK(automatic->applied);
        CHECK(automatic->effective.automatic);
        CHECK_THAT(automatic->effective.value, WithinAbs(start_ms, 1e-9));  // what was in effect; it has not chosen yet

        double after = 0.0;
        for (int i = 0; i < 40; ++i) {
            after = level(grab(*camera, frame));
        }
        CHECK_THAT(after, WithinAbs(110.0, 12.0));
        const auto chosen = camera->control(CameraControl::Exposure);
        REQUIRE(outcome(chosen) == "ok");
        CHECK(chosen->automatic);
        CHECK(chosen->value > 2.0);
        CHECK(chosen->value < 40.0);

        // Back to manual: the camera keeps the exposure it is given.
        set(*camera, CameraControl::Exposure, 3.0);
        for (int i = 0; i < 5; ++i) {
            grab(*camera, frame);
        }
        const auto manual = camera->control(CameraControl::Exposure);
        REQUIRE(outcome(manual) == "ok");
        CHECK_FALSE(manual->automatic);
        CHECK_THAT(manual->value, WithinAbs(3.0, 1e-9));
    }
}

TEST_CASE("the clouds drift with time, and the same seed shows the same sky", "[sim][camera]")
{
    ManualClock clock(kStart);
    const auto camera = sky_camera(make_rig(clock));
    const auto twin = sky_camera(make_rig(clock));
    sim::SyntheticSkyOptions other_sky;
    other_sky.seed = 2;
    const auto other = sky_camera(make_rig(clock), false, other_sky);
    start(*camera, kSmallBgr);
    start(*twin, kSmallBgr);
    start(*other, kSmallBgr);
    Frame frame = frame_for(kSmallBgr);

    const cv::Mat at_start = grab(*camera, frame);
    CHECK(difference(at_start, grab(*camera, frame)) < 3.0);  // the next frame: only the noise differs
    CHECK(difference(at_start, grab(*twin, frame)) < 3.0);    // another camera with the same seed
    CHECK(difference(at_start, grab(*other, frame)) > 6.0);   // another seed: other clouds

    clock.advance(60s);
    const cv::Mat later = grab(*camera, frame);
    CHECK(difference(at_start, later) > 6.0);  // a minute on, the clouds have moved

    // The Sun does not drift with the clouds.
    const auto& sun = later.at<cv::Vec3b>(static_cast<int>(0.3 * 479), static_cast<int>(0.7 * 639));
    CHECK(sun == cv::Vec3b(255, 255, 255));
}

TEST_CASE("every pixel format carries the same picture", "[sim][camera][formats]")
{
    const ManualClock clock(kStart);
    const auto camera = sky_camera(make_rig(clock));
    REQUIRE(camera->open());
    const auto one_frame = [&](PixelFormat format, Frame& frame) {
        REQUIRE(outcome(camera->set_mode({.width = 640, .height = 480, .format = format, .fps = 30.0})) == "ok");
        REQUIRE(outcome(camera->start()) == "ok");
        REQUIRE(outcome(camera->read_frame(frame, 0ms)) == "ok");
        camera->stop();
    };

    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 640, 480));
    one_frame(PixelFormat::Bgr8, frame);
    const cv::Mat bgr = cv::Mat(480, 640, CV_8UC3, frame.buffer().data()).clone();
    const cv::Scalar colour = cv::mean(bgr);
    cv::Mat grey;
    cv::cvtColor(bgr, grey, cv::COLOR_BGR2GRAY);
    const double grey_level = cv::mean(grey)[0];
    CHECK(frame.data().size() == std::size_t{640} * 480 * 3);

    one_frame(PixelFormat::Rgb8, frame);
    const cv::Scalar rgb = cv::mean(cv::Mat(480, 640, CV_8UC3, frame.buffer().data()));
    CHECK_THAT(rgb[0], WithinAbs(colour[2], 1.0));  // red first
    CHECK_THAT(rgb[2], WithinAbs(colour[0], 1.0));

    one_frame(PixelFormat::Gray8, frame);
    CHECK(frame.data().size() == std::size_t{640} * 480);
    CHECK_THAT(cv::mean(cv::Mat(480, 640, CV_8UC1, frame.buffer().data()))[0], WithinAbs(grey_level, 1.0));

    one_frame(PixelFormat::Gray16, frame);
    CHECK(frame.data().size() == std::size_t{640} * 480 * 2);
    CHECK(frame.info().stride == 1280);
    const cv::Mat deep(480, 640, CV_16UC1, frame.buffer().data());
    CHECK_THAT(cv::mean(deep)[0] / 257.0, WithinAbs(grey_level, 1.0));
    double brightest = 0.0;
    cv::minMaxLoc(deep, nullptr, &brightest);
    CHECK(brightest == 65535.0);  // the Sun: 255 becomes the 16-bit maximum

    one_frame(PixelFormat::Yuyv, frame);
    CHECK(frame.data().size() == std::size_t{640} * 480 * 2);
    const cv::Mat yuyv(480, 640, CV_8UC2, frame.buffer().data());
    double luma = 0.0;
    double u = 0.0;
    double v = 0.0;
    for (int y = 0; y < 480; ++y) {
        const auto* row = yuyv.ptr<cv::Vec2b>(y);
        for (int x = 0; x < 640; x += 2) {
            luma += row[x][0] + row[x + 1][0];
            u += row[x][1];
            v += row[x + 1][1];
        }
    }
    CHECK_THAT(luma / (640.0 * 480.0), WithinAbs(grey_level, 1.0));
    CHECK(u / (320.0 * 480.0) > 133.0);  // a blue sky: blue-difference above the middle,
    CHECK(v / (320.0 * 480.0) < 123.0);  // red-difference below it

    one_frame(PixelFormat::Mjpeg, frame);
    CHECK(frame.info().stride == 0);
    REQUIRE(frame.data().size() > 1000);
    CHECK(frame.data().size() < std::size_t{640} * 480);  // compressed
    CHECK(frame.data()[0] == std::byte{0xFF});            // a JPEG starts with FF D8
    CHECK(frame.data()[1] == std::byte{0xD8});
    const cv::Mat decoded = cv::imdecode(
        cv::Mat(1, static_cast<int>(frame.data().size()), CV_8UC1, frame.buffer().data()), cv::IMREAD_COLOR);
    REQUIRE(decoded.size() == cv::Size(640, 480));
    CHECK(difference(decoded, bgr) < 6.0);  // the same picture, apart from noise and compression
}

// ----------------------------------------------------------------- sequence numbers, faults, timing

TEST_CASE("lost frames leave a gap in the sequence, a stalled camera times out, a pulled cable ends the stream",
          "[sim][camera][fault]")
{
    ManualClock clock(kStart);
    const auto rig = make_rig(clock);
    const auto camera = sky_camera(rig);
    start(*camera, kSmallGrey);
    Frame frame = frame_for(kSmallGrey);

    for (std::uint64_t expected = 0; expected < 3; ++expected) {
        clock.advance(33ms);
        REQUIRE(camera->read_frame(frame, 0ms));
        CHECK(frame.info().sequence == expected);
        CHECK(frame.info().captured.utc == clock.now_utc());
        CHECK(frame.info().simulated);
    }
    rig->lose_camera_frames(4);
    REQUIRE(camera->read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 7);  // 3, 4, 5 and 6 were lost on the way

    rig->set_camera_stalled(true);
    CHECK(outcome(camera->read_frame(frame, 0ms)) == "Timeout");
    CHECK(camera->is_streaming());  // silent, but still there
    rig->set_camera_stalled(false);
    REQUIRE(camera->read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 8);

    rig->set_camera_connected(false);
    const auto pulled = camera->read_frame(frame, 0ms);
    CHECK(outcome(pulled) == "Io");
    CHECK(pulled.error().message == "camera 'sim:camera:sky' was disconnected");
    CHECK_FALSE(camera->is_streaming());
    CHECK(outcome(camera->read_frame(frame, 0ms)) == "Unavailable");
    CHECK(outcome(camera->start()) == "Io");
    camera->close();
    CHECK(outcome(camera->open()) == "NotFound");

    rig->set_camera_connected(true);
    REQUIRE(camera->open());
    REQUIRE(camera->start());
    REQUIRE(camera->read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 0);
}

TEST_CASE("in real time no frame arrives before the sensor could have made it", "[sim][camera][realtime]")
{
    const SystemClock clock;
    const auto camera = sky_camera(make_rig(clock), true);
    REQUIRE(camera->open());
    REQUIRE(camera->set_mode(kSmallGrey));
    Frame frame = frame_for(kSmallGrey);

    const auto before_start = std::chrono::steady_clock::now();
    REQUIRE(camera->start());
    std::uint64_t previous = 0;
    for (int i = 0; i < 6; ++i) {
        REQUIRE(outcome(camera->read_frame(frame, 2000ms)) == "ok");
        const std::uint64_t sequence = frame.info().sequence;
        // Frame n is complete n + 1 frame periods after the start, at 30 frames per second.
        // (A microsecond of slack: the camera counts its frame period in whole nanoseconds.)
        CHECK(seconds_since(before_start) >= static_cast<double>(sequence + 1) / 30.0 - 1e-6);
        if (i > 0) {
            CHECK(sequence > previous);
        }
        previous = sequence;
    }
    CHECK(previous >= 5);
    CHECK(seconds_since(before_start) < 20.0);
    // The frames carry the time of the clock the rig runs on.
    CHECK(frame.info().captured.monotonic <= clock.now_monotonic());
    CHECK(frame.info().captured.monotonic >= before_start);
}

TEST_CASE("a long exposure lowers the frame rate", "[sim][camera][realtime]")
{
    const SystemClock clock;
    const auto camera = sky_camera(make_rig(clock), true);
    REQUIRE(camera->open());
    REQUIRE(camera->set_mode(kSmallGrey));
    set(*camera, CameraControl::Exposure, 250.0);  // four frames per second at most, whatever the mode says
    Frame frame = frame_for(kSmallGrey);

    const auto before_start = std::chrono::steady_clock::now();
    REQUIRE(camera->start());
    // The first frame takes 250 ms: a read that gives up after 20 ms comes back empty-handed. (Should this
    // thread be held up for a quarter of a second on a busy machine, a frame is allowed, but not an early one.)
    const auto early = camera->read_frame(frame, 20ms);
    CHECK((outcome(early) == "Timeout" || seconds_since(before_start) >= 0.25 - 1e-6));
    REQUIRE(outcome(camera->read_frame(frame, 2000ms)) == "ok");
    CHECK(seconds_since(before_start) >= 0.25 * static_cast<double>(frame.info().sequence + 1) - 1e-6);
    REQUIRE(outcome(camera->read_frame(frame, 2000ms)) == "ok");
    CHECK(seconds_since(before_start) >= 0.25 * static_cast<double>(frame.info().sequence + 1) - 1e-6);
    CHECK(seconds_since(before_start) >= 0.5 - 1e-6);
}

TEST_CASE("a reader that waits for a frame gets a timeout when none is due", "[sim][camera][realtime]")
{
    const SystemClock clock;
    // One picture every two seconds.
    const auto camera = replay_camera(make_rig(clock), data_dir() / "sky", 0.5, true);
    REQUIRE(camera->open());
    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 400, 400));
    REQUIRE(camera->start());

    const auto before = std::chrono::steady_clock::now();
    CHECK(outcome(camera->read_frame(frame, 50ms)) == "Timeout");
    CHECK(seconds_since(before) >= 0.05);  // it waited for the whole timeout
    CHECK(seconds_since(before) < 1.5);    // and no longer
    CHECK(camera->is_streaming());
}

TEST_CASE("a reader that falls behind finds the newest frames waiting and has lost the older ones",
          "[sim][camera][realtime]")
{
    const SystemClock clock;
    const auto camera = sky_camera(make_rig(clock), true);
    start(*camera, kSmallGrey);
    Frame frame = frame_for(kSmallGrey);

    std::this_thread::sleep_for(600ms);                        // the consumer was busy while 18 frames went by
    REQUIRE(outcome(camera->read_frame(frame, 0ms)) == "ok");  // no waiting: a frame was ready
    // Three frames are kept for a late reader; with 18 gone by, the first one it gets is number 15 or so.
    CHECK(frame.info().sequence >= 10);
    const std::uint64_t first = frame.info().sequence;
    REQUIRE(outcome(camera->read_frame(frame, 2000ms)) == "ok");
    CHECK(frame.info().sequence > first);
}

// -------------------------------------------------------------------------------------- replay

TEST_CASE("the replay camera shows the pictures of a folder in name order, again and again", "[sim][camera][replay]")
{
    const ManualClock clock(kStart);
    const std::filesystem::path folder = data_dir() / "sky";
    const std::vector<std::string> names = {"ccsn_ci_n001.jpg", "ccsn_cu_n001.jpg", "ccsn_st_n001.jpg"};
    const auto camera = replay_camera(make_rig(clock), folder, 5.0);
    REQUIRE(camera->open());

    const auto capabilities = camera->capabilities();
    REQUIRE(capabilities);
    REQUIRE(capabilities->modes.size() == 2);
    CHECK(capabilities->modes[0] == CameraMode{.width = 400, .height = 400, .format = PixelFormat::Bgr8, .fps = 5.0});
    CHECK(capabilities->modes[1] == CameraMode{.width = 400, .height = 400, .format = PixelFormat::Mjpeg, .fps = 5.0});
    CHECK(capabilities->controls.empty());  // recorded pictures cannot be re-exposed
    CHECK(camera->info().id == "sim:camera:replay");

    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 400, 400));
    REQUIRE(camera->start());
    for (int i = 0; i < 7; ++i) {  // more than twice round
        CAPTURE(i);
        const cv::Mat shown = grab(*camera, frame);
        const cv::Mat expected = decode(file_bytes(folder / names[static_cast<std::size_t>(i) % 3]));
        REQUIRE(shown.size() == expected.size());
        CHECK(difference(shown, expected) == 0.0);  // exactly the recorded picture
        CHECK(frame.info().sequence == static_cast<std::uint64_t>(i));
        CHECK(frame.info().simulated);  // a replay is not a measurement of now
    }
    camera->stop();

    // In MJPEG mode a JPEG file is passed on byte for byte.
    REQUIRE(camera->set_mode(capabilities->modes[1]));
    REQUIRE(camera->start());
    for (const std::string& name : names) {
        REQUIRE(outcome(camera->read_frame(frame, 0ms)) == "ok");
        const std::string file = file_bytes(folder / name);
        REQUIRE(frame.data().size() == file.size());
        CHECK(std::memcmp(frame.data().data(), file.data(), file.size()) == 0);
        CHECK(frame.info().stride == 0);
    }
}

TEST_CASE("replayed pictures of another size or type are brought to the size of the first", "[sim][camera][replay]")
{
    const ManualClock clock(kStart);
    const TempWorkspace workspace;
    cv::Mat first(120, 160, CV_8UC3, cv::Scalar(200, 100, 50));
    cv::rectangle(first, cv::Rect(40, 30, 80, 60), cv::Scalar(0, 255, 255), cv::FILLED);
    const cv::Mat second(60, 80, CV_8UC3, cv::Scalar(10, 20, 250));  // half the size, lossless
    write_picture(workspace, "replay/a_first.JPG", ".jpg", first);   // upper-case extension
    write_picture(workspace, "replay/b_second.png", ".png", second);
    workspace.write("replay/notes.txt", "not a picture");          // ignored
    workspace.write("replay/sub/c_third.png", "in a sub-folder");  // ignored: only the folder itself

    CHECK(sim::has_replay_pictures(workspace.path("replay")));
    const auto camera = replay_camera(make_rig(clock), workspace.path("replay"));
    REQUIRE(camera->open());
    CHECK(camera->mode()->width == 160);
    CHECK(camera->mode()->height == 120);
    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 160, 120));

    REQUIRE(camera->start());
    const cv::Mat shown_first = grab(*camera, frame);
    CHECK(difference(shown_first, first) < 3.0);  // JPEG is lossy
    const cv::Mat shown_second = grab(*camera, frame);
    REQUIRE(shown_second.size() == cv::Size(160, 120));
    CHECK(shown_second.at<cv::Vec3b>(60, 80) == cv::Vec3b(10, 20, 250));  // enlarged, same colour
    CHECK(difference(grab(*camera, frame), shown_first) == 0.0);          // and round again
    camera->stop();

    // MJPEG mode: the PNG picture is encoded for the stream.
    REQUIRE(camera->set_mode({.width = 160, .height = 120, .format = PixelFormat::Mjpeg, .fps = 2.0}));
    REQUIRE(camera->start());
    REQUIRE(camera->read_frame(frame, 0ms));  // the JPEG file, passed through
    REQUIRE(camera->read_frame(frame, 0ms));  // the PNG file
    CHECK(frame.data()[0] == std::byte{0xFF});
    CHECK(frame.data()[1] == std::byte{0xD8});
    const cv::Mat decoded = cv::imdecode(
        cv::Mat(1, static_cast<int>(frame.data().size()), CV_8UC1, frame.buffer().data()), cv::IMREAD_COLOR);
    REQUIRE(decoded.size() == cv::Size(160, 120));
    CHECK(difference(decoded, shown_second) < 3.0);
}

TEST_CASE("a replay needs a folder with pictures that can be decoded", "[sim][camera][replay]")
{
    const ManualClock clock(kStart);
    const TempWorkspace workspace;
    std::filesystem::create_directories(workspace.path("empty"));
    workspace.write("text/readme.txt", "no pictures here");
    workspace.write("broken/picture.jpg", "this is not a JPEG file");

    const auto missing = sim::make_replay_source(workspace.path("no-such-folder"), 2.0);
    CHECK(outcome(missing) == "NotFound");
    CHECK_THAT(missing.error().message, ContainsSubstring("cannot be read or holds no JPEG or PNG picture"));
    CHECK(outcome(sim::make_replay_source(workspace.path("empty"), 2.0)) == "NotFound");
    CHECK(outcome(sim::make_replay_source(workspace.path("text"), 2.0)) == "NotFound");
    CHECK_FALSE(sim::has_replay_pictures(workspace.path("text")));
    CHECK_FALSE(sim::has_replay_pictures(workspace.path("no-such-folder")));

    const auto broken = sim::make_replay_source(workspace.path("broken"), 2.0);
    CHECK(outcome(broken) == "Parse");
    CHECK_THAT(broken.error().message, ContainsSubstring("picture.jpg is not a picture that can be decoded"));
    CHECK(outcome(sim::make_replay_source(data_dir() / "sky", 0.0)) == "InvalidArgument");

    // A picture that disappears or goes bad while the replay runs ends the stream with an error.
    cv::Mat picture(48, 64, CV_8UC3, cv::Scalar(90, 90, 90));
    write_picture(workspace, "live/a.png", ".png", picture);
    write_picture(workspace, "live/b.png", ".png", picture);
    const auto camera = replay_camera(make_rig(clock), workspace.path("live"));
    REQUIRE(camera->open());
    REQUIRE(camera->start());
    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 64, 48));
    REQUIRE(camera->read_frame(frame, 0ms));
    std::filesystem::remove(workspace.path("live/b.png"));
    const auto gone = camera->read_frame(frame, 0ms);
    CHECK(outcome(gone) == "Io");
    CHECK_THAT(gone.error().message, ContainsSubstring("camera 'sim:camera:replay': "));
    CHECK_FALSE(camera->is_streaming());
}

// Not run with the other tests (hidden tag): prints how fast the simulated camera can make frames.
//   cloudscope-unit-tests "[simbench]"
TEST_CASE("speed of the simulated sky camera in every mode", "[.][simbench]")
{
    const SystemClock clock;
    const auto camera = sky_camera(make_rig(clock));
    REQUIRE(camera->open());
    const auto capabilities = camera->capabilities();
    REQUIRE(capabilities);
    for (const CameraMode& mode : capabilities->modes) {
        REQUIRE(camera->set_mode(mode));
        REQUIRE(camera->start());
        Frame frame = frame_for(mode);
        REQUIRE(camera->read_frame(frame, 0ms));  // the first frame prepares buffers
        const auto began = std::chrono::steady_clock::now();
        int frames = 0;
        while (seconds_since(began) < 1.0) {
            REQUIRE(camera->read_frame(frame, 0ms));
            ++frames;
        }
        const double fps = frames / seconds_since(began);
        std::printf("%4d x %4d %-6s nominal %4.0f fps   can make %7.1f fps\n", mode.width, mode.height,
                    std::string(to_string(mode.format)).c_str(), mode.fps, fps);
        camera->stop();
    }
}
