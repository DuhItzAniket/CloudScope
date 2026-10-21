#include "cloudscope/app/devices.hpp"

#include "cloudscope/ai/onnx_engine.hpp"
#include "cloudscope/ai/settings.hpp"

#include "cloudscope/sim/sim_driver.hpp"
#include "cloudscope/uvc/uvc_driver.hpp"

namespace cloudscope {

Expected<void> add_configured_drivers(hal::DeviceRegistry& registry, const nlohmann::json& effective,
                                      const IClock& clock)
{
    const auto camera = uvc::camera_settings(effective);
    if (!camera) {
        return fail(camera.error());
    }
    if (camera->uvc) {
        if (auto added = registry.add_driver(uvc::UvcDriver::make(clock)); !added) {
            return fail(added.error());
        }
    }
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
    const auto ai = ai::ai_settings(effective);
    if (!ai) {
        return fail(ai.error());
    }
    if (ai->enabled && ai::onnx_runtime_available()) {
        if (auto added = registry.add_driver(ai::OnnxDriver::make(ai->threads)); !added) {
            return fail(added.error());
        }
    }
    return {};
}

}  // namespace cloudscope
