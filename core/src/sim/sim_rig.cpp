#include "cloudscope/sim/sim_rig.hpp"

#include "sim_random.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string_view>
#include <utility>

namespace cloudscope::sim {

namespace {

using hal::MountMotion;
using hal::MountPosition;
using hal::SensorQuantity;

constexpr double kAtRest = 1e-9;       // degrees per second below which an axis counts as at rest
constexpr double kAtTarget = 1e-9;     // degrees
constexpr double kNotFollowing = 2.0;  // degrees between command and measurement that a mount with feedback reports
// How far back the commanded motion is remembered: more than any latency and any sample age allowed by check().
constexpr std::chrono::seconds kHistory{3};

double seconds_between(MonotonicTime from, MonotonicTime to)
{
    return std::chrono::duration<double>(to - from).count();
}

// False for NaN as well: every comparison with NaN is false.
bool within(double value, double low, double high)
{
    return value >= low && value <= high;
}

Unexpected bad(std::string_view what)
{
    return fail(ErrorCode::InvalidArgument, fmt::format("simulation: {}", what));
}

}  // namespace

Expected<void> check(const SimulationConfig& config)
{
    const SimMountConfig& mount = config.mount;
    for (const hal::AxisLimits* axis : {&mount.pan, &mount.tilt}) {
        if (!(axis->minimum < axis->maximum) || !within(axis->minimum.value(), -3600.0, 3600.0) ||
            !within(axis->maximum.value(), -3600.0, 3600.0)) {
            return bad("the mount's limits need a minimum below the maximum");
        }
        if (!within(axis->max_speed_deg_s, 0.001, 3600.0)) {
            return bad("the mount's maximum speed must be between 0.001 and 3600 degrees per second");
        }
    }
    if (!within(mount.max_acceleration_deg_s2, 0.001, 100000.0)) {
        return bad("the mount's maximum acceleration must be between 0.001 and 100000 degrees per second squared");
    }
    if (!within(static_cast<double>(mount.command_latency.count()), 0.0, 1000.0) ||
        !within(static_cast<double>(mount.telemetry_latency.count()), 0.0, 1000.0)) {
        return bad("the mount's latencies must be between 0 and 1000 ms");
    }
    if (!within(mount.start.pan.value(), mount.pan.minimum.value(), mount.pan.maximum.value()) ||
        !within(mount.start.tilt.value(), mount.tilt.minimum.value(), mount.tilt.maximum.value())) {
        return bad("the mount's start position must lie within its limits");
    }
    if (!within(mount.feedback_noise.value(), 0.0, 10.0)) {
        return bad("the mount's feedback noise must be between 0 and 10 degrees");
    }
    if (!within(mount.pan_zero_azimuth.value(), -360.0, 360.0)) {
        return bad("the azimuth of pan zero must be between -360 and 360 degrees");
    }

    if (!within(config.imu.rate_hz, 1.0, 1000.0)) {
        return bad("the IMU rate must be between 1 and 1000 Hz");
    }
    if (!within(config.imu.noise.value(), 0.0, 10.0)) {
        return bad("the IMU noise must be between 0 and 10 degrees");
    }
    if (!within(config.imu.heading_drift_deg_min, -60.0, 60.0)) {
        return bad("the IMU heading drift must be between -60 and 60 degrees per minute");
    }

    if (!within(config.gps.latitude_deg, -90.0, 90.0) || !within(config.gps.longitude_deg, -180.0, 180.0)) {
        return bad("the site needs a latitude between -90 and 90 and a longitude between -180 and 180 degrees");
    }
    if (!within(config.gps.altitude_m, -500.0, 9000.0)) {
        return bad("the site's altitude must be between -500 and 9000 m");
    }
    if (!within(config.gps.horizontal_noise_m, 0.0, 1000.0)) {
        return bad("the GPS noise must be between 0 and 1000 m");
    }
    if (!within(static_cast<double>(config.gps.time_to_first_fix.count()), 0.0, 3600.0)) {
        return bad("the GPS time to first fix must be between 0 and 3600 s");
    }

    const SimEnvironmentConfig& environment = config.environment;
    if (!within(environment.temperature_c, -90.0, 90.0) || !within(environment.relative_humidity, 0.0, 100.0) ||
        !within(environment.pressure_hpa, 100.0, 1200.0) || !within(environment.illuminance_lx, 0.0, 200000.0)) {
        return bad("the environment values are outside what occurs on Earth");
    }

    const SimCameraConfig& camera = config.camera;
    if (!within(camera.cloud_fraction, 0.0, 1.0)) {
        return bad("the cloud fraction must be between 0 and 1");
    }
    if (!within(camera.cloud_drift, -1.0, 1.0)) {
        return bad("the cloud drift must be between -1 and 1 image widths per second");
    }
    if (!within(camera.sun_x, -1.0, 2.0) || !within(camera.sun_y, -1.0, 2.0)) {
        return bad("the Sun's position must be between -1 and 2 (0 to 1 is inside the image)");
    }
    if (!within(camera.replay_fps, 0.01, 120.0)) {
        return bad("the replay rate must be between 0.01 and 120 frames per second");
    }
    return {};
}

SimRig::SimRig(const IClock& clock, const SimulationConfig& config)
    : clock_(clock),
      config_(config),
      timeline_{Epoch{.start = clock.now_monotonic(),
                      .pan = AxisProfile::at_rest(config.mount.start.pan.value()),
                      .tilt = AxisProfile::at_rest(config.mount.start.tilt.value())}},
      target_(config.mount.start),
      environment_{{SensorQuantity::Temperature, config.environment.temperature_c},
                   {SensorQuantity::RelativeHumidity, config.environment.relative_humidity},
                   {SensorQuantity::Pressure, config.environment.pressure_hpa},
                   {SensorQuantity::Illuminance, config.environment.illuminance_lx},
                   {SensorQuantity::Rain, config.environment.rain ? 1.0 : 0.0}}
{
}

Expected<void> SimRig::claim(const std::string& id)
{
    const std::lock_guard lock(mutex_);
    if (!claimed_.insert(id).second) {
        return fail(ErrorCode::Unavailable, fmt::format("device '{}' is already in use", id));
    }
    return {};
}

void SimRig::release(const std::string& id)
{
    const std::lock_guard lock(mutex_);
    claimed_.erase(id);
}

// ----------------------------------------------------------------------------------------- mount

hal::MountCapabilities SimRig::mount_capabilities() const
{
    return {.pan = config_.mount.pan, .tilt = config_.mount.tilt, .position_feedback = config_.mount.position_feedback};
}

const SimRig::Epoch& SimRig::epoch_at(MonotonicTime at) const
{
    for (const Epoch& epoch : std::views::reverse(timeline_)) {
        if (epoch.start <= at) {
            return epoch;
        }
    }
    return timeline_.front();
}

SimRig::AxisState SimRig::commanded(MountAxis axis, MonotonicTime at) const
{
    const Epoch& epoch = epoch_at(at);
    const AxisProfile& profile = axis == MountAxis::Pan ? epoch.pan : epoch.tilt;
    const double seconds = seconds_between(epoch.start, at);
    return {.position = profile.position(seconds), .velocity = profile.velocity(seconds)};
}

double SimRig::actual(MountAxis axis, MonotonicTime at) const
{
    const Stall& stall = axis == MountAxis::Pan ? pan_stall_ : tilt_stall_;
    if (stall.stalled) {
        return stall.frozen_at;
    }
    // After a stall the axis closes the gap to its command at its maximum speed.
    const double command = commanded(axis, at).position;
    const double max_speed = (axis == MountAxis::Pan ? config_.mount.pan : config_.mount.tilt).max_speed_deg_s;
    const double gap = std::abs(stall.lag_at_release) - max_speed * std::max(0.0, seconds_between(stall.released, at));
    return gap > 0.0 ? command + std::copysign(gap, stall.lag_at_release) : command;
}

bool SimRig::in_motion(MonotonicTime at) const
{
    const Epoch& current = epoch_at(at);
    const double elapsed = seconds_between(current.start, at);
    // (An instant before the first entry of the timeline, as in a status right after start-up, is "at rest".)
    if (elapsed >= 0.0 && elapsed < std::max(current.pan.duration(), current.tilt.duration())) {
        return true;
    }
    // A command that is on its way to the mechanism and will move it.
    return std::ranges::any_of(timeline_, [at](const Epoch& epoch) {
        return epoch.start > at && std::max(epoch.pan.duration(), epoch.tilt.duration()) > 0.0;
    });
}

void SimRig::begin_epoch(Epoch epoch)
{
    const MonotonicTime keep_from = clock_.now_monotonic() - kHistory;
    while (timeline_.size() > 1 && timeline_[1].start <= keep_from) {
        timeline_.erase(timeline_.begin());
    }
    timeline_.push_back(epoch);
}

Expected<void> SimRig::move_to(MountPosition target, double speed_deg_s)
{
    const std::lock_guard lock(mutex_);
    const SimMountConfig& mount = config_.mount;
    if (!controller_reachable_) {
        return fail(ErrorCode::Io, "the simulated mount controller does not answer");
    }
    if (emergency_stop_) {
        return fail(ErrorCode::Unavailable, "the simulated mount is in emergency stop; clear it before moving");
    }
    if (!within(target.pan.value(), mount.pan.minimum.value(), mount.pan.maximum.value()) ||
        !within(target.tilt.value(), mount.tilt.minimum.value(), mount.tilt.maximum.value())) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("target pan {:.2f} deg, tilt {:.2f} deg is outside the limits of the simulated mount "
                                "(pan {:.1f} to {:.1f}, tilt {:.1f} to {:.1f})",
                                target.pan.value(), target.tilt.value(), mount.pan.minimum.value(),
                                mount.pan.maximum.value(), mount.tilt.minimum.value(), mount.tilt.maximum.value()));
    }
    const double fastest = std::min(mount.pan.max_speed_deg_s, mount.tilt.max_speed_deg_s);
    if (!within(speed_deg_s, 0.0, fastest) || speed_deg_s == 0.0) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("speed {} deg/s is not within (0, {}] for the simulated mount", speed_deg_s, fastest));
    }
    const MonotonicTime now = clock_.now_monotonic();
    if (mount.position_feedback) {
        for (const MountAxis axis : {MountAxis::Pan, MountAxis::Tilt}) {
            if (std::abs(actual(axis, now) - commanded(axis, now).position) > kNotFollowing) {
                return fail(ErrorCode::Unavailable, "the simulated mount reports a fault: an axis does not follow");
            }
        }
    }

    // The command reaches the mechanism one latency from now, in the state the mechanism will have then.
    const MonotonicTime effective = now + mount.command_latency;
    const AxisState pan = commanded(MountAxis::Pan, effective);
    const AxisState tilt = commanded(MountAxis::Tilt, effective);
    const double pan_distance = std::abs(target.pan.value() - pan.position);
    const double tilt_distance = std::abs(target.tilt.value() - tilt.position);
    const double longest = std::max(pan_distance, tilt_distance);
    const bool from_rest = std::abs(pan.velocity) < kAtRest && std::abs(tilt.velocity) < kAtRest;
    const double acceleration = mount.max_acceleration_deg_s2;
    const auto profile = [&](const AxisState& state, double goal, double distance) {
        if (!from_rest) {
            // Replacing a move in progress: each axis blends from its present velocity on its own.
            return AxisProfile::to_target(state.position, state.velocity, goal, speed_deg_s, acceleration);
        }
        if (distance < kAtTarget) {
            return AxisProfile::at_rest(goal);
        }
        // From rest, the axis with less to do is slowed in proportion: both follow the same profile in time,
        // so the head moves along a straight line in pan and tilt and both axes arrive together.
        const double share = distance / longest;
        return AxisProfile::to_target(state.position, 0.0, goal, speed_deg_s * share, acceleration * share);
    };
    begin_epoch({.start = effective,
                 .pan = profile(pan, target.pan.value(), pan_distance),
                 .tilt = profile(tilt, target.tilt.value(), tilt_distance)});
    target_ = target;
    return {};
}

