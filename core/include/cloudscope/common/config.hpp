// Configuration files (FR-PLT-03, ADR-007): human-readable TOML, validated against a schema, layered,
// and migrated automatically when the format changes.
//
// Layers, lowest priority first: built-in defaults <- system file <- user file <- overrides (session, command line).
// Each file may set only the keys it wants to change, but must state the format version it was written for:
//
//   schema_version = 1
//   [logging]
//   level = "debug"
//
// Unknown keys and invalid values are errors that name the file and the key. Nothing is silently ignored.
#pragma once

#include "cloudscope/common/error.hpp"
#include "cloudscope/common/json_schema.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope {

// TOML text <-> JSON document. Dates and times are not used in CloudScope configuration and are rejected;
// JSON null has no TOML form and is rejected by to_toml().
[[nodiscard]] Expected<nlohmann::json> parse_toml(std::string_view text, std::string_view source_name);
[[nodiscard]] Expected<nlohmann::json> read_toml_file(const std::filesystem::path& path);
[[nodiscard]] Expected<std::string> to_toml(const nlohmann::json& document);

// Replaces a file's content completely or not at all: writes a temporary file, flushes it to disk, renames it.
[[nodiscard]] Expected<void> write_file_atomically(const std::filesystem::path& path, std::string_view content);

// One step of a format change: rewrites a document from `from_version` to `from_version + 1`.
// The document may be partial (one layer), so a migration must check that a key exists before using it.
struct ConfigMigration {
    int from_version = 0;
    std::string description;  // shown to the user, e.g. "logging.verbose was replaced by logging.level"
    std::function<Expected<void>(nlohmann::json& document)> apply;
};

// A configuration format: current version, schema, complete default document, migrations from older versions.
class ConfigFormat {
public:
    // Fails (ErrorCode::Internal: these are programming errors) if the schema does not compile, the defaults are
    // not a complete valid document of the current version, or the migrations do not form a chain up to it.
    [[nodiscard]] static Expected<ConfigFormat> create(int current_version, nlohmann::json schema,
                                                       nlohmann::json defaults,
                                                       std::vector<ConfigMigration> migrations);

    [[nodiscard]] int current_version() const { return current_version_; }
    [[nodiscard]] int oldest_supported_version() const;
    [[nodiscard]] const JsonSchema& schema() const { return schema_; }
    [[nodiscard]] const nlohmann::json& defaults() const { return defaults_; }

    // Brings a document to the current version. Returns one line per migration applied (empty if up to date).
    [[nodiscard]] Expected<std::vector<std::string>> migrate(nlohmann::json& document) const;

private:
    ConfigFormat(int current_version, JsonSchema schema, nlohmann::json defaults,
                 std::vector<ConfigMigration> migrations);

    int current_version_;
    JsonSchema schema_;
    nlohmann::json defaults_;
    std::vector<ConfigMigration> migrations_;  // sorted by from_version
};

struct LoadedConfig {
    nlohmann::json effective;                  // complete, valid, current version
    std::vector<std::filesystem::path> files;  // the files that existed and were read, lowest priority first
    std::vector<std::string> notes;            // what the user should know: migrations, files left unchanged
};

// Reads and merges the layers. Files that do not exist are skipped. A file in an older format is migrated in
// memory and then rewritten in the current format; the original is kept next to it as "<name>.v<N>.bak".
// If the file cannot be rewritten (read-only system file), loading still succeeds and a note says so.
[[nodiscard]] Expected<LoadedConfig> load_config(const ConfigFormat& format,
                                                 const std::vector<std::filesystem::path>& files,
                                                 const nlohmann::json& overrides = nlohmann::json::object());

// Later layers win; tables are merged key by key, every other value (lists included) is replaced.
void merge_layer(nlohmann::json& base, const nlohmann::json& layer);

}  // namespace cloudscope
