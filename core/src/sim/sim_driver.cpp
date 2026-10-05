#include "cloudscope/sim/sim_driver.hpp"

#include "cloudscope/sim/sim_camera.hpp"
#include "cloudscope/sim/sim_devices.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <string>
#include <utility>

namespace cloudscope::sim {

namespace {

using hal::DeviceInfo;
using hal::DeviceKind;

DeviceInfo sky_camera_info()
{
    return sim_device_info(kSkyCameraId, DeviceKind::Camera, "Simulated sky camera");
}

DeviceInfo replay_camera_info()
{
    return sim_device_info(kReplayCameraId, DeviceKind::Camera, "Simulated camera (replay of recorded pictures)");
}

bool replay_available(const SimulationConfig& config)
{
    return !config.camera.replay_folder.empty() && has_replay_pictures(config.camera.replay_folder);
}

}  // namespace

SimDriver::SimDriver(std::shared_ptr<SimRig> rig) : rig_(std::move(rig)) {}

Expected<std::shared_ptr<SimDriver>> SimDriver::make(const IClock& clock, const SimulationConfig& config)
{
    if (auto usable = check(config); !usable) {
        return fail(usable.error());
    }
    return std::shared_ptr<SimDriver>(new SimDriver(std::make_shared<SimRig>(clock, config)));
}

std::vector<DeviceInfo> SimDriver::enumerate()
{
    std::vector<DeviceInfo> devices;
    if (rig_->camera_connected()) {
        devices.push_back(sky_camera_info());
        if (replay_available(rig_->config())) {
            devices.push_back(replay_camera_info());
        }
    }
    devices.push_back(sim_device_info(kMountId, DeviceKind::Mount, "Simulated pan-tilt mount"));
    devices.push_back(sim_device_info(kImuId, DeviceKind::Imu, "Simulated IMU on the camera head"));
    devices.push_back(sim_device_info(kGpsId, DeviceKind::Sensor, "Simulated GPS receiver"));
    devices.push_back(sim_device_info(kEnvironmentId, DeviceKind::Sensor, "Simulated environment sensors"));
    return devices;
}

Expected<std::shared_ptr<hal::IDevice>> SimDriver::create(std::string_view id)
{
    const SimulationConfig& config = rig_->config();
    if (id == kMountId) {
        return std::make_shared<SimMount>(rig_);
    }
    if (id == kImuId) {
        return std::make_shared<SimImu>(rig_);
    }
    if (id == kGpsId) {
        return std::make_shared<SimGps>(rig_);
    }
    if (id == kEnvironmentId) {
        return std::make_shared<SimEnvironment>(rig_);
    }
    if (id == kSkyCameraId && rig_->camera_connected()) {
        auto source = make_synthetic_sky_source({.seed = config.seed,
                                                 .cloud_fraction = config.camera.cloud_fraction,
                                                 .cloud_drift = config.camera.cloud_drift,
                                                 .sun_visible = config.camera.sun_visible,
                                                 .sun_x = config.camera.sun_x,
                                                 .sun_y = config.camera.sun_y});
        return std::make_shared<SimCamera>(rig_, sky_camera_info(), std::move(source), config.camera.real_time);
    }
    if (id == kReplayCameraId && rig_->camera_connected() && !config.camera.replay_folder.empty()) {
        auto source = make_replay_source(config.camera.replay_folder, config.camera.replay_fps);
        if (!source) {
            return fail(source.error());
        }
        return std::make_shared<SimCamera>(rig_, replay_camera_info(), std::move(*source), config.camera.real_time);
    }
    return fail(ErrorCode::NotFound, fmt::format("driver 'sim' has no device '{}'", id));
}

Expected<SimulationSettings> simulation_settings(const nlohmann::json& effective)
{
    if (!effective.is_object() || !effective.contains("simulation")) {
        return fail(ErrorCode::InvalidArgument, "the configuration has no [simulation] section");
    }
    const nlohmann::json& section = effective.at("simulation");
    SimulationSettings settings;
    settings.enabled = section.at("enabled").get<bool>();
    settings.config.seed = section.at("seed").get<std::uint64_t>();
    const std::string folder = section.at("replay_folder").get<std::string>();
    settings.config.camera.replay_folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    settings.config.camera.replay_fps = section.at("replay_fps").get<double>();
    if (auto usable = check(settings.config); !usable) {
        return fail(usable.error());
    }
    return settings;
}

}  // namespace cloudscope::sim
