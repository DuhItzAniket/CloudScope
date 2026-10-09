#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/recording.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/session/catalogue.hpp>
#include <cloudscope/session/session.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/format.h>
#include <opencv2/core.hpp>

#include <chrono>
#include <fstream>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;

namespace {

UtcTime at(std::string_view iso)
{
    const auto parsed = parse_iso8601(iso);
    REQUIRE(parsed);
    return *parsed;
}

const SiteInfo kSite{.id = "blr-roof", .latitude_deg = 12.97, .longitude_deg = 77.59, .altitude_m = 920.0};
const hal::DeviceInfo kCamera{.id = "sim:camera:sky",
                              .kind = hal::DeviceKind::Camera,
                              .name = "Simulated sky camera",
                              .driver = "sim",
                              .simulated = true};

// An absolute picture path that exists on no disk: the catalogue stores paths as text, so the test uses the same
// spelling the catalogue does (absolute, normalised) on every platform.
std::filesystem::path picture_path(const std::string& session, std::uint64_t sequence)
{
    return std::filesystem::absolute(std::filesystem::path("data") / session / fmt::format("{}.jpg", sequence))
        .lexically_normal();
}

FrameEntry entry(const std::string& session, UtcTime utc, std::uint64_t sequence, std::uint64_t bytes = 1000)
{
    FrameEntry frame;
    frame.session_id = session;
    frame.path = picture_path(session, sequence);
    frame.utc = utc;
    frame.mjd = modified_julian_date(utc);
    frame.sequence = sequence;
    frame.format = "jpeg";
    frame.bytes = bytes;
    frame.sha256 = std::string(64, 'a');
    frame.width = 1920;
    frame.height = 1080;
    frame.exposure_ms = 15.625;
    frame.mean = 120.5;
    frame.sun_elevation_deg = 30.0;
    frame.profile = "day";
    frame.simulated = true;
    return frame;
}

// Writes a small picture with a sidecar, as the sequencer would.
CapturedPicture write_test_picture(const std::filesystem::path& folder, const std::string& name, UtcTime utc,
                                   std::uint64_t sequence)
{
    CaptureRecord record;
    record.info.sequence = sequence;
    record.info.captured.utc = utc;
    record.info.width = 16;
    record.info.height = 8;
    record.info.format = PixelFormat::Bgr8;
    record.info.simulated = true;
    record.mode = {.width = 16, .height = 8, .format = PixelFormat::Bgr8, .fps = 30.0};
    record.camera_id = kCamera.id;
    record.camera_name = kCamera.name;
    record.exposure_ms = 10.0;
    record.site = kSite;
    record.sun = SunInfo{.azimuth_deg = 100.0, .elevation_deg = 20.0};
    cv::Mat image(8, 16, CV_8UC3, cv::Scalar(10, 20, static_cast<double>(sequence)));
    record.statistics = compute_statistics(image);
    const auto written = write_picture(image, ImageFileFormat::Png, folder / (name + ".png"), record);
    REQUIRE(outcome(written) == "ok");
    REQUIRE(outcome(write_sidecar(written->path, sidecar_json(record, *written, ImageFileFormat::Png))) == "ok");
    return CapturedPicture{.file = *written, .record = record, .profile = "day"};
}

}  // namespace

