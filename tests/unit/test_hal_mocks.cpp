#include "hal_contract.hpp"
#include "mock_devices.hpp"
#include "test_support.hpp"

#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);  // a moment in September 2026

}  // namespace

// ---------------------------------------------------------------------- the mocks obey the contracts

TEST_CASE("the mock camera obeys the camera contract", "[hal][contract][mock]")
{
    const ManualClock clock(kStart);
    check_camera_contract([&clock] { return std::make_shared<MockCamera>(clock); });
}

TEST_CASE("the mock mount obeys the mount contract", "[hal][contract][mock]")
{
    ManualClock clock(kStart);
    check_mount_contract({.make = [&clock] { return std::make_shared<MockMount>(clock); },
                          .pass_time = [&clock](std::chrono::milliseconds time) { clock.advance(time); },
                          .tolerance = Degrees(1e-9)});
}

TEST_CASE("the mock IMU obeys the IMU contract", "[hal][contract][mock]")
{
    const ManualClock clock(kStart);
    check_imu_contract([&clock] { return std::make_shared<MockImu>(clock); });
}

TEST_CASE("the mock sensor obeys the sensor contract", "[hal][contract][mock]")
{
    const ManualClock clock(kStart);
    check_sensor_contract([&clock] { return std::make_shared<MockSensor>(clock); });
}

TEST_CASE("the in-memory pipe obeys the transport contract", "[hal][contract][mock][threads]")
{
    check_transport_contract([] { return TransportPair(PipeTransport::make_pair()); });
}

TEST_CASE("the mock inference engine obeys the inference contract", "[hal][contract][mock]")
{
    const TempWorkspace workspace;
    const std::filesystem::path model = workspace.write("model.onnx", "not a real model");
    check_inference_contract([] { return std::make_shared<MockInference>(); }, model);
}

// --------------------------------------------------------------------- what the mocks do beyond that

TEST_CASE("the mock camera numbers its frames from zero and fills them with that number", "[hal][mock]")
{
    ManualClock clock(kStart);
    MockCamera camera(clock);
    REQUIRE(camera.open());
    REQUIRE(camera.set_mode({.width = 320, .height = 240, .format = PixelFormat::Gray8, .fps = 120.0}));
    REQUIRE(camera.start());

    Frame frame(std::size_t{320} * 240);
    for (std::uint64_t expected = 0; expected < 3; ++expected) {
        clock.advance(8ms);
        REQUIRE(camera.read_frame(frame, 0ms));
        CHECK(frame.info().sequence == expected);
        CHECK(frame.info().captured.utc == clock.now_utc());
        CHECK(frame.info().simulated);
        CHECK(frame.info().stride == 320);
        CHECK(frame.data().size() == std::size_t{320} * 240);
        CHECK(frame.data().front() == static_cast<std::byte>(expected));
        CHECK(frame.data().back() == static_cast<std::byte>(expected));
    }
}

TEST_CASE("a test can make the mock camera lose frames, deliver none, or be unplugged", "[hal][mock]")
{
    const ManualClock clock(kStart);
    MockCamera camera(clock);
    REQUIRE(camera.open());
    REQUIRE(camera.start());
    Frame frame(std::size_t{640} * 480);

    REQUIRE(camera.read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 0);
    camera.lose_frames(3);
    REQUIRE(camera.read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 4);  // 1, 2 and 3 never arrived

    camera.hold_frames(true);
    CHECK(outcome(camera.read_frame(frame, 0ms)) == "Timeout");
    CHECK(camera.is_streaming());  // a timeout does not end the stream
    camera.hold_frames(false);
    REQUIRE(camera.read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 5);

    camera.unplug();
    CHECK(outcome(camera.read_frame(frame, 0ms)) == "Io");
    CHECK_FALSE(camera.is_streaming());  // the stream has ended
    CHECK(outcome(camera.read_frame(frame, 0ms)) == "Unavailable");
    camera.close();
    CHECK(outcome(camera.open()) == "NotFound");  // gone until it is plugged in again

    camera.plug_in();
    REQUIRE(camera.open());
    REQUIRE(camera.start());
    REQUIRE(camera.read_frame(frame, 0ms));
    CHECK(frame.info().sequence == 0);
}

TEST_CASE("the mock camera rounds a control to its step and says when that changed the request", "[hal][mock]")
{
    const ManualClock clock(kStart);
    MockCamera camera(clock);
    REQUIRE(camera.open());

    const auto exact = camera.set_control(hal::CameraControl::Gain, {.value = 6.5, .automatic = false});
    REQUIRE(exact);
    CHECK(exact->applied);
    CHECK(exact->effective.value == 6.5);

    const auto rounded = camera.set_control(hal::CameraControl::Gain, {.value = 6.7, .automatic = false});
    REQUIRE(rounded);
    CHECK_FALSE(rounded->applied);
    CHECK(rounded->requested.value == 6.7);
    CHECK(rounded->effective.value == 6.5);  // the step is 0.5 dB
    CHECK(camera.control(hal::CameraControl::Gain)->value == 6.5);

    // Asking for "automatic" on a control that has none is reported, not obeyed.
    const auto automatic = camera.set_control(hal::CameraControl::Gain, {.value = 3.0, .automatic = true});
    REQUIRE(automatic);
    CHECK_FALSE(automatic->applied);
    CHECK_FALSE(automatic->effective.automatic);
}

