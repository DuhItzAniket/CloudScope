// The device drivers this build of CloudScope offers, added to a registry according to the configuration.
// Every driver phase adds its driver here; nothing else in the program names a concrete driver.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/hal/registry.hpp"

#include <nlohmann/json.hpp>

namespace cloudscope {

// Adds every driver that the effective configuration enables: the computer's USB cameras ([camera] uvc) and
// the simulators ([simulation]).
// The clock must outlive the registry and the devices made from it.
[[nodiscard]] Expected<void> add_configured_drivers(hal::DeviceRegistry& registry, const nlohmann::json& effective,
                                                    const IClock& clock);

}  // namespace cloudscope
