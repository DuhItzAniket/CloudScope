// What the desktop application is made of (P033): configuration, paths, the clock, the device registry and the
// event bus, plus the [desktop] section of config.toml as typed settings. One AppContext per process, owned by
// main() (or by a test); everything else borrows it. Lives on the UI thread; the registry, clock and bus are
// thread-safe on their own.
#pragma once

#include "overlay.hpp"
#include "theme.hpp"

#include <cloudscope/capture/recording.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/common/error.hpp>
#include <cloudscope/common/event_bus.hpp>
#include <cloudscope/hal/registry.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cloudscope::desktop {

struct DesktopSettings {
    Theme theme = Theme::Dark;
    std::string renderer = "auto";  // "auto", "opengl", "raster"
    std::filesystem::path session_root;
    std::string default_camera;
    std::string time_display = "both";         // "utc", "local", "both"
    std::string temperature_unit = "celsius";  // "celsius", "fahrenheit"
    std::filesystem::path calibration_file;    // empty: no intrinsic calibration for the overlays
    SiteInfo site;
    SkyPose pointing;
    std::map<std::string, std::string> shortcuts;
};

// The [desktop] section of an effective configuration. An empty session_root becomes default_session_root().
[[nodiscard]] Expected<DesktopSettings> desktop_settings(const nlohmann::json& effective);
// <Pictures>/CloudScope, or <home>/CloudScope when the platform has no Pictures folder.
[[nodiscard]] std::filesystem::path default_session_root();

class AppContext {
public:
    // Loads the configuration (standard files, then `extra_config_files`, then `overrides`) and sets up the
    // device registry from it. Errors name the file and key concerned.
    [[nodiscard]] static Expected<std::unique_ptr<AppContext>>
    create(const std::vector<std::filesystem::path>& extra_config_files = {},
           const nlohmann::json& overrides = nlohmann::json::object());
    // The same with explicit paths (tests redirect the configuration folders).
    [[nodiscard]] static Expected<std::unique_ptr<AppContext>>
    create(AppPaths paths, const std::vector<std::filesystem::path>& extra_config_files,
           const nlohmann::json& overrides);

    [[nodiscard]] const AppPaths& paths() const { return paths_; }
    [[nodiscard]] const LoadedConfig& config() const { return config_; }
    [[nodiscard]] const DesktopSettings& settings() const { return settings_; }
    [[nodiscard]] const SystemClock& clock() const { return clock_; }
    [[nodiscard]] hal::DeviceRegistry& registry() { return registry_; }
    [[nodiscard]] EventBus& bus() { return bus_; }

    // Files next to the user's config.toml.
    [[nodiscard]] std::filesystem::path state_file() const;     // window layout and perspectives (INI)
    [[nodiscard]] std::filesystem::path profiles_file() const;  // camera profiles (JSON)

    // Merges `patch` (a partial document, for example {"desktop": {"theme": "night"}}) into the user's config.toml.
    // The result is validated against the schema together with every other layer before anything is written;
    // on failure nothing changes and the error names the key. On success the context reloads its configuration
    // and returns the loader's notes.
    [[nodiscard]] Expected<std::vector<std::string>> update_user_config(const nlohmann::json& patch);
    // Re-reads the configuration files (the registry is not rebuilt: devices need a restart).
    [[nodiscard]] Expected<void> reload();

private:
    AppContext(AppPaths paths, std::vector<std::filesystem::path> extra_files, nlohmann::json overrides);

    AppPaths paths_;
    std::vector<std::filesystem::path> extra_files_;
    nlohmann::json overrides_;
    LoadedConfig config_;
    DesktopSettings settings_;
    SystemClock clock_;
    hal::DeviceRegistry registry_;
    EventBus bus_;
};

}  // namespace cloudscope::desktop
