#include "cloudscope/common/json_schema.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>

namespace cloudscope {

namespace {

using nlohmann::json;

constexpr std::array<std::string_view, 6> kAnnotations = {"$schema",     "$id",     "title",
                                                          "description", "default", "examples"};
constexpr std::array<std::string_view, 19> kKeywords = {
    "type",      "enum",      "const",       "properties", "required", "additionalProperties", "items",
    "minItems",  "maxItems",  "uniqueItems", "minimum",    "maximum",  "exclusiveMinimum",     "exclusiveMaximum",
    "minLength", "maxLength", "pattern",     "$defs",      "$ref"};
constexpr std::array<std::string_view, 7> kTypeNames = {"object",  "array",   "string", "number",
                                                        "integer", "boolean", "null"};
constexpr std::string_view kRefPrefix = "#/$defs/";

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& list, std::string_view item)
{
    return std::ranges::find(list, item) != list.end();
}

// --------------------------------------------------------------------------------------- compile

struct Compiler {
    const json& root;
    std::map<std::string, std::regex>& patterns;
    std::string error;  // first problem found; empty while everything is fine

    void problem(const std::string& where, const std::string& what)
    {
        if (error.empty()) {
            error = fmt::format("schema{}: {}", where.empty() ? std::string() : " at " + where, what);
        }
    }

    void check(const json& node, const std::string& where)
    {
        if (!error.empty()) {
            return;
        }
        if (!node.is_object()) {
            problem(where, "a schema must be an object");
            return;
        }
        for (const auto& [key, value] : node.items()) {
            if (contains(kAnnotations, key)) {
                continue;
            }
            if (!contains(kKeywords, key)) {
                problem(where, fmt::format("unsupported keyword '{}'", key));
                return;
            }
            check_keyword(key, value, where);
        }
    }

    void check_keyword(const std::string& key, const json& value, const std::string& where)
    {
        const std::string here = where + "/" + key;
        if (key == "type") {
            check_type(value, here);
        } else if (key == "enum") {
            require(value.is_array() && !value.empty(), here, "must be a non-empty array");
        } else if (key == "properties" || key == "$defs") {
            check_schema_map(value, here);
        } else if (key == "required") {
            require(value.is_array() && all_strings(value), here, "must be an array of key names");
        } else if (key == "additionalProperties") {
            if (!value.is_boolean()) {
                check(value, here);
            }
        } else if (key == "items") {
            check(value, here);
        } else if (key == "minItems" || key == "maxItems" || key == "minLength" || key == "maxLength") {
            require(value.is_number_integer() && value.get<long long>() >= 0, here, "must be a non-negative integer");
        } else if (key == "uniqueItems") {
            require(value.is_boolean(), here, "must be true or false");
        } else if (key == "minimum" || key == "maximum" || key == "exclusiveMinimum" || key == "exclusiveMaximum") {
            require(value.is_number(), here, "must be a number");
        } else if (key == "pattern") {
            check_pattern(value, here);
        } else if (key == "$ref") {
            check_reference(value, here);
        }
        // "const" accepts any value.
    }

    void require(bool condition, const std::string& where, const std::string& what)
    {
        if (!condition) {
            problem(where, what);
        }
    }

    static bool all_strings(const json& list)
    {
        return std::ranges::all_of(list, [](const json& item) { return item.is_string(); });
    }

    void check_type(const json& value, const std::string& here)
    {
        const json names = value.is_array() ? value : json::array({value});
        require(!names.empty(), here, "needs at least one type name");
        for (const json& name : names) {
            if (!name.is_string() || !contains(kTypeNames, name.get_ref<const std::string&>())) {
                problem(here, fmt::format("{} is not a type name", name.dump()));
            }
        }
    }

    void check_schema_map(const json& value, const std::string& here)
    {
        if (!value.is_object()) {
            problem(here, "must be an object of schemas");
            return;
        }
        for (const auto& [name, schema] : value.items()) {
            check(schema, fmt::format("{}/{}", here, name));
        }
    }

