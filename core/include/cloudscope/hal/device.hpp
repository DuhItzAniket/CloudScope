// Hardware abstraction layer (HAL): what every device has in common (FR-CTL-01).
//
// All hardware is reached through the interfaces in this folder: ICamera, IMount, IImu, ISensor, ITransport,
// IInference. Drivers, simulators and test doubles implement them; the rest of CloudScope never talks to an
// operating-system or vendor API directly. Every implementation must pass the contract tests in
// tests/contract/, which are the executable form of the rules written in these headers.
//
// Rules for all devices:
//   - A device object is used from one thread at a time unless its interface says otherwise.
//   - A new device object is closed. open() and close() may be repeated: opening an open device and closing a
//     closed one do nothing. close() ends anything in progress (streaming, motion commands).
//   - Functions that need an open device fail with ErrorCode::Unavailable when it is closed.
//   - Nothing is silently substituted: a request the device cannot honour is an error, or it is reported back
//     as "not applied" together with the value that is really in effect.
#pragma once

#include "cloudscope/common/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cloudscope::hal {

enum class DeviceKind : std::uint8_t { Camera, Mount, Imu, Sensor, Transport, Inference };

// "camera", "mount", "imu", "sensor", "transport", "inference".
[[nodiscard]] std::string_view to_string(DeviceKind kind);

struct DeviceInfo {
    std::string id;  // "<driver>:<rest>", stable across restarts and re-plugging, e.g. "sim:camera:sky"
    DeviceKind kind = DeviceKind::Camera;
    std::string name;        // for people: "Arducam B0268", "Simulated sky camera"
    std::string driver;      // the driver that provides it; equals the part of `id` before the first colon
    bool simulated = false;  // true: its data is not a measurement and must be labelled as such (NFR-DATA-03)
};

class IDevice {
public:
    IDevice() = default;
    virtual ~IDevice() = default;
    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;

    [[nodiscard]] virtual const DeviceInfo& info() const = 0;
    // NotFound if the device has gone, Unavailable if something else is using it.
    [[nodiscard]] virtual Expected<void> open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
};

// The error every device returns for a call that needs it to be open.
[[nodiscard]] Unexpected not_open(const DeviceInfo& info);

}  // namespace cloudscope::hal
