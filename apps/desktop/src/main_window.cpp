#include "main_window.hpp"

#include "preferences_dialog.hpp"

#include <cloudscope/capture/solar.hpp>
#include <cloudscope/common/build_info.hpp>
#include <cloudscope/common/log.hpp>
#include <cloudscope/sim/sim_devices.hpp>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

#include <ranges>
#include <system_error>

namespace cloudscope::desktop {

namespace {

constexpr int kLayoutVersion = 1;
constexpr int kSunIntervalMs = 30'000;
constexpr int kGamepadIntervalMs = 50;

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop");
    return instance;
}

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

ads::CDockManager* make_dock_manager(QMainWindow* window)
{
    // The flags are static and must be set before the first manager exists.
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    auto* manager = new ads::CDockManager(window);
    manager->setStyleSheet(QString());  // the application's theme styles the docks
    return manager;
}

}  // namespace

MainWindow::MainWindow(AppContext& context, MainWindowOptions options, QWidget* parent)
    : QMainWindow(parent),
      context_(context),
      options_(options),
      theme_(options.theme.value_or(context.settings().theme)),
      shortcuts_(new ShortcutRegistry(this)),
      notifications_(new NotificationCenter(this)),
      camera_(new CameraService(context.registry(), context.clock(), this)),
      preview_(new PreviewPipeline(this)),
      dock_manager_(make_dock_manager(this)),
      status_bar_(new StatusBar(this)),
      sun_timer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("CloudScope Desktop"));
    setObjectName(QStringLiteral("MainWindow"));
    setAccessibleName(QStringLiteral("CloudScope main window"));
    resize(1400, 900);

    build_docks();
    build_actions();
    build_menus();
    setStatusBar(status_bar_);
    status_bar_->set_disk_folder(context_.settings().session_root);
    connect_parts();
    apply_theme_to_parts();
    update_overlay_sources();
    refresh_sun();

    sun_timer_->setInterval(kSunIntervalMs);
    connect(sun_timer_, &QTimer::timeout, this, &MainWindow::refresh_sun);
    sun_timer_->start();

    if (options_.gamepad) {
        gamepad_ = std::make_unique<SdlGamepad>();
        if (!gamepad_->available()) {
            notifications_->notify(LogLevel::Info, tr("Gamepad input not available: %1").arg(gamepad_->reason()));
        }
        auto mount = context_.registry().create<hal::IMount>(sim::kMountId);
        if (mount) {
            mount_ = *mount;
            if (auto opened = mount_->open(); !opened) {
                log().warn("simulated mount not opened: {}", opened.error().to_string());
                mount_.reset();
            }
        }
        if (mount_ && gamepad_->available()) {
            mount_jog_ = new MountJog(mount_, *gamepad_, JogSettings{}, this);
            gamepad_timer_ = new QTimer(this);
            gamepad_timer_->setInterval(kGamepadIntervalMs);
            connect(gamepad_timer_, &QTimer::timeout, this, [this] {
                const MountJog::Tick tick = mount_jog_->tick();
                if (tick.state.connected) {
                    const auto status = mount_->status();
                    const QString text = status ? tr("Mount: %1 (simulated) pan %2° tilt %3°%4")
                                                      .arg(tick.state.name)
                                                      .arg(status->position.pan.value(), 0, 'f', 1)
                                                      .arg(status->position.tilt.value(), 0, 'f', 1)
                                                      .arg(status->emergency_stop ? tr(" E-STOP") : QString())
                                                : tr("Mount: %1").arg(QString::fromStdString(status.error().message));
                    status_bar_->set_mount(text, tick.moving ? SegmentState::Active : SegmentState::Idle);
                } else {
                    status_bar_->set_mount(tr("Mount: simulated, no gamepad connected"), SegmentState::Absent);
                }
            });
            gamepad_timer_->start();
            status_bar_->set_mount(tr("Mount: simulated, no gamepad connected"), SegmentState::Absent);
        }
    }

    layout_restored_ = options_.restore_layout && restore_layout();
    if (!shortcuts_->conflicts().empty()) {
        for (const ShortcutConflict& conflict : shortcuts_->conflicts()) {
            notifications_->notify(LogLevel::Warn,
                                   tr("Shortcut %1 is used by both %2 and %3")
                                       .arg(shortcut_text(conflict.shortcut), conflict.first_id, conflict.second_id));
        }
    }
    camera_->refresh_devices();
    log().info("CloudScope Desktop {} started; renderer {}, theme {}, layout {}", build_info().version,
               live_view_->renderer_in_use() == Renderer::OpenGL ? "opengl" : "raster", to_string(theme_),
               layout_restored_ ? "restored" : "default");
}