    void check_pattern(const json& value, const std::string& here)
    {
        if (!value.is_string()) {
            problem(here, "must be a string");
            return;
        }
        const auto& pattern = value.get_ref<const std::string&>();
        try {
            patterns.emplace(pattern, std::regex(pattern, std::regex::ECMAScript));
        } catch (const std::regex_error& regex_error) {
            problem(here, fmt::format("'{}' is not a valid regular expression ({})", pattern, regex_error.what()));
        }
    }

    void check_reference(const json& value, const std::string& here)
    {
        const bool local = value.is_string() && value.get_ref<const std::string&>().starts_with(kRefPrefix);
        if (!local) {
            problem(here, "only references of the form \"#/$defs/<name>\" are supported");
            return;
        }
        const std::string name = value.get_ref<const std::string&>().substr(kRefPrefix.size());
        if (!root.contains("$defs") || !root.at("$defs").is_object() || !root.at("$defs").contains(name)) {
            problem(here, fmt::format("'#/$defs/{}' does not exist", name));
        }
    }
};

// -------------------------------------------------------------------------------------- validate

// True if `list` (a JSON array) has an element equal to `value`.
bool contains_value(const json& list, const json& value)
{
    return std::ranges::any_of(list, [&value](const json& item) { return item == value; });
}

std::string describe_type(const json& value)
{
    switch (value.type()) {
    case json::value_t::object:
        return "a table";
    case json::value_t::array:
        return "a list";
    case json::value_t::string:
        return "a string";
    case json::value_t::boolean:
        return "true or false";
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        return "an integer";
    case json::value_t::number_float:
        return "a number";
    case json::value_t::null:
        return "null";
    default:
        return "an unsupported value";
    }
}

std::string describe_type_name(const std::string& name)
{
    if (name == "object") {
        return "a table";
    }
    if (name == "array") {
        return "a list";
    }
    if (name == "string") {
        return "a string";
    }
    if (name == "number") {
        return "a number";
    }
    if (name == "integer") {
        return "an integer";
    }
    if (name == "boolean") {
        return "true or false";
    }
    return "null";
}

// The value as the user wrote it, shortened: for "got ..." in messages.
std::string show(const json& value)
{
    if (value.is_object() || value.is_array()) {
        return describe_type(value);
    }
    std::string text = value.dump();
    constexpr std::size_t kMax = 60;
    if (text.size() > kMax) {
        text = text.substr(0, kMax) + "...";
    }
    return text;
}

bool matches_type(const json& value, const std::string& name)
{
    if (name == "object") {
        return value.is_object();
    }
    if (name == "array") {
        return value.is_array();
    }
    if (name == "string") {
        return value.is_string();
    }
    if (name == "boolean") {
        return value.is_boolean();
    }
    if (name == "null") {
        return value.is_null();
    }
    if (name == "number") {
        return value.is_number();
    }
    // "integer": JSON Schema counts 2.0 as an integer.
    if (value.is_number_integer()) {
        return true;
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        return std::isfinite(number) && std::floor(number) == number;
    }
    return false;
}

std::size_t code_point_count(const std::string& utf8)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(utf8, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

std::string ascii_lower(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return text;
}

std::size_t edit_distance(std::string_view a, std::string_view b)
{
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t substitution = diagonal + (a[i - 1] == b[j - 1] ? 0 : 1);
            diagonal = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, substitution});
        }
    }
    return row[b.size()];
}

std::string join_quoted(const std::vector<std::string>& names)
{
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "'" : ", '") + name + "'";
    }
    return text;
}

std::string child_path(const std::string& path, const std::string& key)
{
    return path.empty() ? key : path + "." + key;
}

struct Validator {
    const json& root;
    const std::map<std::string, std::regex>& patterns;
    SchemaOptions options;
    std::vector<SchemaIssue>& issues;

    void report(const std::string& path, std::string message) { issues.push_back({path, std::move(message)}); }

