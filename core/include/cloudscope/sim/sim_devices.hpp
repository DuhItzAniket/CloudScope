// The simulated mount, IMU, GPS receiver and environment sensors: HAL devices that are views of a SimRig
// (FR-CTL-02). They can be used wherever real devices are; what they deliver is labelled simulated.
//
// Device ids: "sim:mount:pan-tilt", "sim:imu:head", "sim:sensor:gps", "sim:sensor:environment"
// (and the cameras of sim_camera.hpp: "sim:camera:sky", "sim:camera:replay").
#pragma once

#include "cloudscope/hal/mount.hpp"
#include "cloudscope/hal/sensors.hpp"
#include "cloudscope/sim/sim_rig.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace cloudscope::sim {

inline constexpr std::string_view kSkyCameraId = "sim:camera:sky";
inline constexpr std::string_view kReplayCameraId = "sim:camera:replay";
inline constexpr std::string_view kMountId = "sim:mount:pan-tilt";
inline constexpr std::string_view kImuId = "sim:imu:head";
inline constexpr std::string_view kGpsId = "sim:sensor:gps";
inline constexpr std::string_view kEnvironmentId = "sim:sensor:environment";

// Identity of a simulated device: driver "sim", simulated = true.
[[nodiscard]] hal::DeviceInfo sim_device_info(std::string_view id, hal::DeviceKind kind, std::string_view name);

// What the simulated devices share: identity, and the open state with its claim on the rig (one user per
// device, as with real hardware). Thread-safe.
class SimDeviceState {
public:
    SimDeviceState(std::shared_ptr<SimRig> rig, hal::DeviceInfo info);

    [[nodiscard]] const hal::DeviceInfo& info() const { return info_; }
    [[nodiscard]] SimRig& rig() const { return *rig_; }
    [[nodiscard]] Expected<void> open();
    bool close();  // returns whether the device was open
    [[nodiscard]] bool is_open() const;
    [[nodiscard]] MonotonicTime opened_at() const;

private:
    std::shared_ptr<SimRig> rig_;
    hal::DeviceInfo info_;
    mutable std::mutex mutex_;
    bool open_ = false;
    MonotonicTime opened_at_;
};

// The pan-tilt mount: limited speed and acceleration, command and telemetry latency, travel limits,
// optional position feedback with noise. Its behaviour is that of SimRig; see there.
class SimMount final : public hal::IMount {
public:
    explicit SimMount(std::shared_ptr<SimRig> rig);
    ~SimMount() override;
    SimMount(const SimMount&) = delete;
    SimMount& operator=(const SimMount&) = delete;
    SimMount(SimMount&&) = delete;
    SimMount& operator=(SimMount&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return state_.info(); }
    [[nodiscard]] Expected<void> open() override { return state_.open(); }
    void close() override;
    [[nodiscard]] bool is_open() const override { return state_.is_open(); }

    [[nodiscard]] Expected<hal::MountCapabilities> capabilities() const override;
    [[nodiscard]] Expected<void> move_to(hal::MountPosition target, double speed_deg_s) override;
    void stop() override;
    void emergency_stop() override;
    [[nodiscard]] Expected<void> clear_emergency_stop() override;
    [[nodiscard]] Expected<hal::MountStatus> status() const override;

private:
    SimDeviceState state_;
};

// An IMU fixed to the camera: it measures the true orientation of the head with noise and, without an
// absolute heading reference, a heading that drifts from the moment the sensor is opened.
class SimImu final : public hal::IImu {
public:
    explicit SimImu(std::shared_ptr<SimRig> rig);
    ~SimImu() override;
    SimImu(const SimImu&) = delete;
    SimImu& operator=(const SimImu&) = delete;
    SimImu(SimImu&&) = delete;
    SimImu& operator=(SimImu&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return state_.info(); }
    [[nodiscard]] Expected<void> open() override { return state_.open(); }
    void close() override { state_.close(); }
    [[nodiscard]] bool is_open() const override { return state_.is_open(); }

    [[nodiscard]] Expected<hal::ImuCapabilities> capabilities() const override;
    [[nodiscard]] Expected<hal::ImuSample> read() override;

private:
    SimDeviceState state_;
};

// A GPS receiver at the configured site: no position until the time to first fix has passed after opening
// (or while the fix is taken away); then position with noise, its accuracy, and the offset of GPS time from
// the host clock, renewed once per second.
class SimGps final : public hal::ISensor {
public:
    explicit SimGps(std::shared_ptr<SimRig> rig);
    ~SimGps() override;
    SimGps(const SimGps&) = delete;
    SimGps& operator=(const SimGps&) = delete;
    SimGps(SimGps&&) = delete;
    SimGps& operator=(SimGps&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return state_.info(); }
    [[nodiscard]] Expected<void> open() override { return state_.open(); }
    void close() override { state_.close(); }
    [[nodiscard]] bool is_open() const override { return state_.is_open(); }

    [[nodiscard]] Expected<std::vector<hal::SensorQuantity>> quantities() const override;
    [[nodiscard]] Expected<std::vector<hal::SensorReading>> read() override;

private:
    SimDeviceState state_;
};

// Temperature, humidity, pressure, light and rain as set on the rig, with a little measurement noise.
class SimEnvironment final : public hal::ISensor {
public:
    explicit SimEnvironment(std::shared_ptr<SimRig> rig);
    ~SimEnvironment() override;
    SimEnvironment(const SimEnvironment&) = delete;
    SimEnvironment& operator=(const SimEnvironment&) = delete;
    SimEnvironment(SimEnvironment&&) = delete;
    SimEnvironment& operator=(SimEnvironment&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return state_.info(); }
    [[nodiscard]] Expected<void> open() override { return state_.open(); }
    void close() override { state_.close(); }
    [[nodiscard]] bool is_open() const override { return state_.is_open(); }

    [[nodiscard]] Expected<std::vector<hal::SensorQuantity>> quantities() const override;
    [[nodiscard]] Expected<std::vector<hal::SensorReading>> read() override;

private:
    SimDeviceState state_;
};

}  // namespace cloudscope::sim
