#include <cloudscope/sim/motion_profile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;
using cloudscope::sim::AxisProfile;

TEST_CASE("an axis at rest stays where it is", "[sim][motion]")
{
    const AxisProfile profile = AxisProfile::at_rest(42.5);
    CHECK(profile.duration() == 0.0);
    CHECK(profile.end_position() == 42.5);
    for (const double t : {-1.0, 0.0, 0.5, 1000.0}) {
        CHECK(profile.position(t) == 42.5);
        CHECK(profile.velocity(t) == 0.0);
    }
    CHECK(AxisProfile().position(3.0) == 0.0);
}

TEST_CASE("a short move from rest speeds up half the way and brakes the other half", "[sim][motion]")
{
    // 10 degrees at up to 60 deg/s and 240 deg/s^2: too short to reach the speed limit.
    const AxisProfile profile = AxisProfile::to_target(5.0, 0.0, 15.0, 60.0, 240.0);
    const double expected_duration = 2.0 * std::sqrt(10.0 / 240.0);
    CHECK_THAT(profile.duration(), WithinAbs(expected_duration, 1e-12));
    CHECK(profile.end_position() == 15.0);

    const double half = expected_duration / 2.0;
    CHECK_THAT(profile.position(half), WithinAbs(10.0, 1e-9));                             // half way at half time
    CHECK_THAT(profile.velocity(half), WithinAbs(std::sqrt(240.0 * 10.0), 1e-9));          // peak speed, below 60
    CHECK_THAT(profile.position(0.05), WithinAbs(5.0 + 0.5 * 240.0 * 0.05 * 0.05, 1e-9));  // s = a t^2 / 2
    CHECK(profile.position(expected_duration) == 15.0);
    CHECK(profile.velocity(expected_duration) == 0.0);
    CHECK(profile.position(expected_duration + 10.0) == 15.0);
}

TEST_CASE("a long move from rest cruises at the speed limit", "[sim][motion]")
{
    // 90 degrees at 30 deg/s and 120 deg/s^2: 0.25 s to speed up, 0.25 s to brake, the rest at 30 deg/s.
    const AxisProfile profile = AxisProfile::to_target(0.0, 0.0, 90.0, 30.0, 120.0);
    CHECK_THAT(profile.duration(), WithinAbs(90.0 / 30.0 + 30.0 / 120.0, 1e-12));
    CHECK_THAT(profile.velocity(0.125), WithinAbs(15.0, 1e-9));
    CHECK_THAT(profile.velocity(0.25), WithinAbs(30.0, 1e-9));
    CHECK_THAT(profile.velocity(1.5), WithinAbs(30.0, 1e-9));
    CHECK_THAT(profile.position(0.25), WithinAbs(3.75, 1e-9));
    CHECK_THAT(profile.position(1.25), WithinAbs(3.75 + 30.0, 1e-9));
    CHECK_THAT(profile.velocity(3.125), WithinAbs(15.0, 1e-9));  // half way through braking
    CHECK(profile.position(3.25) == 90.0);
}

TEST_CASE("a move in the negative direction mirrors the positive one", "[sim][motion]")
{
    const AxisProfile forward = AxisProfile::to_target(0.0, 0.0, 50.0, 30.0, 120.0);
    const AxisProfile backward = AxisProfile::to_target(0.0, 0.0, -50.0, 30.0, 120.0);
    CHECK_THAT(backward.duration(), WithinAbs(forward.duration(), 1e-12));
    for (const double t : {0.1, 0.3, 1.0, 1.7}) {
        CHECK_THAT(backward.position(t), WithinAbs(-forward.position(t), 1e-9));
        CHECK_THAT(backward.velocity(t), WithinAbs(-forward.velocity(t), 1e-9));
    }
}

TEST_CASE("an axis moving away from its target reverses and still arrives", "[sim][motion]")
{
    const AxisProfile profile = AxisProfile::to_target(10.0, -20.0, 30.0, 40.0, 100.0);
    CHECK(profile.velocity(0.0) == -20.0);
    CHECK(profile.position(0.1) < 10.0);                             // still going the wrong way
    CHECK_THAT(profile.velocity(0.2), WithinAbs(0.0, 1e-9));         // turned round after v / a = 0.2 s
    CHECK_THAT(profile.position(0.2), WithinAbs(10.0 - 2.0, 1e-9));  // 20^2 / (2 * 100) = 2 degrees further out
    CHECK(profile.position(profile.duration()) == 30.0);
}

