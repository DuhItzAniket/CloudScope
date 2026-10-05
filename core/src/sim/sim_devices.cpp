#include "cloudscope/sim/sim_devices.hpp"

#include "sim_random.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace cloudscope::sim {

namespace {

using hal::DeviceKind;
using hal::not_open;
using hal::SensorQuantity;
using hal::SensorReading;

constexpr double kMetresPerDegree = 111320.0;  // of latitude; of longitude at the equator

using std::chrono::nanoseconds;

// The newest sample of a sensor that takes one every `period`, counted from when it was opened.
struct Sample {
    std::uint64_t index = 0;  // 0 for the sample taken at the moment of opening
    Timestamp time;           // when it was taken
    double elapsed_s = 0.0;   // from opening to the sample
};

// Whole nanoseconds throughout: two reads between the same two sample instants get exactly the same sample.
Sample newest_sample(const Timestamp& now, MonotonicTime opened, nanoseconds period)
{
    const nanoseconds elapsed =
        std::max(nanoseconds(0), std::chrono::duration_cast<nanoseconds>(now.monotonic - opened));
    const nanoseconds::rep index = elapsed / period;
    const nanoseconds taken = period * index;
    const nanoseconds age = elapsed - taken;
    return {.index = static_cast<std::uint64_t>(index),
            .time = {.utc = now.utc - std::chrono::duration_cast<std::chrono::milliseconds>(age),
                     .monotonic = now.monotonic - std::chrono::duration_cast<MonotonicTime::duration>(age),
                     .source = now.source},
            .elapsed_s = std::chrono::duration<double>(taken).count()};
}

constexpr nanoseconds kOneSecond = std::chrono::seconds(1);

}  // namespace

hal::DeviceInfo sim_device_info(std::string_view id, DeviceKind kind, std::string_view name)
{
    return {.id = std::string(id), .kind = kind, .name = std::string(name), .driver = "sim", .simulated = true};
}

SimDeviceState::SimDeviceState(std::shared_ptr<SimRig> rig, hal::DeviceInfo info)
    : rig_(std::move(rig)), info_(std::move(info))
{
}

Expected<void> SimDeviceState::open()
{
    const std::lock_guard lock(mutex_);
    if (open_) {
        return {};
    }
    if (auto claimed = rig_->claim(info_.id); !claimed) {
        return fail(claimed.error());
    }
    open_ = true;
    opened_at_ = rig_->clock().now_monotonic();
    return {};
}

bool SimDeviceState::close()
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return false;
    }
    rig_->release(info_.id);
    open_ = false;
    return true;
}

bool SimDeviceState::is_open() const
{
    const std::lock_guard lock(mutex_);
    return open_;
}

MonotonicTime SimDeviceState::opened_at() const
{
    const std::lock_guard lock(mutex_);
    return opened_at_;
}

// ----------------------------------------------------------------------------------------- mount

SimMount::SimMount(std::shared_ptr<SimRig> rig)
    : state_(std::move(rig), sim_device_info(kMountId, DeviceKind::Mount, "Simulated pan-tilt mount"))
{
}

SimMount::~SimMount()
{
    if (state_.close()) {
        state_.rig().stop();
    }
}

void SimMount::close()
{
    if (state_.close()) {
        state_.rig().stop();  // nobody is in command any more: the head comes to rest
    }
}

Expected<hal::MountCapabilities> SimMount::capabilities() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return state_.rig().mount_capabilities();
}

Expected<void> SimMount::move_to(hal::MountPosition target, double speed_deg_s)
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return state_.rig().move_to(target, speed_deg_s);
}

void SimMount::stop()
{
    if (state_.is_open()) {
        state_.rig().stop();
    }
}

void SimMount::emergency_stop()
{
    if (state_.is_open()) {
        state_.rig().emergency_stop();
    }
}

Expected<void> SimMount::clear_emergency_stop()
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    state_.rig().clear_emergency_stop();
    return {};
}

Expected<hal::MountStatus> SimMount::status() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return state_.rig().mount_status();
}

// ------------------------------------------------------------------------------------------- IMU

SimImu::SimImu(std::shared_ptr<SimRig> rig)
    : state_(std::move(rig), sim_device_info(kImuId, DeviceKind::Imu, "Simulated IMU on the camera head"))
{
}

SimImu::~SimImu()
{
    state_.close();
}

Expected<hal::ImuCapabilities> SimImu::capabilities() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    const SimImuConfig& config = state_.rig().config().imu;
    return hal::ImuCapabilities{.absolute_heading = config.absolute_heading, .max_rate_hz = config.rate_hz};
}

Expected<hal::ImuSample> SimImu::read()
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    const SimRig& rig = state_.rig();
    const SimImuConfig& config = rig.config().imu;
    const Timestamp now = rig.clock().now();

    // The sensor takes a sample every 1 / rate seconds.
    const nanoseconds period(std::llround(1e9 / config.rate_hz));
    const Sample sample = newest_sample(now, state_.opened_at(), period);
    const std::uint64_t index = sample.index;

    Quaternion orientation = rig.true_orientation(sample.time.monotonic);
    if (!config.absolute_heading) {
        // Without a magnetometer the heading error grows with time. Azimuth runs clockwise seen from above,
        // a positive turn about "up" runs anticlockwise: hence the sign.
        const Degrees drift(config.heading_drift_deg_min * sample.elapsed_s / 60.0);
        orientation = multiply(from_axis_angle({.x = 0.0, .y = 0.0, .z = 1.0}, to_radians(-drift)), orientation);
    }
    // Measurement noise: a small turn about a random axis, the same for every read of the same sample.
    const double sigma = to_radians(config.noise).value();
    const std::uint64_t seed = rig.config().seed;
    const Vector3 wobble{.x = sigma * normal(seed, 10, index),
                         .y = sigma * normal(seed, 11, index),
                         .z = sigma * normal(seed, 12, index)};
    const Radians angle(std::sqrt(wobble.x * wobble.x + wobble.y * wobble.y + wobble.z * wobble.z));
    orientation = normalised(multiply(from_axis_angle(wobble, angle), orientation));

    return hal::ImuSample{.time = sample.time,
                          .orientation = orientation,
                          .accuracy = Degrees(3.0 * config.noise.value()),
                          .calibrated = true};
}