MainWindow::~MainWindow()
{
    preview_->stop();
}

ads::CDockWidget* MainWindow::make_dock(const QString& id, const QString& title, QWidget* content)
{
    auto* dock = new ads::CDockWidget(dock_manager_, title);
    dock->setObjectName(id);
    // The live view fills its area; the panels scroll when the area is too small for them.
    dock->setWidget(content, id == QStringLiteral("live_view") ? ads::CDockWidget::ForceNoScrollArea
                                                               : ads::CDockWidget::AutoScrollArea);
    dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
    dock->setAccessibleName(title + tr(" panel"));
    docks_[id] = dock;
    return dock;
}

void MainWindow::build_docks()
{
    live_view_ = new LiveView(options_.renderer, this);
    ads::CDockWidget* centre = make_dock(QStringLiteral("live_view"), tr("Live view"), live_view_);
    centre->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    centre->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    centre->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    dock_manager_->setCentralWidget(centre);

    camera_panel_ = new CameraPanel(*camera_, ProfileStore(context_.profiles_file()), this);
    ads::CDockWidget* camera_dock = make_dock(QStringLiteral("camera"), tr("Camera"), camera_panel_);
    ads::CDockAreaWidget* left = dock_manager_->addDockWidget(ads::LeftDockWidgetArea, camera_dock);

    histogram_panel_ = new HistogramPanel(this);
    ads::CDockWidget* histogram_dock =
        make_dock(QStringLiteral("histogram"), tr("Histogram and stretch"), histogram_panel_);
    ads::CDockAreaWidget* right = dock_manager_->addDockWidget(ads::RightDockWidgetArea, histogram_dock);

    image_tools_panel_ = new ImageToolsPanel(this);
    ads::CDockWidget* tools_dock = make_dock(QStringLiteral("image_tools"), tr("Image tools"), image_tools_panel_);
    dock_manager_->addDockWidgetTabToArea(tools_dock, right);

    const DesktopSettings& settings = context_.settings();
    const CaptureDefaults defaults{.folder = settings.session_root / "captures",
                                   .session_root = settings.session_root,
                                   .site = settings.site,
                                   .pointing = PointingInfo{.azimuth_deg = settings.pointing.azimuth_deg,
                                                            .elevation_deg = settings.pointing.elevation_deg,
                                                            .source = "declared_by_operator"},
                                   .calibration_id = std::string()};
    capture_panel_ = new CapturePanel(*camera_, context_.clock(), defaults, this);
    ads::CDockWidget* capture_dock = make_dock(QStringLiteral("capture"), tr("Capture"), capture_panel_);
    dock_manager_->addDockWidgetTabToArea(capture_dock, left);

    session_browser_ = new SessionBrowser(settings.session_root, settings.time_display, this);
    ads::CDockWidget* sessions_dock = make_dock(QStringLiteral("sessions"), tr("Sessions"), session_browser_);
    ads::CDockAreaWidget* bottom = dock_manager_->addDockWidget(ads::BottomDockWidgetArea, sessions_dock);

    console_panel_ = new ConsolePanel(this);
    ads::CDockWidget* console_dock = make_dock(QStringLiteral("console"), tr("Console"), console_panel_);
    dock_manager_->addDockWidgetTabToArea(console_dock, bottom);
    camera_dock->setAsCurrentTab();
    histogram_dock->setAsCurrentTab();
    sessions_dock->setAsCurrentTab();
}

