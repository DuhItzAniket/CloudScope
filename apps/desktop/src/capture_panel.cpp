#include "capture_panel.hpp"

#include <cloudscope/common/log.hpp>

#include <QtCore/QTimerEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <initializer_list>

namespace cloudscope::desktop {

namespace {

constexpr int kProgressIntervalMs = 250;
constexpr int kPicturesListed = 200;

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop.capture");
    return instance;
}

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

std::filesystem::path path_of(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

QString format_text(ImageFileFormat format)
{
    const std::string_view text = to_string(format);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

CapturePanel::CapturePanel(CameraService& service, const IClock& clock, CaptureDefaults defaults, QWidget* parent)
    : QWidget(parent),
      service_(service),
      clock_(clock),
      defaults_(std::move(defaults)),
      ui_(this),
      worker_("capture", 1)
{
    setAccessibleName(QStringLiteral("Capture panel"));
    auto* layout = new QVBoxLayout(this);
    build_plan_box();
    build_run_box();
    layout->addWidget(findChild<QGroupBox*>(QStringLiteral("capture_plan_box")));
    layout->addWidget(findChild<QWidget*>(QStringLiteral("capture_run_box")), 1);

    connect(browse_, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("Capture folder"), folder_->text());
        if (!chosen.isEmpty()) {
            folder_->setText(chosen);
        }
    });
    connect(start_button_, &QPushButton::clicked, this, [this] {
        if (running_.load()) {
            stop();
        } else {
            start();
        }
    });
    connect(kind_, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto kind = static_cast<CaptureKind>(kind_->currentData().toInt());
        count_->setEnabled(kind == CaptureKind::Burst || kind == CaptureKind::Interval);
        interval_ms_->setEnabled(kind == CaptureKind::Interval);
    });
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int) {
        jpeg_quality_->setEnabled(static_cast<ImageFileFormat>(format_->currentData().toInt()) ==
                                  ImageFileFormat::Jpeg);
    });
    Q_EMIT kind_->currentIndexChanged(kind_->currentIndex());
    Q_EMIT format_->currentIndexChanged(format_->currentIndex());
}

CapturePanel::~CapturePanel()
{
    stop();
    worker_.wait_idle();
}

void CapturePanel::build_plan_box()
{
    auto* plan_box = new QGroupBox(tr("Plan"), this);
    plan_box->setObjectName(QStringLiteral("capture_plan_box"));
    auto* form = new QFormLayout(plan_box);
    kind_ = new QComboBox(plan_box);
    kind_->setAccessibleName(QStringLiteral("Capture kind"));
    kind_->addItem(tr("Single picture"), static_cast<int>(CaptureKind::Single));
    kind_->addItem(tr("Burst (consecutive frames)"), static_cast<int>(CaptureKind::Burst));
    kind_->addItem(tr("Interval series"), static_cast<int>(CaptureKind::Interval));
    kind_->addItem(tr("Exposure bracket (-2, 0, +2 stops)"), static_cast<int>(CaptureKind::Bracket));
    kind_->setToolTip(tr("What to take (FR-SEQ-01)"));
    count_ = new QSpinBox(plan_box);
    count_->setRange(0, 1'000'000);
    count_->setValue(10);
    count_->setSpecialValueText(tr("until stopped"));
    count_->setAccessibleName(QStringLiteral("Picture count"));
    count_->setToolTip(tr("Pictures to take; 0 runs until stopped or the disk guard (FR-REC-07)"));
    interval_ms_ = new QSpinBox(plan_box);
    interval_ms_->setRange(10, 86'400'000);
    interval_ms_->setValue(5000);
    interval_ms_->setSuffix(tr(" ms"));
    interval_ms_->setAccessibleName(QStringLiteral("Interval"));
    interval_ms_->setToolTip(tr("Time between pictures of a series; fixed-rate, late slots are skipped (FR-SEQ-02)"));
    format_ = new QComboBox(plan_box);
    format_->setAccessibleName(QStringLiteral("File format"));
    for (const ImageFileFormat format :
         {ImageFileFormat::Png, ImageFileFormat::Tiff16, ImageFileFormat::Jpeg, ImageFileFormat::Fits}) {
        format_->addItem(format_text(format), static_cast<int>(format));
    }
    format_->setToolTip(tr("File format of the pictures (PNG, 16-bit TIFF, JPEG, FITS); every file gets a sidecar"));
    jpeg_quality_ = new QSpinBox(plan_box);
    jpeg_quality_->setRange(1, 100);
    jpeg_quality_->setValue(92);
    jpeg_quality_->setAccessibleName(QStringLiteral("JPEG quality"));
    jpeg_quality_->setToolTip(tr("JPEG quality; MJPEG frames are stored as the camera sent them"));
    folder_ = new QLineEdit(path_text(defaults_.folder), plan_box);
    folder_->setAccessibleName(QStringLiteral("Capture folder"));
    folder_->setToolTip(tr("Folder for the pictures when no session is used"));
    browse_ = new QPushButton(tr("Browse…"), plan_box);
    browse_->setToolTip(tr("Choose the capture folder"));
    auto* folder_row = new QHBoxLayout();
    folder_row->addWidget(folder_, 1);
    folder_row->addWidget(browse_);
    use_session_ =
        new QCheckBox(tr("Record into a catalogued session under %1").arg(path_text(defaults_.session_root)), plan_box);
    use_session_->setChecked(true);
    use_session_->setAccessibleName(QStringLiteral("Use session"));
    use_session_->setToolTip(tr("Create a session folder and enter every picture into the frame catalogue (P030)"));
    filename_template_ = new QLineEdit(QStringLiteral("{site}_{utc}_{seq}_{profile}"), plan_box);
    filename_template_->setAccessibleName(QStringLiteral("File name template"));
    filename_template_->setToolTip(tr("Tokens: {site} {camera} {utc} {date} {seq} {profile} {kind} (FR-REC-06)"));
    night_profile_ = new QCheckBox(tr("Switch to the night profile below -6° Sun elevation"), plan_box);
    night_profile_->setChecked(true);
    night_profile_->setAccessibleName(QStringLiteral("Night profile"));
    night_profile_->setToolTip(tr("Day and night profiles by Sun elevation; needs a site (FR-SEQ-06)"));
    form->addRow(tr("Kind"), kind_);
    form->addRow(tr("Count"), count_);
    form->addRow(tr("Interval"), interval_ms_);
    form->addRow(tr("Format"), format_);
    form->addRow(tr("JPEG quality"), jpeg_quality_);
    form->addRow(tr("Folder"), folder_row);
    form->addRow(use_session_);
    form->addRow(tr("File names"), filename_template_);
    form->addRow(night_profile_);
}

