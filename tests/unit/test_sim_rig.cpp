#include "hal_contract.hpp"

#include <cloudscope/common/clock.hpp>
#include <cloudscope/sim/sim_devices.hpp>
#include <cloudscope/sim/sim_rig.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using hal::MountPosition;
using sim::MountAxis;
using sim::SimMount;
using sim::SimRig;
using sim::SimulationConfig;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);

// A configuration without latency, for tests that compare with hand-calculated motion.
SimulationConfig immediate()
{
    SimulationConfig config;
    config.mount.command_latency = 0ms;
    config.mount.telemetry_latency = 0ms;
    return config;
}

hal::MountStatus status_of(const SimRig& rig)
{
    const auto status = rig.mount_status();
    REQUIRE(outcome(status) == "ok");
    return *status;
}

}  // namespace

TEST_CASE("the simulated mount obeys the mount contract", "[sim][mount][contract]")
{
    ManualClock clock(kStart);
    check_mount_contract(
        {.make = [&clock] { return std::make_shared<SimMount>(std::make_shared<SimRig>(clock, SimulationConfig{})); },
         .pass_time = [&clock](std::chrono::milliseconds time) { clock.advance(time); },
         .tolerance = Degrees(1e-9)});
}

TEST_CASE("the simulated mount with position feedback obeys the mount contract", "[sim][mount][contract]")
{
    ManualClock clock(kStart);
    SimulationConfig config;
    config.mount.position_feedback = true;
    check_mount_contract(
        {.make = [&clock, &config] { return std::make_shared<SimMount>(std::make_shared<SimRig>(clock, config)); },
         .pass_time = [&clock](std::chrono::milliseconds time) { clock.advance(time); },
         .tolerance = Degrees(0.5)});  // ten times the feedback noise
}

TEST_CASE("a command acts after the command latency, and a status describes the past", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimulationConfig config;
    config.mount.command_latency = 50ms;
    config.mount.telemetry_latency = 30ms;
    SimRig rig(clock, config);

    REQUIRE(rig.move_to({.pan = Degrees(60.0), .tilt = Degrees(45.0)}, 30.0));
    clock.advance(49ms);
    CHECK(rig.true_position().pan.value() == 0.0);  // the command is still on its way
    clock.advance(11ms);                            // 60 ms after the command: moving for 10 ms
    CHECK_THAT(rig.true_position().pan.value(), WithinAbs(0.5 * 240.0 * 0.01 * 0.01, 1e-9));

    const hal::MountStatus status = status_of(rig);
    CHECK(status.position.pan.value() == 0.0);    // as it was 30 ms ago: not moving yet
    CHECK(to_string(status.motion) == "moving");  // but the move is known to be under way
    CHECK(status.time.monotonic == clock.now_monotonic() - 30ms);
    CHECK(status.time.utc == clock.now_utc() - 30ms);
    CHECK(status.target == MountPosition{.pan = Degrees(60.0), .tilt = Degrees(45.0)});
}

TEST_CASE("from rest both axes follow a straight line within the limits and arrive together", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimRig rig(clock, immediate());
    // Pan 0 to 60, tilt 45 to 15, at 30 deg/s: pan has further to go and sets the pace.
    // The move takes distance / speed + time to speed up = 60 / 30 + 30 / 240 = 2.125 s.
    const MountPosition target{.pan = Degrees(60.0), .tilt = Degrees(15.0)};
    REQUIRE(rig.move_to(target, 30.0));

    MountPosition previous = rig.true_position();
    double previous_pan_speed = 0.0;
    bool straight = true;
    bool within_limits = true;
    for (int step = 1; step <= 212; ++step) {  // 2.12 s in steps of 10 ms: 5 ms short of the end
        clock.advance(10ms);
        const MountPosition now = rig.true_position();
        // On a straight line, both axes have done the same share of their way.
        straight = straight && std::abs(now.pan.value() / 60.0 - (45.0 - now.tilt.value()) / 30.0) < 1e-9;
        const double pan_speed = (now.pan - previous.pan).value() / 0.01;
        within_limits = within_limits && pan_speed <= 30.0 + 1e-6 &&
                        std::abs(pan_speed - previous_pan_speed) / 0.01 <= 240.0 + 1e-3;
        previous = now;
        previous_pan_speed = pan_speed;
    }
    CHECK(straight);
    CHECK(within_limits);
    CHECK(previous.pan.value() < 60.0);  // neither axis has arrived yet
    CHECK(previous.tilt.value() > 15.0);
    CHECK(to_string(status_of(rig).motion) == "moving");

    clock.advance(5ms);  // 2.125 s: both arrive in the same instant
    CHECK(rig.true_position() == target);
    CHECK(to_string(status_of(rig).motion) == "idle");
}

