#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/config.hpp>

#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <map>
#include <string>

using namespace cloudscope;
using cloudscope::test::read_file;
using cloudscope::test::TempWorkspace;
using nlohmann::json;
using Catch::Matchers::ContainsSubstring;

namespace {

// A small format with history: version 1 had mount.speed (percent), version 2 renamed it to
// mount.speed_percent, version 3 replaced it by mount.speed_deg_s (100 % = 60 deg/s).
ConfigFormat mount_format()
{
    const json schema = json::parse(R"({
      "type": "object", "additionalProperties": false, "required": ["schema_version", "mount", "site"],
      "properties": {
        "schema_version": {"const": 3},
        "site": {"type": "string", "minLength": 1},
        "mount": {"type": "object", "additionalProperties": false, "required": ["speed_deg_s", "axes"],
                  "properties": {"speed_deg_s": {"type": "number", "exclusiveMinimum": 0, "maximum": 60},
                                 "axes": {"type": "array", "items": {"type": "string"}}}}
      }
    })");
    const json defaults = json::parse(
        R"({"schema_version": 3, "site": "BLR01", "mount": {"speed_deg_s": 30.0, "axes": ["pan", "tilt"]}})");
    std::vector<ConfigMigration> migrations = {
        {2, "mount.speed_percent became mount.speed_deg_s",
         [](json& document) -> Expected<void> {
             if (document.contains("mount") && document["mount"].contains("speed_percent")) {
                 const json percent = document["mount"]["speed_percent"];
                 if (!percent.is_number()) {
                     return fail(ErrorCode::Validation, "mount.speed_percent must be a number");
                 }
                 document["mount"]["speed_deg_s"] = percent.get<double>() * 0.6;
                 document["mount"].erase("speed_percent");
             }
             return {};
         }},
        {1, "mount.speed was renamed to mount.speed_percent",
         [](json& document) -> Expected<void> {
             if (document.contains("mount") && document["mount"].contains("speed")) {
                 document["mount"]["speed_percent"] = document["mount"]["speed"];
                 document["mount"].erase("speed");
             }
             return {};
         }},
    };
    auto format = ConfigFormat::create(3, schema, defaults, std::move(migrations));
    REQUIRE(format.has_value());
    return *format;
}

}  // namespace

// ------------------------------------------------------------------------------------------- TOML

TEST_CASE("TOML documents convert to JSON with their types", "[common][config]")
{
    const auto document = parse_toml(R"(
title = "sky"          # a comment
count = 3
ratio = 0.5
enabled = true
list = [1, 2, 3]
[camera]
name = "B0268"
[camera.roi]
size = [640, 480]
[[targets]]
az = 10.0
[[targets]]
az = 20.0
)",
                                     "test.toml");
    REQUIRE(document.has_value());
    CHECK(*document == json::parse(R"({
      "title": "sky", "count": 3, "ratio": 0.5, "enabled": true, "list": [1, 2, 3],
      "camera": {"name": "B0268", "roi": {"size": [640, 480]}},
      "targets": [{"az": 10.0}, {"az": 20.0}]
    })"));
    CHECK(document->at("count").is_number_integer());
    CHECK(document->at("ratio").is_number_float());
}

TEST_CASE("TOML syntax errors name the file, line and column", "[common][config]")
{
    const auto document = parse_toml("a = 1\nb = = 2\n", "my config.toml");
    REQUIRE_FALSE(document.has_value());
    CHECK(document.error().code == ErrorCode::Parse);
    CHECK_THAT(document.error().message, ContainsSubstring("my config.toml: line 2, column "));
}

TEST_CASE("TOML values without a JSON form are rejected with their key", "[common][config]")
{
    const auto date = parse_toml("[session]\nstarted = 2026-10-04T12:00:00Z\n", "c.toml");
    REQUIRE_FALSE(date.has_value());
    CHECK(date.error().code == ErrorCode::Validation);
    CHECK_THAT(date.error().message, ContainsSubstring("c.toml: session.started: dates and times are not supported"));

    const auto infinite = parse_toml("gain = inf\n", "c.toml");
    REQUIRE_FALSE(infinite.has_value());
    CHECK_THAT(infinite.error().message, ContainsSubstring("gain: inf and nan are not allowed"));
}