void CapturePanel::build_run_box()
{
    auto* run_box = new QWidget(this);
    run_box->setObjectName(QStringLiteral("capture_run_box"));
    auto* layout = new QVBoxLayout(run_box);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* run_row = new QHBoxLayout();
    start_button_ = new QPushButton(tr("Start"), run_box);
    start_button_->setAccessibleName(QStringLiteral("Start capture"));
    start_button_->setToolTip(tr("Start the plan, or stop the running one"));
    progress_ = new QProgressBar(run_box);
    progress_->setAccessibleName(QStringLiteral("Capture progress"));
    run_row->addWidget(start_button_);
    run_row->addWidget(progress_, 1);
    layout->addLayout(run_row);
    status_label_ = new QLabel(tr("Idle"), run_box);
    status_label_->setAccessibleName(QStringLiteral("Capture status"));
    countdown_label_ = new QLabel(run_box);
    countdown_label_->setAccessibleName(QStringLiteral("Next capture"));
    layout->addWidget(status_label_);
    layout->addWidget(countdown_label_);
    pictures_ = new QListWidget(run_box);
    pictures_->setAccessibleName(QStringLiteral("Pictures written"));
    pictures_->setToolTip(tr("The last pictures written, newest first"));
    layout->addWidget(pictures_, 1);
}

Expected<CapturePlan> CapturePanel::plan() const
{
    CapturePlan plan;
    plan.kind = static_cast<CaptureKind>(kind_->currentData().toInt());
    plan.count = plan.kind == CaptureKind::Single ? 1 : static_cast<std::uint32_t>(count_->value());
    plan.interval = std::chrono::milliseconds(interval_ms_->value());
    const auto format = static_cast<ImageFileFormat>(format_->currentData().toInt());
    plan.day.format = format;
    plan.day.jpeg_quality = jpeg_quality_->value();
    plan.night = CaptureProfile{.name = "night", .format = format, .jpeg_quality = jpeg_quality_->value()};
    if (!night_profile_->isChecked()) {
        plan.night_below_sun_elevation_deg = -90.0;  // never
    }
    plan.folder = path_of(folder_->text());
    plan.filename_template = filename_template_->text().toStdString();
    if (plan.kind == CaptureKind::Burst && plan.count == 0) {
        return fail(ErrorCode::InvalidArgument, "a burst needs a count of at least 1");
    }
    if (auto valid = validate(plan); !valid) {
        return fail(valid.error());
    }
    return plan;
}

