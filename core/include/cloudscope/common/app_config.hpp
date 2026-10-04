// CloudScope's own configuration file, config.toml: its format, where it lives, and typed views of its sections.
// The schema and the defaults are compiled into the library from core/resources/.
#pragma once

#include "cloudscope/common/config.hpp"
#include "cloudscope/common/log.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace cloudscope {

// The format of config.toml: version, schema, defaults, migrations. Valid for the whole program.
[[nodiscard]] const ConfigFormat& app_config_format();

struct AppPaths {
    std::filesystem::path system_config;  // machine-wide settings; normally writable by an administrator only
    std::filesystem::path user_config;    // the current user's settings
    std::filesystem::path log_directory;  // where log files go unless the configuration names a folder
};

// Returns the value of an environment variable as UTF-8, or an empty string if it is not set.
using EnvironmentLookup = std::function<std::string(const std::string& name)>;

// Standard locations.
//   Windows: %PROGRAMDATA%\CloudScope\config.toml, %APPDATA%\CloudScope\config.toml, %LOCALAPPDATA%\CloudScope\logs
//   Linux:   /etc/cloudscope/config.toml, $XDG_CONFIG_HOME/cloudscope/config.toml (default ~/.config),
//            $XDG_STATE_HOME/cloudscope/logs (default ~/.local/state)
[[nodiscard]] AppPaths standard_paths();
[[nodiscard]] AppPaths standard_paths(const EnvironmentLookup& environment);

// Loads defaults <- system file <- user file <- extra files (in order) <- overrides.
[[nodiscard]] Expected<LoadedConfig> load_app_config(const AppPaths& paths,
                                                     const std::vector<std::filesystem::path>& extra_files = {},
                                                     const nlohmann::json& overrides = nlohmann::json::object());

// The [logging] section of an effective configuration. An empty `directory` becomes paths.log_directory.
[[nodiscard]] Expected<LoggingConfig> logging_config(const nlohmann::json& effective, const AppPaths& paths);

}  // namespace cloudscope