TEST_CASE("JSON documents are written as TOML and read back unchanged", "[common][config]")
{
    const json original = json::parse(R"({
      "schema_version": 1, "name": "a \"quoted\" name", "ratio": 0.25, "big": 9007199254740993, "on": false,
      "list": ["x", "y"], "empty": [], "nested": {"deep": {"value": -7}}, "rows": [{"a": 1}, {"a": 2}]
    })");
    const auto text = to_toml(original);
    REQUIRE(text.has_value());
    const auto reread = parse_toml(*text, "written.toml");
    REQUIRE(reread.has_value());
    CHECK(*reread == original);
    CHECK(reread->at("big").get<std::int64_t>() == 9007199254740993LL);  // integers keep all their digits
}

TEST_CASE("values TOML cannot hold are refused when writing", "[common][config]")
{
    const auto with_null = to_toml(json::parse(R"({"camera": {"name": null}})"));
    REQUIRE_FALSE(with_null.has_value());
    CHECK_THAT(with_null.error().message, ContainsSubstring("camera.name: null cannot be written"));
    CHECK_FALSE(to_toml(json::array({1, 2})).has_value());
    CHECK_FALSE(to_toml(json{{"n", 18446744073709551615ULL}}).has_value());
}

// ------------------------------------------------------------------------------------------ files

TEST_CASE("files are written atomically, also into folders with non-ASCII names", "[common][config]")
{
    const TempWorkspace workspace;
    const std::filesystem::path file = workspace.path("sub/dir/settings.toml");

    REQUIRE(write_file_atomically(file, "a = 1\n").has_value());
    CHECK(read_file(file) == "a = 1\n");
    REQUIRE(write_file_atomically(file, "a = 2\n").has_value());  // replaces the content
    CHECK(read_file(file) == "a = 2\n");

    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(file.parent_path())) {
        CAPTURE(entry.path().filename().string());
        ++entries;
    }
    CHECK(entries == 1);  // no temporary file is left behind

    const auto document = read_toml_file(file);
    REQUIRE(document.has_value());
    CHECK(document->at("a") == 2);
}

TEST_CASE("reading a missing file is NotFound and writing below a regular file is an Io error", "[common][config]")
{
    const TempWorkspace workspace;
    const auto missing = read_toml_file(workspace.path("nothing.toml"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);

    const std::filesystem::path file = workspace.write("plain.txt", "x");
    const auto written = write_file_atomically(file / "child.toml", "a = 1\n");
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::Io);
}

// ----------------------------------------------------------------------------------------- format

TEST_CASE("a configuration format checks its own consistency", "[common][config]")
{
    const json schema = json::parse(R"({"type": "object", "required": ["schema_version", "n"],
        "properties": {"schema_version": {"const": 2}, "n": {"type": "integer"}}})");
    const json defaults = json::parse(R"({"schema_version": 2, "n": 1})");
    const ConfigMigration step1{1, "first", [](json&) -> Expected<void> { return {}; }};

    CHECK(ConfigFormat::create(2, schema, defaults, {step1}).has_value());
    CHECK(ConfigFormat::create(2, schema, defaults, {}).has_value());  // no history: only version 2 is readable

    const auto bad_defaults = ConfigFormat::create(2, schema, json::parse(R"({"schema_version": 2})"), {step1});
    REQUIRE_FALSE(bad_defaults.has_value());
    CHECK(bad_defaults.error().code == ErrorCode::Internal);
    CHECK_THAT(bad_defaults.error().message, ContainsSubstring("required key 'n' is missing"));

    CHECK_FALSE(ConfigFormat::create(2, schema, json::parse(R"({"schema_version": 1, "n": 1})"), {step1}).has_value());
    CHECK_FALSE(ConfigFormat::create(2, json::parse(R"({"anyOf": []})"), defaults, {step1}).has_value());
    CHECK_FALSE(ConfigFormat::create(0, schema, defaults, {}).has_value());

    const ConfigMigration gap{0, "gap", [](json&) -> Expected<void> { return {}; }};
    const ConfigMigration no_function{1, "nothing to call", nullptr};
    CHECK_FALSE(ConfigFormat::create(2, schema, defaults, {step1, step1}).has_value());   // same step twice
    CHECK_FALSE(ConfigFormat::create(2, schema, defaults, {gap}).has_value());            // does not reach version 2
    CHECK_FALSE(ConfigFormat::create(2, schema, defaults, {no_function}).has_value());
}

