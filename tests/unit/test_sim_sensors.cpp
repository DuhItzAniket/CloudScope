#include "hal_contract.hpp"

#include <cloudscope/common/clock.hpp>
#include <cloudscope/geometry/rotation.hpp>
#include <cloudscope/sim/sim_devices.hpp>
#include <cloudscope/sim/sim_rig.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
using hal::SensorQuantity;
using sim::SimEnvironment;
using sim::SimGps;
using sim::SimImu;
using sim::SimRig;
using sim::SimulationConfig;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);

SimulationConfig immediate()
{
    SimulationConfig config;
    config.mount.command_latency = 0ms;
    config.mount.telemetry_latency = 0ms;
    return config;
}

hal::ImuSample read(SimImu& imu)
{
    const auto sample = imu.read();
    REQUIRE(outcome(sample) == "ok");
    return *sample;
}

// The readings of a sensor by quantity.
std::map<SensorQuantity, double> read(hal::ISensor& sensor)
{
    const auto readings = sensor.read();
    REQUIRE(outcome(readings) == "ok");
    std::map<SensorQuantity, double> values;
    for (const hal::SensorReading& reading : *readings) {
        values[reading.quantity] = reading.value;
    }
    return values;
}

}  // namespace

// ------------------------------------------------------------------------------------------- IMU

TEST_CASE("the simulated IMU obeys the IMU contract", "[sim][imu][contract]")
{
    const ManualClock clock(kStart);
    check_imu_contract(
        [&clock] { return std::make_shared<SimImu>(std::make_shared<SimRig>(clock, SimulationConfig{})); });
}

TEST_CASE("the simulated IMU measures where the head really points, within its noise", "[sim][imu]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.imu.absolute_heading = true;
    const auto rig = std::make_shared<SimRig>(clock, config);
    SimImu imu(rig);
    REQUIRE(imu.open());

    const auto capabilities = imu.capabilities();
    REQUIRE(capabilities);
    CHECK(capabilities->absolute_heading);
    CHECK(capabilities->max_rate_hz == 100.0);

    hal::ImuSample sample = read(imu);
    CHECK(sample.calibrated);
    CHECK_THAT(sample.accuracy.value(), WithinAbs(0.3, 1e-12));  // three times the noise of 0.1 degrees
    CHECK(angle_between(sample.orientation, rig->true_orientation(sample.time.monotonic)).value() < 1.0);
    SkyDirection seen = optical_axis(sample.orientation);
    CHECK_THAT(angular_difference(Degrees(0.0), seen.azimuth).value(), WithinAbs(0.0, 1.0));
    CHECK_THAT(seen.elevation.value(), WithinAbs(45.0, 1.0));

    // The IMU follows the head when the mount moves.
    REQUIRE(rig->move_to({.pan = Degrees(90.0), .tilt = Degrees(10.0)}, 60.0));
    clock.advance(5s);
    sample = read(imu);
    seen = optical_axis(sample.orientation);
    CHECK_THAT(seen.azimuth.value(), WithinAbs(90.0, 1.0));
    CHECK_THAT(seen.elevation.value(), WithinAbs(10.0, 1.0));
    CHECK(angle_between(sample.orientation, rig->true_orientation(sample.time.monotonic)).value() > 0.0);  // noise
}

TEST_CASE("without noise and drift the simulated IMU reports the true orientation", "[sim][imu]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.imu.absolute_heading = true;
    config.imu.noise = Degrees(0.0);
    config.mount.pan_zero_azimuth = Degrees(25.0);
    const auto rig = std::make_shared<SimRig>(clock, config);
    SimImu imu(rig);
    REQUIRE(imu.open());

    for (const hal::MountPosition& target : {hal::MountPosition{.pan = Degrees(0.0), .tilt = Degrees(45.0)},
                                             hal::MountPosition{.pan = Degrees(-120.0), .tilt = Degrees(5.0)},
                                             hal::MountPosition{.pan = Degrees(160.0), .tilt = Degrees(90.0)}}) {
        REQUIRE(rig->move_to(target, 60.0));
        clock.advance(10s);
        const hal::ImuSample sample = read(imu);
        CAPTURE(target.pan.value(), target.tilt.value());
        CHECK(angle_between(sample.orientation, rig->true_orientation(clock.now_monotonic())).value() < 1e-6);
        const SkyDirection seen = optical_axis(sample.orientation);
        CHECK_THAT(seen.elevation.value(), WithinAbs(target.tilt.value(), 1e-6));
        if (target.tilt.value() < 89.0) {  // straight up has no azimuth
            CHECK_THAT(angular_difference(target.pan + Degrees(25.0), seen.azimuth).value(), WithinAbs(0.0, 1e-6));
        }
    }
}