void SimRig::stop()
{
    const std::lock_guard lock(mutex_);
    if (!controller_reachable_) {
        return;  // the command is lost on the way
    }
    const MonotonicTime effective = clock_.now_monotonic() + config_.mount.command_latency;
    const AxisState pan = commanded(MountAxis::Pan, effective);
    const AxisState tilt = commanded(MountAxis::Tilt, effective);
    const double acceleration = config_.mount.max_acceleration_deg_s2;
    begin_epoch({.start = effective,
                 .pan = AxisProfile::braking(pan.position, pan.velocity, acceleration),
                 .tilt = AxisProfile::braking(tilt.position, tilt.velocity, acceleration)});
}

void SimRig::emergency_stop()
{
    const std::lock_guard lock(mutex_);
    emergency_stop_ = true;  // from this moment the host side refuses moves
    if (!controller_reachable_) {
        return;
    }
    const MonotonicTime effective = clock_.now_monotonic() + config_.mount.command_latency;
    // The drive is cut and the head stops dead: a small pan-tilt head has little inertia (NFR-SAFE-01).
    begin_epoch({.start = effective,
                 .pan = AxisProfile::at_rest(commanded(MountAxis::Pan, effective).position),
                 .tilt = AxisProfile::at_rest(commanded(MountAxis::Tilt, effective).position)});
}