void CapturePanel::set_plan_controls(CaptureKind kind, int count, int interval_ms, ImageFileFormat format)
{
    kind_->setCurrentIndex(kind_->findData(static_cast<int>(kind)));
    count_->setValue(count);
    interval_ms_->setValue(interval_ms);
    format_->setCurrentIndex(format_->findData(static_cast<int>(format)));
}

void CapturePanel::set_folder(const std::filesystem::path& folder)
{
    folder_->setText(path_text(folder));
}

void CapturePanel::set_use_session(bool use)
{
    use_session_->setChecked(use);
}

void CapturePanel::set_controls_enabled(bool enabled)
{
    for (QWidget* widget : std::initializer_list<QWidget*>{kind_, count_, interval_ms_, format_, jpeg_quality_, folder_,
                                                           browse_, use_session_, filename_template_, night_profile_}) {
        widget->setEnabled(enabled);
    }
    if (enabled) {
        Q_EMIT kind_->currentIndexChanged(kind_->currentIndex());
        Q_EMIT format_->currentIndexChanged(format_->currentIndex());
    }
}

void CapturePanel::start()
{
    if (running_.load()) {
        return;
    }
    auto plan = this->plan();
    if (!plan) {
        Q_EMIT notice(tr("Cannot start: %1").arg(QString::fromStdString(plan.error().message)));
        return;
    }
    const std::optional<hal::DeviceInfo>& device = service_.state().device;
    if (!device.has_value()) {
        Q_EMIT notice(tr("Cannot start: open a camera first"));
        return;
    }
    const std::shared_ptr<hal::ICamera> camera = service_.camera();
    const std::shared_ptr<FrameHub> hub = service_.hub();
    if (!camera || !hub) {
        Q_EMIT notice(tr("Cannot start: the camera is not ready"));
        return;
    }
    if (!service_.state().streaming) {
        service_.start_stream();
    }
    auto sequencer = std::make_shared<Sequencer>(camera, hub, clock_);
    sequencer->set_site(defaults_.site);
    sequencer->set_pointing(defaults_.pointing);
    sequencer->set_calibration_id(defaults_.calibration_id);
    sequencer->set_recovery([this]() -> Expected<void> { return service_.recover_blocking(); });
    sequencer_ = sequencer;
    running_.store(true);
    planned_count_ = plan->count;
    planned_interval_ = plan->kind == CaptureKind::Interval ? plan->interval : std::chrono::milliseconds(0);
    last_picture_at_ = std::chrono::steady_clock::now();
    last_stats_ = {};
    session_id_.reset();
    pictures_->clear();
    progress_->setRange(0,
                        planned_count_ == 0 ? 0 : static_cast<int>(std::min<std::uint32_t>(planned_count_, 1'000'000)));
    progress_->setValue(0);
    status_label_->setText(tr("Starting…"));
    start_button_->setText(tr("Stop"));
    set_controls_enabled(false);
    timer_ = startTimer(kProgressIntervalMs);
    Q_EMIT started();
    const bool use_session = use_session_->isChecked();
    const hal::DeviceInfo device_info = *device;
    worker_.post([this, sequencer, run_plan = *plan, use_session, device_info] {
        worker_run(sequencer, run_plan, use_session, device_info);
    });
}