TEST_CASE("the IMU takes samples at its rate, and reading twice between two samples gives the same one", "[sim][imu]")
{
    ManualClock clock(kStart);
    const auto rig = std::make_shared<SimRig>(clock, immediate());
    SimImu imu(rig);
    REQUIRE(imu.open());
    const MonotonicTime opened = clock.now_monotonic();

    clock.advance(25ms);  // 100 Hz: samples at 0, 10, 20 ms; the newest is 5 ms old
    const hal::ImuSample first = read(imu);
    CHECK(first.time.monotonic == opened + 20ms);
    CHECK(first.time.utc == kStart + 20ms);

    clock.advance(4ms);  // 29 ms: still the sample of 20 ms
    const hal::ImuSample same = read(imu);
    CHECK(same.time.monotonic == opened + 20ms);
    CHECK(angle_between(first.orientation, same.orientation).value() == 0.0);
    CHECK(first.orientation.w == same.orientation.w);

    clock.advance(1ms);  // 30 ms: a new sample, with its own noise
    const hal::ImuSample next = read(imu);
    CHECK(next.time.monotonic == opened + 30ms);
    CHECK(first.orientation.w != next.orientation.w);
}

TEST_CASE("the heading of an IMU without a north reference drifts; with one it does not", "[sim][imu]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.imu.noise = Degrees(0.0);
    config.imu.heading_drift_deg_min = 0.5;

    config.imu.absolute_heading = false;
    SimImu drifting(std::make_shared<SimRig>(clock, config));
    config.imu.absolute_heading = true;
    SimImu referenced(std::make_shared<SimRig>(clock, config));
    REQUIRE(drifting.open());
    REQUIRE(referenced.open());

    SkyDirection seen = optical_axis(read(drifting).orientation);
    CHECK_THAT(angular_difference(Degrees(0.0), seen.azimuth).value(), WithinAbs(0.0, 1e-6));  // none at the start

    clock.advance(10min);
    seen = optical_axis(read(drifting).orientation);
    CHECK_THAT(angular_difference(Degrees(0.0), seen.azimuth).value(), WithinAbs(5.0, 1e-6));  // 0.5 deg/min
    CHECK_THAT(seen.elevation.value(), WithinAbs(45.0, 1e-6));                                 // only the heading
    seen = optical_axis(read(referenced).orientation);
    CHECK_THAT(angular_difference(Degrees(0.0), seen.azimuth).value(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("the IMU shows a stalled axis that the mount's own report hides", "[sim][imu][fault]")
{
    ManualClock clock(kStart);
    SimulationConfig config = immediate();
    config.imu.absolute_heading = true;
    const auto rig = std::make_shared<SimRig>(clock, config);
    SimImu imu(rig);
    REQUIRE(imu.open());

    rig->set_axis_stalled(sim::MountAxis::Pan, true);
    REQUIRE(rig->move_to({.pan = Degrees(60.0), .tilt = Degrees(45.0)}, 30.0));
    clock.advance(5s);

    const auto status = rig->mount_status();
    REQUIRE(status);
    const SkyDirection seen = optical_axis(read(imu).orientation);
    CHECK(status->position.pan.value() == 60.0);                                              // "I am at 60"
    CHECK_THAT(angular_difference(Degrees(0.0), seen.azimuth).value(), WithinAbs(0.0, 1.0));  // it is not
}

// ------------------------------------------------------------------------------------------- GPS

TEST_CASE("the simulated GPS receiver obeys the sensor contract", "[sim][gps][contract]")
{
    const ManualClock clock(kStart);
    SimulationConfig config;
    config.gps.time_to_first_fix = 0s;  // so that the contract sees readings
    check_sensor_contract(
        [&clock, &config] { return std::make_shared<SimGps>(std::make_shared<SimRig>(clock, config)); });
}

TEST_CASE("the GPS receiver has no position before its first fix or while the fix is taken away", "[sim][gps]")
{
    ManualClock clock(kStart);
    const auto rig = std::make_shared<SimRig>(clock, SimulationConfig{});  // first fix after 5 s
    SimGps gps(rig);
    REQUIRE(gps.open());
    CHECK(gps.quantities()->size() == 5);

    CHECK(read(gps).empty());
    clock.advance(4999ms);
    CHECK(read(gps).empty());
    CHECK(gps.quantities()->size() == 5);  // what it can deliver does not change
    clock.advance(1ms);
    const std::map<SensorQuantity, double> fix = read(gps);
    REQUIRE(fix.size() == 5);
    CHECK_THAT(fix.at(SensorQuantity::Latitude), WithinAbs(12.97, 0.001));
    CHECK_THAT(fix.at(SensorQuantity::Longitude), WithinAbs(77.59, 0.001));
    CHECK_THAT(fix.at(SensorQuantity::Altitude), WithinAbs(920.0, 40.0));
    CHECK(fix.at(SensorQuantity::HorizontalAccuracy) == 2.5);
    CHECK(std::abs(fix.at(SensorQuantity::ClockOffset)) < 0.02);

    rig->set_gps_fix(false);  // under a roof
    CHECK(read(gps).empty());
    rig->set_gps_fix(true);
    CHECK(read(gps).size() == 5);

    gps.close();  // switched off and on: it has to find the satellites again
    REQUIRE(gps.open());
    CHECK(read(gps).empty());
}

TEST_CASE("GPS positions scatter around the site by the configured noise, one solution per second", "[sim][gps]")
{
    ManualClock clock(kStart);
    SimulationConfig config;
    config.gps.time_to_first_fix = 0s;
    config.gps.horizontal_noise_m = 2.5;
    SimGps gps(std::make_shared<SimRig>(clock, config));
    REQUIRE(gps.open());

    clock.advance(1500ms);
    const auto first = gps.read();
    clock.advance(400ms);  // 1.9 s: the same solution
    const auto again = gps.read();
    REQUIRE(first);
    REQUIRE(again);
    REQUIRE(first->size() == 5);
    CHECK(first->front().value == again->front().value);
    CHECK(first->front().time.utc == kStart + 1s);  // the solution of second 1
    CHECK(again->front().time.utc == kStart + 1s);

    const double metres_per_degree = 111320.0;
    const double east_scale = metres_per_degree * std::cos(12.97 * std::numbers::pi / 180.0);
    const int solutions = 2000;
    double north_sum = 0.0;
    double north_squares = 0.0;
    double east_squares = 0.0;
    double up_squares = 0.0;
    for (int i = 0; i < solutions; ++i) {
        clock.advance(1s);
        const std::map<SensorQuantity, double> fix = read(gps);
        const double north = (fix.at(SensorQuantity::Latitude) - 12.97) * metres_per_degree;
        const double east = (fix.at(SensorQuantity::Longitude) - 77.59) * east_scale;
        const double up = fix.at(SensorQuantity::Altitude) - 920.0;
        north_sum += north;
        north_squares += north * north;
        east_squares += east * east;
        up_squares += up * up;
    }
    CHECK_THAT(north_sum / solutions, WithinAbs(0.0, 0.3));                  // centred on the site
    CHECK_THAT(std::sqrt(north_squares / solutions), WithinAbs(2.5, 0.25));  // 1 sigma, as configured
    CHECK_THAT(std::sqrt(east_squares / solutions), WithinAbs(2.5, 0.25));
    CHECK_THAT(std::sqrt(up_squares / solutions), WithinAbs(3.75, 0.4));  // height is 1.5 times worse
}

// ----------------------------------------------------------------------------------- environment

TEST_CASE("the simulated environment sensors obey the sensor contract", "[sim][environment][contract]")
{
    const ManualClock clock(kStart);
    check_sensor_contract(
        [&clock] { return std::make_shared<SimEnvironment>(std::make_shared<SimRig>(clock, SimulationConfig{})); });
}

TEST_CASE("the environment sensors report what is set on the rig, with a little noise", "[sim][environment]")
{
    ManualClock clock(kStart);
    const auto rig = std::make_shared<SimRig>(clock, SimulationConfig{});
    SimEnvironment sensors(rig);
    REQUIRE(sensors.open());

    std::map<SensorQuantity, double> now = read(sensors);
    REQUIRE(now.size() == 5);
    CHECK_THAT(now.at(SensorQuantity::Temperature), WithinAbs(27.0, 0.5));
    CHECK_THAT(now.at(SensorQuantity::RelativeHumidity), WithinAbs(60.0, 3.0));
    CHECK_THAT(now.at(SensorQuantity::Pressure), WithinAbs(912.0, 0.5));
    CHECK_THAT(now.at(SensorQuantity::Illuminance), WithinAbs(20000.0, 1000.0));
    CHECK(now.at(SensorQuantity::Rain) == 0.0);
    CHECK(now.at(SensorQuantity::Temperature) != 27.0);  // a measurement, not the set value

    // Rain starts and it gets hot: what a safety rule has to react to (FR-SAF-07).
    rig->set_environment(SensorQuantity::Rain, 1.0);
    rig->set_environment(SensorQuantity::Temperature, 61.0);
    rig->set_environment(SensorQuantity::RelativeHumidity, 150.0);  // clamped to what humidity can be
    rig->set_environment(SensorQuantity::Latitude, 5.0);            // not an environment quantity: ignored
    rig->set_environment(SensorQuantity::Pressure, std::nan(""));   // not a number: ignored
    clock.advance(1s);
    now = read(sensors);
    CHECK(now.at(SensorQuantity::Rain) == 1.0);
    CHECK_THAT(now.at(SensorQuantity::Temperature), WithinAbs(61.0, 0.5));
    CHECK(now.at(SensorQuantity::RelativeHumidity) <= 100.0);
    CHECK(now.at(SensorQuantity::RelativeHumidity) > 95.0);
    CHECK_THAT(now.at(SensorQuantity::Pressure), WithinAbs(912.0, 0.5));
    CHECK(rig->environment(SensorQuantity::Latitude) == 0.0);

    const auto readings = sensors.read();
    REQUIRE(readings);
    CHECK(readings->front().time.utc == clock.now_utc());
}