void SimRig::clear_emergency_stop()
{
    const std::lock_guard lock(mutex_);
    emergency_stop_ = false;
}

Expected<hal::MountStatus> SimRig::mount_status() const
{
    const std::lock_guard lock(mutex_);
    if (!controller_reachable_) {
        return fail(ErrorCode::Io, "the simulated mount controller does not answer");
    }
    const SimMountConfig& mount = config_.mount;
    const MonotonicTime at = clock_.now_monotonic() - mount.telemetry_latency;
    const MountPosition command{.pan = Degrees(commanded(MountAxis::Pan, at).position),
                                .tilt = Degrees(commanded(MountAxis::Tilt, at).position)};

    hal::MountStatus status;
    status.position = command;
    status.position_measured = mount.position_feedback;
    if (mount.position_feedback) {
        const double pan = actual(MountAxis::Pan, at);
        const double tilt = actual(MountAxis::Tilt, at);
        if (std::abs(pan - command.pan.value()) > kNotFollowing) {
            status.fault = "the pan axis does not follow its command";
        } else if (std::abs(tilt - command.tilt.value()) > kNotFollowing) {
            status.fault = "the tilt axis does not follow its command";
        }
        // The noise of a measurement depends on when it was taken, not on how often it is read.
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count());
        const double sigma = mount.feedback_noise.value();
        status.position = {.pan = Degrees(pan + sigma * normal(config_.seed, 1, stamp)),
                           .tilt = Degrees(tilt + sigma * normal(config_.seed, 2, stamp))};
    }
    status.target = target_;
    status.emergency_stop = emergency_stop_;
    status.time = {.utc = clock_.now_utc() - mount.telemetry_latency, .monotonic = at, .source = clock_.time_source()};
    const bool at_target = std::abs(command.pan.value() - target_.pan.value()) <= kAtTarget &&
                           std::abs(command.tilt.value() - target_.tilt.value()) <= kAtTarget;
    if (!status.fault.empty()) {
        status.motion = MountMotion::Fault;
    } else if (in_motion(at)) {
        status.motion = MountMotion::Moving;
    } else {
        status.motion = at_target ? MountMotion::Idle : MountMotion::Stopped;
    }
    return status;
}

