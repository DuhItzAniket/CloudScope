#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/acquisition.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/capture/recording.hpp>
#include <cloudscope/capture/sequencer.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/sim/sim_devices.hpp>
#include <cloudscope/sim/sim_driver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;

namespace {

UtcTime at(std::string_view iso)
{
    const auto parsed = parse_iso8601(iso);
    REQUIRE(parsed);
    return *parsed;
}

// A simulated sky camera that renders a frame on every read (no real-time pacing), its acquisition thread and a
// sequencer that shares the camera's manual clock, so that a test chooses the time of day.
struct Rig {
    ManualClock clock{at("2026-10-09T10:00:00Z")};  // Bengaluru: Sun 36° high
    std::shared_ptr<sim::SimDriver> driver;
    std::shared_ptr<hal::ICamera> camera;
    std::shared_ptr<FrameHub> hub = std::make_shared<FrameHub>();
    std::unique_ptr<Acquisition> acquisition;
    std::unique_ptr<Sequencer> sequencer;

    explicit Rig(PixelFormat format = PixelFormat::Mjpeg, bool start = true)
    {
        sim::SimulationConfig config;
        config.camera.real_time = false;
        driver = *sim::SimDriver::make(clock, config);
        const auto device = driver->create(sim::kSkyCameraId);
        REQUIRE(outcome(device) == "ok");
        camera = std::dynamic_pointer_cast<hal::ICamera>(*device);
        REQUIRE(camera != nullptr);
        REQUIRE(outcome(camera->open()) == "ok");
        const auto caps = camera->capabilities();
        REQUIRE(outcome(caps) == "ok");
        const auto mode = std::ranges::find_if(
            caps->modes, [&](const hal::CameraMode& m) { return m.width == 640 && m.format == format; });
        REQUIRE(mode != caps->modes.end());
        REQUIRE(outcome(camera->set_mode(*mode)) == "ok");
        acquisition = std::make_unique<Acquisition>(camera, hub, clock);
        if (start) {
            REQUIRE(outcome(acquisition->start()) == "ok");
        }
        sequencer = std::make_unique<Sequencer>(camera, hub, clock);
        sequencer->set_site(
            SiteInfo{.id = "blr-roof", .latitude_deg = 12.97, .longitude_deg = 77.59, .altitude_m = 920.0});
    }
    ~Rig() { acquisition->stop(); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    // What an application does after the camera failed: make a fresh acquisition on the same hub.
    Expected<void> restart_acquisition()
    {
        acquisition->stop();
        acquisition = std::make_unique<Acquisition>(camera, hub, clock);
        return acquisition->start();
    }
};

std::vector<std::filesystem::path> files_with(const std::filesystem::path& folder, std::string_view extension)
{
    std::vector<std::filesystem::path> out;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() == extension) {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

nlohmann::json read_json(const std::filesystem::path& file)
{
    std::ifstream in(file);
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

}  // namespace

TEST_CASE("file name templates expand their tokens and refuse unknown ones", "[capture][sequencer]")
{
    const FilenameFields fields{.site = "blr-roof",
                                .camera = "uvc:0c45:636d:1",
                                .utc = at("2026-10-09T10:15:30.123Z"),
                                .sequence = 42,
                                .profile = "day",
                                .kind = "interval"};
    CHECK(expand_filename("{utc}_{seq}_{profile}", fields).value() == "20261009T101530_123Z_000042_day");
    CHECK(expand_filename("{site}_{utc}_{seq}_{profile}", fields).value() ==
          "blr-roof_20261009T101530_123Z_000042_day");
    CHECK(expand_filename("{site}/{date}/{camera}-{kind}", fields).value() ==
          "blr-roof/20261009/uvc-0c45-636d-1-interval");
    CHECK(expand_filename("plain", fields).value() == "plain");
    CHECK(expand_filename("{seq}", FilenameFields{}).value() == "000000");
    CHECK(expand_filename("{site}", FilenameFields{}).value() == "site");
    CHECK_FALSE(expand_filename("{nope}", fields));
    CHECK_FALSE(expand_filename("{utc", fields));
    CHECK_FALSE(expand_filename("utc}", fields));
    CHECK_FALSE(expand_filename("", fields));
    // The default template satisfies FR-REC-06: site id and UTC to the millisecond, sortable.
    const std::string a = expand_filename(CapturePlan{}.filename_template, fields).value();
    FilenameFields later = fields;
    later.utc += 1ms;
    later.sequence = 43;
    const std::string b = expand_filename(CapturePlan{}.filename_template, later).value();
    CHECK(a < b);
    CHECK(a.find("blr-roof") == 0);
    CHECK(a.find("_123Z_") != std::string::npos);
}

TEST_CASE("capture plans are validated before they run", "[capture][sequencer]")
{
    CapturePlan plan;
    plan.folder = "somewhere";
    CHECK(validate(plan));
    CHECK_FALSE(validate(CapturePlan{}));  // no folder
    plan.kind = CaptureKind::Burst;
    plan.count = 0;
    CHECK_FALSE(validate(plan));
    plan.kind = CaptureKind::Bracket;
    plan.bracket_stops.clear();
    CHECK_FALSE(validate(plan));
    plan = CapturePlan{};
    plan.folder = "somewhere";
    plan.kind = CaptureKind::Scheduled;
    CHECK_FALSE(validate(plan));  // no start
    plan.start_at = at("2026-10-09T10:00:00Z");
    plan.end_at = at("2026-10-09T09:00:00Z");
    CHECK_FALSE(validate(plan));  // ends before it starts
    plan.end_at = at("2026-10-09T11:00:00Z");
    CHECK(validate(plan));
    plan.duration = 0ms;
    CHECK_FALSE(validate(plan));
    plan.duration.reset();
    plan.max_consecutive_failures = 0;
    CHECK_FALSE(validate(plan));
    plan.max_consecutive_failures = 3;
    plan.day.jpeg_quality = 0;
    CHECK_FALSE(validate(plan));
    plan.day.jpeg_quality = 90;
    plan.night.exposure_ms = -1.0;
    CHECK_FALSE(validate(plan));
    plan.night.exposure_ms.reset();
    plan.filename_template = "{bogus}";
    CHECK_FALSE(validate(plan));
    CHECK(capture_kind_from_string("interval").value() == CaptureKind::Interval);
    CHECK_FALSE(capture_kind_from_string("sometimes"));
    CHECK(to_string(CaptureKind::Bracket) == "bracket");
}

namespace {

// An interval run of `count` pictures as fast as frames come, every picture checked for its sidecar.
void run_interval(std::uint32_t count)
{
    const TempWorkspace workspace;
    Rig rig;
    CapturePlan plan;
    plan.kind = CaptureKind::Interval;
    plan.count = count;
    plan.interval = 0ms;  // as fast as frames come
    plan.folder = workspace.path("run");
    plan.day.format = ImageFileFormat::Jpeg;  // MJPEG frames are stored as sent
    plan.filename_template = "{seq}_{profile}";
    rig.sequencer->set_session_id("test-session");

    const auto started = std::chrono::steady_clock::now();
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    const std::string last_error = stats->last_error ? stats->last_error->to_string() : std::string("no error");
    INFO(last_error);
    WARN(fmt::format(
        "{} pictures in {} ms", count,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
    CHECK(stats->written == count);
    CHECK(stats->failed == 0);
    CHECK(stats->profile_switches == 0);
    CHECK(stats->current_profile == "day");
    CHECK_FALSE(stats->stopped_by_request);
    CHECK_FALSE(stats->stopped_by_disk_guard);
    CHECK_FALSE(stats->stopped_by_failures);
    CHECK(stats->bytes > 0);

    const auto pictures = files_with(plan.folder, ".jpg");
    const auto sidecars = files_with(plan.folder, ".json");
    REQUIRE(pictures.size() == count);
    REQUIRE(sidecars.size() == count);
    const std::string last = fmt::format("{:06}_day.jpg", count - 1);
    CHECK(pictures.front().filename() == "000000_day.jpg");
    CHECK(pictures.back().filename() == last);
    CHECK(sidecars.back().filename() == last + ".json");
    CHECK(files_with(plan.folder, ".part").empty());

    const nlohmann::json sidecar = read_json(sidecars[count / 2]);
    CHECK(sidecar_schema().validate(sidecar).empty());
    CHECK(sidecar["file"]["format"] == "jpeg");
    CHECK(sidecar["file"]["width"] == 640);
    CHECK(sidecar["capture"]["simulated"] == true);
    CHECK(sidecar["camera"]["id"] == "sim:camera:sky");
    CHECK(sidecar["site"]["id"] == "blr-roof");
    CHECK_THAT(sidecar["sun"]["elevation_deg"].get<double>(), WithinAbs(36.0, 0.5));
    CHECK(sidecar["session_id"] == "test-session");
    CHECK(sidecar["statistics"].contains("mean"));
    // The stored bytes are the camera's own JPEG: the hash in the sidecar matches the file.
    CHECK(file_sha256(pictures[count / 2]).value() == sidecar["file"]["sha256"].get<std::string>());
    // Sequence numbers in the sidecars rise over the run.
    std::uint64_t previous = 0;
    for (const auto& file : {sidecars[0], sidecars[1], sidecars[count - 1]}) {
        const auto sequence = read_json(file)["capture"]["sequence"].get<std::uint64_t>();
        CHECK(sequence >= previous);
        previous = sequence;
    }
}

}  // namespace

TEST_CASE("a 100-frame interval run writes every picture with its sidecar", "[capture][sequencer]")
{
    run_interval(100);
}

// The exit criterion of P029 (1,000 pictures without error); about a minute in a Debug build, so hidden from the
// default run: cloudscope-unit-tests "[slow]".
TEST_CASE("a 1,000-frame interval run writes every picture with its sidecar", "[.][slow][capture][sequencer]")
{
    run_interval(1000);
}

TEST_CASE("a burst takes consecutive frames in order", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig(PixelFormat::Bgr8);
    CapturePlan plan;
    plan.kind = CaptureKind::Burst;
    plan.count = 10;
    plan.folder = workspace.path("burst");
    plan.day.format = ImageFileFormat::Png;
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    CHECK(stats->written == 10);
    CHECK(stats->failed == 0);
    const auto sidecars = files_with(plan.folder, ".json");
    REQUIRE(sidecars.size() == 10);
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < sidecars.size(); ++i) {
        const auto sequence = read_json(sidecars[i])["capture"]["sequence"].get<std::uint64_t>();
        if (i > 0) {
            CHECK(sequence > previous);
        }
        previous = sequence;
    }
    CHECK(files_with(plan.folder, ".png").size() == 10);
}

TEST_CASE("interval runs keep their cadence and skip missed slots instead of catching up", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig;
    CapturePlan plan;
    plan.kind = CaptureKind::Interval;
    plan.count = 5;
    plan.interval = 250ms;  // well above the time a picture takes in a Debug build with other tests running
    plan.folder = workspace.path("cadence");
    plan.day.format = ImageFileFormat::Jpeg;
    plan.filename_template = "{seq}";
    std::vector<std::chrono::steady_clock::time_point> taken;
    rig.sequencer->set_on_picture([&](const CapturedPicture&) {
        taken.push_back(std::chrono::steady_clock::now());
        if (taken.size() == 2) {
            std::this_thread::sleep_for(800ms);  // a slow consumer: about three slots pass
        }
    });
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    CHECK(stats->written == 5);
    CHECK(stats->missed_slots >= 2);
    CHECK(stats->missed_slots <= 4);
    REQUIRE(taken.size() == 5);
    // The run spans the slots taken plus the slots skipped: had the sequencer caught up in a burst instead, it
    // would have finished right after the sleep (about 250 + 800 ms plus three quick pictures).
    const auto slots = static_cast<int>(4 + stats->missed_slots);
    CHECK(stats->elapsed >= slots * 250ms - 100ms);
    CHECK(taken.back() - taken.front() >= slots * 250ms - 150ms);
}

TEST_CASE("the night profile takes over when the Sun sets below the threshold", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig;
    CapturePlan plan;
    plan.kind = CaptureKind::Interval;
    plan.count = 4;
    plan.folder = workspace.path("dusk");
    plan.day.format = ImageFileFormat::Jpeg;
    plan.night.format = ImageFileFormat::Png;
    plan.night.exposure_ms = 20.0;
    plan.night_below_sun_elevation_deg = -6.0;
    plan.filename_template = "{seq}_{profile}";
    plan.settle_frames = 1;

    int pictures = 0;
    rig.sequencer->set_on_picture([&](const CapturedPicture& picture) {
        ++pictures;
        if (pictures == 2) {
            rig.clock.set_utc(at("2026-10-09T23:00:00Z"));  // 04:30 local: Sun about 50° below the horizon
        }
        CHECK(picture.profile == (pictures <= 2 ? "day" : "night"));
    });
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    CHECK(stats->written == 4);
    CHECK(stats->failed == 0);
    CHECK(stats->profile_switches == 1);
    CHECK(stats->current_profile == "night");
    CHECK(stats->skipped == 1);  // one settling frame after the exposure change
    CHECK(files_with(plan.folder, ".jpg").size() == 2);
    CHECK(files_with(plan.folder, ".png").size() == 2);
    const auto night = files_with(plan.folder, ".png");
    CHECK(night.front().filename() == "000002_night.png");
    const nlohmann::json sidecar = read_json(plan.folder / "000003_night.png.json");
    CHECK_THAT(sidecar["camera"]["exposure_ms"].get<double>(), WithinAbs(20.0, 1.0));
    CHECK(sidecar["sun"]["elevation_deg"].get<double>() < -6.0);
    // The choice is the plan's: without a site the day profile always applies.
    CHECK(rig.sequencer->profile_for(plan, std::nullopt).name == "day");
    CHECK(rig.sequencer->profile_for(plan, -5.0).name == "day");
    CHECK(rig.sequencer->profile_for(plan, -7.0).name == "night");
}

TEST_CASE("a run pauses while the Sun is below the pause elevation and resumes after", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig;
    rig.clock.set_utc(at("2026-10-09T23:00:00Z"));  // night
    CapturePlan plan;
    plan.kind = CaptureKind::Interval;
    plan.count = 2;
    plan.folder = workspace.path("pause");
    plan.day.format = ImageFileFormat::Jpeg;
    plan.pause_below_sun_elevation_deg = 0.0;
    std::thread sunrise([&] {
        while (!rig.sequencer->is_running() || !rig.sequencer->stats().paused) {
            std::this_thread::sleep_for(1ms);
        }
        std::this_thread::sleep_for(60ms);
        rig.clock.set_utc(at("2026-10-09T10:00:00Z"));  // day
    });
    const auto stats = rig.sequencer->run(plan);
    sunrise.join();
    REQUIRE(outcome(stats) == "ok");
    CHECK(stats->written == 2);
    CHECK(stats->pauses == 1);
    CHECK_FALSE(stats->paused);
    CHECK(stats->elapsed >= 60ms);
    CHECK(stats->current_profile == "day");

    SECTION("a paused run still honours its duration")
    {
        rig.clock.set_utc(at("2026-10-09T23:00:00Z"));
        plan.duration = 120ms;
        const auto timed = rig.sequencer->run(plan);
        REQUIRE(outcome(timed) == "ok");
        CHECK(timed->written == 0);
        CHECK(timed->pauses == 1);
        CHECK(timed->elapsed >= 120ms);
        CHECK(timed->elapsed < 2000ms);
    }
}

TEST_CASE("a bracket takes one picture per stop around the current exposure", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig(PixelFormat::Bgr8);
    REQUIRE(outcome(rig.camera->set_control(hal::CameraControl::Exposure, {.value = 10.0, .automatic = false})) ==
            "ok");
    CapturePlan plan;
    plan.kind = CaptureKind::Bracket;
    plan.bracket_stops = {-2.0, 0.0, 2.0};
    plan.folder = workspace.path("bracket");
    plan.filename_template = "{seq}";
    plan.settle_frames = 1;
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    CHECK(stats->written == 3);
    CHECK(stats->failed == 0);
    CHECK(stats->skipped == 3);
    std::vector<double> exposures;
    for (const auto& file : files_with(plan.folder, ".json")) {
        exposures.push_back(read_json(file)["camera"]["exposure_ms"].get<double>());
    }
    REQUIRE(exposures.size() == 3);
    CHECK_THAT(exposures[0], WithinAbs(2.5, 0.5));
    CHECK_THAT(exposures[1], WithinAbs(10.0, 0.5));
    CHECK_THAT(exposures[2], WithinAbs(40.0, 1.0));
    // The exposure is back where it started.
    CHECK_THAT(rig.camera->control(hal::CameraControl::Exposure)->value, WithinAbs(10.0, 0.5));
}

TEST_CASE("scheduled runs wait for their start and honour their end and a stop request", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig;
    CapturePlan plan;
    plan.kind = CaptureKind::Scheduled;
    plan.folder = workspace.path("sched");
    plan.day.format = ImageFileFormat::Jpeg;
    plan.filename_template = "{seq}";

    SECTION("a start in the past runs at once up to the count")
    {
        plan.start_at = at("2026-10-09T09:59:00Z");
        plan.end_at = at("2026-10-09T12:00:00Z");
        plan.count = 3;
        const auto stats = rig.sequencer->run(plan);
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 3);
    }
    SECTION("an end already reached writes nothing")
    {
        plan.start_at = at("2026-10-09T09:00:00Z");
        plan.end_at = at("2026-10-09T09:30:00Z");
        plan.count = 0;
        const auto stats = rig.sequencer->run(plan);
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 0);
        CHECK_FALSE(stats->stopped_by_request);
    }
    SECTION("a duration ends an open-ended run")
    {
        plan.start_at = at("2026-10-09T09:00:00Z");
        plan.count = 0;
        plan.interval = 100ms;
        plan.duration = 700ms;
        const auto stats = rig.sequencer->run(plan);
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written >= 3);
        CHECK(stats->written <= 9);
        CHECK(stats->elapsed >= 700ms);
    }
    SECTION("waiting for a future start ends on request_stop()")
    {
        plan.start_at = at("2026-10-09T11:00:00Z");
        plan.count = 1;
        std::thread stopper([&] {
            while (!rig.sequencer->is_running()) {
                std::this_thread::sleep_for(1ms);
            }
            std::this_thread::sleep_for(60ms);
            rig.sequencer->request_stop();
        });
        const auto stats = rig.sequencer->run(plan);
        stopper.join();
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 0);
        CHECK(stats->stopped_by_request);
    }
    SECTION("advancing the clock past the start releases the run")
    {
        plan.start_at = at("2026-10-09T10:00:30Z");
        plan.count = 2;
        std::thread advancer([&] {
            while (!rig.sequencer->is_running()) {
                std::this_thread::sleep_for(1ms);
            }
            std::this_thread::sleep_for(30ms);
            rig.clock.set_utc(at("2026-10-09T10:01:00Z"));
        });
        const auto stats = rig.sequencer->run(plan);
        advancer.join();
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 2);
    }
}

