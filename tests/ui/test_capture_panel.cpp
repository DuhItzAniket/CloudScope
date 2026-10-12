// P039: the capture panel drives the sequencer end to end on the simulated camera.

#include "capture_panel.hpp"
#include "desktop_fixture.hpp"

#include <cloudscope/session/catalogue.hpp>
#include <cloudscope/sim/sim_devices.hpp>

#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

using namespace std::chrono_literals;
using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::wait_until;

namespace {

int count_files(const std::filesystem::path& folder, const std::string& extension)
{
    int count = 0;
    if (!std::filesystem::exists(folder)) {
        return 0;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("a burst from the panel writes pictures, sidecars, a session and catalogue rows", "[desktop][capture_panel]")
{
    DesktopFixture fixture;
    CameraService service(fixture.context->registry(), fixture.context->clock());
    const std::filesystem::path session_root = fixture.context->settings().session_root;
    CaptureDefaults defaults{.folder = fixture.workspace.path("captures"),
                             .session_root = session_root,
                             .site = fixture.context->settings().site,
                             .pointing = std::nullopt,
                             .calibration_id = std::string()};
    CapturePanel panel(service, fixture.context->clock(), defaults);
    CHECK(panel.plan().has_value());

    panel.start();  // no camera: refused with a notice, nothing runs
    CHECK_FALSE(panel.is_running());

    service.open(std::string(sim::kSkyCameraId));
    REQUIRE(wait_until([&] { return service.is_open() && !service.state().busy; }, 5s));
    const std::vector<hal::CameraMode> modes = service.state().capabilities.modes;
    const auto small = std::ranges::find_if(
        modes, [](const hal::CameraMode& m) { return m.width == 640 && m.format == PixelFormat::Mjpeg; });
    REQUIRE(small != modes.end());
    service.set_mode(*small);
    REQUIRE(wait_until([&] { return !service.state().busy; }, 5s));

    panel.set_plan_controls(CaptureKind::Burst, 3, 1000, ImageFileFormat::Jpeg);
    panel.set_use_session(true);
    const auto plan = panel.plan();
    REQUIRE(plan.has_value());
    CHECK(plan->kind == CaptureKind::Burst);
    CHECK(plan->count == 3);

    int written_signals = 0;
    QObject::connect(&panel, &CapturePanel::pictureWritten, &panel, [&](const QString&) { ++written_signals; });
    panel.start_button()->click();
    REQUIRE(wait_until([&] { return panel.is_running(); }, 5s));
    CHECK(panel.start_button()->text() == QStringLiteral("Stop"));
    REQUIRE(wait_until([&] { return !panel.is_running(); }, 30s));
    const SequencerStats stats = panel.last_stats();
    CHECK(stats.written == 3);
    CHECK(stats.failed == 0);
    CHECK(written_signals == 3);
    CHECK(panel.pictures_list()->count() == 3);
    REQUIRE(panel.session_id().has_value());
    CHECK(count_files(session_root, ".jpg") == 3);
    CHECK(count_files(session_root, ".json") >= 4);  // three sidecars and the session manifest

    auto catalogue = Catalogue::open(session_root / "catalogue.sqlite");
    REQUIRE(catalogue.has_value());
    const auto count = (*catalogue)->count(panel.session_id());
    REQUIRE(count.has_value());
    CHECK(*count == 3);
    const auto session = (*catalogue)->session(*panel.session_id());
    REQUIRE(session.has_value());
    REQUIRE(session->has_value());
    CHECK((*session)->camera_id == std::string(sim::kSkyCameraId));

    // Stopping an interval run: the panel asks the sequencer to stop and reports it.
    panel.set_plan_controls(CaptureKind::Interval, 0, 500, ImageFileFormat::Png);
    panel.set_use_session(false);
    panel.start();
    REQUIRE(wait_until([&] { return panel.is_running(); }, 5s));
    REQUIRE(wait_until([&] { return panel.last_stats().written >= 1; }, 15s));
    panel.stop();
    REQUIRE(wait_until([&] { return !panel.is_running(); }, 15s));
    CHECK(panel.last_stats().stopped_by_request);
    CHECK(count_files(fixture.workspace.path("captures"), ".png") >= 1);
    service.close();
    REQUIRE(wait_until([&] { return !service.is_open() && !service.state().busy; }, 5s));
}