TEST_CASE("a session has its folder layout and a manifest that reads back", "[session]")
{
    const TempWorkspace workspace;
    const UtcTime started = at("2026-10-09T10:15:30.123Z");
    CHECK(session_id(started, "blr-roof") == "20261009T101530Z-blr-roof");
    CHECK(session_id(started, "") == "20261009T101530Z-site");
    CHECK(session_folder("root", "blr-roof", started) ==
          std::filesystem::path("root") / "blr-roof" / "2026-10-09" / "20261009T101530Z-blr-roof");

    auto created = Session::create(workspace.path("data"), kSite, kCamera, started);
    REQUIRE(outcome(created) == "ok");
    Session& session = *created;
    CHECK(session.info().id == "20261009T101530Z-blr-roof");
    CHECK(std::filesystem::is_directory(session.frames_folder()));
    CHECK(std::filesystem::is_directory(session.calibration_folder()));
    CHECK(std::filesystem::is_directory(session.logs_folder()));
    CHECK(std::filesystem::is_regular_file(session.manifest_path()));
    CHECK(session.info().folder == workspace.path("data") / "blr-roof" / "2026-10-09" / "20261009T101530Z-blr-roof");

    const nlohmann::json manifest = session.manifest();
    CHECK(session_schema().validate(manifest).empty());
    CHECK(manifest["schema"] == "cloudscope.session/1");
    CHECK(manifest["started_utc"] == "2026-10-09T10:15:30.123+00:00");
    CHECK_FALSE(manifest.contains("ended_utc"));

    // Pictures are counted; the manifest is saved every N pictures and on close.
    WrittenFile file;
    file.bytes = 1234;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(session.record_picture(file, 2));
    }
    session.set_notes("first light");
    REQUIRE(session.close(started + 1h));
    const auto opened = Session::open(session.info().folder);
    REQUIRE(outcome(opened) == "ok");
    CHECK(opened->info().id == session.info().id);
    CHECK(opened->info().frames == 5);
    CHECK(opened->info().bytes == static_cast<std::uint64_t>(5) * 1234);
    CHECK(opened->info().notes == "first light");
    CHECK(opened->info().ended == started + 1h);
    CHECK(opened->info().site.latitude_deg == 12.97);
    CHECK(opened->info().camera_id == "sim:camera:sky");
    CHECK_FALSE(std::filesystem::exists(session.info().folder / "session.json.part"));

    // The same start at the same site is already a session; a folder without a manifest is not one.
    CHECK(Session::create(workspace.path("data"), kSite, kCamera, started).error().code == ErrorCode::AlreadyExists);
    CHECK(Session::open(workspace.path("nothing")).error().code == ErrorCode::NotFound);
    // A manifest missing a required part is refused.
    nlohmann::json broken = manifest;
    broken.erase("site");
    CHECK(session_info_from_json(broken, "x").error().code == ErrorCode::Validation);
}

TEST_CASE("the catalogue stores sessions and frames and answers time queries", "[session][catalogue]")
{
    const TempWorkspace workspace;
    auto opened = Catalogue::open(workspace.path("db/catalogue.sqlite"));
    REQUIRE(outcome(opened) == "ok");
    Catalogue& catalogue = **opened;
    CHECK(catalogue.count().value() == 0);
    CHECK(catalogue.total_bytes().value() == 0);

    SessionInfo session;
    session.id = "20261009T100000Z-blr-roof";
    session.site = kSite;
    session.camera_id = kCamera.id;
    session.camera_name = kCamera.name;
    session.started = at("2026-10-09T10:00:00Z");
    session.folder = workspace.path("data/s1");
    REQUIRE(catalogue.add_session(session));
    session.frames = 3;
    session.ended = at("2026-10-09T10:00:03Z");
    REQUIRE(catalogue.add_session(session));  // update
    const auto sessions = catalogue.sessions();
    REQUIRE(outcome(sessions) == "ok");
    REQUIRE(sessions->size() == 1);
    CHECK(sessions->front().frames == 3);
    CHECK(sessions->front().ended == session.ended);
    CHECK(sessions->front().folder == workspace.path("data/s1"));
    CHECK(catalogue.session("nope").value() == std::nullopt);
    const auto found = catalogue.session(session.id);
    REQUIRE(outcome(found) == "ok");
    INFO("looking for '" << session.id << "'; the catalogue lists '" << sessions->front().id << "'");
    REQUIRE(found->has_value());
    CHECK((*found)->site.id == "blr-roof");

    const UtcTime t0 = at("2026-10-09T10:00:00Z");
    for (std::uint64_t i = 0; i < 3; ++i) {
        const auto id = catalogue.add_frame(entry(session.id, t0 + std::chrono::seconds(i), i, 500 + i));
        REQUIRE(outcome(id) == "ok");
        CHECK(*id > 0);
    }
    CHECK(catalogue.count().value() == 3);
    CHECK(catalogue.count(session.id).value() == 3);
    CHECK(catalogue.count("other").value() == 0);
    CHECK(catalogue.total_bytes().value() == 500 + 501 + 502);

    const auto middle = catalogue.frames({.from = t0 + 1s, .to = t0 + 2s});
    REQUIRE(outcome(middle) == "ok");
    REQUIRE(middle->size() == 1);
    const FrameEntry& frame = middle->front();
    CHECK(frame.sequence == 1);
    CHECK(frame.utc == t0 + 1s);
    CHECK(frame.path == picture_path(session.id, 1));
    CHECK(frame.exposure_ms.value() == 15.625);
    CHECK_FALSE(frame.gain);
    CHECK(frame.mean.value() == 120.5);
    CHECK(frame.sun_elevation_deg.value() == 30.0);
    CHECK_FALSE(frame.sun_azimuth_deg);
    CHECK(frame.profile == "day");
    CHECK(frame.simulated);
    CHECK(frame.sha256 == std::string(64, 'a'));

    const auto newest = catalogue.frames({.limit = 2, .newest_first = true});
    REQUIRE(newest->size() == 2);
    CHECK(newest->front().sequence == 2);
    CHECK(catalogue.frames({.session_id = "other"})->empty());
    // The same path again replaces the row.
    REQUIRE(catalogue.add_frame(entry(session.id, t0 + 1s, 1, 9999)));
    CHECK(catalogue.count().value() == 3);
    const auto replaced = catalogue.frame_at(picture_path(session.id, 1));
    REQUIRE(outcome(replaced) == "ok");
    REQUIRE(replaced->has_value());
    CHECK((*replaced)->bytes == 9999);

    // Reopening sees the same data.
    opened->reset();
    auto again = Catalogue::open(workspace.path("db/catalogue.sqlite"));
    REQUIRE(outcome(again) == "ok");
    CHECK((*again)->count().value() == 3);
}