TEST_CASE("a camera that fails is brought back with backoff and the run continues", "[capture][sequencer]")
{
    const TempWorkspace workspace;
    Rig rig;
    CapturePlan plan;
    plan.kind = CaptureKind::Interval;
    plan.count = 6;
    plan.folder = workspace.path("recover");
    plan.day.format = ImageFileFormat::Jpeg;
    plan.filename_template = "{seq}";
    plan.frame_timeout = 200ms;
    int pictures = 0;
    rig.sequencer->set_on_picture([&](const CapturedPicture&) {
        if (++pictures == 2) {
            rig.driver->rig()->set_camera_connected(false);  // the cable comes out
        }
    });
    int attempts = 0;
    rig.sequencer->set_recovery(
        [&]() -> Expected<void> {
            ++attempts;
            if (attempts < 2) {
                return fail(ErrorCode::Unavailable, "still unplugged");
            }
            rig.driver->rig()->set_camera_connected(true);
            if (!rig.camera->is_streaming()) {
                return rig.restart_acquisition();
            }
            return {};
        },
        20ms, 80ms);
    const auto stats = rig.sequencer->run(plan);
    REQUIRE(outcome(stats) == "ok");
    const std::string last_error = stats->last_error ? stats->last_error->to_string() : std::string("no error");
    INFO(last_error);
    CHECK(stats->written == 6);
    CHECK(stats->recoveries == 1);
    CHECK(stats->failed == 1);
    CHECK(attempts == 2);
    CHECK_FALSE(stats->stopped_by_failures);
    CHECK(files_with(plan.folder, ".jpg").size() == 6);
    CHECK(files_with(plan.folder, ".jpg").back().filename() == "000005.jpg");
}

