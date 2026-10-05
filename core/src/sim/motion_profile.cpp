#include "cloudscope/sim/motion_profile.hpp"

#include <algorithm>
#include <cmath>

namespace cloudscope::sim {

AxisProfile AxisProfile::at_rest(double position)
{
    AxisProfile profile;
    profile.start_position_ = position;
    profile.end_position_ = position;
    return profile;
}

AxisProfile AxisProfile::to_target(double position, double velocity, double target, double max_speed,
                                   double max_acceleration)
{
    AxisProfile profile;
    profile.start_position_ = position;
    profile.start_velocity_ = velocity;

    const double distance = target - position;
    // Where the axis would come to rest if it braked now, relative to where it is (signed).
    const double stopping = velocity * std::abs(velocity) / (2.0 * max_acceleration);
    // The side of that point on which the target lies decides the direction of travel. From here on the
    // motion is computed as if that direction were positive: v0 and d are speed and distance "forwards".
    const double direction = distance - stopping < 0.0 ? -1.0 : 1.0;
    const double v0 = direction * velocity;
    const double d = direction * distance;
    const double a = max_acceleration;

    // Without a speed limit: go from v0 to a peak speed, then brake to rest, covering d in all.
    //   (peak^2 - v0^2) / 2a + peak^2 / 2a = d
    double peak = std::sqrt(std::max(0.0, a * d + v0 * v0 / 2.0));
    double cruise_time = 0.0;
    if (peak > max_speed) {
        peak = max_speed;
        // Distance covered while the speed changes from v0 to the cruise speed. Negative if the axis first has
        // to reverse out of a motion away from the target.
        const double changing = v0 <= peak ? (peak * peak - v0 * v0) / (2.0 * a) : (v0 * v0 - peak * peak) / (2.0 * a);
        const double braking_distance = peak * peak / (2.0 * a);
        cruise_time = std::max(0.0, d - changing - braking_distance) / peak;
    }
    profile.phases_[0] = {.duration = std::abs(peak - v0) / a, .acceleration = direction * (v0 <= peak ? a : -a)};
    profile.phases_[1] = {.duration = cruise_time, .acceleration = 0.0};
    profile.phases_[2] = {.duration = peak / a, .acceleration = -direction * a};
    profile.finish(target);
    return profile;
}

AxisProfile AxisProfile::braking(double position, double velocity, double max_acceleration)
{
    AxisProfile profile;
    profile.start_position_ = position;
    profile.start_velocity_ = velocity;
    profile.phases_[0] = {.duration = std::abs(velocity) / max_acceleration,
                          .acceleration = velocity > 0.0 ? -max_acceleration : max_acceleration};
    profile.finish(position + velocity * std::abs(velocity) / (2.0 * max_acceleration));
    return profile;
}

void AxisProfile::finish(double end_position)
{
    duration_ = 0.0;
    for (const Phase& phase : phases_) {
        duration_ += phase.duration;
    }
    end_position_ = end_position;
}

double AxisProfile::position(double seconds) const
{
    if (seconds <= 0.0) {
        return start_position_;
    }
    if (seconds >= duration_) {
        return end_position_;  // exactly the target, not the sum of rounded pieces
    }
    double position = start_position_;
    double velocity = start_velocity_;
    for (const Phase& phase : phases_) {
        const double dt = std::min(seconds, phase.duration);
        position += velocity * dt + 0.5 * phase.acceleration * dt * dt;
        velocity += phase.acceleration * dt;
        seconds -= dt;
        if (seconds <= 0.0) {
            break;
        }
    }
    return position;
}

double AxisProfile::velocity(double seconds) const
{
    if (seconds <= 0.0) {
        return start_velocity_;
    }
    if (seconds >= duration_) {
        return 0.0;
    }
    double velocity = start_velocity_;
    for (const Phase& phase : phases_) {
        const double dt = std::min(seconds, phase.duration);
        velocity += phase.acceleration * dt;
        seconds -= dt;
        if (seconds <= 0.0) {
            break;
        }
    }
    return velocity;
}

}  // namespace cloudscope::sim