void MainWindow::build_actions()
{
    const std::vector<ActionSpec> specs = {
        {.id = QStringLiteral("file.preferences"),
         .text = tr("&Preferences…"),
         .shortcut = QKeySequence(Qt::CTRL | Qt::Key_Comma),
         .status_tip = tr("Devices, paths, site, units and shortcuts")},
        {.id = QStringLiteral("file.quit"),
         .text = tr("&Quit"),
         .shortcut = QKeySequence::Quit,
         .status_tip = tr("Close CloudScope Desktop")},
        {.id = QStringLiteral("view.fit"),
         .text = tr("&Fit to window"),
         .shortcut = QKeySequence(Qt::Key_F),
         .status_tip = tr("Show the whole picture")},
        {.id = QStringLiteral("view.one_to_one"),
         .text = tr("&1:1"),
         .shortcut = QKeySequence(Qt::Key_1),
         .status_tip = tr("One image pixel per screen pixel")},
        {.id = QStringLiteral("view.zoom_in"),
         .text = tr("Zoom &in"),
         .shortcut = QKeySequence::ZoomIn,
         .status_tip = tr("Zoom into the picture")},
        {.id = QStringLiteral("view.zoom_out"),
         .text = tr("Zoom &out"),
         .shortcut = QKeySequence::ZoomOut,
         .status_tip = tr("Zoom out of the picture")},
        {.id = QStringLiteral("view.indicators"),
         .text = tr("Frame-rate &indicators"),
         .shortcut = QKeySequence(Qt::Key_I),
         .status_tip = tr("Show the camera and display rates, lost and dropped frames")},
        {.id = QStringLiteral("view.theme_dark"),
         .text = tr("&Dark theme"),
         .shortcut = QKeySequence(Qt::CTRL | Qt::Key_D),
         .status_tip = tr("Dark theme for daytime")},
        {.id = QStringLiteral("view.theme_night"),
         .text = tr("&Night-vision theme"),
         .shortcut = QKeySequence(Qt::CTRL | Qt::Key_N),
         .status_tip = tr("Red theme that keeps dark adaptation")},
        {.id = QStringLiteral("view.save_perspective"),
         .text = tr("&Save layout as…"),
         .shortcut = QKeySequence(),
         .status_tip = tr("Save the current panel layout under a name")},
        {.id = QStringLiteral("view.reset_layout"),
         .text = tr("&Reset layout"),
         .shortcut = QKeySequence(),
         .status_tip = tr("Back to the default panel layout")},
        {.id = QStringLiteral("overlay.reticle"),
         .text = tr("&Reticle"),
         .shortcut = QKeySequence(Qt::Key_R),
         .status_tip = tr("Crosshair at the image centre")},
        {.id = QStringLiteral("overlay.grid"),
         .text = tr("&Grid"),
         .shortcut = QKeySequence(Qt::Key_G),
         .status_tip = tr("Grid over the picture")},
        {.id = QStringLiteral("overlay.altitude"),
         .text = tr("&Altitude circles"),
         .shortcut = QKeySequence(Qt::Key_A),
         .status_tip = tr("Circles of constant elevation (needs a calibration and a pose)")},
        {.id = QStringLiteral("overlay.compass"),
         .text = tr("&Compass"),
         .shortcut = QKeySequence(Qt::Key_C),
         .status_tip = tr("North, east, south and west (needs a calibration and a pose)")},
        {.id = QStringLiteral("overlay.sun"),
         .text = tr("&Sun"),
         .shortcut = QKeySequence(Qt::Key_S),
         .status_tip = tr("The Sun's position (needs a site, a calibration and a pose)")},
        {.id = QStringLiteral("overlay.keep_out"),
         .text = tr("Sun &keep-out zone"),
         .shortcut = QKeySequence(Qt::Key_K),
         .status_tip = tr("The cone around the Sun that the mount must not enter")},
        {.id = QStringLiteral("camera.open"),
         .text = tr("&Open or close camera"),
         .shortcut = QKeySequence(Qt::CTRL | Qt::Key_O),
         .status_tip = tr("Open the selected camera, or close the open one")},
        {.id = QStringLiteral("camera.stream"),
         .text = tr("&Start or stop stream"),
         .shortcut = QKeySequence(Qt::Key_Space),
         .status_tip = tr("Start or stop the live stream")},
        {.id = QStringLiteral("capture.start"),
         .text = tr("Start &capture"),
         .shortcut = QKeySequence(Qt::CTRL | Qt::Key_R),
         .status_tip = tr("Run the capture plan")},
        {.id = QStringLiteral("capture.stop"),
         .text = tr("Sto&p capture"),
         .shortcut = QKeySequence(Qt::Key_Escape),
         .status_tip = tr("Stop the running plan")},
        {.id = QStringLiteral("capture.single"),
         .text = tr("&Take one picture"),
         .shortcut = QKeySequence(Qt::Key_T),
         .status_tip = tr("One picture with the current settings")},
        {.id = QStringLiteral("help.about"),
         .text = tr("&About"),
         .shortcut = QKeySequence(),
         .status_tip = tr("Version and build information")},
    };
    for (const ActionSpec& spec : specs) {
        QAction* action = shortcuts_->add(spec, this);
        addAction(action);  // window-wide shortcuts
    }
    if (auto applied = shortcuts_->apply_overrides(context_.settings().shortcuts); !applied) {
        notifications_->notify(LogLevel::Warn, QString::fromStdString(applied.error().message));
    }
    for (const char* id : {"view.indicators", "overlay.reticle", "overlay.grid", "overlay.altitude", "overlay.compass",
                           "overlay.sun", "overlay.keep_out", "view.theme_dark", "view.theme_night"}) {
        shortcuts_->action(QString::fromLatin1(id))->setCheckable(true);
    }
    shortcuts_->action(QStringLiteral("view.indicators"))->setChecked(true);
    shortcuts_->action(QStringLiteral("overlay.reticle"))->setChecked(true);
    shortcuts_->action(QStringLiteral("view.theme_dark"))->setChecked(theme_ == Theme::Dark);
    shortcuts_->action(QStringLiteral("view.theme_night"))->setChecked(theme_ == Theme::Night);
}

