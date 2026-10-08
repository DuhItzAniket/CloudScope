#include "cloudscope/uvc/uvc_driver.hpp"

#include "cloudscope/uvc/uvc_camera.hpp"
#include "uvc_backend.hpp"

#include <fmt/format.h>

#include <map>
#include <utility>

namespace cloudscope::uvc {

std::string device_id(std::uint16_t vendor_id, std::uint16_t product_id, int ordinal)
{
    return fmt::format("uvc:{:04x}:{:04x}:{}", vendor_id, product_id, ordinal);
}

std::shared_ptr<UvcDriver> UvcDriver::make(const IClock& clock)
{
    return std::shared_ptr<UvcDriver>(new UvcDriver(clock));
}

Expected<std::vector<UvcCameraEntry>> UvcDriver::entries()
{
    auto descriptors = enumerate_platform_cameras();
    if (!descriptors) {
        return fail(descriptors.error());
    }
    std::vector<UvcCameraEntry> entries;
    std::map<std::pair<std::uint16_t, std::uint16_t>, int> ordinals;
    for (const UvcDeviceDescriptor& descriptor : *descriptors) {  // sorted by path: stable numbering
        const int ordinal = ++ordinals[{descriptor.vendor_id, descriptor.product_id}];
        UvcCameraEntry entry;
        entry.info = {.id = device_id(descriptor.vendor_id, descriptor.product_id, ordinal),
                      .kind = hal::DeviceKind::Camera,
                      .name = descriptor.name.empty() ? std::string("USB camera") : descriptor.name,
                      .driver = "uvc",
                      .simulated = false};
        entry.vendor_id = descriptor.vendor_id;
        entry.product_id = descriptor.product_id;
        entry.path = descriptor.path;
        entry.serial = descriptor.serial;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<hal::DeviceInfo> UvcDriver::enumerate()
{
    std::vector<hal::DeviceInfo> devices;
    if (auto found = entries()) {
        for (UvcCameraEntry& entry : *found) {
            devices.push_back(std::move(entry.info));
        }
    }
    return devices;
}

Expected<std::shared_ptr<hal::IDevice>> UvcDriver::create(std::string_view id)
{
    auto found = entries();
    if (!found) {
        return fail(found.error());
    }
    for (const UvcCameraEntry& entry : *found) {
        if (entry.info.id == id) {
            UvcDeviceDescriptor descriptor{.path = entry.path,
                                           .name = entry.info.name,
                                           .vendor_id = entry.vendor_id,
                                           .product_id = entry.product_id,
                                           .serial = entry.serial};
            return std::make_shared<UvcCamera>(entry.info, make_platform_backend(descriptor, clock_));
        }
    }
    return fail(ErrorCode::NotFound, fmt::format("driver 'uvc' has no device '{}'", id));
}

Expected<CameraSettings> camera_settings(const nlohmann::json& effective)
{
    if (!effective.is_object() || !effective.contains("camera")) {
        return fail(ErrorCode::InvalidArgument, "the configuration has no [camera] section");
    }
    CameraSettings settings;
    settings.uvc = effective.at("camera").at("uvc").get<bool>();
    return settings;
}

}  // namespace cloudscope::uvc
