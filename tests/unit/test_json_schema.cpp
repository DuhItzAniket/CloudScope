#include <cloudscope/common/json_schema.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace cloudscope;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

namespace {

JsonSchema compiled(const char* text)
{
    auto schema = JsonSchema::compile(json::parse(text));
    REQUIRE(schema.has_value());
    return *schema;
}

std::vector<std::string> messages(const JsonSchema& schema, const char* document, SchemaOptions options = {})
{
    std::vector<std::string> lines;
    for (const SchemaIssue& issue : schema.validate(json::parse(document), options)) {
        lines.push_back(issue.to_string());
    }
    return lines;
}

const char* const kCameraSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "required": ["name", "exposure_ms"],
  "properties": {
    "name": {"type": "string", "minLength": 1, "maxLength": 8, "pattern": "^[A-Za-z0-9_-]+$"},
    "code": {"type": "string", "minLength": 3},
    "exposure_ms": {"type": "number", "exclusiveMinimum": 0, "maximum": 10000},
    "gain": {"type": "integer", "minimum": 0, "maximum": 100},
    "mode": {"enum": ["auto", "manual"]},
    "version": {"const": 2},
    "roi": {"type": "array", "items": {"type": "integer", "minimum": 0}, "minItems": 4, "maxItems": 4},
    "tags": {"type": "array", "items": {"type": "string"}, "uniqueItems": true},
    "note": {"type": ["string", "null"]},
    "extra": {"type": "object", "additionalProperties": {"type": "boolean"}}
  }
})";

}  // namespace

TEST_CASE("a valid document has no issues", "[common][json_schema]")
{
    const JsonSchema schema = compiled(kCameraSchema);
    CHECK(messages(schema, R"({"name": "B0268", "exposure_ms": 12.5, "gain": 10, "mode": "manual", "version": 2,
                               "roi": [0, 0, 640, 480], "tags": ["sky", "zenith"], "note": null,
                               "extra": {"cooled": false}})")
              .empty());
    CHECK(messages(schema, R"({"name": "a", "exposure_ms": 10000})").empty());
    CHECK(messages(schema, R"({"name": "a", "exposure_ms": 1, "gain": 5.0})").empty());  // 5.0 is an integer
}

TEST_CASE("each violation is reported with its path and the offending value", "[common][json_schema]")
{
    const JsonSchema schema = compiled(kCameraSchema);
    const auto [document, expected] = GENERATE(table<const char*, std::string>({
        {R"({"exposure_ms": 5})", "required key 'name' is missing"},
        {R"({"name": 7, "exposure_ms": 5})", "name: must be a string; got an integer (7)"},
        {R"({"name": "a", "exposure_ms": 5, "code": "ab"})", "code: must have at least 3 character(s); got \"ab\""},
        {R"({"name": "much-too-long", "exposure_ms": 5})", "name: must have at most 8 character(s); got 13 characters"},
        {R"({"name": "a b", "exposure_ms": 5})", "name: must match the pattern ^[A-Za-z0-9_-]+$; got \"a b\""},
        {R"({"name": "a", "exposure_ms": 0})", "exposure_ms: must be greater than 0; got 0"},
        {R"({"name": "a", "exposure_ms": 10000.5})", "exposure_ms: must be at most 10000; got 10000.5"},
        {R"({"name": "a", "exposure_ms": "fast"})", "exposure_ms: must be a number; got a string (\"fast\")"},
        {R"({"name": "a", "exposure_ms": 5, "gain": 2.5})", "gain: must be an integer; got a number (2.5)"},
        {R"({"name": "a", "exposure_ms": 5, "gain": -1})", "gain: must be at least 0; got -1"},
        {R"({"name": "a", "exposure_ms": 5, "mode": "fast"})",
         "mode: must be one of \"auto\", \"manual\"; got \"fast\""},
        {R"({"name": "a", "exposure_ms": 5, "version": 3})", "version: must be 2; got 3"},
        {R"({"name": "a", "exposure_ms": 5, "roi": [0, 0, 640]})", "roi: must have at least 4 item(s); got 3"},
        {R"({"name": "a", "exposure_ms": 5, "roi": [0, 0, 640, 480, 1]})", "roi: must have at most 4 item(s); got 5"},
        {R"({"name": "a", "exposure_ms": 5, "roi": [0, 0, -640, 480]})", "roi[2]: must be at least 0; got -640"},
        {R"({"name": "a", "exposure_ms": 5, "tags": ["sky", "x", "sky"]})", "tags[2]: repeats item [0]: \"sky\""},
        {R"({"name": "a", "exposure_ms": 5, "note": 3})", "note: must be a string or null; got an integer (3)"},
        {R"({"name": "a", "exposure_ms": 5, "extra": {"cooled": "yes"}})",
         "extra.cooled: must be true or false; got a string (\"yes\")"},
        {R"({"name": "a", "exposure_ms": 5, "roi": {"x": 1}})", "roi: must be a list; got a table"},
        {R"([1, 2])", "must be a table; got a list"},
    }));
    CAPTURE(document);
    const std::vector<std::string> found = messages(schema, document);
    REQUIRE(found.size() == 1);
    CHECK(found.front() == expected);
}