    void validate(const json& schema, const json& value, const std::string& path, int depth = 0)
    {
        constexpr int kMaxDepth = 64;  // guards against reference cycles in a schema
        if (depth > kMaxDepth) {
            report(path, "the schema refers to itself without end");
            return;
        }
        if (schema.contains("$ref")) {
            const std::string name = schema.at("$ref").get<std::string>().substr(kRefPrefix.size());
            validate(root.at("$defs").at(name), value, path, depth + 1);
        }

        if (schema.contains("type")) {
            const json& type = schema.at("type");
            const json names = type.is_array() ? type : json::array({type});
            bool ok = false;
            for (const json& name : names) {
                ok = ok || matches_type(value, name.get_ref<const std::string&>());
            }
            if (!ok) {
                std::string expected;
                for (std::size_t i = 0; i < names.size(); ++i) {
                    expected += (i == 0 ? "" : " or ") + describe_type_name(names[i].get<std::string>());
                }
                const std::string got = value.is_object() || value.is_array()
                                            ? describe_type(value)
                                            : fmt::format("{} ({})", describe_type(value), show(value));
                report(path, fmt::format("must be {}; got {}", expected, got));
                return;  // further checks on a value of the wrong type only add noise
            }
        }
        if (schema.contains("enum")) {
            const json& allowed = schema.at("enum");
            if (!contains_value(allowed, value)) {
                std::string list;
                for (const json& item : allowed) {
                    list += (list.empty() ? "" : ", ") + item.dump();
                }
                report(path, fmt::format("must be one of {}; got {}", list, show(value)));
            }
        }
        if (schema.contains("const") && schema.at("const") != value) {
            report(path, fmt::format("must be {}; got {}", schema.at("const").dump(), show(value)));
        }
        if (value.is_number()) {
            check_number(schema, value, path);
        } else if (value.is_string()) {
            check_string(schema, value.get_ref<const std::string&>(), path);
        } else if (value.is_array()) {
            check_array(schema, value, path, depth);
        } else if (value.is_object()) {
            check_object(schema, value, path, depth);
        }
    }

    void check_number(const json& schema, const json& value, const std::string& path)
    {
        const double number = value.get<double>();
        if (schema.contains("minimum") && number < schema.at("minimum").get<double>()) {
            report(path, fmt::format("must be at least {}; got {}", schema.at("minimum").dump(), show(value)));
        }
        if (schema.contains("maximum") && number > schema.at("maximum").get<double>()) {
            report(path, fmt::format("must be at most {}; got {}", schema.at("maximum").dump(), show(value)));
        }
        if (schema.contains("exclusiveMinimum") && number <= schema.at("exclusiveMinimum").get<double>()) {
            report(path,
                   fmt::format("must be greater than {}; got {}", schema.at("exclusiveMinimum").dump(), show(value)));
        }
        if (schema.contains("exclusiveMaximum") && number >= schema.at("exclusiveMaximum").get<double>()) {
            report(path,
                   fmt::format("must be less than {}; got {}", schema.at("exclusiveMaximum").dump(), show(value)));
        }
    }

    void check_string(const json& schema, const std::string& text, const std::string& path)
    {
        const std::size_t length = code_point_count(text);
        if (schema.contains("minLength") && length < schema.at("minLength").get<std::size_t>()) {
            report(path, fmt::format("must have at least {} character(s); got {}",
                                     schema.at("minLength").get<std::size_t>(), show(json(text))));
        }
        if (schema.contains("maxLength") && length > schema.at("maxLength").get<std::size_t>()) {
            report(path, fmt::format("must have at most {} character(s); got {} characters",
                                     schema.at("maxLength").get<std::size_t>(), length));
        }
        if (schema.contains("pattern")) {
            const auto& pattern = schema.at("pattern").get_ref<const std::string&>();
            if (!std::regex_search(text, patterns.at(pattern))) {
                report(path, fmt::format("must match the pattern {}; got {}", pattern, show(json(text))));
            }
        }
    }