TEST_CASE("documents are migrated step by step to the current version", "[common][config]")
{
    const ConfigFormat format = mount_format();
    CHECK(format.current_version() == 3);
    CHECK(format.oldest_supported_version() == 1);

    json version1 = json::parse(R"({"schema_version": 1, "mount": {"speed": 50}})");
    const auto steps = format.migrate(version1);
    REQUIRE(steps.has_value());
    CHECK(*steps == std::vector<std::string>{"format 1 -> 2: mount.speed was renamed to mount.speed_percent",
                                             "format 2 -> 3: mount.speed_percent became mount.speed_deg_s"});
    CHECK(version1 == json::parse(R"({"schema_version": 3, "mount": {"speed_deg_s": 30.0}})"));

    json version2 = json::parse(R"({"schema_version": 2, "site": "X"})");  // partial: no mount table at all
    REQUIRE(format.migrate(version2).value().size() == 1);
    CHECK(version2 == json::parse(R"({"schema_version": 3, "site": "X"})"));

    json current = json::parse(R"({"schema_version": 3})");
    CHECK(format.migrate(current).value().empty());
}

TEST_CASE("documents that cannot be migrated are refused with a clear reason", "[common][config]")
{
    const ConfigFormat format = mount_format();

    json no_version = json::parse(R"({"site": "X"})");
    auto result = format.migrate(no_version);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::Validation);
    CHECK_THAT(result.error().message, ContainsSubstring("add the line 'schema_version = 3'"));

    json newer = json::parse(R"({"schema_version": 4})");
    result = format.migrate(newer);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::Unsupported);
    CHECK_THAT(result.error().message, ContainsSubstring("written by a newer CloudScope"));

    json ancient = json::parse(R"({"schema_version": 0})");
    CHECK(format.migrate(ancient).error().code == ErrorCode::Unsupported);

    json text_version = json::parse(R"({"schema_version": "3"})");
    CHECK(format.migrate(text_version).error().code == ErrorCode::Validation);

    json failing = json::parse(R"({"schema_version": 2, "mount": {"speed_percent": "fast"}})");
    result = format.migrate(failing);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message,
               ContainsSubstring("migration from format 2 to 3: mount.speed_percent must be a number"));
}

// ------------------------------------------------------------------------------------------- load

TEST_CASE("layers merge: defaults, then files in order, then overrides", "[common][config]")
{
    const TempWorkspace workspace;
    const ConfigFormat format = mount_format();
    const auto system = workspace.write("system.toml", "schema_version = 3\nsite = \"ROOF\"\n[mount]\nspeed_deg_s = 10.0\n");
    const auto user = workspace.write("user.toml", "schema_version = 3\n[mount]\nspeed_deg_s = 20.0\naxes = [\"pan\"]\n");
    const auto absent = workspace.path("absent.toml");

    const auto defaults_only = load_config(format, {absent});
    REQUIRE(defaults_only.has_value());
    CHECK(defaults_only->effective == format.defaults());
    CHECK(defaults_only->files.empty());

    const auto loaded = load_config(format, {system, absent, user}, json::parse(R"({"site": "CLI"})"));
    REQUIRE(loaded.has_value());
    CHECK(loaded->effective == json::parse(
        R"({"schema_version": 3, "site": "CLI", "mount": {"speed_deg_s": 20.0, "axes": ["pan"]}})"));  // lists are replaced
    CHECK(loaded->files == std::vector<std::filesystem::path>{system, user});
    CHECK(loaded->notes.empty());

    const auto without_user = load_config(format, {system});
    REQUIRE(without_user.has_value());
    CHECK(without_user->effective.at("site") == "ROOF");
    CHECK(without_user->effective.at("mount").at("axes") == json::array({"pan", "tilt"}));  // from the defaults
}

