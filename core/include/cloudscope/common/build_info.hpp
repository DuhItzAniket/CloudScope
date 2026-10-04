// Build and dependency information: what exactly is this binary made of?
// Used by `cloudscope-info`, bug reports, session records and the About dialog.
#pragma once

#include <string>
#include <vector>

namespace cloudscope {

struct DependencyInfo {
    std::string name;
    std::string compiled_version;  // version of the headers this binary was compiled against
    std::string runtime_version;   // version reported by the loaded library; empty if it offers no such query
    std::string licence;           // SPDX licence expression
};

struct BuildInfo {
    std::string version;       // CloudScope version, e.g. "0.2.0"
    std::string git_revision;  // e.g. "b59a591c2e" or "b59a591c2e-dirty"; "unknown" outside a git checkout
    std::string build_type;    // "Debug", "RelWithDebInfo" or "Release"
    std::string compiler;      // e.g. "MSVC 19.44.35207.1" or "GNU 14.2.0"
    std::string system;        // "Windows" or "Linux"
    std::string architecture;  // "x86_64" or "aarch64"
    std::vector<DependencyInfo> dependencies;
};

// Information about the running binary. Thread-safe; the object lives for the whole program.
[[nodiscard]] const BuildInfo& build_info();

// Multi-line, human-readable report.
[[nodiscard]] std::string to_text(const BuildInfo& info);

// JSON object with the same content (keys in a fixed order, two-space indentation).
[[nodiscard]] std::string to_json(const BuildInfo& info);

}  // namespace cloudscope