TEST_CASE("a query over 100,000 catalogued frames answers within 100 ms", "[session][catalogue]")
{
    const TempWorkspace workspace;
    auto opened = Catalogue::open(workspace.path("big.sqlite"));
    REQUIRE(outcome(opened) == "ok");
    Catalogue& catalogue = **opened;
    const UtcTime t0 = at("2026-01-01T00:00:00Z");
    std::vector<FrameEntry> bulk;
    bulk.reserve(100'000);
    for (std::uint64_t i = 0; i < 100'000; ++i) {
        bulk.push_back(entry(fmt::format("s{}", i / 10'000), t0 + std::chrono::seconds(i), i));
    }
    const auto insert_started = std::chrono::steady_clock::now();
    REQUIRE(outcome(catalogue.add_frames(bulk)) == "ok");
    const auto insert_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - insert_started);
    INFO("inserting 100,000 frames took " << insert_ms.count() << " ms");
    REQUIRE(catalogue.count().value() == 100'000);

    // An hour in the middle: 3,600 frames. Timed as the best of three runs (the first warms SQLite's page cache).
    std::chrono::milliseconds query_ms{1'000'000};
    std::vector<FrameEntry> hour;
    for (int run = 0; run < 3; ++run) {
        const auto query_started = std::chrono::steady_clock::now();
        auto result = catalogue.frames({.from = t0 + 50'000s, .to = t0 + 53'600s, .limit = 10'000});
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - query_started);
        REQUIRE(outcome(result) == "ok");
        query_ms = std::min(query_ms, elapsed);
        hour = std::move(*result);
    }
    CHECK(hour.size() == 3600);
    CHECK(hour.front().sequence == 50'000);
    INFO("the time-range query took " << query_ms.count() << " ms");
#ifdef NDEBUG
    CHECK(query_ms < 100ms);  // the exit criterion, on the build that ships
#else
    CHECK(query_ms < 1000ms);  // a Debug build with checked iterators is several times slower; the number is reported
#endif

    const auto count_started = std::chrono::steady_clock::now();
    CHECK(catalogue.count("s7").value() == 10'000);
    const auto newest = catalogue.frames({.limit = 100, .newest_first = true});
    CHECK(newest->front().sequence == 99'999);
    const auto count_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - count_started);
    INFO("count + newest-100 took " << count_ms.count() << " ms");
    CHECK(count_ms < 100ms);
    WARN(fmt::format("catalogue timings: insert 100k {} ms, hour query {} ms, count+newest {} ms", insert_ms.count(),
                     query_ms.count(), count_ms.count()));
}

TEST_CASE("retention removes the oldest pictures with their sidecars and rows", "[session][catalogue]")
{
    const TempWorkspace workspace;
    const std::filesystem::path folder = workspace.path("frames");
    std::filesystem::create_directories(folder);
    auto opened = Catalogue::open(workspace.path("ret.sqlite"));
    REQUIRE(outcome(opened) == "ok");
    Catalogue& catalogue = **opened;
    const UtcTime t0 = at("2026-10-09T10:00:00Z");
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t i = 0; i < 6; ++i) {
        const CapturedPicture picture =
            write_test_picture(folder, fmt::format("f{}", i), t0 + std::chrono::minutes(i), i);
        sizes.push_back(picture.file.bytes);
        REQUIRE(catalogue.add_frame(frame_entry(picture, "s")));
    }
    REQUIRE(catalogue.count().value() == 6);
    const auto sum = [&](std::size_t from, std::size_t to) {
        std::uint64_t bytes = 0;
        for (std::size_t i = from; i < to; ++i) {
            bytes += sizes[i];
        }
        return bytes;
    };
    const std::uint64_t total = catalogue.total_bytes().value();
    CHECK(total == sum(0, 6));

    SECTION("by size: keeps the newest within the budget")
    {
        const auto result = catalogue.apply_retention({.max_bytes = sum(3, 6)}, t0 + 1h);
        REQUIRE(outcome(result) == "ok");
        CHECK(result->removed_frames == 3);
        CHECK(result->removed_bytes == sum(0, 3));
        CHECK(result->missing_files == 0);
        CHECK(catalogue.count().value() == 3);
        CHECK(catalogue.total_bytes().value() == sum(3, 6));
        CHECK_FALSE(std::filesystem::exists(folder / "f0.png"));
        CHECK_FALSE(std::filesystem::exists(folder / "f0.png.json"));
        CHECK(std::filesystem::exists(folder / "f3.png"));
        CHECK(std::filesystem::exists(folder / "f5.png.json"));
        CHECK(catalogue.frames({})->front().sequence == 3);
    }
    SECTION("by age: removes what is older than the limit")
    {
        const auto result =
            catalogue.apply_retention({.max_age = 1h}, t0 + 1h + 2min + 30s);  // f0, f1, f2 are > 1 h old
        REQUIRE(outcome(result) == "ok");
        CHECK(result->removed_frames == 3);
        CHECK(catalogue.count().value() == 3);
        CHECK(std::filesystem::exists(folder / "f3.png"));
        CHECK_FALSE(std::filesystem::exists(folder / "f2.png"));
    }
    SECTION("a missing file is counted, not an error; nothing to do is nothing")
    {
        std::filesystem::remove(folder / "f0.png");
        const auto result = catalogue.apply_retention({.max_bytes = sum(1, 6)}, t0 + 1h);
        REQUIRE(outcome(result) == "ok");
        CHECK(result->removed_frames == 1);
        CHECK(result->missing_files == 1);
        CHECK(catalogue.apply_retention({}, t0)->removed_frames == 0);
        CHECK(catalogue.apply_retention({.max_bytes = 100 * total}, t0)->removed_frames == 0);
    }
    SECTION("rows only, when the files are kept elsewhere")
    {
        const auto result = catalogue.apply_retention({.max_bytes = sum(4, 6)}, t0 + 1h, false);
        REQUIRE(outcome(result) == "ok");
        CHECK(result->removed_frames == 4);
        CHECK(std::filesystem::exists(folder / "f0.png"));
    }
}

TEST_CASE("a folder of pictures is indexed from its sidecars", "[session][catalogue]")
{
    const TempWorkspace workspace;
    const std::filesystem::path folder = workspace.path("session/frames");
    std::filesystem::create_directories(folder);
    const UtcTime t0 = at("2026-10-09T10:00:00Z");
    for (std::uint64_t i = 0; i < 4; ++i) {
        (void)write_test_picture(folder, fmt::format("p{}", i), t0 + std::chrono::seconds(i), i);
    }
    workspace.write("session/frames/notes.json", R"({"schema": "something.else/1"})");
    workspace.write("session/frames/orphan.png.json", "{}");
    auto opened = Catalogue::open(workspace.path("idx.sqlite"));
    REQUIRE(outcome(opened) == "ok");
    Catalogue& catalogue = **opened;
    const auto added = catalogue.index_folder(folder, "s-indexed");
    REQUIRE(outcome(added) == "ok");
    CHECK(*added == 4);
    CHECK(catalogue.count("s-indexed").value() == 4);
    const auto frames = catalogue.frames({.session_id = "s-indexed"});
    REQUIRE(frames->size() == 4);
    CHECK(frames->at(2).sequence == 2);
    CHECK(frames->at(2).utc == t0 + 2s);
    CHECK(frames->at(2).format == "png");
    CHECK(frames->at(2).path == std::filesystem::absolute(folder / "p2.png"));
    CHECK(frames->at(2).exposure_ms.value() == 10.0);
    CHECK(frames->at(2).sun_elevation_deg.value() == 20.0);
    CHECK(frames->at(2).mean.has_value());
    CHECK(frames->at(2).width == 16);
    CHECK(frames->at(2).simulated);
    CHECK(frames->at(2).sha256 == file_sha256(folder / "p2.png").value());
    // Indexing again adds nothing; a new picture is picked up.
    CHECK(catalogue.index_folder(folder, "s-indexed").value() == 0);
    (void)write_test_picture(folder, "p4", t0 + 4s, 4);
    CHECK(catalogue.index_folder(folder, "s-indexed").value() == 1);
    CHECK(catalogue.count().value() == 5);
    CHECK(catalogue.index_folder(workspace.path("missing"), "x").error().code == ErrorCode::NotFound);
}