TEST_CASE("an invalid file is rejected with the file name and every problem", "[common][config]")
{
    const TempWorkspace workspace;
    const ConfigFormat format = mount_format();
    const auto file = workspace.write("user.toml",
                                      "schema_version = 3\nsit = \"X\"\n[mount]\nspeed_deg_s = 90.0\ncolour = 1\n");
    const auto loaded = load_config(format, {file});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::Validation);
    CHECK_THAT(loaded.error().message, ContainsSubstring("user.toml"));
    CHECK_THAT(loaded.error().message, ContainsSubstring("mount.speed_deg_s: must be at most 60; got 90.0"));
    CHECK_THAT(loaded.error().message, ContainsSubstring("mount.colour: unknown key"));
    CHECK_THAT(loaded.error().message, ContainsSubstring("sit: unknown key (did you mean 'site'?)"));

    const auto broken = workspace.write("broken.toml", "schema_version = 3\nsite = \n");
    CHECK(load_config(format, {broken}).error().code == ErrorCode::Parse);

    const auto unversioned = workspace.write("unversioned.toml", "site = \"X\"\n");
    const auto missing_version = load_config(format, {unversioned});
    REQUIRE_FALSE(missing_version.has_value());
    CHECK_THAT(missing_version.error().message, ContainsSubstring("unversioned.toml: schema_version is missing"));
}

