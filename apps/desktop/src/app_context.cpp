#include "app_context.hpp"

#include <cloudscope/app/devices.hpp>
#include <cloudscope/common/config.hpp>

#include <QtCore/QStandardPaths>
#include <QtCore/QString>

#include <system_error>

namespace cloudscope::desktop {

namespace {

std::filesystem::path path_from_utf8(const std::string& text)
{
    return std::filesystem::path(QString::fromStdString(text).toStdU16String());
}

std::string utf8_of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

std::filesystem::path default_session_root()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    return std::filesystem::path(base.toStdU16String()) / "CloudScope";
}

Expected<DesktopSettings> desktop_settings(const nlohmann::json& effective)
{
    DesktopSettings settings;
    try {
        const nlohmann::json& d = effective.at("desktop");
        auto theme = theme_from_string(d.at("theme").get<std::string>());
        if (!theme) {
            return fail(theme.error().with_context("desktop.theme"));
        }
        settings.theme = *theme;
        settings.renderer = d.at("renderer").get<std::string>();
        const std::string root = d.at("session_root").get<std::string>();
        settings.session_root = root.empty() ? default_session_root() : path_from_utf8(root);
        settings.default_camera = d.at("default_camera").get<std::string>();
        settings.time_display = d.at("time_display").get<std::string>();
        settings.temperature_unit = d.at("temperature_unit").get<std::string>();
        const std::string calibration = d.at("calibration_file").get<std::string>();
        settings.calibration_file = calibration.empty() ? std::filesystem::path() : path_from_utf8(calibration);
        const nlohmann::json& site = d.at("site");
        settings.site = {.id = site.at("id").get<std::string>(),
                         .latitude_deg = site.at("latitude_deg").get<double>(),
                         .longitude_deg = site.at("longitude_deg").get<double>(),
                         .altitude_m = site.at("altitude_m").get<double>()};
        const nlohmann::json& pointing = d.at("pointing");
        settings.pointing = {.azimuth_deg = pointing.at("azimuth_deg").get<double>(),
                             .elevation_deg = pointing.at("elevation_deg").get<double>(),
                             .roll_deg = pointing.at("roll_deg").get<double>()};
        for (const auto& [id, key] : d.at("shortcuts").items()) {
            settings.shortcuts[id] = key.get<std::string>();
        }
    } catch (const nlohmann::json::exception& error) {
        // The schema guarantees the keys; reaching this means the schema and this reader disagree.
        return fail(ErrorCode::Internal, std::string("desktop settings: ") + error.what());
    }
    return settings;
}

AppContext::AppContext(AppPaths paths, std::vector<std::filesystem::path> extra_files, nlohmann::json overrides)
    : paths_(std::move(paths)), extra_files_(std::move(extra_files)), overrides_(std::move(overrides))
{
}

Expected<std::unique_ptr<AppContext>> AppContext::create(const std::vector<std::filesystem::path>& extra_config_files,
                                                         const nlohmann::json& overrides)
{
    return create(standard_paths(), extra_config_files, overrides);
}

Expected<std::unique_ptr<AppContext>> AppContext::create(AppPaths paths,
                                                         const std::vector<std::filesystem::path>& extra_config_files,
                                                         const nlohmann::json& overrides)
{
    std::unique_ptr<AppContext> context(new AppContext(std::move(paths), extra_config_files, overrides));
    if (auto loaded = context->reload(); !loaded) {
        return fail(loaded.error());
    }
    if (auto drivers = add_configured_drivers(context->registry_, context->config_.effective, context->clock_);
        !drivers) {
        return fail(drivers.error());
    }
    return context;
}

std::filesystem::path AppContext::state_file() const
{
    return paths_.user_config.parent_path() / "desktop-state.ini";
}

std::filesystem::path AppContext::profiles_file() const
{
    return paths_.user_config.parent_path() / "camera-profiles.json";
}

Expected<void> AppContext::reload()
{
    auto loaded = load_app_config(paths_, extra_files_, overrides_);
    if (!loaded) {
        return fail(loaded.error());
    }
    auto settings = desktop_settings(loaded->effective);
    if (!settings) {
        return fail(settings.error());
    }
    config_ = std::move(*loaded);
    settings_ = std::move(*settings);
    return {};
}

Expected<std::vector<std::string>> AppContext::update_user_config(const nlohmann::json& patch)
{
    nlohmann::json document = nlohmann::json::object();
    std::error_code error;
    if (std::filesystem::exists(paths_.user_config, error)) {
        auto existing = read_toml_file(paths_.user_config);
        if (!existing) {
            return fail(existing.error());
        }
        document = std::move(*existing);
    }
    merge_layer(document, patch);
    document["schema_version"] = app_config_format().current_version();
    auto text = to_toml(document);
    if (!text) {
        return fail(text.error());
    }
    // Validate the candidate in the place of the user file, with every other layer, before it replaces anything.
    const std::filesystem::path candidate = paths_.user_config.parent_path() / "config.toml.candidate";
    std::filesystem::create_directories(paths_.user_config.parent_path(), error);
    if (auto written = write_file_atomically(candidate, *text); !written) {
        return fail(written.error());
    }
    AppPaths trial = paths_;
    trial.user_config = candidate;
    const auto checked = load_app_config(trial, extra_files_, overrides_);
    std::filesystem::remove(candidate, error);
    if (!checked) {
        Error problem = checked.error();
        // The message names the candidate file; the user knows it as config.toml.
        const std::string candidate_name = utf8_of(candidate);
        const std::string user_name = utf8_of(paths_.user_config);
        if (const std::size_t at = problem.message.find(candidate_name); at != std::string::npos) {
            problem.message.replace(at, candidate_name.size(), user_name);
        }
        return fail(problem);
    }
    if (auto desktop = desktop_settings(checked->effective); !desktop) {
        return fail(desktop.error());
    }
    if (auto written = write_file_atomically(paths_.user_config, *text); !written) {
        return fail(written.error());
    }
    if (auto reloaded = reload(); !reloaded) {
        return fail(reloaded.error());
    }
    return config_.notes;
}

}  // namespace cloudscope::desktop
