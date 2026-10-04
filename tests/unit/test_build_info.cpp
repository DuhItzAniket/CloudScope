#include <cloudscope/common/build_info.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <regex>
#include <string>

using cloudscope::BuildInfo;
using cloudscope::DependencyInfo;

TEST_CASE("build info describes this binary", "[common][build_info]")
{
    const BuildInfo& info = cloudscope::build_info();

    CHECK(std::regex_match(info.version, std::regex(R"(\d+\.\d+\.\d+)")));
    CHECK(std::regex_match(info.git_revision, std::regex(R"(unknown|[0-9a-f]{7,40}(-dirty)?)")));
    CHECK((info.build_type == "Debug" || info.build_type == "RelWithDebInfo" || info.build_type == "Release"));
    CHECK((info.system == "Windows" || info.system == "Linux"));
    CHECK((info.architecture == "x86_64" || info.architecture == "aarch64"));
    CHECK_FALSE(info.compiler.empty());

#ifdef NDEBUG
    CHECK(info.build_type != "Debug");
#else
    CHECK(info.build_type == "Debug");
#endif
}

TEST_CASE("build info is collected once", "[common][build_info]")
{
    CHECK(&cloudscope::build_info() == &cloudscope::build_info());
}

TEST_CASE("every bundled library is listed with a version and a licence", "[common][build_info]")
{
    const BuildInfo& info = cloudscope::build_info();
    const std::regex version(R"(\d+\.\d+(\.\d+)*)");

    for (const char* name :
         {"Qt", "OpenCV", "spdlog", "fmt", "toml++", "nlohmann-json", "tl-expected", "CFITSIO", "libjpeg-turbo"}) {
        INFO("library: " << name);
        const auto found = std::ranges::find(info.dependencies, name, &DependencyInfo::name);
        REQUIRE(found != info.dependencies.end());
        CHECK(std::regex_search(found->compiled_version, version));
        CHECK_FALSE(found->licence.empty());
    }
    CHECK(info.dependencies.size() == 9);
}

TEST_CASE("Qt is at least the version Raspberry Pi OS ships", "[common][build_info]")
{
    const BuildInfo& info = cloudscope::build_info();
    const auto qt = std::ranges::find(info.dependencies, "Qt", &DependencyInfo::name);
    REQUIRE(qt != info.dependencies.end());

    std::smatch match;
    REQUIRE(std::regex_search(qt->runtime_version, match, std::regex(R"((\d+)\.(\d+))")));
    const int major = std::stoi(match[1]);
    const int minor = std::stoi(match[2]);
    CHECK(major == 6);
    CHECK(minor >= 8);
}

TEST_CASE("loaded libraries match the headers they were compiled against", "[common][build_info]")
{
    // A mismatch means a DLL or shared object from another installation was picked up at run time.
    for (const DependencyInfo& dep : cloudscope::build_info().dependencies) {
        if (!dep.runtime_version.empty()) {
            INFO("library: " << dep.name);
            CHECK(dep.runtime_version == dep.compiled_version);
        }
    }
}

TEST_CASE("text report names the version and every library", "[common][build_info]")
{
    const BuildInfo& info = cloudscope::build_info();
    const std::string text = cloudscope::to_text(info);

    CHECK_THAT(text, Catch::Matchers::StartsWith("CloudScope " + info.version + " (git " + info.git_revision + ")"));
    for (const DependencyInfo& dep : info.dependencies) {
        CHECK_THAT(text, Catch::Matchers::ContainsSubstring(dep.name));
        CHECK_THAT(text, Catch::Matchers::ContainsSubstring(dep.compiled_version));
    }
}

TEST_CASE("JSON report carries the same content as the struct", "[common][build_info]")
{
    const BuildInfo& info = cloudscope::build_info();
    const nlohmann::json json = nlohmann::json::parse(cloudscope::to_json(info));

    CHECK(json.at("name") == "CloudScope");
    CHECK(json.at("version") == info.version);
    CHECK(json.at("git_revision") == info.git_revision);
    CHECK(json.at("build_type") == info.build_type);
    CHECK(json.at("compiler") == info.compiler);
    CHECK(json.at("system") == info.system);
    CHECK(json.at("architecture") == info.architecture);

    const nlohmann::json& dependencies = json.at("dependencies");
    REQUIRE(dependencies.size() == info.dependencies.size());
    for (std::size_t i = 0; i < info.dependencies.size(); ++i) {
        const DependencyInfo& dep = info.dependencies[i];
        CHECK(dependencies[i].at("name") == dep.name);
        CHECK(dependencies[i].at("compiled_version") == dep.compiled_version);
        CHECK(dependencies[i].at("licence") == dep.licence);
        // Libraries without a runtime version query must not get an empty placeholder.
        CHECK(dependencies[i].contains("runtime_version") == !dep.runtime_version.empty());
    }
}
