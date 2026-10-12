// P040: the session browser over a catalogue of 100,000 frames, thumbnails and export.

#include "desktop_fixture.hpp"
#include "session_browser.hpp"

#include <cloudscope/capture/recording.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <QtWidgets/QTableView>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <fstream>

using namespace std::chrono_literals;
using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::wait_until;

namespace {

FrameEntry entry(const std::string& session, UtcTime utc, std::uint64_t sequence, const std::filesystem::path& path)
{
    return FrameEntry{.session_id = session,
                      .path = path,
                      .utc = utc,
                      .mjd = modified_julian_date(utc),
                      .sequence = sequence,
                      .format = sequence % 7 == 0 ? "fits" : "jpeg",
                      .bytes = 1000 + sequence,
                      .sha256 = fmt::format("{:064x}", sequence),
                      .width = 640,
                      .height = 480,
                      .exposure_ms = 10.0,
                      .profile = {},
                      .simulated = sequence % 2 == 0};
}

std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}

}  // namespace

TEST_CASE("a catalogue of 100,000 frames opens at once and is browsed in pages", "[desktop][session_browser]")
{
    DesktopFixture fixture;
    const std::filesystem::path root = fixture.context->settings().session_root;
    std::filesystem::create_directories(root);
    const UtcTime t0 = *parse_iso8601("2026-01-01T00:00:00Z");
    {
        auto catalogue = Catalogue::open(root / "catalogue.sqlite");
        REQUIRE(catalogue.has_value());
        std::vector<FrameEntry> bulk;
        bulk.reserve(100'000);
        for (std::uint64_t i = 0; i < 100'000; ++i) {
            bulk.push_back(entry(fmt::format("s{}", i / 10'000), t0 + std::chrono::seconds(i), i,
                                 root / fmt::format("frame_{:06}.jpg", i)));
        }
        REQUIRE((*catalogue)->add_frames(bulk).has_value());
        for (int s = 0; s < 10; ++s) {
            SessionInfo info;
            info.id = fmt::format("s{}", s);
            info.folder = root / info.id;
            REQUIRE((*catalogue)->add_session(info).has_value());
        }
    }
    const auto opened_at = std::chrono::steady_clock::now();
    SessionBrowser browser(root, "utc");
    const auto open_ms = elapsed_since(opened_at);
    INFO("opening the browser took " << open_ms.count() << " ms");
    CHECK(open_ms < 2000ms);
    CatalogueModel* model = browser.model();
    REQUIRE(model->rowCount() == static_cast<int>(CatalogueModel::kPageSize));
    CHECK(model->canFetchMore(QModelIndex()));
    // Newest first.
    CHECK(model->entry(0).sequence == 99'999);
    CHECK(model->entry(1).sequence == 99'998);
    const auto page_at = std::chrono::steady_clock::now();
    for (int page = 0; page < 5; ++page) {
        model->fetchMore(QModelIndex());
    }
    const auto pages_ms = elapsed_since(page_at);
    INFO("five more pages took " << pages_ms.count() << " ms");
    CHECK(pages_ms < 1500ms);
    CHECK(model->rowCount() == 6 * static_cast<int>(CatalogueModel::kPageSize));
    CHECK(model->entry(model->rowCount() - 1).sequence == 100'000 - 3000);
    CHECK(model->sessions().size() == 10);

    // Filters: one session, measured frames only, one format.
    BrowserFilter filter;
    filter.session_id = "s3";
    model->set_filter(filter);
    REQUIRE(model->rowCount() > 0);
    for (int row = 0; row < model->rowCount(); ++row) {
        CHECK(model->entry(row).session_id == "s3");
    }
    filter.include_simulated = false;
    model->set_filter(filter);
    for (int row = 0; row < model->rowCount(); ++row) {
        CHECK_FALSE(model->entry(row).simulated);
    }
    filter.format = "fits";
    filter.from = t0 + std::chrono::seconds(30'000);
    filter.to = t0 + std::chrono::seconds(30'100);
    model->set_filter(filter);
    for (int row = 0; row < model->rowCount(); ++row) {
        CHECK(model->entry(row).format == "fits");
        CHECK(model->entry(row).sequence % 7 == 0);
    }
    CHECK(model->rowCount() == 7);  // 30000..30099 in s3: odd multiples of 7 (even ones are simulated)
    CHECK(model->data(model->index(0, CatalogueModel::TimeColumn)).toString().contains(QStringLiteral("UTC")));
}

TEST_CASE("thumbnails are decoded and an export copies pictures, sidecars and a manifest", "[desktop][session_browser]")
{
    DesktopFixture fixture;
    const std::filesystem::path root = fixture.context->settings().session_root;
    std::filesystem::create_directories(root / "frames");
    std::vector<FrameEntry> frames;
    const UtcTime t0 = *parse_iso8601("2026-03-01T00:00:00Z");
    for (int i = 0; i < 2; ++i) {
        cv::Mat picture(300, 400, CV_8UC3, cv::Scalar(10 * i, 100, 200));
        const std::filesystem::path path = root / "frames" / fmt::format("pic{}.png", i);
        std::vector<unsigned char> bytes;
        REQUIRE(cv::imencode(".png", picture, bytes));
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));  // NOLINT
        out.close();
        if (i == 0) {
            std::ofstream sidecar(std::filesystem::path(path).concat(".json"));
            sidecar << R"({"schema": "cloudscope.frame/1"})";
        }
        frames.push_back(entry("s0", t0 + std::chrono::seconds(i), static_cast<std::uint64_t>(i), path));
        frames.back().format = "png";
    }
    const QImage thumbnail = load_thumbnail(frames[0].path, 96);
    REQUIRE_FALSE(thumbnail.isNull());
    CHECK(thumbnail.width() == 96);
    CHECK(thumbnail.height() == 72);
    CHECK(load_thumbnail(root / "missing.png", 96).isNull());

    const std::filesystem::path target = fixture.workspace.path("export");
    const auto result = export_frames(frames, target);
    REQUIRE(result.has_value());
    CHECK(result->files == 2);
    CHECK(result->sidecars == 1);
    CHECK(result->failed == 0);
    CHECK(std::filesystem::exists(target / "pic0.png"));
    CHECK(std::filesystem::exists(target / "pic0.png.json"));
    CHECK(std::filesystem::exists(target / "pic1.png"));
    REQUIRE(std::filesystem::exists(result->manifest));
    std::ifstream in(result->manifest);
    const nlohmann::json manifest = nlohmann::json::parse(in);
    CHECK(manifest.at("schema") == "cloudscope.export/1");
    REQUIRE(manifest.at("files").size() == 2);
    CHECK(manifest.at("files")[0].at("sha256") == frames[0].sha256);
    CHECK(manifest.at("files")[0].at("sidecar") == "pic0.png.json");

    // A missing file is counted, not fatal.
    frames.push_back(entry("s0", t0 + std::chrono::seconds(5), 5, root / "frames" / "gone.png"));
    const auto partial = export_frames(frames, fixture.workspace.path("export2"));
    REQUIRE(partial.has_value());
    CHECK(partial->failed == 1);
    CHECK(partial->files == 2);

    // The browser lists them with thumbnails arriving in the background.
    {
        auto catalogue = Catalogue::open(root / "catalogue.sqlite");
        REQUIRE(catalogue.has_value());
        frames.pop_back();
        REQUIRE((*catalogue)->add_frames(frames).has_value());
    }
    SessionBrowser browser(root, "both");
    REQUIRE(browser.model()->rowCount() == 2);
    static_cast<void>(browser.model()->data(browser.model()->index(0, CatalogueModel::FileColumn), Qt::DecorationRole));
    REQUIRE(wait_until([&] { return !browser.model()->thumbnail(0).isNull(); }, 10s));
    CHECK(browser.model()->thumbnail(0).width() == 96);
    browser.table()->selectAll();
    CHECK(browser.selected_frames().size() == 2);
}
