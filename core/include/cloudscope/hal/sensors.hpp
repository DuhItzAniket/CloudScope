// HAL: orientation sensors (IMU) and scalar sensors: environment, light, rain, GPS (FR-CTL-11).
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/units.hpp"
#include "cloudscope/hal/device.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace cloudscope::hal {

// ----------------------------------------------------------------------------------------------- IMU

// Rotation that takes sensor coordinates to local East-North-Up coordinates (ADR-012), as a unit quaternion.
struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ImuCapabilities {
    bool absolute_heading = false;  // true: yaw is referenced to north (magnetometer); false: it drifts
    double max_rate_hz = 0.0;       // how often a new sample is available
};

struct ImuSample {
    Timestamp time;
    Quaternion orientation;
    Degrees accuracy;  // the sensor's own estimate of its orientation error; 0 if it gives none
    bool calibrated = false;
};

class IImu : public IDevice {
public:
    [[nodiscard]] virtual Expected<ImuCapabilities> capabilities() const = 0;
    // The most recent sample. Unavailable if the sensor has not delivered one yet.
    [[nodiscard]] virtual Expected<ImuSample> read() = 0;
};

// -------------------------------------------------------------------------------------- scalar sensors

// What a sensor measures. Each quantity has one fixed unit, so a reading never needs a unit field.
enum class SensorQuantity : std::uint8_t {
    Temperature,         // degrees Celsius
    RelativeHumidity,    // percent, 0 to 100
    Pressure,            // hectopascal
    Illuminance,         // lux
    Rain,                // 0 = dry, 1 = rain detected
    Latitude,            // degrees, north positive (WGS 84)
    Longitude,           // degrees, east positive (WGS 84)
    Altitude,            // metres above the WGS 84 ellipsoid
    HorizontalAccuracy,  // metres, 1 sigma, of latitude and longitude
    ClockOffset,         // seconds: the sensor's own clock (GPS, RTC) minus the host's UTC clock
};

// "temperature", "relative_humidity", ...: the names used in sidecars and the API.
[[nodiscard]] std::string_view to_string(SensorQuantity quantity);
// "degC", "%", "hPa", "lx", "", "deg", "deg", "m", "m", "s".
[[nodiscard]] std::string_view unit_of(SensorQuantity quantity);

struct SensorReading {
    SensorQuantity quantity = SensorQuantity::Temperature;
    double value = 0.0;
    Timestamp time;
};

class ISensor : public IDevice {
public:
    // The quantities this sensor can deliver.
    [[nodiscard]] virtual Expected<std::vector<SensorQuantity>> quantities() const = 0;
    // One current reading per quantity that is available now: a quantity without a value at the moment is
    // left out (a GPS receiver without a fix returns no position).
    [[nodiscard]] virtual Expected<std::vector<SensorReading>> read() = 0;
};

}  // namespace cloudscope::hal
