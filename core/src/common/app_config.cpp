#include "cloudscope/common/app_config.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QString>
#include <QtCore/QtEnvironmentVariables>
#include <fmt/format.h>

#include <stdexcept>
#include <string_view>

namespace cloudscope {

namespace {

constexpr int kConfigVersion = 1;

std::string read_resource(const char* name)
{
    QFile file(QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly)) {
        // The file is compiled into the library; not finding it means the build is broken.
        throw std::logic_error(fmt::format("embedded resource {} is missing", name));
    }
    return file.readAll().toStdString();
}

ConfigFormat make_app_config_format()
{
    const nlohmann::json schema = nlohmann::json::parse(read_resource(":/cloudscope/config.schema.json"));
    auto defaults = parse_toml(read_resource(":/cloudscope/config.defaults.toml"), "built-in defaults");
    if (!defaults) {
        throw std::logic_error(defaults.error().to_string());
    }
    // No migrations yet: version 1 is the first format. A format change adds a ConfigMigration here,
    // raises kConfigVersion, and updates the schema and the defaults.
    auto format = ConfigFormat::create(kConfigVersion, schema, std::move(*defaults), {});
    if (!format) {
        throw std::logic_error(format.error().to_string());
    }
    return std::move(*format);
}

std::filesystem::path path_from_utf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::filesystem::path home_directory()
{
    return std::filesystem::path(QDir::homePath().toStdU16String());
}

// The variable as a path, or `fallback` when it is unset.
std::filesystem::path from_environment(const EnvironmentLookup& environment, const std::string& name,
                                       const std::filesystem::path& fallback)
{
    const std::string value = environment(name);
    return value.empty() ? fallback : path_from_utf8(value);
}

}  // namespace

const ConfigFormat& app_config_format()
{
    static const ConfigFormat format = make_app_config_format();
    return format;
}

AppPaths standard_paths()
{
    return standard_paths([](const std::string& name) { return qEnvironmentVariable(name.c_str()).toStdString(); });
}

AppPaths standard_paths(const EnvironmentLookup& environment)
{
    AppPaths paths;
#ifdef _WIN32
    const std::filesystem::path home = from_environment(environment, "USERPROFILE", home_directory());
    const auto program_data = from_environment(environment, "PROGRAMDATA", "C:\\ProgramData");
    const auto roaming = from_environment(environment, "APPDATA", home / "AppData" / "Roaming");
    const auto local = from_environment(environment, "LOCALAPPDATA", home / "AppData" / "Local");
    paths.system_config = program_data / "CloudScope" / "config.toml";
    paths.user_config = roaming / "CloudScope" / "config.toml";
    paths.log_directory = local / "CloudScope" / "logs";
#else
    const std::filesystem::path home = from_environment(environment, "HOME", home_directory());
    const auto config_home = from_environment(environment, "XDG_CONFIG_HOME", home / ".config");
    const auto state_home = from_environment(environment, "XDG_STATE_HOME", home / ".local" / "state");
    paths.system_config = "/etc/cloudscope/config.toml";
    paths.user_config = config_home / "cloudscope" / "config.toml";
    paths.log_directory = state_home / "cloudscope" / "logs";
#endif
    return paths;
}

Expected<LoadedConfig> load_app_config(const AppPaths& paths, const std::vector<std::filesystem::path>& extra_files,
                                       const nlohmann::json& overrides)
{
    std::vector<std::filesystem::path> files = {paths.system_config, paths.user_config};
    for (const std::filesystem::path& file : extra_files) {
        std::error_code error;
        if (!std::filesystem::exists(file, error)) {
            // A file the user named explicitly must exist; the standard ones are optional.
            const std::u8string name = file.u8string();
            return fail(ErrorCode::NotFound,
                        fmt::format("{}: the file does not exist", std::string(name.begin(), name.end())));
        }
        files.push_back(file);
    }
    return load_config(app_config_format(), files, overrides);
}

Expected<LoggingConfig> logging_config(const nlohmann::json& effective, const AppPaths& paths)
{
    if (!effective.is_object() || !effective.contains("logging")) {
        return fail(ErrorCode::InvalidArgument, "the configuration has no [logging] section");
    }
    const nlohmann::json& section = effective.at("logging");
    auto level = log_level_from_string(section.at("level").get<std::string>());
    if (!level) {
        return fail(level.error().with_context("logging.level"));
    }
    LoggingConfig config;
    config.level = *level;
    config.console = section.at("console").get<bool>();
    config.file = section.at("file").get<bool>();
    const std::string directory = section.at("directory").get<std::string>();
    config.directory = directory.empty() ? paths.log_directory : path_from_utf8(directory);
    config.max_file_mb = section.at("max_file_mb").get<int>();
    config.max_files = section.at("max_files").get<int>();
    return config;
}

}  // namespace cloudscope
