// Shared set-up of the user-interface tests: an application context whose configuration folders, session
// folder and state file lie in a temporary workspace (never the developer's own), with the USB cameras switched
// off and the simulators on. The test settings go into the workspace's *system* configuration file, so that
// what a test writes to the user file wins over them, as it does for a real user.
#pragma once

#include "app_context.hpp"

#include "test_support.hpp"

#include <cloudscope/common/config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace cloudscope::desktop::test {

inline std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// The configuration a test reads back, as the loader sees the file (empty if there is no file).
inline nlohmann::json read_config_file(const std::filesystem::path& file)
{
    if (!std::filesystem::exists(file)) {
        return nlohmann::json::object();
    }
    auto document = read_toml_file(file);
    if (!document) {
        FAIL("config file: " << document.error().to_string());
    }
    return *document;
}

struct DesktopFixture {
    cloudscope::test::TempWorkspace workspace;
    AppPaths paths;
    std::unique_ptr<AppContext> context;

    explicit DesktopFixture(const nlohmann::json& extra_overrides = nlohmann::json::object())
    {
        paths = AppPaths{.system_config = workspace.path("system/config.toml"),
                         .user_config = workspace.path("user/config.toml"),
                         .log_directory = workspace.path("logs")};
        const nlohmann::json system{
            {"schema_version", 1},
            {"camera", {{"uvc", false}}},
            {"logging", {{"console", false}, {"file", false}}},
            {"desktop",
             {{"session_root", utf8(workspace.path("sessions"))},
              {"site",
               {{"id", "test-site"}, {"latitude_deg", 12.97}, {"longitude_deg", 77.59}, {"altitude_m", 920.0}}}}}};
        auto text = to_toml(system);
        if (!text) {
            FAIL("system config: " << text.error().to_string());
        }
        std::filesystem::create_directories(paths.system_config.parent_path());
        if (auto written = write_file_atomically(paths.system_config, *text); !written) {
            FAIL("system config: " << written.error().to_string());
        }
        auto created = AppContext::create(paths, {}, extra_overrides);
        if (!created) {
            FAIL("context: " << created.error().to_string());
        }
        context = std::move(*created);
    }
};

}  // namespace cloudscope::desktop::test
