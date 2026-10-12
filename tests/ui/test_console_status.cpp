// P042: the log console, notifications and the status bar.

#include "console_panel.hpp"
#include "status_bar.hpp"
#include "test_support.hpp"

#include <cloudscope/common/log.hpp>

#include <QtCore/QSortFilterProxyModel>
#include <QtWidgets/QLabel>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::test::wait_until;

namespace {

bool has_message(const LogModel& model, const QString& text)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, LogModel::MessageColumn)).toString().contains(text)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the console shows what is logged, filtered by level and text", "[desktop][console]")
{
    ConsolePanel panel;
    LogModel* model = panel.model();
    logger("test.console").warn("hello console {}", 42);
    logger("test.console").error("an error line");
    REQUIRE(wait_until([&] { return has_message(*model, QStringLiteral("hello console 42")); }, 5s));
    REQUIRE(wait_until([&] { return has_message(*model, QStringLiteral("an error line")); }, 5s));
    panel.set_minimum_level(LogLevel::Error);
    bool warn_hidden = true;
    for (int row = 0; row < panel.proxy()->rowCount(); ++row) {
        if (panel.proxy()
                ->data(panel.proxy()->index(row, LogModel::MessageColumn))
                .toString()
                .contains(QStringLiteral("hello console"))) {
            warn_hidden = false;
        }
    }
    CHECK(warn_hidden);
    panel.set_minimum_level(LogLevel::Trace);
    panel.set_search(QStringLiteral("hello console"));
    CHECK(panel.proxy()->rowCount() >= 1);
    for (int row = 0; row < panel.proxy()->rowCount(); ++row) {
        CHECK(panel.proxy()
                  ->data(panel.proxy()->index(row, LogModel::MessageColumn))
                  .toString()
                  .contains(QStringLiteral("hello console")));
    }
    panel.set_search(QStringLiteral("test.console"));  // the component matches too
    CHECK(panel.proxy()->rowCount() >= 2);
    CHECK(model->data(model->index(0, LogModel::TimeColumn)).toString().contains(QStringLiteral("T")));  // ISO 8601
    model->clear();
    CHECK(model->rowCount() == 0);
}

TEST_CASE("the log model keeps at most its capacity", "[desktop][console]")
{
    LogModel model(20);
    for (int i = 0; i < 50; ++i) {
        model.append(
            LogRecord{.time = UtcTime{}, .level = LogLevel::Info, .component = "t", .message = std::to_string(i)});
    }
    CHECK(model.rowCount() == 20);
    CHECK(model.record(0).message == "30");
    CHECK(model.record(19).message == "49");
}

TEST_CASE("notifications are counted until read and the status bar reports every segment", "[desktop][status]")
{
    NotificationCenter centre;
    int signalled = 0;
    QObject::connect(&centre, &NotificationCenter::notified, &centre, [&](const Notification&) { ++signalled; });
    centre.notify(LogLevel::Info, QStringLiteral("first"));
    centre.notify(LogLevel::Error, QStringLiteral("second"));
    CHECK(signalled == 2);
    CHECK(centre.unread() == 2);
    CHECK(centre.notifications().size() == 2);
    centre.mark_read();
    CHECK(centre.unread() == 0);

    StatusBar bar;
    bar.set_camera(QStringLiteral("Camera: sim streaming"), SegmentState::Active);
    CHECK(bar.camera_label()->text() == QStringLiteral("Camera: sim streaming"));
    CHECK(bar.camera_label()->property("segmentState").toInt() == static_cast<int>(SegmentState::Active));
    bar.set_mount(QStringLiteral("Mount: none"), SegmentState::Absent);
    bar.set_sensors(QStringLiteral("Sensors: none"), SegmentState::Absent);
    bar.set_ai(QStringLiteral("AI: not available"), SegmentState::Absent);
    bar.set_remote(QStringLiteral("Remote: 0 clients"), SegmentState::Idle);
    CHECK(bar.remote_label()->text().contains(QStringLiteral("0 clients")));
    bar.set_unread(3);
    bar.set_last_notification(QStringLiteral("second"));
    const cloudscope::test::TempWorkspace workspace;
    bar.set_disk_folder(workspace.path("not/yet/created"));  // the nearest existing ancestor decides
    const DiskStatus disk = bar.disk();
    CHECK(disk.known);
    CHECK(disk.free_bytes > 0);
    CHECK(disk.total_bytes >= disk.free_bytes);
    CHECK(bar.disk_label()->text().contains(QStringLiteral("GiB")));
    bar.set_disk_warning_bytes(disk.free_bytes + 1);
    bar.refresh_disk();
    CHECK(bar.disk_label()->property("segmentState").toInt() == static_cast<int>(SegmentState::Warning));
    CHECK_FALSE(disk_status(std::filesystem::path()).known);
}