TEST_CASE("stop brakes the mount to rest; an emergency stop halts it dead", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimRig rig(clock, immediate());
    const MountPosition target{.pan = Degrees(150.0), .tilt = Degrees(45.0)};

    REQUIRE(rig.move_to(target, 60.0));
    clock.advance(1000ms);  // cruising at 60 deg/s
    const double at_stop = rig.true_position().pan.value();
    rig.stop();
    clock.advance(100ms);
    CHECK(to_string(status_of(rig).motion) == "moving");  // still slowing down
    clock.advance(150ms);                                 // 60 / 240 = 0.25 s to stop
    CHECK_THAT(rig.true_position().pan.value(), WithinAbs(at_stop + 60.0 * 60.0 / (2.0 * 240.0), 1e-9));
    CHECK(to_string(status_of(rig).motion) == "stopped");
    clock.advance(5000ms);
    CHECK_THAT(rig.true_position().pan.value(), WithinAbs(at_stop + 7.5, 1e-9));  // and it stays there

    REQUIRE(rig.move_to(target, 60.0));
    clock.advance(500ms);
    const double at_emergency = rig.true_position().pan.value();
    rig.emergency_stop();
    CHECK(rig.true_position().pan.value() == at_emergency);  // no braking distance at all
    clock.advance(1000ms);
    CHECK(rig.true_position().pan.value() == at_emergency);
    const hal::MountStatus status = status_of(rig);
    CHECK(to_string(status.motion) == "stopped");
    CHECK(status.emergency_stop);
    CHECK(outcome(rig.move_to(target, 60.0)) == "Unavailable");
}

TEST_CASE("a new target during a move is blended in without a jump", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimRig rig(clock, immediate());
    REQUIRE(rig.move_to({.pan = Degrees(150.0), .tilt = Degrees(45.0)}, 60.0));
    clock.advance(1000ms);
    const double at_change = rig.true_position().pan.value();
    REQUIRE(rig.move_to({.pan = Degrees(-50.0), .tilt = Degrees(45.0)}, 60.0));  // back the other way

    double previous = at_change;
    double furthest = at_change;
    bool smooth = true;
    for (int step = 0; step < 1000; ++step) {
        clock.advance(5ms);
        const double pan = rig.true_position().pan.value();
        smooth = smooth && std::abs(pan - previous) <= 60.0 * 0.005 + 1e-9;  // never faster than 60 deg/s
        furthest = std::max(furthest, pan);
        previous = pan;
    }
    CHECK(smooth);
    // It could not turn round on the spot: it ran on for the braking distance of 7.5 degrees first.
    CHECK_THAT(furthest, WithinAbs(at_change + 7.5, 0.05));
    CHECK(previous == -50.0);
    CHECK(to_string(status_of(rig).motion) == "idle");
}

TEST_CASE("a stalled axis stays put while a mount without feedback reports that it moved", "[sim][mount][fault]")
{
    ManualClock clock(kStart);
    SimRig rig(clock, immediate());
    rig.set_axis_stalled(MountAxis::Pan, true);
    REQUIRE(rig.move_to({.pan = Degrees(60.0), .tilt = Degrees(15.0)}, 30.0));
    clock.advance(5000ms);

    CHECK(rig.true_position().pan.value() == 0.0);    // the pan axis did not move
    CHECK(rig.true_position().tilt.value() == 15.0);  // the tilt axis did
    const hal::MountStatus status = status_of(rig);
    CHECK(status.position.pan.value() == 60.0);  // the controller reports where it told the servo to be
    CHECK_FALSE(status.position_measured);       // and says honestly that this is not a measurement
    CHECK(to_string(status.motion) == "idle");

    // Released, the axis catches up with its command at its maximum speed of 60 deg/s.
    rig.set_axis_stalled(MountAxis::Pan, false);
    clock.advance(500ms);
    CHECK_THAT(rig.true_position().pan.value(), WithinAbs(30.0, 1e-9));
    clock.advance(600ms);
    CHECK(rig.true_position().pan.value() == 60.0);
}

