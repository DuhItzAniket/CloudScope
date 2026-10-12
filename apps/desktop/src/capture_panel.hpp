// The capture panel (P039, FR-SEQ-04): a capture plan for the sequencer of P029 (single, burst, interval,
// bracket), the file format and folder, optionally a catalogued session, progress with the time to the next
// picture, and the list of pictures written. The sequencer runs on a worker thread of its own; the camera stays
// with CameraService, which also brings it back after a failure (FR-SEQ-05).
//
// Lives on the UI thread.
#pragma once

#include "camera_service.hpp"

#include <cloudscope/capture/recording.hpp>
#include <cloudscope/capture/sequencer.hpp>
#include <cloudscope/common/executor.hpp>
#include <cloudscope/session/catalogue.hpp>
#include <cloudscope/session/session.hpp>

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace cloudscope::desktop {

struct CaptureDefaults {
    std::filesystem::path folder;  // where pictures go when no session is used
    std::filesystem::path session_root;
    std::optional<SiteInfo> site;
    std::optional<PointingInfo> pointing;
    std::string calibration_id;
};

class CapturePanel : public QWidget {
    Q_OBJECT

public:
    CapturePanel(CameraService& service, const IClock& clock, CaptureDefaults defaults, QWidget* parent = nullptr);
    ~CapturePanel() override;
    CapturePanel(const CapturePanel&) = delete;
    CapturePanel& operator=(const CapturePanel&) = delete;
    CapturePanel(CapturePanel&&) = delete;
    CapturePanel& operator=(CapturePanel&&) = delete;

    // The plan the controls describe; InvalidArgument from the sequencer's validate() names the problem.
    [[nodiscard]] Expected<CapturePlan> plan() const;
    void set_plan_controls(CaptureKind kind, int count, int interval_ms, ImageFileFormat format);
    void set_folder(const std::filesystem::path& folder);
    void set_use_session(bool use);
    [[nodiscard]] bool is_running() const { return running_.load(); }
    [[nodiscard]] SequencerStats last_stats() const { return last_stats_; }
    [[nodiscard]] std::optional<std::string> session_id() const { return session_id_; }

    [[nodiscard]] QPushButton* start_button() const { return start_button_; }
    [[nodiscard]] QListWidget* pictures_list() const { return pictures_; }

    void start();
    void stop();

Q_SIGNALS:
    void started();
    void finished(const SequencerStats& stats);
    void pictureWritten(const QString& path);
    void notice(const QString& text);

private:
    void build_plan_box();
    void build_run_box();
    void update_progress();
    void set_controls_enabled(bool enabled);
    void timerEvent(QTimerEvent* event) override;
    // Worker thread: the whole run, from the session to the sequencer's end.
    void worker_run(const std::shared_ptr<Sequencer>& sequencer, CapturePlan plan, bool use_session,
                    const hal::DeviceInfo& device);
    // UI thread: after the run.
    void finish(const SequencerStats& stats, const std::optional<Error>& error);
    void note_picture(const QString& path);

    CameraService& service_;
    const IClock& clock_;
    CaptureDefaults defaults_;
    QComboBox* kind_ = nullptr;
    QSpinBox* count_ = nullptr;
    QSpinBox* interval_ms_ = nullptr;
    QComboBox* format_ = nullptr;
    QSpinBox* jpeg_quality_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QPushButton* browse_ = nullptr;
    QCheckBox* use_session_ = nullptr;
    QLineEdit* filename_template_ = nullptr;
    QCheckBox* night_profile_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* countdown_label_ = nullptr;
    QListWidget* pictures_ = nullptr;

    std::shared_ptr<Sequencer> sequencer_;  // exists while a run is in progress
    std::optional<Session> session_;
    std::unique_ptr<Catalogue> catalogue_;
    std::optional<std::string> session_id_;
    std::atomic<bool> running_{false};
    SequencerStats last_stats_;
    std::uint32_t planned_count_ = 0;
    std::chrono::milliseconds planned_interval_{0};
    std::chrono::steady_clock::time_point last_picture_at_{};
    int timer_ = 0;
    QtExecutor ui_;
    ThreadPool worker_;  // last: joined first
};

}  // namespace cloudscope::desktop
