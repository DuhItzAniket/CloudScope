#include "test_support.hpp"

#include <QtTest/QTest>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

namespace cloudscope::test {

std::filesystem::path data_dir()
{
    return std::filesystem::path(std::u8string_view(u8"" CLOUDSCOPE_TEST_DATA_DIR));
}

std::filesystem::path data_path(std::string_view name)
{
    const std::filesystem::path path = data_dir() / std::filesystem::path(name);
    INFO("fixture: " << name);
    REQUIRE(std::filesystem::is_regular_file(path));
    return path;
}

std::string read_file(const std::filesystem::path& path)
{
    const std::ifstream stream(path, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

TempWorkspace::TempWorkspace()
{
    REQUIRE(temporary_.isValid());
    root_ = std::filesystem::path(temporary_.path().toStdU16String()) / std::filesystem::path(u8"espace été");
    std::filesystem::create_directories(root_);
}

std::filesystem::path TempWorkspace::path(std::string_view relative) const
{
    return root_ / std::filesystem::path(relative);
}

std::filesystem::path TempWorkspace::write(std::string_view relative, std::string_view text) const
{
    const std::filesystem::path file = path(relative);
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << text;
    stream.close();
    REQUIRE(stream.good());
    return file;
}

bool wait_until(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    return QTest::qWaitFor(condition, static_cast<int>(timeout.count()));
}

}  // namespace cloudscope::test