void MainWindow::build_menus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(shortcuts_->action(QStringLiteral("file.preferences")));
    file->addSeparator();
    file->addAction(shortcuts_->action(QStringLiteral("file.quit")));

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(shortcuts_->action(QStringLiteral("view.fit")));
    view->addAction(shortcuts_->action(QStringLiteral("view.one_to_one")));
    view->addAction(shortcuts_->action(QStringLiteral("view.zoom_in")));
    view->addAction(shortcuts_->action(QStringLiteral("view.zoom_out")));
    view->addAction(shortcuts_->action(QStringLiteral("view.indicators")));
    view->addSeparator();
    panels_menu_ = view->addMenu(tr("&Panels"));
    for (const auto& [id, dock] : docks_) {
        if (id != QStringLiteral("live_view")) {
            panels_menu_->addAction(dock->toggleViewAction());
        }
    }
    perspectives_menu_ = view->addMenu(tr("&Layouts"));
    view->addAction(shortcuts_->action(QStringLiteral("view.save_perspective")));
    view->addAction(shortcuts_->action(QStringLiteral("view.reset_layout")));
    view->addSeparator();
    view->addAction(shortcuts_->action(QStringLiteral("view.theme_dark")));
    view->addAction(shortcuts_->action(QStringLiteral("view.theme_night")));

    QMenu* overlays = menuBar()->addMenu(tr("&Overlays"));
    for (const char* id : {"overlay.reticle", "overlay.grid", "overlay.altitude", "overlay.compass", "overlay.sun",
                           "overlay.keep_out"}) {
        overlays->addAction(shortcuts_->action(QString::fromLatin1(id)));
    }

    QMenu* camera = menuBar()->addMenu(tr("&Camera"));
    camera->addAction(shortcuts_->action(QStringLiteral("camera.open")));
    camera->addAction(shortcuts_->action(QStringLiteral("camera.stream")));

    QMenu* capture = menuBar()->addMenu(tr("Ca&pture"));
    capture->addAction(shortcuts_->action(QStringLiteral("capture.single")));
    capture->addAction(shortcuts_->action(QStringLiteral("capture.start")));
    capture->addAction(shortcuts_->action(QStringLiteral("capture.stop")));

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(shortcuts_->action(QStringLiteral("help.about")));

    connect(perspectives_menu_, &QMenu::aboutToShow, this, [this] {
        perspectives_menu_->clear();
        const QStringList names = perspective_names();
        if (names.isEmpty()) {
            QAction* none = perspectives_menu_->addAction(tr("(no saved layout)"));
            none->setEnabled(false);
        }
        for (const QString& name : names) {
            QAction* open = perspectives_menu_->addAction(name);
            connect(open, &QAction::triggered, this, [this, name] { open_perspective(name); });
        }
    });
}

