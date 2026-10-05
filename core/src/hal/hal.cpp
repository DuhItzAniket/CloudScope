#include "cloudscope/hal/camera.hpp"
#include "cloudscope/hal/device.hpp"
#include "cloudscope/hal/inference.hpp"
#include "cloudscope/hal/mount.hpp"
#include "cloudscope/hal/registry.hpp"
#include "cloudscope/hal/sensors.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace cloudscope::hal {

std::string_view to_string(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Camera:
        return "camera";
    case DeviceKind::Mount:
        return "mount";
    case DeviceKind::Imu:
        return "imu";
    case DeviceKind::Sensor:
        return "sensor";
    case DeviceKind::Transport:
        return "transport";
    case DeviceKind::Inference:
        return "inference";
    }
    return "unknown";
}

Unexpected not_open(const DeviceInfo& info)
{
    return fail(ErrorCode::Unavailable, fmt::format("{} '{}' is not open", to_string(info.kind), info.id));
}

std::string_view to_string(CameraControl control)
{
    switch (control) {
    case CameraControl::Exposure:
        return "exposure";
    case CameraControl::Gain:
        return "gain";
    case CameraControl::WhiteBalance:
        return "white_balance";
    case CameraControl::Brightness:
        return "brightness";
    case CameraControl::Contrast:
        return "contrast";
    case CameraControl::Saturation:
        return "saturation";
    case CameraControl::Gamma:
        return "gamma";
    case CameraControl::Sharpness:
        return "sharpness";
    case CameraControl::Focus:
        return "focus";
    }
    return "unknown";
}

double nearest_setting(const ControlInfo& info, double value)
{
    const double clamped = std::clamp(value, info.minimum, info.maximum);
    if (!(info.step > 0.0)) {
        return clamped;
    }
    // The last step that still lies inside the range (a range need not be a whole number of steps).
    const double last_step = std::floor((info.maximum - info.minimum) / info.step + 1e-9);
    const double step = std::min(std::round((clamped - info.minimum) / info.step), last_step);
    // Clamped once more: minimum + n * step can exceed the maximum by a rounding error.
    return std::clamp(info.minimum + step * info.step, info.minimum, info.maximum);
}

std::string_view to_string(MountMotion motion)
{
    switch (motion) {
    case MountMotion::Idle:
        return "idle";
    case MountMotion::Moving:
        return "moving";
    case MountMotion::Stopped:
        return "stopped";
    case MountMotion::Fault:
        return "fault";
    }
    return "unknown";
}

std::string_view to_string(SensorQuantity quantity)
{
    switch (quantity) {
    case SensorQuantity::Temperature:
        return "temperature";
    case SensorQuantity::RelativeHumidity:
        return "relative_humidity";
    case SensorQuantity::Pressure:
        return "pressure";
    case SensorQuantity::Illuminance:
        return "illuminance";
    case SensorQuantity::Rain:
        return "rain";
    case SensorQuantity::Latitude:
        return "latitude";
    case SensorQuantity::Longitude:
        return "longitude";
    case SensorQuantity::Altitude:
        return "altitude";
    case SensorQuantity::HorizontalAccuracy:
        return "horizontal_accuracy";
    case SensorQuantity::ClockOffset:
        return "clock_offset";
    }
    return "unknown";
}

std::string_view unit_of(SensorQuantity quantity)
{
    switch (quantity) {
    case SensorQuantity::Temperature:
        return "degC";
    case SensorQuantity::RelativeHumidity:
        return "%";
    case SensorQuantity::Pressure:
        return "hPa";
    case SensorQuantity::Illuminance:
        return "lx";
    case SensorQuantity::Rain:
        return "";
    case SensorQuantity::Latitude:
    case SensorQuantity::Longitude:
        return "deg";
    case SensorQuantity::Altitude:
    case SensorQuantity::HorizontalAccuracy:
        return "m";
    case SensorQuantity::ClockOffset:
        return "s";
    }
    return "";
}

std::size_t element_count(const std::vector<std::int64_t>& shape)
{
    std::size_t count = shape.empty() ? 0 : 1;
    for (const std::int64_t dimension : shape) {
        if (dimension <= 0) {
            return 0;
        }
        count *= static_cast<std::size_t>(dimension);
    }
    return count;
}

// ------------------------------------------------------------------------------------------ registry

namespace {

bool usable_driver_name(std::string_view name)
{
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

}  // namespace

Expected<void> DeviceRegistry::add_driver(std::shared_ptr<IDriver> driver)
{
    if (!driver || !usable_driver_name(driver->name())) {
        return fail(ErrorCode::InvalidArgument,
                    "a driver needs a name of lower-case letters, digits, '-' or '_' (it prefixes device ids)");
    }
    const std::lock_guard lock(mutex_);
    const bool taken = std::ranges::any_of(
        drivers_, [&driver](const std::shared_ptr<IDriver>& existing) { return existing->name() == driver->name(); });
    if (taken) {
        return fail(ErrorCode::AlreadyExists, fmt::format("a driver named '{}' is already registered", driver->name()));
    }
    drivers_.push_back(std::move(driver));
    return {};
}

std::vector<std::string> DeviceRegistry::driver_names() const
{
    const std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    names.reserve(drivers_.size());
    for (const std::shared_ptr<IDriver>& driver : drivers_) {
        names.emplace_back(driver->name());
    }
    return names;
}

std::vector<std::shared_ptr<IDriver>> DeviceRegistry::drivers() const
{
    const std::lock_guard lock(mutex_);
    return drivers_;
}

std::vector<DeviceInfo> DeviceRegistry::enumerate() const
{
    // Drivers are asked without the lock held: enumerating may be slow.
    std::vector<DeviceInfo> devices;
    for (const std::shared_ptr<IDriver>& driver : drivers()) {
        std::vector<DeviceInfo> found = driver->enumerate();
        devices.insert(devices.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    }
    return devices;
}

std::vector<DeviceInfo> DeviceRegistry::enumerate(DeviceKind kind) const
{
    std::vector<DeviceInfo> devices = enumerate();
    std::erase_if(devices, [kind](const DeviceInfo& info) { return info.kind != kind; });
    return devices;
}

Expected<std::shared_ptr<IDevice>> DeviceRegistry::create_device(std::string_view id) const
{
    const std::string_view driver_name = id.substr(0, id.find(':'));
    std::shared_ptr<IDriver> driver;
    {
        const std::lock_guard lock(mutex_);
        const auto found = std::ranges::find_if(drivers_, [driver_name](const std::shared_ptr<IDriver>& candidate) {
            return candidate->name() == driver_name;
        });
        if (found != drivers_.end()) {
            driver = *found;
        }
    }
    if (!driver) {
        return fail(ErrorCode::NotFound, fmt::format("no driver '{}' for device '{}'", driver_name, id));
    }
    auto device = driver->create(id);
    if (device && *device == nullptr) {
        return fail(ErrorCode::Internal, fmt::format("driver '{}' returned no device for '{}'", driver_name, id));
    }
    return device;
}

Error DeviceRegistry::wrong_kind(const IDevice& device)
{
    return Error{.code = ErrorCode::InvalidArgument,
                 .message = fmt::format("device '{}' is a {}; it cannot be used as another kind of device",
                                        device.info().id, to_string(device.info().kind))};
}

}  // namespace cloudscope::hal
