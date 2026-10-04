// Helpers shared by all test executables.
#pragma once

#include <QtCore/QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace cloudscope::test {

// The folder with the fixtures listed in tests/data/manifest.json.
[[nodiscard]] std::filesystem::path data_dir();

// A fixture by its name in the manifest, e.g. "sky/ccsn_cu_n001.jpg". Fails the test if it does not exist.
[[nodiscard]] std::filesystem::path data_path(std::string_view name);

// Whole content of a file; empty if it cannot be read.
[[nodiscard]] std::string read_file(const std::filesystem::path& path);

// A folder for one test, deleted afterwards. Its name contains a non-ASCII character on purpose:
// user profile folders on Windows often do, and file code must cope.
class TempWorkspace {
public:
    TempWorkspace();

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] std::filesystem::path path(std::string_view relative) const;
    // Writes a text file (creating folders as needed) and returns its path.
    std::filesystem::path write(std::string_view relative, std::string_view text) const;

private:
    QTemporaryDir temporary_;
    std::filesystem::path root_;
};

// Runs the Qt event loop until `condition` holds or `timeout` has passed. Returns whether it held.
// For code that works through signals, timers or queued calls.
[[nodiscard]] bool wait_until(const std::function<bool()>& condition,
                              std::chrono::milliseconds timeout = std::chrono::seconds(2));

}  // namespace cloudscope::test