void MainWindow::connect_parts()
{
    // Actions
    const auto act = [this](const char* id) { return shortcuts_->action(QString::fromLatin1(id)); };
    connect(act("file.preferences"), &QAction::triggered, this, &MainWindow::show_preferences);
    connect(act("file.quit"), &QAction::triggered, this, &QWidget::close);
    connect(act("view.fit"), &QAction::triggered, live_view_, &LiveView::fit);
    connect(act("view.one_to_one"), &QAction::triggered, live_view_, &LiveView::one_to_one);
    connect(act("view.zoom_in"), &QAction::triggered, live_view_, &LiveView::zoom_in);
    connect(act("view.zoom_out"), &QAction::triggered, live_view_, &LiveView::zoom_out);
    connect(act("view.indicators"), &QAction::toggled, live_view_, &LiveView::set_show_indicators);
    connect(act("view.theme_dark"), &QAction::triggered, this, [this] { set_theme(Theme::Dark); });
    connect(act("view.theme_night"), &QAction::triggered, this, [this] { set_theme(Theme::Night); });
    connect(act("view.save_perspective"), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Save layout"), tr("Layout name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            add_perspective(name.trimmed());
            save_layout();
        }
    });
    connect(act("view.reset_layout"), &QAction::triggered, this, [this] {
        for (const auto& [id, dock] : docks_) {
            dock->toggleView(true);
        }
        notifications_->notify(LogLevel::Info,
                               tr("Panels shown again; restart with --reset-layout for the default arrangement"));
    });
    const auto overlay_toggle = [this](const char* id, bool OverlaySettings::* field) {
        connect(shortcuts_->action(QString::fromLatin1(id)), &QAction::toggled, this, [this, field](bool on) {
            overlays_.*field = on;
            live_view_->set_overlay_settings(overlays_);
            update_overlay_sources();
        });
    };
    overlay_toggle("overlay.reticle", &OverlaySettings::reticle);
    overlay_toggle("overlay.grid", &OverlaySettings::grid);
    overlay_toggle("overlay.altitude", &OverlaySettings::altitude_circles);
    overlay_toggle("overlay.compass", &OverlaySettings::compass);
    overlay_toggle("overlay.sun", &OverlaySettings::sun);
    overlay_toggle("overlay.keep_out", &OverlaySettings::keep_out);
    live_view_->set_overlay_settings(overlays_);
    connect(act("camera.open"), &QAction::triggered, camera_panel_->open_button(), &QPushButton::click);
    connect(act("camera.stream"), &QAction::triggered, camera_panel_->stream_button(), &QPushButton::click);
    connect(act("capture.start"), &QAction::triggered, capture_panel_, &CapturePanel::start);
    connect(act("capture.stop"), &QAction::triggered, capture_panel_, &CapturePanel::stop);
    connect(act("capture.single"), &QAction::triggered, this, [this] {
        capture_panel_->set_plan_controls(CaptureKind::Single, 1, 5000, ImageFileFormat::Png);
        capture_panel_->start();
    });
    connect(act("help.about"), &QAction::triggered, this, [this] {
        const BuildInfo& info = build_info();
        QMessageBox::about(this, tr("About CloudScope Desktop"),
                           tr("CloudScope Desktop %1\n%2\nRenderer: %3\nConfiguration: %4")
                               .arg(QString::fromStdString(info.version), QString::fromStdString(to_text(info)),
                                    live_view_->renderer_in_use() == Renderer::OpenGL ? tr("OpenGL") : tr("software"),
                                    path_text(context_.paths().user_config)));
    });

    // Camera service -> preview, panels, status
    connect(camera_, &CameraService::opened, this, [this] {
        update_camera_status();
        live_view_->set_message(tr("Camera open; start the stream"));
    });
    connect(camera_, &CameraService::closed, this, [this] {
        preview_->stop();
        live_view_->set_frame(nullptr);
        live_view_->set_message(tr("No camera open"));
        update_camera_status();
    });
    connect(camera_, &CameraService::streamingChanged, this, [this](bool streaming) {
        if (streaming) {
            preview_->start(camera_->hub());
        } else {
            preview_->stop();
        }
        update_camera_status();
    });
    connect(camera_, &CameraService::modeChanged, this, &MainWindow::update_camera_status);
    connect(camera_, &CameraService::statsUpdated, this, &MainWindow::update_camera_status);
    connect(camera_, &CameraService::failed, this, [this](const QString& what) {
        notifications_->notify(LogLevel::Error, what);
        update_camera_status();
    });
    connect(preview_, &PreviewPipeline::frameReady, this, &MainWindow::on_frame_ready);

    // Panels
    connect(histogram_panel_, &HistogramPanel::stretchChanged, this, [this](const StretchSettings& settings) {
        PreviewOptions options = preview_->options();
        options.stretch = settings;
        preview_->set_options(options);
    });
    connect(histogram_panel_, &HistogramPanel::paradeWantedChanged, this, [this](bool wanted) {
        PreviewOptions options = preview_->options();
        options.compute_parade = wanted;
        preview_->set_options(options);
    });
    connect(live_view_, &LiveView::cursorMoved, this, [this](QPoint point, bool over) {
        image_tools_panel_->set_cursor(over ? std::optional<QPoint>(point) : std::nullopt);
    });
    connect(live_view_, &LiveView::roiChanged, image_tools_panel_, &ImageToolsPanel::set_roi);
    connect(camera_panel_, &CameraPanel::notice, this,
            [this](const QString& text) { notifications_->notify(LogLevel::Info, text); });
    connect(capture_panel_, &CapturePanel::notice, this,
            [this](const QString& text) { notifications_->notify(LogLevel::Warn, text); });
    connect(capture_panel_, &CapturePanel::finished, this, [this](const SequencerStats& stats) {
        notifications_->notify(
            LogLevel::Info,
            tr("Capture finished: %1 pictures written, %2 failed").arg(stats.written).arg(stats.failed));
        if (auto reopened = session_browser_->reopen(); !reopened) {
            log().warn("catalogue not reopened: {}", reopened.error().to_string());
        }
        status_bar_->refresh_disk();
    });
    connect(session_browser_, &SessionBrowser::notice, this,
            [this](const QString& text) { notifications_->notify(LogLevel::Info, text); });

    // Notifications -> status bar and log
    connect(notifications_, &NotificationCenter::notified, this, [this](const Notification& notification) {
        status_bar_->set_last_notification(notification.text);
        switch (notification.level) {
        case LogLevel::Error:
        case LogLevel::Critical:
            log().error("{}", notification.text.toStdString());
            break;
        case LogLevel::Warn:
            log().warn("{}", notification.text.toStdString());
            break;
        default:
            log().info("{}", notification.text.toStdString());
            break;
        }
    });
    connect(notifications_, &NotificationCenter::unreadChanged, status_bar_, &StatusBar::set_unread);
    connect(status_bar_, &StatusBar::notificationsRequested, this, &MainWindow::show_notifications);
    update_camera_status();
}