TEST_CASE("a mount with feedback reports an axis that does not follow, and refuses to go on", "[sim][mount][fault]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.mount.position_feedback = true;
    SimRig rig(clock, config);
    const MountPosition target{.pan = Degrees(60.0), .tilt = Degrees(45.0)};

    rig.set_axis_stalled(MountAxis::Tilt, true);
    REQUIRE(rig.move_to({.pan = Degrees(0.0), .tilt = Degrees(80.0)}, 30.0));
    clock.advance(3000ms);
    hal::MountStatus status = status_of(rig);
    CHECK(to_string(status.motion) == "fault");
    CHECK(status.fault == "the tilt axis does not follow its command");
    CHECK(status.position_measured);
    CHECK_THAT(status.position.tilt.value(), WithinAbs(45.0, 0.5));  // measured: still where it was
    const auto refused = rig.move_to(target, 30.0);
    CHECK(outcome(refused) == "Unavailable");
    CHECK_THAT(refused.error().message, ContainsSubstring("does not follow"));

    rig.set_axis_stalled(MountAxis::Tilt, false);
    clock.advance(2000ms);
    status = status_of(rig);
    CHECK(to_string(status.motion) == "idle");
    CHECK(status.fault.empty());
    CHECK(outcome(rig.move_to(target, 30.0)) == "ok");
}

TEST_CASE("measured positions carry noise of the configured size, the same for the same instant", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.mount.position_feedback = true;
    config.mount.feedback_noise = Degrees(0.2);
    const SimRig rig(clock, config);

    const hal::MountStatus first = status_of(rig);
    const hal::MountStatus again = status_of(rig);
    CHECK(first.position == again.position);  // one measurement, read twice

    double sum = 0.0;
    double sum_of_squares = 0.0;
    const int samples = 4000;
    for (int i = 0; i < samples; ++i) {
        clock.advance(1ms);
        const double error = status_of(rig).position.pan.value() - 0.0;
        sum += error;
        sum_of_squares += error * error;
    }
    const double mean = sum / samples;
    const double sigma = std::sqrt(sum_of_squares / samples - mean * mean);
    CHECK_THAT(mean, WithinAbs(0.0, 0.02));
    CHECK_THAT(sigma, WithinAbs(0.2, 0.02));
}

TEST_CASE("when the controller cannot be reached, calls fail and the mechanism finishes its move",
          "[sim][mount][fault]")
{
    ManualClock clock(kStart);
    SimRig rig(clock, immediate());
    const MountPosition target{.pan = Degrees(60.0), .tilt = Degrees(45.0)};
    REQUIRE(rig.move_to(target, 30.0));
    clock.advance(500ms);

    rig.set_controller_reachable(false);
    CHECK(outcome(rig.mount_status()) == "Io");
    CHECK(outcome(rig.move_to(target, 30.0)) == "Io");
    rig.stop();  // lost on the way: nothing stops the move
    clock.advance(5000ms);
    CHECK(rig.true_position() == target);

    // An emergency stop that cannot be delivered still blocks the host side.
    rig.emergency_stop();
    rig.set_controller_reachable(true);
    CHECK(status_of(rig).emergency_stop);
    CHECK(outcome(rig.move_to(target, 30.0)) == "Unavailable");
    rig.clear_emergency_stop();
    CHECK(outcome(rig.move_to(target, 30.0)) == "ok");
}

TEST_CASE("a simulated device has one user at a time", "[sim][devices]")
{
    const ManualClock clock(kStart);
    const auto rig = std::make_shared<SimRig>(clock, SimulationConfig{});
    SimMount first(rig);
    auto second = std::make_unique<SimMount>(rig);

    REQUIRE(first.open());
    const auto busy = second->open();
    CHECK(outcome(busy) == "Unavailable");
    CHECK(busy.error().message == "device 'sim:mount:pan-tilt' is already in use");
    CHECK_FALSE(second->is_open());

    first.close();
    REQUIRE(second->open());
    CHECK(outcome(first.open()) == "Unavailable");
    second.reset();  // destroying an open device releases it
    CHECK(outcome(first.open()) == "ok");
}