TEST_CASE("an axis too fast to stop at its target overshoots and comes back", "[sim][motion]")
{
    // 50 deg/s needs 12.5 degrees to stop at 100 deg/s^2; the target is only 5 degrees ahead.
    const AxisProfile profile = AxisProfile::to_target(0.0, 50.0, 5.0, 60.0, 100.0);
    CHECK_THAT(profile.position(0.5), WithinAbs(12.5, 1e-9));  // furthest point, at rest for an instant
    CHECK_THAT(profile.velocity(0.5), WithinAbs(0.0, 1e-9));
    CHECK(profile.velocity(0.6) < 0.0);  // on the way back
    CHECK(profile.position(profile.duration()) == 5.0);
    CHECK(profile.velocity(profile.duration()) == 0.0);
}

TEST_CASE("an axis faster than the speed limit is braked down to it, not driven on", "[sim][motion]")
{
    const AxisProfile profile = AxisProfile::to_target(0.0, 50.0, 100.0, 20.0, 100.0);
    double fastest_after_braking = 0.0;
    const int steps = static_cast<int>(profile.duration() * 1000.0);
    for (int step = 0; step <= steps; ++step) {
        const double t = step / 1000.0;
        CHECK(profile.velocity(t) <= 50.0 + 1e-9);  // never faster than it started
        if (step >= 300) {                          // (50 - 20) / 100 = 0.3 s to come down to the limit
            fastest_after_braking = std::max(fastest_after_braking, profile.velocity(t));
        }
    }
    CHECK_THAT(fastest_after_braking, WithinAbs(20.0, 1e-9));
    CHECK(profile.position(profile.duration()) == 100.0);
}

TEST_CASE("braking brings an axis to rest as soon as the acceleration limit allows", "[sim][motion]")
{
    const AxisProfile profile = AxisProfile::braking(10.0, -30.0, 120.0);
    CHECK_THAT(profile.duration(), WithinAbs(0.25, 1e-12));
    CHECK_THAT(profile.end_position(), WithinAbs(10.0 - 30.0 * 30.0 / 240.0, 1e-12));
    CHECK_THAT(profile.velocity(0.125), WithinAbs(-15.0, 1e-9));
    CHECK(profile.velocity(0.25) == 0.0);
    CHECK(profile.position(5.0) == profile.end_position());

    const AxisProfile already = AxisProfile::braking(7.0, 0.0, 120.0);
    CHECK(already.duration() == 0.0);
    CHECK(already.position(1.0) == 7.0);
}

TEST_CASE("every move ends exactly at its target, without jumps and within its limits", "[sim][motion]")
{
    const double start_velocity = GENERATE(0.0, 12.0, -12.0, 55.0, -55.0, 90.0);
    const double distance = GENERATE(0.0, 0.01, 3.0, -3.0, 40.0, -170.0);
    const double max_speed = GENERATE(5.0, 60.0);
    const double max_acceleration = GENERATE(30.0, 240.0);
    CAPTURE(start_velocity, distance, max_speed, max_acceleration);

    const double start = 20.0;
    const AxisProfile profile =
        AxisProfile::to_target(start, start_velocity, start + distance, max_speed, max_acceleration);
    REQUIRE(std::isfinite(profile.duration()));
    REQUIRE(profile.duration() >= 0.0);
    CHECK(profile.position(-1.0) == start);
    CHECK(profile.position(profile.duration()) == start + distance);
    CHECK(profile.velocity(profile.duration()) == 0.0);

    const double speed_limit = std::max(max_speed, std::abs(start_velocity)) + 1e-6;
    const int steps = 2000;
    const double dt = profile.duration() / steps;
    double previous_position = profile.position(0.0);
    double previous_velocity = profile.velocity(0.0);
    bool smooth = true;
    bool within_limits = true;
    for (int i = 1; i <= steps && dt > 0.0; ++i) {
        const double t = dt * i;
        const double position = profile.position(t);
        const double velocity = profile.velocity(t);
        // Position changes by no more than the speed allows, speed by no more than the acceleration allows.
        smooth = smooth && std::abs(position - previous_position) <= speed_limit * dt + 1e-9;
        within_limits = within_limits && std::abs(velocity) <= speed_limit &&
                        std::abs(velocity - previous_velocity) <= max_acceleration * dt + 1e-9;
        previous_position = position;
        previous_velocity = velocity;
    }
    CHECK(smooth);
    CHECK(within_limits);
}
