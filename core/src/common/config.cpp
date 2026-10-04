#include "cloudscope/common/config.hpp"

#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QString>
#include <fmt/format.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <system_error>
#include <variant>

namespace cloudscope {

namespace {

using nlohmann::json;

constexpr qint64 kMaxConfigBytes = 1024 * 1024;
constexpr const char* kVersionKey = "schema_version";

QString to_qstring(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

std::string display(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::string child_path(const std::string& path, std::string_view key)
{
    return path.empty() ? std::string(key) : path + "." + std::string(key);
}

// ---------------------------------------------------------------------------------- TOML -> JSON

Expected<json> node_to_json(const toml::node& node, const std::string& path)
{
    if (const toml::table* table = node.as_table()) {
        json object = json::object();
        for (const auto& [key, value] : *table) {
            auto child = node_to_json(value, child_path(path, key.str()));
            if (!child) {
                return child;
            }
            object[std::string(key.str())] = std::move(*child);
        }
        return object;
    }
    if (const toml::array* array = node.as_array()) {
        json list = json::array();
        std::size_t index = 0;
        for (const toml::node& item : *array) {
            auto child = node_to_json(item, fmt::format("{}[{}]", path, index++));
            if (!child) {
                return child;
            }
            list.push_back(std::move(*child));
        }
        return list;
    }
    if (const auto* text = node.as_string()) {
        return json(text->get());
    }
    if (const auto* integer = node.as_integer()) {
        return json(integer->get());
    }
    if (const auto* boolean = node.as_boolean()) {
        return json(boolean->get());
    }
    if (const auto* number = node.as_floating_point()) {
        if (!std::isfinite(number->get())) {
            return fail(ErrorCode::Validation, fmt::format("{}: inf and nan are not allowed", path));
        }
        return json(number->get());
    }
    return fail(ErrorCode::Validation,
                fmt::format("{}: dates and times are not supported here; write the value as a string", path));
}

// ---------------------------------------------------------------------------------- JSON -> TOML

using Scalar = std::variant<std::string, bool, std::int64_t, double>;

Expected<Scalar> to_scalar(const json& value, const std::string& path)
{
    switch (value.type()) {
    case json::value_t::string:
        return Scalar(value.get<std::string>());
    case json::value_t::boolean:
        return Scalar(value.get<bool>());
    case json::value_t::number_integer:
        return Scalar(value.get<std::int64_t>());
    case json::value_t::number_unsigned:
        if (value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return fail(ErrorCode::InvalidArgument, fmt::format("{}: the integer is too large for TOML", path));
        }
        return Scalar(static_cast<std::int64_t>(value.get<std::uint64_t>()));
    case json::value_t::number_float:
        return Scalar(value.get<double>());
    default:
        return fail(ErrorCode::InvalidArgument, fmt::format("{}: null cannot be written to TOML", path));
    }
}

Expected<toml::table> make_table(const json& object, const std::string& path);

Expected<toml::array> make_array(const json& list, const std::string& path)
{
    toml::array array;
    std::size_t index = 0;
    for (const json& item : list) {
        const std::string here = fmt::format("{}[{}]", path, index++);
        if (item.is_object()) {
            auto child = make_table(item, here);
            if (!child) {
                return fail(child.error());
            }
            array.push_back(std::move(*child));
        } else if (item.is_array()) {
            auto child = make_array(item, here);
            if (!child) {
                return child;
            }
            array.push_back(std::move(*child));
        } else {
            auto scalar = to_scalar(item, here);
            if (!scalar) {
                return fail(scalar.error());
            }
            std::visit([&array](const auto& content) { array.push_back(content); }, *scalar);
        }
    }
    return array;
}

Expected<toml::table> make_table(const json& object, const std::string& path)
{
    toml::table table;
    for (const auto& [key, value] : object.items()) {
        const std::string here = child_path(path, key);
        if (value.is_object()) {
            auto child = make_table(value, here);
            if (!child) {
                return child;
            }
            table.insert_or_assign(key, std::move(*child));
        } else if (value.is_array()) {
            auto child = make_array(value, here);
            if (!child) {
                return fail(child.error());
            }
            table.insert_or_assign(key, std::move(*child));
        } else {
            auto scalar = to_scalar(value, here);
            if (!scalar) {
                return fail(scalar.error());
            }
            const std::string& name = key;
            std::visit([&table, &name](const auto& content) { table.insert_or_assign(name, content); }, *scalar);
        }
    }
    return table;
}

std::string join_issues(const std::vector<SchemaIssue>& issues)
{
    std::string text;
    for (const SchemaIssue& issue : issues) {
        text += (text.empty() ? "" : "\n  ") + issue.to_string();
    }
    return text;
}

// Copies `path` to "<path>.v<N>.bak" without ever overwriting an existing backup.
Expected<std::filesystem::path> back_up(const std::filesystem::path& path, int version)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::filesystem::path backup = path;
        backup += attempt == 0 ? fmt::format(".v{}.bak", version) : fmt::format(".v{}.{}.bak", version, attempt);
        std::error_code error;
        if (std::filesystem::exists(backup, error)) {
            continue;
        }
        std::filesystem::copy_file(path, backup, error);
        if (error) {
            return fail(ErrorCode::Io, fmt::format("cannot create backup {}: {}", display(backup), error.message()));
        }
        return backup;
    }
    return fail(ErrorCode::Io, fmt::format("too many backups of {}", display(path)));
}

}  // namespace

Expected<nlohmann::json> parse_toml(std::string_view text, std::string_view source_name)
{
    try {
        const toml::table table = toml::parse(text, source_name);
        return node_to_json(table, "").transform_error(
            [source_name](const Error& error) { return error.with_context(source_name); });
    } catch (const toml::parse_error& error) {
        return fail(ErrorCode::Parse, fmt::format("{}: line {}, column {}: {}", source_name, error.source().begin.line,
                                                  error.source().begin.column, error.description()));
    }
}

Expected<nlohmann::json> read_toml_file(const std::filesystem::path& path)
{
    QFile file(to_qstring(path));
    if (!file.exists()) {
        return fail(ErrorCode::NotFound, fmt::format("{}: the file does not exist", display(path)));
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::Io, fmt::format("{}: {}", display(path), file.errorString().toStdString()));
    }
    if (file.size() > kMaxConfigBytes) {
        return fail(ErrorCode::Validation, fmt::format("{}: larger than 1 MB; this is not a configuration file",
                                                       display(path)));
    }
    const QByteArray bytes = file.readAll();
    return parse_toml(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), display(path));
}

