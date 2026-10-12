#include "status_bar.hpp"

#include <QtCore/QStorageInfo>
#include <QtCore/QTimerEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolButton>

#include <algorithm>
#include <system_error>

namespace cloudscope::desktop {

namespace {

constexpr int kDiskIntervalMs = 5000;

QString gib(std::uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GiB");
}

QLabel* make_segment(QStatusBar* bar, const QString& name, const QString& tip)
{
    auto* label = new QLabel(bar);
    label->setAccessibleName(name);
    label->setToolTip(tip);
    label->setMargin(2);
    bar->addPermanentWidget(label);
    return label;
}

}  // namespace

DiskStatus disk_status(const std::filesystem::path& folder)
{
    DiskStatus status{.folder = folder};
    // The nearest existing ancestor decides: the session folder may not exist yet.
    std::filesystem::path probe = folder;
    std::error_code error;
    while (!probe.empty() && !std::filesystem::exists(probe, error)) {
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    if (probe.empty()) {
        return status;
    }
    const QStorageInfo info(QString::fromStdU16String(probe.u16string()));
    if (!info.isValid() || !info.isReady()) {
        return status;
    }
    status.known = true;
    status.free_bytes = static_cast<std::uint64_t>(std::max<qint64>(info.bytesAvailable(), 0));
    status.total_bytes = static_cast<std::uint64_t>(std::max<qint64>(info.bytesTotal(), 0));
    return status;
}

StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent),
      notification_(new QLabel(this)),
      unread_(new QToolButton(this)),
      disk_timer_(startTimer(kDiskIntervalMs))
{
    setAccessibleName(QStringLiteral("Status bar"));
    setSizeGripEnabled(true);
    notification_->setAccessibleName(QStringLiteral("Last notification"));
    addWidget(notification_, 1);
    unread_->setAccessibleName(QStringLiteral("Notifications"));
    unread_->setToolTip(tr("Open the notification list"));
    unread_->setText(QStringLiteral("0"));
    unread_->setAutoRaise(true);
    addWidget(unread_);
    connect(unread_, &QToolButton::clicked, this, &StatusBar::notificationsRequested);

    camera_ = make_segment(this, QStringLiteral("Camera status"), tr("Camera: device, mode and stream state"));
    mount_ = make_segment(this, QStringLiteral("Mount status"), tr("Pan-tilt mount (Stage E)"));
    sensors_ = make_segment(this, QStringLiteral("Sensors status"), tr("IMU, GPS and environment sensors (Stage E)"));
    ai_ = make_segment(this, QStringLiteral("AI status"), tr("STRATIA inference (Stage F)"));
    remote_ = make_segment(this, QStringLiteral("Remote status"), tr("Remote clients of the daemon (Stage H)"));
    disk_ = make_segment(this, QStringLiteral("Disk status"), tr("Free space of the session folder"));

    set_camera(tr("Camera: none"), SegmentState::Absent);
    set_mount(tr("Mount: none"), SegmentState::Absent);
    set_sensors(tr("Sensors: none"), SegmentState::Absent);
    set_ai(tr("AI: not available (model stage F)"), SegmentState::Absent);
    set_remote(tr("Remote: local only"), SegmentState::Absent);
    paint_segment(disk_, tr("Disk: unknown"), SegmentState::Absent);
}

void StatusBar::paint_segment(QLabel* label, const QString& text, SegmentState state)
{
    label->setText(text);
    QColor colour = foreground_;
    switch (state) {
    case SegmentState::Absent:
        colour = foreground_.darker(160);
        break;
    case SegmentState::Idle:
    case SegmentState::Active:
        colour = foreground_;
        break;
    case SegmentState::Warning:
        colour = warning_;
        break;
    case SegmentState::Error:
        colour = error_;
        break;
    }
    label->setStyleSheet(
        QStringLiteral("QLabel { color: %1; %2 }")
            .arg(colour.name(), state == SegmentState::Active ? QStringLiteral("font-weight: bold;") : QString()));
    label->setProperty("segmentState", static_cast<int>(state));
}

void StatusBar::set_camera(const QString& text, SegmentState state)
{
    paint_segment(camera_, text, state);
}

void StatusBar::set_mount(const QString& text, SegmentState state)
{
    paint_segment(mount_, text, state);
}

void StatusBar::set_sensors(const QString& text, SegmentState state)
{
    paint_segment(sensors_, text, state);
}

void StatusBar::set_ai(const QString& text, SegmentState state)
{
    paint_segment(ai_, text, state);
}

void StatusBar::set_remote(const QString& text, SegmentState state)
{
    paint_segment(remote_, text, state);
}

void StatusBar::set_disk_folder(const std::filesystem::path& folder)
{
    disk_folder_ = folder;
    refresh_disk();
}

void StatusBar::refresh_disk()
{
    disk_status_ = disk_status(disk_folder_);
    if (!disk_status_.known) {
        paint_segment(disk_, tr("Disk: unknown"), SegmentState::Warning);
        return;
    }
    const SegmentState state =
        disk_status_.free_bytes < disk_warning_bytes_ ? SegmentState::Warning : SegmentState::Idle;
    paint_segment(disk_, tr("Disk: %1 free of %2").arg(gib(disk_status_.free_bytes), gib(disk_status_.total_bytes)),
                  state);
}

void StatusBar::set_last_notification(const QString& text)
{
    notification_->setText(text);
}

void StatusBar::set_unread(int count)
{
    unread_->setText(QString::number(count));
    unread_->setToolTip(count == 0 ? tr("No unread notification") : tr("%1 unread notifications").arg(count));
}

void StatusBar::set_colours(QColor foreground, QColor warning, QColor error)
{
    foreground_ = foreground;
    warning_ = warning;
    error_ = error;
    for (QLabel* label : {camera_, mount_, sensors_, ai_, remote_, disk_}) {
        paint_segment(label, label->text(), static_cast<SegmentState>(label->property("segmentState").toInt()));
    }
}

void StatusBar::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == disk_timer_) {
        if (!disk_folder_.empty()) {
            refresh_disk();
        }
        return;
    }
    QStatusBar::timerEvent(event);
}

}  // namespace cloudscope::desktop
