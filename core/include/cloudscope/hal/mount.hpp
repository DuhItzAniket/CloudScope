// HAL: pan-tilt mounts (FR-CTL-05, FR-CTL-08, FR-SAF-03, FR-SAF-05).
//
// Positions here are actuator angles of the two axes (pan, tilt) in degrees, as the mechanism defines them.
// Turning them into azimuth and elevation is the job of the kinematic model (P056), not of a driver.
//
// A mount enforces its own travel limits and its emergency stop: these are the last line of defence below
// the host's planner and safety supervisor (safety is enforced in two places).
//
// "Measured" versus "commanded": a hobby servo has no feedback, so its driver can only report where it was
// told to be. status().position_measured says which kind of position is reported (NFR-DATA-02).
//
// Threads: every function of a mount may be called from any thread; the planner commands it while the safety
// supervisor watches it.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/units.hpp"
#include "cloudscope/hal/device.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cloudscope::hal {

struct AxisLimits {
    Degrees minimum;
    Degrees maximum;
    double max_speed_deg_s = 0.0;
};

struct MountCapabilities {
    AxisLimits pan;
    AxisLimits tilt;
    bool position_feedback = false;  // true: positions come from encoders or an IMU, not from the last command
};

struct MountPosition {
    Degrees pan;
    Degrees tilt;

    friend bool operator==(const MountPosition&, const MountPosition&) = default;
};

enum class MountMotion : std::uint8_t {
    Idle,     // at rest at its target
    Moving,   // on the way to its target, or still slowing down after a stop
    Stopped,  // at rest short of its target, after stop() or an emergency stop
    Fault,    // the device reported a problem (stall, lost communication)
};

// "idle", "moving", "stopped", "fault".
[[nodiscard]] std::string_view to_string(MountMotion motion);

struct MountStatus {
    MountPosition position;
    bool position_measured = false;
    MountPosition target;
    MountMotion motion = MountMotion::Idle;
    bool emergency_stop = false;
    std::string fault;  // what the device reported; empty unless motion is Fault
    Timestamp time;     // when this status was true
};

class IMount : public IDevice {
public:
    [[nodiscard]] virtual Expected<MountCapabilities> capabilities() const = 0;

    // Starts a move and returns at once; progress is seen through status(). `speed_deg_s` is the speed of the
    // axis that has further to go; the other axis moves proportionally slower, so that both arrive together.
    // A move_to() during a move replaces the target.
    //   InvalidArgument  target outside the limits, or speed not within (0, max speed of the slower axis]
    //   Unavailable      emergency stop active, or the mount is closed
    [[nodiscard]] virtual Expected<void> move_to(MountPosition target, double speed_deg_s) = 0;

    // Brings the mount to rest where it is; a new move_to() is accepted afterwards.
    virtual void stop() = 0;

    // Halts the mount and refuses every move_to() until clear_emergency_stop().
    virtual void emergency_stop() = 0;
    [[nodiscard]] virtual Expected<void> clear_emergency_stop() = 0;

    [[nodiscard]] virtual Expected<MountStatus> status() const = 0;
};

}  // namespace cloudscope::hal
