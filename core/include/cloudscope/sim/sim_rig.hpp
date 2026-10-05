// The simulated rig: one pan-tilt head carrying a camera and an IMU, with a GPS receiver and environment
// sensors next to it (FR-CTL-02). It is the "world" behind the simulated devices: they are views of this one
// object, so the IMU turns when the mount moves, and a test (or a person operating the simulator) has one
// place to make things happen: stall an axis, pull the camera's cable, take the GPS fix away.
//
// Nothing here runs on a thread or advances in steps. The state of the mechanism is a function of time and
// is computed when it is asked for, from the clock the rig was given: with the system clock it moves in real
// time, with a ManualClock a test decides how much time has passed.
//
// Everything a simulated device delivers is labelled simulated (NFR-DATA-03).
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/common/units.hpp"
#include "cloudscope/geometry/rotation.hpp"
#include "cloudscope/hal/mount.hpp"
#include "cloudscope/hal/sensors.hpp"
#include "cloudscope/sim/motion_profile.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace cloudscope::sim {

struct SimCameraConfig {
    // true: frames arrive at the mode's rate, as from a real camera. false: every read returns a frame at
    // once, for tests that must not wait.
    bool real_time = true;
    // The synthetic sky.
    double cloud_fraction = 0.4;  // 0 to 1
    double cloud_drift = 0.005;   // how fast the clouds move: fraction of the image width per second
    bool sun_visible = true;
    double sun_x = 0.7;  // Sun centre as a fraction of the image width, and of its height
    double sun_y = 0.3;
    // Replay of recorded pictures: a folder of JPEG or PNG files, shown in name order, again and again.
    // Empty: no replay camera.
    std::filesystem::path replay_folder;
    double replay_fps = 2.0;
};

struct SimMountConfig {
    hal::AxisLimits pan{.minimum = Degrees(-170.0), .maximum = Degrees(170.0), .max_speed_deg_s = 60.0};
    hal::AxisLimits tilt{.minimum = Degrees(0.0), .maximum = Degrees(90.0), .max_speed_deg_s = 60.0};
    double max_acceleration_deg_s2 = 240.0;
    std::chrono::milliseconds command_latency{20};    // from a command to its effect on the mechanism
    std::chrono::milliseconds telemetry_latency{20};  // age of the state a status reports
    bool position_feedback = false;                   // false: like hobby servos, it reports where it was told to be
    Degrees feedback_noise{0.05};                     // 1 sigma of a measured position
    hal::MountPosition start{.pan = Degrees(0.0), .tilt = Degrees(45.0)};
    // Ideal geometry: the camera looks at azimuth pan + pan_zero_azimuth and elevation tilt.
    Degrees pan_zero_azimuth{0.0};
};

struct SimImuConfig {
    double rate_hz = 100.0;
    Degrees noise{0.1};                  // 1 sigma per axis and sample
    bool absolute_heading = false;       // false: no magnetometer, the heading drifts
    double heading_drift_deg_min = 0.5;  // used when absolute_heading is false
};

struct SimGpsConfig {
    // Centre of Bengaluru, rounded to 0.01 degrees: a plausible site, nobody's address.
    double latitude_deg = 12.97;
    double longitude_deg = 77.59;
    double altitude_m = 920.0;
    double horizontal_noise_m = 2.5;            // 1 sigma
    std::chrono::seconds time_to_first_fix{5};  // after the receiver is opened
};

struct SimEnvironmentConfig {
    double temperature_c = 27.0;
    double relative_humidity = 60.0;
    double pressure_hpa = 912.0;
    double illuminance_lx = 20000.0;
    bool rain = false;
};

struct SimulationConfig {
    std::uint64_t seed = 1;  // the same seed gives the same clouds and the same noise
    SimCameraConfig camera;
    SimMountConfig mount;
    SimImuConfig imu;
    SimGpsConfig gps;
    SimEnvironmentConfig environment;
};

// InvalidArgument, naming the value, if the configuration is not usable (limits the wrong way round, a rate
// of zero, a start position outside the limits, ...).
[[nodiscard]] Expected<void> check(const SimulationConfig& config);

enum class MountAxis : std::uint8_t { Pan, Tilt };

// Thread-safe.
class SimRig {
public:
    // `config` must have passed check(). The clock must outlive the rig.
    SimRig(const IClock& clock, const SimulationConfig& config);