TEST_CASE("the disk guard, a silent camera and repeated failures end a run without pretending success",
          "[capture][sequencer]")
{
    const TempWorkspace workspace;
    SECTION("the disk guard")
    {
        Rig rig;
        CapturePlan plan;
        plan.folder = workspace.path("guard");
        plan.min_free_bytes = std::numeric_limits<std::uintmax_t>::max();
        const auto stats = rig.sequencer->run(plan);
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 0);
        CHECK(stats->stopped_by_disk_guard);
    }
    SECTION("no frames arrive and no recovery is set")
    {
        Rig rig(PixelFormat::Mjpeg, false);  // acquisition never started
        CapturePlan plan;
        plan.folder = workspace.path("silent");
        plan.frame_timeout = 150ms;
        const auto stats = rig.sequencer->run(plan);
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 0);
        CHECK(stats->failed == 1);
        CHECK(stats->stopped_by_failures);
        REQUIRE(stats->last_error);
        CHECK(stats->last_error->code == ErrorCode::Timeout);
    }
    SECTION("a camera that keeps failing to come back ends the run on request_stop()")
    {
        Rig rig;
        rig.driver->rig()->set_camera_connected(false);
        CapturePlan plan;
        plan.folder = workspace.path("gone");
        plan.frame_timeout = 100ms;
        plan.kind = CaptureKind::Interval;
        plan.count = 3;
        std::atomic<int> attempts{0};  // written by the run, read by the stopping thread
        rig.sequencer->set_recovery(
            [&]() -> Expected<void> {
                ++attempts;
                return fail(ErrorCode::Unavailable, "no camera");
            },
            10ms, 40ms);
        std::thread stopper([&] {
            while (attempts < 3) {
                std::this_thread::sleep_for(1ms);
            }
            rig.sequencer->request_stop();
        });
        const auto stats = rig.sequencer->run(plan);
        stopper.join();
        REQUIRE(outcome(stats) == "ok");
        CHECK(stats->written == 0);
        CHECK(stats->stopped_by_request);
        CHECK(stats->recoveries == 0);
        CHECK(attempts >= 3);
    }
    SECTION("a second run while one is in progress is refused")
    {
        Rig rig;
        CapturePlan plan;
        plan.kind = CaptureKind::Scheduled;
        plan.start_at = at("2026-10-09T11:00:00Z");
        plan.folder = workspace.path("busy");
        std::thread runner([&] { (void)rig.sequencer->run(plan); });
        while (!rig.sequencer->is_running()) {
            std::this_thread::sleep_for(1ms);
        }
        const auto second = rig.sequencer->run(plan);
        REQUIRE_FALSE(second);
        CHECK(second.error().code == ErrorCode::Unavailable);
        rig.sequencer->request_stop();
        runner.join();
        CHECK_FALSE(rig.sequencer->is_running());
    }
}
