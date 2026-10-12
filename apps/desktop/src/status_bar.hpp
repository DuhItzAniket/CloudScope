// The status bar (P042, FR-DSP-09): one segment each for the camera, the mount, the sensors, the AI, remote
// clients and the disk, the last notification, and the unread count. Every segment states what it knows and
// says "none" or "not available (why)" otherwise; nothing is shown as present that is not.
//
// Lives on the UI thread. The disk segment polls free space every few seconds.
#pragma once

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtWidgets/QStatusBar>

#include <cstdint>
#include <filesystem>

class QLabel;
class QToolButton;

namespace cloudscope::desktop {

enum class SegmentState : std::uint8_t { Absent, Idle, Active, Warning, Error };

struct DiskStatus {
    bool known = false;
    std::uint64_t free_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::filesystem::path folder;
};

[[nodiscard]] DiskStatus disk_status(const std::filesystem::path& folder);

class StatusBar : public QStatusBar {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);

    void set_camera(const QString& text, SegmentState state);
    void set_mount(const QString& text, SegmentState state);
    void set_sensors(const QString& text, SegmentState state);
    void set_ai(const QString& text, SegmentState state);
    void set_remote(const QString& text, SegmentState state);
    void set_disk_folder(const std::filesystem::path& folder);
    void refresh_disk();
    void set_last_notification(const QString& text);
    void set_unread(int count);
    void set_colours(QColor foreground, QColor warning, QColor error);

    [[nodiscard]] QLabel* camera_label() const { return camera_; }
    [[nodiscard]] QLabel* mount_label() const { return mount_; }
    [[nodiscard]] QLabel* sensors_label() const { return sensors_; }
    [[nodiscard]] QLabel* ai_label() const { return ai_; }
    [[nodiscard]] QLabel* remote_label() const { return remote_; }
    [[nodiscard]] QLabel* disk_label() const { return disk_; }
    [[nodiscard]] DiskStatus disk() const { return disk_status_; }
    [[nodiscard]] std::uint64_t disk_warning_bytes() const { return disk_warning_bytes_; }
    void set_disk_warning_bytes(std::uint64_t bytes) { disk_warning_bytes_ = bytes; }

Q_SIGNALS:
    void notificationsRequested();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    void paint_segment(QLabel* label, const QString& text, SegmentState state);

    QLabel* camera_ = nullptr;
    QLabel* mount_ = nullptr;
    QLabel* sensors_ = nullptr;
    QLabel* ai_ = nullptr;
    QLabel* remote_ = nullptr;
    QLabel* disk_ = nullptr;
    QLabel* notification_ = nullptr;
    QToolButton* unread_ = nullptr;
    std::filesystem::path disk_folder_;
    DiskStatus disk_status_;
    std::uint64_t disk_warning_bytes_ = 2ULL * 1024ULL * 1024ULL * 1024ULL;  // 2 GiB
    int disk_timer_ = 0;
    QColor foreground_{230, 230, 230};
    QColor warning_{255, 140, 40};
    QColor error_{255, 90, 90};
};

}  // namespace cloudscope::desktop
