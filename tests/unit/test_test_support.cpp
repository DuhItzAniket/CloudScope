// Tests of the test infrastructure itself: fixtures, temporary folders, the Qt event loop inside Catch2.

#include "test_support.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QCryptographicHash>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace cloudscope::test;
using namespace std::chrono_literals;

TEST_CASE("fixtures match the manifest", "[support][fixtures]")
{
    constexpr std::uintmax_t kMaxFileBytes = 100U * 1024U;
    constexpr std::uintmax_t kMaxTotalBytes = 1024U * 1024U;

    const nlohmann::json manifest = nlohmann::json::parse(read_file(data_path("manifest.json")));
    std::set<std::string> listed;
    std::uintmax_t total = 0;
    for (const nlohmann::json& entry : manifest.at("files")) {
        const std::string name = entry.at("file");
        INFO("fixture: " << name);
        listed.insert(name);
        const std::string content = read_file(data_path(name));

        CHECK(content.size() == entry.at("bytes").get<std::size_t>());
        CHECK(content.size() <= kMaxFileBytes);
        total += content.size();
        const QByteArray digest = QCryptographicHash::hash(
            QByteArray::fromRawData(content.data(), static_cast<qsizetype>(content.size())), QCryptographicHash::Sha256);
        CHECK(digest.toHex().toStdString() == entry.at("sha256").get<std::string>());
        CHECK_FALSE(entry.at("source").get<std::string>().empty());
        CHECK(entry.at("licence") == "CC0-1.0");

        // The image decodes to what the manifest says.
        const std::vector<unsigned char> bytes(content.begin(), content.end());
        const cv::Mat image = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
        REQUIRE_FALSE(image.empty());
        CHECK(image.cols == entry.at("width").get<int>());
        CHECK(image.rows == entry.at("height").get<int>());
        CHECK(image.type() == CV_8UC3);
    }
    CHECK(total <= kMaxTotalBytes);
    CHECK(listed.size() == 3);

    // Nothing unlisted lies in the fixture folder.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data_dir())) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = std::filesystem::relative(entry.path(), data_dir()).generic_string();
        if (name == "manifest.json" || name == "README.md") {
            continue;
        }
        INFO("file in tests/data: " << name);
        CHECK(listed.count(name) == 1);
    }
}

TEST_CASE("a temporary workspace exists during the test and is removed afterwards", "[support]")
{
    std::filesystem::path root;
    {
        const TempWorkspace workspace;
        root = workspace.root();
        CHECK(std::filesystem::is_directory(root));
        const std::filesystem::path file = workspace.write("deep/er/note.txt", "line 1\nline 2\n");
        CHECK(file == workspace.path("deep/er/note.txt"));
        CHECK(read_file(file) == "line 1\nline 2\n");
        // The folder name is not plain ASCII, so tests exercise such paths without extra effort.
        const std::u8string name = root.filename().u8string();
        CHECK(std::any_of(name.begin(), name.end(), [](char8_t c) { return c > 0x7F; }));
    }
    CHECK_FALSE(std::filesystem::exists(root));
    CHECK(read_file(root / "deep/er/note.txt").empty());
}

TEST_CASE("the Qt event loop runs inside a test", "[support][qt]")
{
    QTimer timer;
    timer.setSingleShot(true);
    QSignalSpy fired(&timer, &QTimer::timeout);
    REQUIRE(fired.isValid());

    bool queued_call_ran = false;
    QTimer::singleShot(0, &timer, [&queued_call_ran] { queued_call_ran = true; });
    timer.start(20);
    CHECK(fired.count() == 0);  // nothing happens until the event loop runs

    CHECK(wait_until([&fired] { return fired.count() == 1; }));
    CHECK(queued_call_ran);
    CHECK(fired.wait(50) == false);  // single shot: it does not fire again
    CHECK(fired.count() == 1);
}

TEST_CASE("wait_until gives up after its timeout", "[support][qt]")
{
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(wait_until([] { return false; }, 100ms));
    const auto waited = std::chrono::steady_clock::now() - start;
    CHECK(waited >= 100ms);
    CHECK(waited < 2s);

    CHECK(wait_until([] { return true; }, 0ms));  // a condition that already holds needs no waiting
}