// ------------------------------------------------------------------------------------------- GPS

SimGps::SimGps(std::shared_ptr<SimRig> rig)
    : state_(std::move(rig), sim_device_info(kGpsId, DeviceKind::Sensor, "Simulated GPS receiver"))
{
}

SimGps::~SimGps()
{
    state_.close();
}

Expected<std::vector<SensorQuantity>> SimGps::quantities() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return std::vector<SensorQuantity>{SensorQuantity::Latitude, SensorQuantity::Longitude, SensorQuantity::Altitude,
                                       SensorQuantity::HorizontalAccuracy, SensorQuantity::ClockOffset};
}

Expected<std::vector<SensorReading>> SimGps::read()
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    const SimRig& rig = state_.rig();
    const SimGpsConfig& config = rig.config().gps;
    const Timestamp now = rig.clock().now();
    if (!rig.gps_fix() || now.monotonic - state_.opened_at() < config.time_to_first_fix) {
        return std::vector<SensorReading>{};  // no fix: no position and no time
    }

    // One solution per second.
    const Sample solution = newest_sample(now, state_.opened_at(), kOneSecond);
    const std::uint64_t index = solution.index;
    const Timestamp time = solution.time;
    const std::uint64_t seed = rig.config().seed;
    const double north_m = config.horizontal_noise_m * normal(seed, 20, index);
    const double east_m = config.horizontal_noise_m * normal(seed, 21, index);
    const double metres_per_degree_east =
        kMetresPerDegree * std::max(0.01, std::cos(to_radians(Degrees(config.latitude_deg)).value()));
    const double latitude = std::clamp(config.latitude_deg + north_m / kMetresPerDegree, -90.0, 90.0);
    const double longitude = wrap_180(Degrees(config.longitude_deg + east_m / metres_per_degree_east)).value();
    // Height is the weakest coordinate of a GPS solution.
    const double altitude = config.altitude_m + 1.5 * config.horizontal_noise_m * normal(seed, 22, index);
    const double clock_offset_s = 0.002 * normal(seed, 23, index);

    return std::vector<SensorReading>{
        {.quantity = SensorQuantity::Latitude, .value = latitude, .time = time},
        {.quantity = SensorQuantity::Longitude, .value = longitude, .time = time},
        {.quantity = SensorQuantity::Altitude, .value = altitude, .time = time},
        {.quantity = SensorQuantity::HorizontalAccuracy, .value = config.horizontal_noise_m, .time = time},
        {.quantity = SensorQuantity::ClockOffset, .value = clock_offset_s, .time = time},
    };
}

// ----------------------------------------------------------------------------------- environment

SimEnvironment::SimEnvironment(std::shared_ptr<SimRig> rig)
    : state_(std::move(rig), sim_device_info(kEnvironmentId, DeviceKind::Sensor, "Simulated environment sensors"))
{
}

SimEnvironment::~SimEnvironment()
{
    state_.close();
}

Expected<std::vector<SensorQuantity>> SimEnvironment::quantities() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return std::vector<SensorQuantity>{SensorQuantity::Temperature, SensorQuantity::RelativeHumidity,
                                       SensorQuantity::Pressure, SensorQuantity::Illuminance, SensorQuantity::Rain};
}

Expected<std::vector<SensorReading>> SimEnvironment::read()
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    const SimRig& rig = state_.rig();
    const Timestamp now = rig.clock().now();
    // New noise once per second.
    const std::uint64_t index = newest_sample(now, state_.opened_at(), kOneSecond).index;
    const std::uint64_t seed = rig.config().seed;

    const double temperature = rig.environment(SensorQuantity::Temperature) + 0.05 * normal(seed, 30, index);
    const double humidity =
        std::clamp(rig.environment(SensorQuantity::RelativeHumidity) + 0.3 * normal(seed, 31, index), 0.0, 100.0);
    const double pressure = rig.environment(SensorQuantity::Pressure) + 0.05 * normal(seed, 32, index);
    const double light = rig.environment(SensorQuantity::Illuminance);
    const double illuminance = std::max(0.0, light * (1.0 + 0.005 * normal(seed, 33, index)));

    return std::vector<SensorReading>{
        {.quantity = SensorQuantity::Temperature, .value = temperature, .time = now},
        {.quantity = SensorQuantity::RelativeHumidity, .value = humidity, .time = now},
        {.quantity = SensorQuantity::Pressure, .value = pressure, .time = now},
        {.quantity = SensorQuantity::Illuminance, .value = illuminance, .time = now},
        {.quantity = SensorQuantity::Rain, .value = rig.environment(SensorQuantity::Rain), .time = now},
    };
}

}  // namespace cloudscope::sim