TEST_CASE("the mock mount moves both axes in a straight line and they arrive together", "[hal][mock]")
{
    ManualClock clock(kStart);
    MockMount mount(clock);
    REQUIRE(mount.open());
    // From pan 0, tilt 45 to pan 60, tilt 15: pan has further to go, so it moves at the commanded speed.
    REQUIRE(mount.move_to({.pan = Degrees(60.0), .tilt = Degrees(15.0)}, 30.0));

    clock.advance(500ms);
    auto status = mount.status();
    REQUIRE(status);
    CHECK_THAT(status->position.pan.value(), WithinAbs(15.0, 1e-9));
    CHECK_THAT(status->position.tilt.value(), WithinAbs(37.5, 1e-9));
    CHECK(to_string(status->motion) == "moving");
    CHECK_FALSE(status->position_measured);  // it reports where it was told to be

    clock.advance(1500ms);  // 2 s in all: 60 degrees at 30 degrees per second
    status = mount.status();
    REQUIRE(status);
    CHECK(status->position == hal::MountPosition{.pan = Degrees(60.0), .tilt = Degrees(15.0)});
    CHECK(to_string(status->motion) == "idle");
    CHECK(status->time.utc == clock.now_utc());
}

TEST_CASE("a fault reported by the mock mount ends the move and blocks new ones until it clears", "[hal][mock]")
{
    ManualClock clock(kStart);
    MockMount mount(clock);
    REQUIRE(mount.open());
    const hal::MountPosition target{.pan = Degrees(60.0), .tilt = Degrees(45.0)};
    REQUIRE(mount.move_to(target, 30.0));
    clock.advance(1000ms);

    mount.set_fault("pan axis stalled");
    auto status = mount.status();
    REQUIRE(status);
    CHECK(to_string(status->motion) == "fault");
    CHECK(status->fault == "pan axis stalled");
    const auto refused = mount.move_to(target, 30.0);
    CHECK(outcome(refused) == "Unavailable");
    CHECK(refused.error().message.find("pan axis stalled") != std::string::npos);

    clock.advance(5000ms);
    status = mount.status();
    REQUIRE(status);
    CHECK_THAT(status->position.pan.value(), WithinAbs(30.0, 1e-9));  // it did not go on

    mount.set_fault("");
    REQUIRE(mount.move_to(target, 30.0));
    clock.advance(1000ms);
    status = mount.status();
    REQUIRE(status);
    CHECK(to_string(status->motion) == "idle");
    CHECK(status->fault.empty());
}

TEST_CASE("the mock IMU and sensor report what a test gives them", "[hal][mock]")
{
    ManualClock clock(kStart, TimeSource::GpsPps);
    MockImu imu(clock);
    REQUIRE(imu.open());
    imu.set_orientation({.w = 0.0, .x = 1.0, .y = 0.0, .z = 0.0});
    const auto sample = imu.read();
    REQUIRE(sample);
    CHECK(sample->orientation.x == 1.0);
    CHECK(sample->time.utc == kStart);
    CHECK(to_string(sample->time.source) == "gps-pps");

    MockSensor sensor(clock);
    REQUIRE(sensor.open());
    sensor.set_value(hal::SensorQuantity::Temperature, 31.5);
    sensor.set_value(hal::SensorQuantity::Pressure, std::nullopt);  // listed, but no value at the moment
    clock.advance(1s);
    const auto readings = sensor.read();
    REQUIRE(readings);
    REQUIRE(readings->size() == 2);
    CHECK(to_string(readings->at(0).quantity) == "temperature");
    CHECK(readings->at(0).value == 31.5);
    CHECK(readings->at(0).time.utc == kStart + 1s);
    CHECK(to_string(readings->at(1).quantity) == "relative_humidity");
    CHECK(sensor.quantities()->size() == 3);  // the list of quantities does not shrink
}

TEST_CASE("writing into a pipe whose other end is closed fails, and unread bytes are discarded on close", "[hal][mock]")
{
    const auto [a, b] = PipeTransport::make_pair();
    REQUIRE(a->open());
    const std::array<std::byte, 4> data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    CHECK(outcome(a->write(data)) == "Io");  // nobody listens at the other end

    REQUIRE(b->open());
    REQUIRE(a->write(data));
    b->close();
    REQUIRE(b->open());
    std::array<std::byte, 4> buffer{};
    const auto got = b->read(buffer, 0ms);
    REQUIRE(got);
    CHECK(*got == 0);  // what was sent to the closed end is gone
}

TEST_CASE("the mock model sums and doubles each row of its input", "[hal][mock]")
{
    const TempWorkspace workspace;
    MockInference engine;
    REQUIRE(engine.open());
    REQUIRE(engine.load(workspace.write("model.onnx", "x")));

    const auto outputs = engine.run({{.name = "values", .shape = {2, 4}, .data = {1, 2, 3, 4, 10, 20, 30, 40}}});
    REQUIRE(outputs);
    REQUIRE(outputs->size() == 2);
    CHECK(outputs->at(0).name == "sum");
    CHECK(outputs->at(0).shape == std::vector<std::int64_t>{2, 1});
    CHECK(outputs->at(0).data == std::vector<float>{10.0F, 100.0F});
    CHECK(outputs->at(1).name == "doubled");
    CHECK(outputs->at(1).shape == std::vector<std::int64_t>{2, 4});
    CHECK(outputs->at(1).data == std::vector<float>{2, 4, 6, 8, 20, 40, 60, 80});

    // A second row that is cut short is refused as a whole.
    CHECK(outcome(engine.run({{.name = "values", .shape = {2, 4}, .data = {1, 2, 3, 4, 5}}})) == "InvalidArgument");
}