void CapturePanel::worker_run(const std::shared_ptr<Sequencer>& sequencer, CapturePlan plan, bool use_session,
                              const hal::DeviceInfo& device)
{
    std::optional<Session> session;
    std::unique_ptr<Catalogue> catalogue;
    if (use_session) {
        const SiteInfo site = defaults_.site.value_or(
            SiteInfo{.id = "site", .latitude_deg = 0.0, .longitude_deg = 0.0, .altitude_m = 0.0});
        auto created = Session::create(defaults_.session_root, site, device, clock_.now_utc());
        if (!created) {
            const QString text = tr("Session not created: %1").arg(QString::fromStdString(created.error().message));
            ui_.post([this, text] { Q_EMIT notice(text); });
        } else {
            session = std::move(*created);
            plan.folder = session->frames_folder();
            sequencer->set_session_id(session->info().id);
            auto opened = Catalogue::open(defaults_.session_root / "catalogue.sqlite");
            if (!opened) {
                const QString text = tr("Catalogue not opened: %1").arg(QString::fromStdString(opened.error().message));
                ui_.post([this, text] { Q_EMIT notice(text); });
            } else {
                catalogue = std::move(*opened);
                if (auto added = catalogue->add_session(session->info()); !added) {
                    log().warn("session not catalogued: {}", added.error().to_string());
                }
            }
            const std::string id = session->info().id;
            ui_.post([this, id] { session_id_ = id; });
        }
    }
    sequencer->set_on_picture([this, &session, &catalogue](const CapturedPicture& picture) {
        if (session.has_value()) {
            if (auto recorded = session->record_picture(picture.file); !recorded) {
                log().warn("picture not recorded in the session: {}", recorded.error().to_string());
            }
            if (catalogue) {
                if (auto added = catalogue->add_frame(frame_entry(picture, session->info().id)); !added) {
                    log().warn("picture not catalogued: {}", added.error().to_string());
                }
            }
        }
        const QString path = path_text(picture.file.path);
        ui_.post([this, path] { note_picture(path); });
    });
    auto result = sequencer->run(plan);
    if (session.has_value()) {
        if (auto closed = session->close(clock_.now_utc()); !closed) {
            log().warn("session not closed: {}", closed.error().to_string());
        }
        if (catalogue) {
            if (auto added = catalogue->add_session(session->info()); !added) {
                log().warn("session counts not catalogued: {}", added.error().to_string());
            }
        }
    }
    catalogue.reset();
    const SequencerStats stats = result ? *result : SequencerStats{};
    const std::optional<Error> error = result ? std::nullopt : std::optional<Error>(result.error());
    ui_.post([this, stats, error] { finish(stats, error); });
}

void CapturePanel::note_picture(const QString& path)
{
    last_picture_at_ = std::chrono::steady_clock::now();
    pictures_->insertItem(0, path);
    while (pictures_->count() > kPicturesListed) {
        delete pictures_->takeItem(pictures_->count() - 1);
    }
    Q_EMIT pictureWritten(path);
}

void CapturePanel::finish(const SequencerStats& stats, const std::optional<Error>& error)
{
    running_.store(false);
    sequencer_.reset();
    if (timer_ != 0) {
        killTimer(timer_);
        timer_ = 0;
    }
    last_stats_ = stats;
    start_button_->setText(tr("Start"));
    set_controls_enabled(true);
    progress_->setRange(0, 1);
    progress_->setValue(1);
    countdown_label_->clear();
    if (error.has_value()) {
        status_label_->setText(tr("Failed: %1").arg(QString::fromStdString(error->message)));
        Q_EMIT notice(tr("Capture failed: %1").arg(QString::fromStdString(error->message)));
    } else {
        QString why;
        if (stats.stopped_by_request) {
            why = tr("stopped");
        } else if (stats.stopped_by_disk_guard) {
            why = tr("stopped by the disk guard");
        } else if (stats.stopped_by_failures) {
            why = tr("stopped after repeated failures");
        } else {
            why = tr("done");
        }
        status_label_->setText(tr("%1: %2 written, %3 failed, %4 slots missed, %5 recoveries")
                                   .arg(why)
                                   .arg(stats.written)
                                   .arg(stats.failed)
                                   .arg(stats.missed_slots)
                                   .arg(stats.recoveries));
    }
    Q_EMIT finished(stats);
}

void CapturePanel::stop()
{
    if (const std::shared_ptr<Sequencer> sequencer = sequencer_) {
        sequencer->request_stop();
    }
}

void CapturePanel::update_progress()
{
    const std::shared_ptr<Sequencer> sequencer = sequencer_;
    if (!sequencer) {
        return;
    }
    const SequencerStats stats = sequencer->stats();
    last_stats_ = stats;
    if (planned_count_ > 0) {
        progress_->setValue(static_cast<int>(std::min<std::uint32_t>(stats.written, planned_count_)));
    }
    QString text = tr("Running: %1 written, %2 failed, %3 slots missed")
                       .arg(stats.written)
                       .arg(stats.failed)
                       .arg(stats.missed_slots);
    if (stats.paused) {
        text += tr(" (paused for the Sun)");
    }
    if (!stats.current_profile.empty()) {
        text += tr(", profile %1").arg(QString::fromStdString(stats.current_profile));
    }
    status_label_->setText(text);
    if (planned_interval_.count() > 0) {
        const auto next = last_picture_at_ + planned_interval_;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
        const auto remaining_ms = std::max<std::int64_t>(static_cast<std::int64_t>(remaining.count()), 0);
        countdown_label_->setText(
            tr("Next picture in %1 s").arg(static_cast<double>(remaining_ms) / 1000.0, 0, 'f', 1));
    } else {
        countdown_label_->clear();
    }
}

void CapturePanel::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == timer_) {
        update_progress();
        return;
    }
    QWidget::timerEvent(event);
}

}  // namespace cloudscope::desktop