Expected<std::string> to_toml(const nlohmann::json& document)
{
    if (!document.is_object()) {
        return fail(ErrorCode::InvalidArgument, "only a table can be written as a TOML document");
    }
    auto table = make_table(document, "");
    if (!table) {
        return fail(table.error());
    }
    std::ostringstream out;
    out << *table << "\n";
    return out.str();
}

Expected<void> write_file_atomically(const std::filesystem::path& path, std::string_view content)
{
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return fail(ErrorCode::Io, fmt::format("cannot create folder {}: {}", display(path.parent_path()),
                                                   error.message()));
        }
    }
    QSaveFile file(to_qstring(path));
    const auto size = static_cast<qint64>(content.size());
    if (!file.open(QIODevice::WriteOnly) || file.write(content.data(), size) != size || !file.commit()) {
        return fail(ErrorCode::Io, fmt::format("cannot write {}: {}", display(path), file.errorString().toStdString()));
    }
    return {};
}

void merge_layer(nlohmann::json& base, const nlohmann::json& layer)
{
    if (!base.is_object() || !layer.is_object()) {
        base = layer;
        return;
    }
    for (const auto& [key, value] : layer.items()) {
        if (value.is_object() && base.contains(key) && base.at(key).is_object()) {
            merge_layer(base.at(key), value);
        } else {
            base[key] = value;
        }
    }
}

// ------------------------------------------------------------------------------------ ConfigFormat

ConfigFormat::ConfigFormat(int current_version, JsonSchema schema, nlohmann::json defaults,
                           std::vector<ConfigMigration> migrations)
    : current_version_(current_version),
      schema_(std::move(schema)),
      defaults_(std::move(defaults)),
      migrations_(std::move(migrations))
{
}

Expected<ConfigFormat> ConfigFormat::create(int current_version, nlohmann::json schema, nlohmann::json defaults,
                                            std::vector<ConfigMigration> migrations)
{
    if (current_version < 1) {
        return fail(ErrorCode::Internal, "configuration format: the version must be 1 or higher");
    }
    auto compiled = JsonSchema::compile(std::move(schema));
    if (!compiled) {
        return fail(Error{ErrorCode::Internal, compiled.error().message}.with_context("configuration format"));
    }
    if (!defaults.is_object() || !defaults.contains(kVersionKey) || defaults.at(kVersionKey) != current_version) {
        return fail(ErrorCode::Internal,
                    fmt::format("configuration format: the defaults must contain {} = {}", kVersionKey,
                                current_version));
    }
    if (const auto issues = compiled->validate(defaults); !issues.empty()) {
        return fail(ErrorCode::Internal,
                    fmt::format("configuration format: the defaults are not valid:\n  {}", join_issues(issues)));
    }
    std::sort(migrations.begin(), migrations.end(),
              [](const ConfigMigration& a, const ConfigMigration& b) { return a.from_version < b.from_version; });
    for (std::size_t i = 0; i < migrations.size(); ++i) {
        const int expected_version = current_version - static_cast<int>(migrations.size() - i);
        if (migrations[i].from_version != expected_version || expected_version < 1 || !migrations[i].apply) {
            return fail(ErrorCode::Internal,
                        fmt::format("configuration format: migrations must form an unbroken chain up to version {}",
                                    current_version));
        }
    }
    return ConfigFormat(current_version, std::move(*compiled), std::move(defaults), std::move(migrations));
}