TEST_CASE("the rig knows where the camera really points", "[sim][mount]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.mount.pan_zero_azimuth = Degrees(350.0);  // pan 0 looks 10 degrees west of north
    config.mount.start = {.pan = Degrees(20.0), .tilt = Degrees(30.0)};
    SimRig rig(clock, config);

    SkyDirection pointing = rig.true_pointing();
    CHECK_THAT(pointing.azimuth.value(), WithinAbs(10.0, 1e-9));
    CHECK_THAT(pointing.elevation.value(), WithinAbs(30.0, 1e-9));

    REQUIRE(rig.move_to({.pan = Degrees(-100.0), .tilt = Degrees(90.0)}, 60.0));
    clock.advance(10s);
    pointing = rig.true_pointing();
    CHECK_THAT(pointing.elevation.value(), WithinAbs(90.0, 1e-6));  // the zenith

    const hal::MountCapabilities capabilities = rig.mount_capabilities();
    CHECK(capabilities.pan.minimum == Degrees(-170.0));
    CHECK(capabilities.tilt.maximum == Degrees(90.0));
    CHECK_FALSE(capabilities.position_feedback);
}

TEST_CASE("unusable simulation settings are refused with a reason", "[sim][config]")
{
    CHECK(outcome(sim::check(SimulationConfig{})) == "ok");

    using Change = std::function<void(SimulationConfig&)>;
    const std::vector<std::pair<std::string, Change>> cases = {
        {"limits", [](SimulationConfig& c) { c.mount.pan.minimum = Degrees(200.0); }},
        {"maximum speed", [](SimulationConfig& c) { c.mount.tilt.max_speed_deg_s = 0.0; }},
        {"maximum acceleration", [](SimulationConfig& c) { c.mount.max_acceleration_deg_s2 = -1.0; }},
        {"latencies", [](SimulationConfig& c) { c.mount.command_latency = -5ms; }},
        {"latencies", [](SimulationConfig& c) { c.mount.telemetry_latency = 5s; }},
        {"start position", [](SimulationConfig& c) { c.mount.start.tilt = Degrees(120.0); }},
        {"feedback noise", [](SimulationConfig& c) { c.mount.feedback_noise = Degrees(-0.1); }},
        {"pan zero", [](SimulationConfig& c) { c.mount.pan_zero_azimuth = Degrees(std::nan("")); }},
        {"IMU rate", [](SimulationConfig& c) { c.imu.rate_hz = 0.0; }},
        {"IMU noise", [](SimulationConfig& c) { c.imu.noise = Degrees(45.0); }},
        {"heading drift", [](SimulationConfig& c) { c.imu.heading_drift_deg_min = 1000.0; }},
        {"latitude", [](SimulationConfig& c) { c.gps.latitude_deg = 91.0; }},
        {"latitude", [](SimulationConfig& c) { c.gps.longitude_deg = -181.0; }},
        {"altitude", [](SimulationConfig& c) { c.gps.altitude_m = 20000.0; }},
        {"GPS noise", [](SimulationConfig& c) { c.gps.horizontal_noise_m = -1.0; }},
        {"first fix", [](SimulationConfig& c) { c.gps.time_to_first_fix = -1s; }},
        {"environment", [](SimulationConfig& c) { c.environment.relative_humidity = 130.0; }},
        {"cloud fraction", [](SimulationConfig& c) { c.camera.cloud_fraction = 1.5; }},
        {"cloud drift", [](SimulationConfig& c) { c.camera.cloud_drift = 9.0; }},
        {"Sun", [](SimulationConfig& c) { c.camera.sun_x = std::nan(""); }},
        {"replay rate", [](SimulationConfig& c) { c.camera.replay_fps = 0.0; }},
    };
    for (const auto& [expected_word, change] : cases) {
        CAPTURE(expected_word);
        SimulationConfig config;
        change(config);
        const auto result = sim::check(config);
        REQUIRE(outcome(result) == "InvalidArgument");
        CHECK_THAT(result.error().message, ContainsSubstring("simulation: "));
        CHECK_THAT(result.error().message, ContainsSubstring(expected_word));
    }
}
