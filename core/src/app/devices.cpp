#include "cloudscope/app/devices.hpp"

#include "cloudscope/sim/sim_driver.hpp"

namespace cloudscope {

Expected<void> add_configured_drivers(hal::DeviceRegistry& registry, const nlohmann::json& effective,
                                      const IClock& clock)
{
    const auto simulation = sim::simulation_settings(effective);
    if (!simulation) {
        return fail(simulation.error());
    }
    if (simulation->enabled) {
        auto driver = sim::SimDriver::make(clock, simulation->config);
        if (!driver) {
            return fail(driver.error());
        }
        if (auto added = registry.add_driver(*driver); !added) {
            return fail(added.error());
        }
    }
    return {};
}

}  // namespace cloudscope