    void check_array(const json& schema, const json& value, const std::string& path, int depth)
    {
        if (schema.contains("minItems") && value.size() < schema.at("minItems").get<std::size_t>()) {
            report(path, fmt::format("must have at least {} item(s); got {}", schema.at("minItems").get<std::size_t>(),
                                     value.size()));
        }
        if (schema.contains("maxItems") && value.size() > schema.at("maxItems").get<std::size_t>()) {
            report(path, fmt::format("must have at most {} item(s); got {}", schema.at("maxItems").get<std::size_t>(),
                                     value.size()));
        }
        if (schema.value("uniqueItems", false)) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                for (std::size_t j = 0; j < i; ++j) {
                    if (value[i] == value[j]) {
                        report(fmt::format("{}[{}]", path, i), fmt::format("repeats item [{}]: {}", j, show(value[i])));
                        j = i;  // one report per repeated item
                    }
                }
            }
        }
        if (schema.contains("items")) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                validate(schema.at("items"), value[i], fmt::format("{}[{}]", path, i), depth + 1);
            }
        }
    }

    void check_object(const json& schema, const json& value, const std::string& path, int depth)
    {
        static const json no_properties = json::object();
        const json& properties = schema.contains("properties") ? schema.at("properties") : no_properties;

        if (options.check_required && schema.contains("required")) {
            for (const json& key : schema.at("required")) {
                if (!value.contains(key.get_ref<const std::string&>())) {
                    report(path, fmt::format("required key '{}' is missing", key.get<std::string>()));
                }
            }
        }
        for (const auto& [key, item] : value.items()) {
            if (properties.contains(key)) {
                validate(properties.at(key), item, child_path(path, key), depth + 1);
                continue;
            }
            if (!schema.contains("additionalProperties")) {
                continue;
            }
            const json& additional = schema.at("additionalProperties");
            if (additional.is_boolean()) {
                if (!additional.get<bool>()) {
                    report(child_path(path, key), unknown_key_message(key, properties));
                }
            } else {
                validate(additional, item, child_path(path, key), depth + 1);
            }
        }
    }

    static std::string unknown_key_message(const std::string& key, const json& properties)
    {
        std::vector<std::string> known;
        std::string closest;
        std::size_t closest_distance = 3;  // suggest only near misses: up to two edits, ignoring case
        const std::string lower_key = ascii_lower(key);
        for (const auto& [name, unused] : properties.items()) {
            known.push_back(name);
            const std::size_t distance = edit_distance(lower_key, ascii_lower(name));
            if (distance < closest_distance) {
                closest = name;
                closest_distance = distance;
            }
        }
        if (!closest.empty()) {
            return fmt::format("unknown key (did you mean '{}'?)", closest);
        }
        if (known.empty()) {
            return "unknown key (no keys are allowed here)";
        }
        return fmt::format("unknown key (allowed here: {})", join_quoted(known));
    }
};

}  // namespace

std::string SchemaIssue::to_string() const
{
    return path.empty() ? message : path + ": " + message;
}

JsonSchema::JsonSchema(nlohmann::json schema, Patterns patterns)
    : schema_(std::make_shared<const nlohmann::json>(std::move(schema))),
      patterns_(std::make_shared<const Patterns>(std::move(patterns)))
{
}

Expected<JsonSchema> JsonSchema::compile(nlohmann::json schema)
{
    Patterns patterns;
    Compiler compiler{.root = schema, .patterns = patterns, .error = {}};
    compiler.check(schema, "");
    if (!compiler.error.empty()) {
        return fail(ErrorCode::Validation, compiler.error);
    }
    return JsonSchema(std::move(schema), std::move(patterns));
}

std::vector<SchemaIssue> JsonSchema::validate(const nlohmann::json& document, SchemaOptions options) const
{
    std::vector<SchemaIssue> issues;
    Validator validator{.root = *schema_, .patterns = *patterns_, .options = options, .issues = issues};
    validator.validate(*schema_, document, "");
    return issues;
}

}  // namespace cloudscope