int ConfigFormat::oldest_supported_version() const
{
    return current_version_ - static_cast<int>(migrations_.size());
}

Expected<std::vector<std::string>> ConfigFormat::migrate(nlohmann::json& document) const
{
    if (!document.is_object() || !document.contains(kVersionKey)) {
        return fail(ErrorCode::Validation,
                    fmt::format("{0} is missing; add the line '{0} = {1}' at the top of the file", kVersionKey,
                                current_version_));
    }
    if (!document.at(kVersionKey).is_number_integer()) {
        return fail(ErrorCode::Validation, fmt::format("{} must be an integer", kVersionKey));
    }
    const auto version = document.at(kVersionKey).get<long long>();
    if (version > current_version_) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("format version {} was written by a newer CloudScope; this version reads up to {}",
                                version, current_version_));
    }
    if (version < oldest_supported_version()) {
        return fail(ErrorCode::Unsupported, fmt::format("format version {} is too old; the oldest supported is {}",
                                                        version, oldest_supported_version()));
    }
    std::vector<std::string> steps;
    for (const ConfigMigration& migration : migrations_) {
        if (migration.from_version < version) {
            continue;
        }
        if (auto applied = migration.apply(document); !applied) {
            return fail(applied.error().with_context(
                fmt::format("migration from format {} to {}", migration.from_version, migration.from_version + 1)));
        }
        document[kVersionKey] = migration.from_version + 1;
        steps.push_back(fmt::format("format {} -> {}: {}", migration.from_version, migration.from_version + 1,
                                    migration.description));
    }
    return steps;
}

// ------------------------------------------------------------------------------------- load_config

Expected<LoadedConfig> load_config(const ConfigFormat& format, const std::vector<std::filesystem::path>& files,
                                   const nlohmann::json& overrides)
{
    LoadedConfig loaded;
    loaded.effective = format.defaults();

    for (const std::filesystem::path& path : files) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            continue;
        }
        const std::string name = display(path);
        auto document = read_toml_file(path);
        if (!document) {
            return fail(document.error());
        }
        const json original_version = document->value(kVersionKey, json());
        auto steps = format.migrate(*document);
        if (!steps) {
            return fail(steps.error().with_context(name));
        }
        if (const auto issues = format.schema().validate(*document, {.check_required = false}); !issues.empty()) {
            return fail(ErrorCode::Validation, fmt::format("{}:\n  {}", name, join_issues(issues)));
        }

        if (!steps->empty()) {
            for (const std::string& step : *steps) {
                loaded.notes.push_back(fmt::format("{}: migrated, {}", name, step));
            }
            const int old_version = original_version.get<int>();
            auto backup = back_up(path, old_version);
            auto text = to_toml(*document);
            Expected<void> written = fail(ErrorCode::Io, "not attempted");
            if (backup && text) {
                const std::string header = fmt::format(
                    "# CloudScope configuration, format version {}.\n"
                    "# Migrated automatically from version {}; comments were not carried over.\n"
                    "# The previous file is kept as {}.\n\n",
                    format.current_version(), old_version, display(backup->filename()));
                written = write_file_atomically(path, header + *text);
            }
            if (backup && written) {
                loaded.notes.push_back(
                    fmt::format("{}: rewritten in format {}; the previous file is {}", name, format.current_version(),
                                display(*backup)));
            } else {
                const Error& reason = !backup ? backup.error() : (!text ? text.error() : written.error());
                loaded.notes.push_back(fmt::format(
                    "{}: left unchanged on disk ({}); it is migrated in memory at every start", name, reason.message));
            }
        }
        merge_layer(loaded.effective, *document);
        loaded.files.push_back(path);
    }

    if (!overrides.is_object()) {
        return fail(ErrorCode::InvalidArgument, "configuration overrides must be a table");
    }
    if (const auto issues = format.schema().validate(overrides, {.check_required = false}); !issues.empty()) {
        return fail(ErrorCode::Validation, fmt::format("configuration overrides:\n  {}", join_issues(issues)));
    }
    merge_layer(loaded.effective, overrides);

    if (const auto issues = format.schema().validate(loaded.effective); !issues.empty()) {
        return fail(ErrorCode::Internal,
                    fmt::format("the merged configuration is not valid:\n  {}", join_issues(issues)));
    }
    return loaded;
}

}  // namespace cloudscope
