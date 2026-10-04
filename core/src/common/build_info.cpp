#include "cloudscope/common/build_info.hpp"

#include "build_config.hpp"

#include <QtCore/QtGlobal>
#include <fitsio.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/core/version.hpp>
#include <spdlog/version.h>
#include <tl/expected.hpp>
#include <toml++/toml.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>  // jconfig.h/jpeglib.h expect it
#if __has_include(<jconfig.h>)
#include <jconfig.h>
#endif

namespace cloudscope {

namespace detail {
extern const char* const kGitRevision;  // generated: git_revision.cpp
}

namespace {

std::string dotted(int major, int minor, int patch)
{
    return fmt::format("{}.{}.{}", major, minor, patch);
}

std::string jpeg_turbo_compiled_version()
{
#ifdef LIBJPEG_TURBO_VERSION_NUMBER
    const int number = LIBJPEG_TURBO_VERSION_NUMBER;  // e.g. 2001005 for 2.1.5
    return dotted(number / 1000000, (number / 1000) % 1000, number % 1000);
#else
    return "unknown";
#endif
}

std::string cfitsio_runtime_version()
{
    float encoded = 0.0F;
    fits_get_version(&encoded);  // major + minor/100 + micro/10000, e.g. 4.0602 for 4.6.2
    const int ten_thousandths = static_cast<int>(std::lround(static_cast<double>(encoded) * 10000.0));
    return dotted(ten_thousandths / 10000, (ten_thousandths / 100) % 100, ten_thousandths % 100);
}

BuildInfo collect()
{
    BuildInfo info;
    info.version = CLOUDSCOPE_VERSION;
    info.git_revision = detail::kGitRevision;
    info.build_type = CLOUDSCOPE_BUILD_TYPE;
    info.compiler = CLOUDSCOPE_COMPILER;
    info.system = CLOUDSCOPE_SYSTEM;
    info.architecture = CLOUDSCOPE_ARCH;
    info.dependencies = {
        {"Qt", QT_VERSION_STR, qVersion(), "LGPL-3.0-only"},
        {"OpenCV", CV_VERSION, cv::getVersionString(), "Apache-2.0"},
        {"spdlog", dotted(SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH), "", "MIT"},
        {"fmt", dotted(FMT_VERSION / 10000, (FMT_VERSION / 100) % 100, FMT_VERSION % 100), "", "MIT"},
        {"toml++", dotted(TOML_LIB_MAJOR, TOML_LIB_MINOR, TOML_LIB_PATCH), "", "MIT"},
        {"nlohmann-json",
         dotted(NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH), "", "MIT"},
        {"tl-expected", dotted(TL_EXPECTED_VERSION_MAJOR, TL_EXPECTED_VERSION_MINOR, TL_EXPECTED_VERSION_PATCH), "",
         "CC0-1.0"},
        {"CFITSIO", dotted(CFITSIO_MAJOR, CFITSIO_MINOR, CFITSIO_MICRO), cfitsio_runtime_version(), "CFITSIO"},
        {"libjpeg-turbo", jpeg_turbo_compiled_version(), "", "IJG AND BSD-3-Clause AND Zlib"},
    };
    return info;
}

}  // namespace

const BuildInfo& build_info()
{
    static const BuildInfo info = collect();
    return info;
}

std::string to_text(const BuildInfo& info)
{
    std::string out = fmt::format("CloudScope {} (git {})\n", info.version, info.git_revision);
    out += fmt::format("Build:    {}, {}, {} {}\n", info.build_type, info.compiler, info.system, info.architecture);
    out += "Libraries:\n";
    for (const DependencyInfo& dep : info.dependencies) {
        out += fmt::format("  {:<14} {:<10}", dep.name, dep.compiled_version);
        if (!dep.runtime_version.empty()) {
            out += fmt::format(" (loaded: {})", dep.runtime_version);
        }
        out += fmt::format("  [{}]\n", dep.licence);
    }
    return out;
}

std::string to_json(const BuildInfo& info)
{
    nlohmann::ordered_json dependencies = nlohmann::ordered_json::array();
    for (const DependencyInfo& dep : info.dependencies) {
        nlohmann::ordered_json entry;
        entry["name"] = dep.name;
        entry["compiled_version"] = dep.compiled_version;
        if (!dep.runtime_version.empty()) {
            entry["runtime_version"] = dep.runtime_version;
        }
        entry["licence"] = dep.licence;
        dependencies.push_back(std::move(entry));
    }
    nlohmann::ordered_json root;
    root["name"] = "CloudScope";
    root["version"] = info.version;
    root["git_revision"] = info.git_revision;
    root["build_type"] = info.build_type;
    root["compiler"] = info.compiler;
    root["system"] = info.system;
    root["architecture"] = info.architecture;
    root["dependencies"] = std::move(dependencies);
    return root.dump(2);
}

}  // namespace cloudscope
