// Validation of JSON documents against a JSON Schema (draft 2020-12), with messages a user can act on:
//
//   logging.level: must be one of "trace", "debug", "info", "warn", "error"; got "verbose"
//   logging.colour: unknown key (did you mean 'console'?)
//
// Used for configuration files (ADR-007); later for sidecars and API payloads.
//
// Supported keywords: type, enum, const, properties, required, additionalProperties, items, minItems, maxItems,
// uniqueItems, minimum, maximum, exclusiveMinimum, exclusiveMaximum, minLength, maxLength, pattern, $defs and
// $ref to "#/$defs/<name>", plus the annotations $schema, $id, title, description, default, examples.
// A schema that uses any other keyword is rejected by compile(): nothing is ever silently left unchecked.
#pragma once

#include "cloudscope/common/error.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace cloudscope {

struct SchemaIssue {
    std::string path;     // "logging.level" or "cameras[2].name"; empty for the document itself
    std::string message;  // "must be at least 1; got 0"

    [[nodiscard]] std::string to_string() const;  // "logging.level: must be ..."
    friend bool operator==(const SchemaIssue&, const SchemaIssue&) = default;
};

struct SchemaOptions {
    // false: missing required keys are not reported. For partial documents such as one configuration layer.
    bool check_required = true;
};

class JsonSchema {
public:
    // Checks the schema itself: structure, supported keywords, regular expressions, references.
    [[nodiscard]] static Expected<JsonSchema> compile(nlohmann::json schema);

    // Every violation in the document, in document order; empty when the document is valid.
    [[nodiscard]] std::vector<SchemaIssue> validate(const nlohmann::json& document, SchemaOptions options = {}) const;

    [[nodiscard]] const nlohmann::json& document() const { return *schema_; }

private:
    using Patterns = std::map<std::string, std::regex>;

    JsonSchema(nlohmann::json schema, Patterns patterns);

    std::shared_ptr<const nlohmann::json> schema_;
    std::shared_ptr<const Patterns> patterns_;
};

}  // namespace cloudscope