    [[nodiscard]] const IClock& clock() const { return clock_; }
    [[nodiscard]] const SimulationConfig& config() const { return config_; }

    // One user per device, as with real hardware. Unavailable if `id` is in use.
    [[nodiscard]] Expected<void> claim(const std::string& id);
    void release(const std::string& id);

    // ---- The mount: mechanism and controller -------------------------------------------------------------
    [[nodiscard]] hal::MountCapabilities mount_capabilities() const;
    // The checks and the behaviour that hal::IMount describes. A command acts after the command latency.
    [[nodiscard]] Expected<void> move_to(hal::MountPosition target, double speed_deg_s);
    void stop();            // the axes brake to rest
    void emergency_stop();  // the axes stop dead; moves are refused until cleared
    void clear_emergency_stop();
    // What the controller reports: the commanded position, or the measured one (with noise) if the mount has
    // position feedback; as it was one telemetry latency ago. Io if the controller is unreachable.
    [[nodiscard]] Expected<hal::MountStatus> mount_status() const;

    // Where the mechanism really is, and how the camera on it is really oriented. Not delayed, not noisy:
    // this is the truth that the simulated IMU measures and that tests compare against.
    [[nodiscard]] hal::MountPosition true_position() const;
    [[nodiscard]] hal::MountPosition true_position(MonotonicTime at) const;
    [[nodiscard]] Quaternion true_orientation(MonotonicTime at) const;
    [[nodiscard]] SkyDirection true_pointing() const;

    // ---- What a test, or a person operating the simulator, can make happen ---------------------------------
    // A stalled axis stays where it is while the controller believes it follows; released, it catches up.
    void set_axis_stalled(MountAxis axis, bool stalled);
    // Unreachable: moves and status fail with Io, stop commands are lost, the mechanism finishes its move.
    void set_controller_reachable(bool reachable);
    // Disconnected: the camera's stream ends with Io and it cannot be opened.
    void set_camera_connected(bool connected);
    [[nodiscard]] bool camera_connected() const;
    // Stalled: the camera stays connected but delivers no frames.
    void set_camera_stalled(bool stalled);
    [[nodiscard]] bool camera_stalled() const;
    // The next `count` frames are lost on the way; their sequence numbers are skipped.
    void lose_camera_frames(std::uint64_t count);
    [[nodiscard]] std::uint64_t take_lost_camera_frames();  // called by the camera: returns the count and clears it
    // No fix: the GPS receiver reports no position.
    void set_gps_fix(bool available);
    [[nodiscard]] bool gps_fix() const;
    // Temperature, RelativeHumidity, Pressure, Illuminance or Rain; other quantities are ignored.
    void set_environment(hal::SensorQuantity quantity, double value);
    [[nodiscard]] double environment(hal::SensorQuantity quantity) const;

private:
    // The commanded motion from `start` on, until the next entry of the timeline begins.
    struct Epoch {
        MonotonicTime start;
        AxisProfile pan;
        AxisProfile tilt;
    };
    struct AxisState {
        double position = 0.0;
        double velocity = 0.0;
    };
    struct Stall {
        bool stalled = false;
        double frozen_at = 0.0;       // where the axis stays while stalled
        MonotonicTime released;       // when the stall ended
        double lag_at_release = 0.0;  // how far it was from its command then
    };

    [[nodiscard]] const Epoch& epoch_at(MonotonicTime at) const;
    [[nodiscard]] AxisState commanded(MountAxis axis, MonotonicTime at) const;
    [[nodiscard]] double actual(MountAxis axis, MonotonicTime at) const;
    [[nodiscard]] bool in_motion(MonotonicTime at) const;
    void begin_epoch(Epoch epoch);

    const IClock& clock_;
    SimulationConfig config_;
    mutable std::mutex mutex_;
    std::set<std::string> claimed_;
    std::vector<Epoch> timeline_;  // ascending by start, never empty
    hal::MountPosition target_;
    bool emergency_stop_ = false;
    bool controller_reachable_ = true;
    Stall pan_stall_;
    Stall tilt_stall_;
    bool camera_connected_ = true;
    bool camera_stalled_ = false;
    std::uint64_t camera_frames_to_lose_ = 0;
    bool gps_fix_ = true;
    std::map<hal::SensorQuantity, double> environment_;
};

}  // namespace cloudscope::sim