TEST_CASE("unknown keys are errors and near misses get a suggestion", "[common][json_schema]")
{
    const JsonSchema schema = compiled(kCameraSchema);
    CHECK(messages(schema, R"({"name": "a", "exposure_ms": 5, "gian": 3})") ==
          std::vector<std::string>{"gian: unknown key (did you mean 'gain'?)"});
    CHECK(messages(schema, R"({"name": "a", "exposure_ms": 5, "Mode": "auto"})") ==
          std::vector<std::string>{"Mode: unknown key (did you mean 'mode'?)"});

    const std::vector<std::string> far = messages(schema, R"({"name": "a", "exposure_ms": 5, "temperature": 3})");
    REQUIRE(far.size() == 1);
    CHECK_THAT(far.front(), ContainsSubstring("temperature: unknown key (allowed here: "));
    CHECK_THAT(far.front(), ContainsSubstring("'exposure_ms'"));
}

TEST_CASE("all violations of a document are reported, in document order", "[common][json_schema]")
{
    const JsonSchema schema = compiled(kCameraSchema);
    const std::vector<std::string> found =
        messages(schema, R"({"exposure_ms": -1, "gain": 500, "mode": "x", "zzz": 1})");
    REQUIRE(found.size() == 5);
    CHECK(found[0] == "required key 'name' is missing");
    CHECK_THAT(found[1], ContainsSubstring("exposure_ms: must be greater than 0"));
    CHECK_THAT(found[2], ContainsSubstring("gain: must be at most 100"));
    CHECK_THAT(found[3], ContainsSubstring("mode: must be one of"));
    CHECK_THAT(found[4], ContainsSubstring("zzz: unknown key"));
}

TEST_CASE("partial documents can skip the required-key check but nothing else", "[common][json_schema]")
{
    const JsonSchema schema = compiled(kCameraSchema);
    CHECK(messages(schema, R"({"gain": 3})", {.check_required = false}).empty());
    CHECK(messages(schema, R"({"gain": 300})", {.check_required = false}).size() == 1);
    CHECK(messages(schema, R"({"gian": 3})", {.check_required = false}).size() == 1);
}

TEST_CASE("string length counts characters, not bytes", "[common][json_schema]")
{
    const JsonSchema schema = compiled(R"({"type": "string", "maxLength": 4})");
    CHECK(messages(schema, "\"\xC3\xA9t\xC3\xA9s\"").empty());       // "étés": 4 characters, 6 bytes
    CHECK(messages(schema, "\"\xC3\xA9t\xC3\xA9s!\"").size() == 1);  // 5 characters
}

TEST_CASE("local references are followed", "[common][json_schema]")
{
    const JsonSchema schema = compiled(R"({
      "type": "object",
      "properties": {"pan": {"$ref": "#/$defs/axis"}, "tilt": {"$ref": "#/$defs/axis"}},
      "$defs": {"axis": {"type": "object", "required": ["limit_deg"],
                         "properties": {"limit_deg": {"type": "number", "minimum": 0, "maximum": 360}}}}
    })");
    CHECK(messages(schema, R"({"pan": {"limit_deg": 270}, "tilt": {"limit_deg": 90}})").empty());
    CHECK(messages(schema, R"({"pan": {"limit_deg": 400}, "tilt": {}})") ==
          std::vector<std::string>{"pan.limit_deg: must be at most 360; got 400",
                                   "tilt: required key 'limit_deg' is missing"});
}

TEST_CASE("a schema that refers to itself does not hang the validator", "[common][json_schema]")
{
    const JsonSchema schema = compiled(R"({"$ref": "#/$defs/loop", "$defs": {"loop": {"$ref": "#/$defs/loop"}}})");
    const std::vector<std::string> found = messages(schema, "1");
    REQUIRE_FALSE(found.empty());
    CHECK_THAT(found.front(), ContainsSubstring("refers to itself"));
}

TEST_CASE("schemas with unsupported or malformed keywords are rejected, never half-applied", "[common][json_schema]")
{
    const auto [schema, reason] = GENERATE(table<const char*, std::string>({
        {R"({"oneOf": [{"type": "string"}]})", "unsupported keyword 'oneOf'"},
        {R"({"type": "object", "patternProperties": {}})", "unsupported keyword 'patternProperties'"},
        {R"({"properties": {"a": {"format": "date"}}})", "schema at /properties/a: unsupported keyword 'format'"},
        {R"({"type": "text"})", "\"text\" is not a type name"},
        {R"({"type": []})", "needs at least one type name"},
        {R"({"enum": []})", "must be a non-empty array"},
        {R"({"required": "name"})", "must be an array of key names"},
        {R"({"properties": []})", "must be an object of schemas"},
        {R"({"items": 3})", "a schema must be an object"},
        {R"({"minLength": -1})", "must be a non-negative integer"},
        {R"({"maximum": "ten"})", "must be a number"},
        {R"({"uniqueItems": 1})", "must be true or false"},
        {R"({"pattern": "(unclosed"})", "is not a valid regular expression"},
        {R"({"$ref": "other.json#/a"})", "only references of the form"},
        {R"({"$ref": "#/$defs/missing"})", "'#/$defs/missing' does not exist"},
        {R"("just a string")", "a schema must be an object"},
    }));
    CAPTURE(schema);
    const auto result = JsonSchema::compile(json::parse(schema));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::Validation);
    CHECK_THAT(result.error().message, ContainsSubstring(reason));
}

TEST_CASE("annotations are accepted and ignored", "[common][json_schema]")
{
    const JsonSchema schema = compiled(R"({
      "$schema": "https://json-schema.org/draft/2020-12/schema", "$id": "x", "title": "t",
      "description": "d", "default": 1, "examples": [1, 2], "type": "integer"
    })");
    CHECK(messages(schema, "3").empty());
    CHECK(schema.document().at("title") == "t");
}
