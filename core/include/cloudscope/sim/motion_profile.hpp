// How one axis gets from where it is to rest at a target, with limited speed and limited acceleration:
// speed up, cruise, slow down (a "trapezoidal" velocity profile), starting from any position and velocity.
//
// The whole motion is a function of time that can be evaluated at any instant, so the simulated mount needs
// no thread and no time step: it computes where the axis is whenever it is asked.
//
// Positions, speeds and accelerations are plain numbers in consistent units (the simulators use degrees and
// seconds).
#pragma once

#include <array>

namespace cloudscope::sim {

class AxisProfile {
public:
    // At rest at 0.
    AxisProfile() = default;

    // At rest at `position`, for ever.
    [[nodiscard]] static AxisProfile at_rest(double position);

    // From `position` moving at `velocity` to rest exactly at `target`. The axis accelerates at up to
    // `max_acceleration` and travels at up to `max_speed`. A start velocity that is too high to stop before
    // the target makes it overshoot and come back; a start velocity above `max_speed` is braked down first.
    // `max_speed` and `max_acceleration` must be positive.
    [[nodiscard]] static AxisProfile to_target(double position, double velocity, double target, double max_speed,
                                               double max_acceleration);

    // From `position` moving at `velocity` to rest as soon as `max_acceleration` allows.
    [[nodiscard]] static AxisProfile braking(double position, double velocity, double max_acceleration);

    // State `seconds` after the start; before the start it is the start state, after the end the end state.
    [[nodiscard]] double position(double seconds) const;
    [[nodiscard]] double velocity(double seconds) const;

    // Seconds from the start until the axis is at rest.
    [[nodiscard]] double duration() const { return duration_; }
    [[nodiscard]] double end_position() const { return end_position_; }

private:
    struct Phase {
        double duration = 0.0;
        double acceleration = 0.0;
    };

    void finish(double end_position);

    double start_position_ = 0.0;
    double start_velocity_ = 0.0;
    std::array<Phase, 3> phases_{};  // change speed, cruise, brake; unused phases have duration 0
    double duration_ = 0.0;
    double end_position_ = 0.0;
};

}  // namespace cloudscope::sim
