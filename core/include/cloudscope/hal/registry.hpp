// The device registry: which devices exist, and how to get one (FR-CTL-01).
//
// A driver provides devices of one family ("sim", later "uvc", "csdp", "alpaca", ...). The registry is told
// about drivers once at start-up; after that the rest of the program asks the registry and never a driver:
//
//   registry.add_driver(std::make_shared<SimulatorDriver>());
//   for (const DeviceInfo& info : registry.enumerate(DeviceKind::Camera)) { ... }
//   auto camera = registry.create<ICamera>("sim:camera:sky");
//
// A device id is "<driver name>:<rest>"; the registry routes by the part before the first colon.
// Thread-safe.
#pragma once

#include "cloudscope/hal/device.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope::hal {

class IDriver {
public:
    IDriver() = default;
    virtual ~IDriver() = default;
    IDriver(const IDriver&) = delete;
    IDriver& operator=(const IDriver&) = delete;
    IDriver(IDriver&&) = delete;
    IDriver& operator=(IDriver&&) = delete;

    // Short, lower-case, without a colon: it is the prefix of every id this driver hands out.
    [[nodiscard]] virtual std::string_view name() const = 0;

    // The devices present right now. May be slow (it can scan buses); not called on the UI thread.
    [[nodiscard]] virtual std::vector<DeviceInfo> enumerate() = 0;

    // A new, closed device object for an id from enumerate(). NotFound if there is no such device.
    [[nodiscard]] virtual Expected<std::shared_ptr<IDevice>> create(std::string_view id) = 0;
};

class DeviceRegistry {
public:
    // InvalidArgument for an unusable name; AlreadyExists if a driver with this name is registered.
    [[nodiscard]] Expected<void> add_driver(std::shared_ptr<IDriver> driver);

    [[nodiscard]] std::vector<std::string> driver_names() const;

    // Devices of all drivers, in the order the drivers were added.
    [[nodiscard]] std::vector<DeviceInfo> enumerate() const;
    [[nodiscard]] std::vector<DeviceInfo> enumerate(DeviceKind kind) const;

    // A new, closed device. NotFound: no such driver or device. InvalidArgument: the device is of another kind
    // than the interface asked for.
    template <class Interface>
    [[nodiscard]] Expected<std::shared_ptr<Interface>> create(std::string_view id) const
    {
        auto device = create_device(id);
        if (!device) {
            return fail(device.error());
        }
        auto typed = std::dynamic_pointer_cast<Interface>(*device);
        if (!typed) {
            return fail(wrong_kind(**device));
        }
        return typed;
    }

private:
    [[nodiscard]] std::vector<std::shared_ptr<IDriver>> drivers() const;  // a copy, taken under the lock
    [[nodiscard]] Expected<std::shared_ptr<IDevice>> create_device(std::string_view id) const;
    [[nodiscard]] static Error wrong_kind(const IDevice& device);

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<IDriver>> drivers_;
};

}  // namespace cloudscope::hal