TEST_CASE("invalid overrides are rejected", "[common][config]")
{
    const ConfigFormat format = mount_format();
    const auto bad_value = load_config(format, {}, json::parse(R"({"mount": {"speed_deg_s": 0}})"));
    REQUIRE_FALSE(bad_value.has_value());
    CHECK_THAT(bad_value.error().message, ContainsSubstring("configuration overrides:"));
    CHECK_THAT(bad_value.error().message, ContainsSubstring("mount.speed_deg_s: must be greater than 0"));
    CHECK(load_config(format, {}, json::array()).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("an old file is migrated, rewritten in the current format and backed up", "[common][config]")
{
    const TempWorkspace workspace;
    const ConfigFormat format = mount_format();
    const std::string original = "# my rooftop\nschema_version = 1\nsite = \"ROOF\"\n[mount]\nspeed = 50\n";
    const auto file = workspace.write("config.toml", original);

    const auto loaded = load_config(format, {file});
    REQUIRE(loaded.has_value());
    CHECK(loaded->effective.at("site") == "ROOF");
    CHECK(loaded->effective.at("mount").at("speed_deg_s") == 30.0);

    const auto backup = workspace.path("config.toml.v1.bak");
    REQUIRE(std::filesystem::exists(backup));
    CHECK(read_file(backup) == original);  // byte for byte, comments included

    const std::string rewritten = read_file(file);
    CHECK_THAT(rewritten, ContainsSubstring("format version 3"));
    CHECK_THAT(rewritten, ContainsSubstring("config.toml.v1.bak"));
    const auto reread = parse_toml(rewritten, "rewritten");
    REQUIRE(reread.has_value());
    CHECK(*reread == json::parse(R"({"schema_version": 3, "site": "ROOF", "mount": {"speed_deg_s": 30.0}})"));

    REQUIRE(loaded->notes.size() == 3);
    CHECK_THAT(loaded->notes[0], ContainsSubstring("format 1 -> 2"));
    CHECK_THAT(loaded->notes[1], ContainsSubstring("format 2 -> 3"));
    CHECK_THAT(loaded->notes[2], ContainsSubstring("rewritten in format 3"));

    // Loading again finds a current file: nothing to migrate, no second backup.
    const auto again = load_config(format, {file});
    REQUIRE(again.has_value());
    CHECK(again->notes.empty());
    CHECK(again->effective == loaded->effective);
    CHECK_FALSE(std::filesystem::exists(workspace.path("config.toml.v1.1.bak")));
}

TEST_CASE("an existing backup is never overwritten", "[common][config]")
{
    const TempWorkspace workspace;
    const ConfigFormat format = mount_format();
    const auto file = workspace.write("config.toml", "schema_version = 2\nsite = \"NEW\"\n");
    workspace.write("config.toml.v2.bak", "precious earlier backup");

    REQUIRE(load_config(format, {file}).has_value());
    CHECK(read_file(workspace.path("config.toml.v2.bak")) == "precious earlier backup");
    CHECK(read_file(workspace.path("config.toml.v2.1.bak")) == "schema_version = 2\nsite = \"NEW\"\n");
}

TEST_CASE("a file that fails validation after migration is left untouched", "[common][config]")
{
    const TempWorkspace workspace;
    const ConfigFormat format = mount_format();
    const std::string original = "schema_version = 1\n[mount]\nspeed = 500\n";  // 500 % -> 300 deg/s: over the limit
    const auto file = workspace.write("config.toml", original);

    const auto loaded = load_config(format, {file});
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("mount.speed_deg_s: must be at most 60"));
    CHECK(read_file(file) == original);
    CHECK_FALSE(std::filesystem::exists(workspace.path("config.toml.v1.bak")));
}

// ------------------------------------------------------------------------------- CloudScope config

TEST_CASE("the built-in CloudScope configuration is complete and valid", "[common][app_config]")
{
    const ConfigFormat& format = app_config_format();
    CHECK(format.current_version() == 1);
    CHECK(format.schema().validate(format.defaults()).empty());
    CHECK(format.defaults().at("logging").at("level") == "info");
    CHECK(&app_config_format() == &format);
}

TEST_CASE("standard paths follow the platform conventions", "[common][app_config]")
{
    const std::map<std::string, std::string> variables = {
#ifdef _WIN32
        {"PROGRAMDATA", "D:\\ProgramData"},
        {"APPDATA", "C:\\Users\\Asha\\AppData\\Roaming"},
        {"LOCALAPPDATA", "C:\\Users\\Asha\\AppData\\Local"},
#else
        {"HOME", "/home/asha"},
        {"XDG_CONFIG_HOME", "/data/config"},
#endif
    };
    const AppPaths paths = standard_paths([&variables](const std::string& name) {
        const auto found = variables.find(name);
        return found == variables.end() ? std::string() : found->second;
    });
#ifdef _WIN32
    CHECK(paths.system_config == std::filesystem::path("D:\\ProgramData\\CloudScope\\config.toml"));
    CHECK(paths.user_config == std::filesystem::path("C:\\Users\\Asha\\AppData\\Roaming\\CloudScope\\config.toml"));
    CHECK(paths.log_directory == std::filesystem::path("C:\\Users\\Asha\\AppData\\Local\\CloudScope\\logs"));
#else
    CHECK(paths.system_config == std::filesystem::path("/etc/cloudscope/config.toml"));
    CHECK(paths.user_config == std::filesystem::path("/data/config/cloudscope/config.toml"));
    CHECK(paths.log_directory == std::filesystem::path("/home/asha/.local/state/cloudscope/logs"));  // XDG default
#endif

    const AppPaths real = standard_paths();
    CHECK(real.user_config.is_absolute());
    CHECK(real.user_config.filename() == "config.toml");
    CHECK(real.log_directory.is_absolute());
}

TEST_CASE("the CloudScope configuration loads from user and extra files", "[common][app_config]")
{
    const TempWorkspace workspace;
    AppPaths paths;
    paths.system_config = workspace.path("etc/config.toml");  // does not exist: optional
    paths.user_config = workspace.write("user.toml", "schema_version = 1\n[logging]\nlevel = \"debug\"\nmax_files = 3\n");
    paths.log_directory = workspace.path("logs");
    const auto extra = workspace.write("extra.toml", "schema_version = 1\n[logging]\nlevel = \"warn\"\n");

    const auto loaded = load_app_config(paths, {extra}, json::parse(R"({"logging": {"console": false}})"));
    REQUIRE(loaded.has_value());
    const auto logging = logging_config(loaded->effective, paths);
    REQUIRE(logging.has_value());
    CHECK(logging->level == LogLevel::Warn);        // extra file wins over the user file
    CHECK(logging->max_files == 3);                 // from the user file
    CHECK(logging->max_file_mb == 10);              // default
    CHECK_FALSE(logging->console);                  // override
    CHECK(logging->file);
    CHECK(logging->directory == paths.log_directory);  // empty in the file: the standard folder

    const auto named = load_app_config(paths, {}, json::parse(R"({"logging": {"directory": "/var/log/sky"}})"));
    REQUIRE(named.has_value());
    CHECK(logging_config(named->effective, paths).value().directory == std::filesystem::path("/var/log/sky"));

    const auto missing_extra = load_app_config(paths, {workspace.path("typo.toml")});
    REQUIRE_FALSE(missing_extra.has_value());
    CHECK(missing_extra.error().code == ErrorCode::NotFound);

    workspace.write("user.toml", "schema_version = 1\n[logging]\nlevel = \"loud\"\n");
    const auto invalid = load_app_config(paths);
    REQUIRE_FALSE(invalid.has_value());
    CHECK_THAT(invalid.error().message, ContainsSubstring("logging.level: must be one of \"trace\""));
}
