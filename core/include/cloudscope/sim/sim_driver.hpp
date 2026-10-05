// The "sim" driver: offers the simulated devices to the device registry (FR-CTL-02).
//
//   auto driver = sim::SimDriver::make(clock, config);            // Expected<std::shared_ptr<SimDriver>>
//   registry.add_driver(*driver);
//   auto camera = registry.create<hal::ICamera>("sim:camera:sky");
//   (*driver)->rig()->set_camera_connected(false);                // what a test can make happen
//
// All devices of one driver are views of one SimRig: the IMU turns when the mount moves.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/hal/registry.hpp"
#include "cloudscope/sim/sim_rig.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace cloudscope::sim {

class SimDriver final : public hal::IDriver {
public:
    // InvalidArgument if the configuration does not pass check(). The clock must outlive the driver and
    // every device it hands out.
    [[nodiscard]] static Expected<std::shared_ptr<SimDriver>> make(const IClock& clock, const SimulationConfig& config);

    [[nodiscard]] std::string_view name() const override { return "sim"; }
    // A camera whose cable is pulled is not listed; the replay camera only if its folder holds pictures.
    [[nodiscard]] std::vector<hal::DeviceInfo> enumerate() override;
    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view id) override;

    // The simulated world behind the devices.
    [[nodiscard]] const std::shared_ptr<SimRig>& rig() const { return rig_; }

private:
    explicit SimDriver(std::shared_ptr<SimRig> rig);

    std::shared_ptr<SimRig> rig_;
};

// The [simulation] section of an effective configuration (config.toml).
struct SimulationSettings {
    bool enabled = true;
    SimulationConfig config;
};
[[nodiscard]] Expected<SimulationSettings> simulation_settings(const nlohmann::json& effective);

}  // namespace cloudscope::sim
