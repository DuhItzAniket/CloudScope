// The "uvc" driver: the computer's USB video cameras (and built-in webcams) for the device registry (P019).
//
//   auto driver = uvc::UvcDriver::make(clock);
//   registry.add_driver(driver);
//   for (const auto& info : registry.enumerate(hal::DeviceKind::Camera)) ...   // "uvc:0c45:636d:1", ...
//   auto camera = registry.create<hal::ICamera>("uvc:0c45:636d:1");
//
// Device ids are "uvc:<vendor id>:<product id>:<n>", with n counting cameras of the same model in the order of
// their system paths; the id does not change when the camera is unplugged and plugged in again, and two
// cameras of one model keep their numbers as long as their USB ports do not change. Enumeration rescans the
// system every time it is called (hot-plug: a new camera appears at the next enumeration).
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/hal/registry.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope::uvc {

// What the driver knows about a camera it listed.
struct UvcCameraEntry {
    hal::DeviceInfo info;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::string path;    // the platform's handle: symbolic link (Windows) or device node (Linux)
    std::string serial;  // when the platform exposes it
};

class UvcDriver final : public hal::IDriver {
public:
    // The clock stamps the frames; it must outlive the driver and its cameras.
    [[nodiscard]] static std::shared_ptr<UvcDriver> make(const IClock& clock);

    [[nodiscard]] std::string_view name() const override { return "uvc"; }
    [[nodiscard]] std::vector<hal::DeviceInfo> enumerate() override;
    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view id) override;

    // The cameras of the last enumeration, with their platform details.
    [[nodiscard]] static Expected<std::vector<UvcCameraEntry>> entries();

private:
    explicit UvcDriver(const IClock& clock) : clock_(clock) {}

    const IClock& clock_;
};

// "uvc:0c45:636d:1" for the n-th camera (1-based) of a model.
[[nodiscard]] std::string device_id(std::uint16_t vendor_id, std::uint16_t product_id, int ordinal);

// The [camera] section of an effective configuration (config.toml).
struct CameraSettings {
    bool uvc = true;  // offer the computer's USB cameras
};
[[nodiscard]] Expected<CameraSettings> camera_settings(const nlohmann::json& effective);

}  // namespace cloudscope::uvc