void MainWindow::on_frame_ready()
{
    const std::shared_ptr<const PreviewFrame> frame = preview_->latest();
    if (!frame) {
        return;
    }
    const AcquisitionStats stats = camera_->acquisition_stats();
    live_view_->set_indicators({.camera_fps = stats.recent_fps,
                                .display_fps = preview_->display_fps(),
                                .lost = stats.lost,
                                .dropped = stats.pool_misses + frame->skipped,
                                .decode_ms = frame->decode_ms,
                                .simulated = frame->info.simulated});
    live_view_->set_frame(frame);
    histogram_panel_->show_frame(frame);
    image_tools_panel_->show_frame(frame);
}

void MainWindow::update_camera_status()
{
    const CameraState& state = camera_->state();
    if (!state.device.has_value()) {
        status_bar_->set_camera(tr("Camera: none"), SegmentState::Absent);
        return;
    }
    QString text = tr("Camera: %1").arg(QString::fromStdString(state.device->name));
    if (state.mode.has_value()) {
        text += QStringLiteral(" ") + QString::fromStdString(mode_text(*state.mode));
    }
    SegmentState segment = SegmentState::Idle;
    if (state.streaming) {
        text += tr(" streaming %1 fps, %2 lost").arg(state.stats.recent_fps, 0, 'f', 1).arg(state.stats.lost);
        segment = SegmentState::Active;
        if (state.stats.lost > 0 || state.stats.pool_misses > 0) {
            segment = SegmentState::Warning;
        }
    } else if (state.stats.last_error.has_value()) {
        text += tr(" stopped: %1").arg(QString::fromStdString(state.stats.last_error->message));
        segment = SegmentState::Error;
    } else {
        text += tr(" open");
    }
    if (state.device->simulated) {
        text += tr(" (simulated)");
    }
    status_bar_->set_camera(text, segment);
}