MountPosition SimRig::true_position() const
{
    return true_position(clock_.now_monotonic());
}

MountPosition SimRig::true_position(MonotonicTime at) const
{
    const std::lock_guard lock(mutex_);
    return {.pan = Degrees(actual(MountAxis::Pan, at)), .tilt = Degrees(actual(MountAxis::Tilt, at))};
}

Quaternion SimRig::true_orientation(MonotonicTime at) const
{
    const MountPosition position = true_position(at);
    return camera_orientation({.azimuth = position.pan + config_.mount.pan_zero_azimuth, .elevation = position.tilt});
}

SkyDirection SimRig::true_pointing() const
{
    return optical_axis(true_orientation(clock_.now_monotonic()));
}

// ------------------------------------------------------------------------------- what can be made to happen

void SimRig::set_axis_stalled(MountAxis axis, bool stalled)
{
    const std::lock_guard lock(mutex_);
    Stall& stall = axis == MountAxis::Pan ? pan_stall_ : tilt_stall_;
    if (stall.stalled == stalled) {
        return;
    }
    const MonotonicTime now = clock_.now_monotonic();
    if (stalled) {
        stall.frozen_at = actual(axis, now);
        stall.stalled = true;
    } else {
        stall.stalled = false;
        stall.released = now;
        stall.lag_at_release = stall.frozen_at - commanded(axis, now).position;
    }
}

void SimRig::set_controller_reachable(bool reachable)
{
    const std::lock_guard lock(mutex_);
    controller_reachable_ = reachable;
}

void SimRig::set_camera_connected(bool connected)
{
    const std::lock_guard lock(mutex_);
    camera_connected_ = connected;
}

bool SimRig::camera_connected() const
{
    const std::lock_guard lock(mutex_);
    return camera_connected_;
}

void SimRig::set_camera_stalled(bool stalled)
{
    const std::lock_guard lock(mutex_);
    camera_stalled_ = stalled;
}

bool SimRig::camera_stalled() const
{
    const std::lock_guard lock(mutex_);
    return camera_stalled_;
}

void SimRig::lose_camera_frames(std::uint64_t count)
{
    const std::lock_guard lock(mutex_);
    camera_frames_to_lose_ += count;
}

std::uint64_t SimRig::take_lost_camera_frames()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(camera_frames_to_lose_, 0);
}

void SimRig::set_gps_fix(bool available)
{
    const std::lock_guard lock(mutex_);
    gps_fix_ = available;
}

bool SimRig::gps_fix() const
{
    const std::lock_guard lock(mutex_);
    return gps_fix_;
}

void SimRig::set_environment(SensorQuantity quantity, double value)
{
    const std::lock_guard lock(mutex_);
    const auto found = environment_.find(quantity);
    if (found == environment_.end() || !std::isfinite(value)) {
        return;
    }
    if (quantity == SensorQuantity::Rain) {
        found->second = value != 0.0 ? 1.0 : 0.0;
    } else if (quantity == SensorQuantity::RelativeHumidity) {
        found->second = std::clamp(value, 0.0, 100.0);
    } else {
        found->second = value;
    }
}

double SimRig::environment(SensorQuantity quantity) const
{
    const std::lock_guard lock(mutex_);
    const auto found = environment_.find(quantity);
    return found == environment_.end() ? 0.0 : found->second;
}

}  // namespace cloudscope::sim