void MainWindow::update_overlay_sources()
{
    const DesktopSettings& settings = context_.settings();
    const bool sky_wanted = overlays_.altitude_circles || overlays_.compass || overlays_.sun || overlays_.keep_out;
    if (!sky_wanted) {
        return;
    }
    if (projection_) {
        return;
    }
    if (settings.calibration_file.empty()) {
        projection_note_ = tr("Sky overlays need an intrinsic calibration file (Preferences > Paths)");
        notifications_->notify(LogLevel::Info, projection_note_);
        return;
    }
    auto model = load_camera_model(settings.calibration_file);
    if (!model) {
        projection_note_ = tr("Calibration file not usable: %1").arg(QString::fromStdString(model.error().message));
        notifications_->notify(LogLevel::Warn, projection_note_);
        return;
    }
    projection_ = std::make_shared<const SkyProjection>(*model, settings.pointing);
    live_view_->set_projection(projection_);
    projection_note_.clear();
}

void MainWindow::refresh_sun()
{
    const SiteInfo& site = context_.settings().site;
    const SunPosition sun = sun_position(context_.clock().now_utc(), site.latitude_deg, site.longitude_deg);
    live_view_->set_sun(sun);
}

void MainWindow::apply_theme_to_parts()
{
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        apply_theme(*app, theme_);
    }
    const QColor overlay = overlay_colour(theme_);
    const QColor text = theme_palette(theme_).color(QPalette::WindowText);
    live_view_->set_colours(overlay, theme_ == Theme::Night ? QColor(255, 100, 60) : QColor(255, 80, 0), text);
    histogram_panel_->set_colours(text, warning_colour(theme_));
    image_tools_panel_->set_colours(text, overlay);
    status_bar_->set_colours(text, warning_colour(theme_), QColor(255, 90, 90));
    PreviewOptions options = preview_->options();
    options.night_vision = theme_ == Theme::Night;
    preview_->set_options(options);
    shortcuts_->action(QStringLiteral("view.theme_dark"))->setChecked(theme_ == Theme::Dark);
    shortcuts_->action(QStringLiteral("view.theme_night"))->setChecked(theme_ == Theme::Night);
}

void MainWindow::set_theme(Theme theme)
{
    theme_ = theme;
    apply_theme_to_parts();
    if (auto written = context_.update_user_config({{"desktop", {{"theme", std::string(to_string(theme))}}}});
        !written) {
        notifications_->notify(LogLevel::Warn,
                               tr("Theme not remembered: %1").arg(QString::fromStdString(written.error().message)));
    }
}

std::filesystem::path MainWindow::state_file() const
{
    return context_.state_file();
}

void MainWindow::save_layout() const
{
    std::error_code error;
    std::filesystem::create_directories(state_file().parent_path(), error);
    QSettings settings(path_text(state_file()), QSettings::IniFormat);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("docks/state"), dock_manager_->saveState(kLayoutVersion));
    settings.setValue(QStringLiteral("docks/version"), kLayoutVersion);
    settings.beginGroup(QStringLiteral("perspectives"));
    settings.remove(QString());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("perspectives"));
    dock_manager_->savePerspectives(settings);
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        log().warn("layout not saved to {}", state_file().string());
    }
}

bool MainWindow::restore_layout()
{
    std::error_code error;
    if (!std::filesystem::exists(state_file(), error)) {
        return false;
    }
    QSettings settings(path_text(state_file()), QSettings::IniFormat);
    if (settings.value(QStringLiteral("docks/version")).toInt() != kLayoutVersion) {
        return false;
    }
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    settings.beginGroup(QStringLiteral("perspectives"));
    dock_manager_->loadPerspectives(settings);
    settings.endGroup();
    const QByteArray state = settings.value(QStringLiteral("docks/state")).toByteArray();
    return !state.isEmpty() && dock_manager_->restoreState(state, kLayoutVersion);
}

QByteArray MainWindow::dock_state() const
{
    return dock_manager_->saveState(kLayoutVersion);
}

void MainWindow::add_perspective(const QString& name)
{
    dock_manager_->addPerspective(name);
}

void MainWindow::open_perspective(const QString& name)
{
    dock_manager_->openPerspective(name);
}

QStringList MainWindow::perspective_names() const
{
    return dock_manager_->perspectiveNames();
}

void MainWindow::show_notifications()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Notifications"));
    dialog.setAccessibleName(QStringLiteral("Notifications dialog"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(QStringLiteral("Notification list"));
    for (const Notification& item : std::views::reverse(notifications_->notifications())) {
        list->addItem(item.time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + item.text);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    notifications_->mark_read();
    dialog.resize(600, 400);
    dialog.exec();
}

void MainWindow::show_preferences()
{
    PreferencesDialog dialog(context_, shortcuts_, this);
    connect(&dialog, &PreferencesDialog::saved, this, [this] {
        const DesktopSettings& settings = context_.settings();
        if (auto applied = shortcuts_->apply_overrides(settings.shortcuts); !applied) {
            notifications_->notify(LogLevel::Warn, QString::fromStdString(applied.error().message));
        }
        if (settings.theme != theme_) {
            theme_ = settings.theme;
            apply_theme_to_parts();
        }
        session_browser_->set_session_root(settings.session_root);
        status_bar_->set_disk_folder(settings.session_root);
        projection_.reset();
        live_view_->set_projection(nullptr);
        update_overlay_sources();
        refresh_sun();
        notifications_->notify(LogLevel::Info,
                               tr("Preferences saved; device and renderer changes take effect at the next start"));
    });
    dialog.exec();
}

void MainWindow::open_camera_at_start(const std::string& device_id, const std::optional<hal::CameraMode>& mode,
                                      bool start)
{
    if (device_id.empty()) {
        return;
    }
    // Queued on the camera worker in order: open, mode, start.
    camera_->open(device_id);
    if (mode.has_value()) {
        camera_->set_mode(*mode);
    }
    if (start) {
        camera_->start_stream();
    }
}

QString MainWindow::preview_report() const
{
    const std::shared_ptr<const PreviewFrame> frame = preview_->latest();
    const AcquisitionStats stats = camera_->acquisition_stats();
    QString report = QStringLiteral("renderer=%1 camera_fps=%2 display_fps=%3 frames=%4 lost=%5 pool_misses=%6")
                         .arg(live_view_->renderer_in_use() == Renderer::OpenGL ? QStringLiteral("opengl")
                                                                                : QStringLiteral("raster"))
                         .arg(stats.fps, 0, 'f', 2)
                         .arg(preview_->display_fps(), 0, 'f', 2)
                         .arg(stats.frames)
                         .arg(stats.lost)
                         .arg(stats.pool_misses);
    if (frame) {
        report += QStringLiteral(" shown=%1 skipped=%2 size=%3x%4 decode_ms=%5 process_ms=%6")
                      .arg(frame->shown)
                      .arg(frame->skipped)
                      .arg(frame->info.width)
                      .arg(frame->info.height)
                      .arg(frame->decode_ms, 0, 'f', 1)
                      .arg(frame->process_ms, 0, 'f', 1);
    }
    return report;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    save_layout();
    capture_panel_->stop();
    preview_->stop();
    QMainWindow::closeEvent(event);
}

}  // namespace cloudscope::desktop
