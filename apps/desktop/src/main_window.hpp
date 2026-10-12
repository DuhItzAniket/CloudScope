// The main window (P033, FR-DSP-08): the docking workbench of the Qt Advanced Docking System with the live view in
// the centre and the panels around it, menus and shortcuts for every action, perspectives (named layouts), the
// layout saved on close and restored at start, and the themes. It wires the panels to the camera service, the
// preview pipeline, the notifications and the status bar.
//
// Lives on the UI thread.
#pragma once

#include "app_context.hpp"
#include "camera_panel.hpp"
#include "camera_service.hpp"
#include "capture_panel.hpp"
#include "console_panel.hpp"
#include "gamepad.hpp"
#include "histogram_panel.hpp"
#include "image_tools_panel.hpp"
#include "live_view.hpp"
#include "preview_pipeline.hpp"
#include "session_browser.hpp"
#include "shortcuts.hpp"
#include "status_bar.hpp"
#include "theme.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ads {
class CDockManager;
class CDockWidget;
}  // namespace ads

class QMenu;
class QTimer;

namespace cloudscope::desktop {

struct MainWindowOptions {
    Renderer renderer = Renderer::Auto;
    std::optional<Theme> theme = std::nullopt;  // overrides the configured theme
    bool restore_layout = true;                 // false: start from the default layout (tests, --reset-layout)
    bool gamepad = true;                        // false: never touch SDL (tests)
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppContext& context, MainWindowOptions options = {}, QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    // The parts, for the tests and for main().
    [[nodiscard]] CameraService& camera_service() const { return *camera_; }
    [[nodiscard]] PreviewPipeline& preview() const { return *preview_; }
    [[nodiscard]] LiveView& live_view() const { return *live_view_; }
    [[nodiscard]] CameraPanel& camera_panel() const { return *camera_panel_; }
    [[nodiscard]] HistogramPanel& histogram_panel() const { return *histogram_panel_; }
    [[nodiscard]] ImageToolsPanel& image_tools_panel() const { return *image_tools_panel_; }
    [[nodiscard]] CapturePanel& capture_panel() const { return *capture_panel_; }
    [[nodiscard]] SessionBrowser& session_browser() const { return *session_browser_; }
    [[nodiscard]] ConsolePanel& console_panel() const { return *console_panel_; }
    [[nodiscard]] StatusBar& status_bar() const { return *status_bar_; }
    [[nodiscard]] NotificationCenter& notifications() const { return *notifications_; }
    [[nodiscard]] ShortcutRegistry& shortcuts() const { return *shortcuts_; }
    [[nodiscard]] ads::CDockManager& dock_manager() const { return *dock_manager_; }
    [[nodiscard]] const std::map<QString, ads::CDockWidget*>& docks() const { return docks_; }
    [[nodiscard]] Theme theme() const { return theme_; }
    [[nodiscard]] std::optional<MountJog*> mount_jog() const
    {
        return mount_jog_ != nullptr ? std::optional<MountJog*>(mount_jog_) : std::nullopt;
    }

    // Layout persistence (QSettings INI next to config.toml): the dock state, the window geometry and the
    // perspectives. save_layout() is also what closing the window does.
    void save_layout() const;
    [[nodiscard]] bool restore_layout();
    [[nodiscard]] QByteArray dock_state() const;
    [[nodiscard]] std::filesystem::path state_file() const;

    void set_theme(Theme theme);
    void add_perspective(const QString& name);
    void open_perspective(const QString& name);
    [[nodiscard]] QStringList perspective_names() const;

    // Starts the configured default camera, or the one given, and optionally its stream (main() options).
    void open_camera_at_start(const std::string& device_id, const std::optional<hal::CameraMode>& mode, bool start);

    // A short text with the live view's numbers, for --exit-after and the benchmarks.
    [[nodiscard]] QString preview_report() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void build_actions();
    void build_docks();
    void build_menus();
    void connect_parts();
    void apply_theme_to_parts();
    void update_camera_status();
    void update_overlay_sources();
    void show_notifications();
    void show_preferences();
    void on_frame_ready();
    void refresh_sun();
    ads::CDockWidget* make_dock(const QString& id, const QString& title, QWidget* content);

    AppContext& context_;
    MainWindowOptions options_;
    Theme theme_;
    ShortcutRegistry* shortcuts_ = nullptr;
    NotificationCenter* notifications_ = nullptr;
    CameraService* camera_ = nullptr;
    PreviewPipeline* preview_ = nullptr;
    ads::CDockManager* dock_manager_ = nullptr;
    std::map<QString, ads::CDockWidget*> docks_;
    LiveView* live_view_ = nullptr;
    CameraPanel* camera_panel_ = nullptr;
    HistogramPanel* histogram_panel_ = nullptr;
    ImageToolsPanel* image_tools_panel_ = nullptr;
    CapturePanel* capture_panel_ = nullptr;
    SessionBrowser* session_browser_ = nullptr;
    ConsolePanel* console_panel_ = nullptr;
    StatusBar* status_bar_ = nullptr;
    QMenu* perspectives_menu_ = nullptr;
    QMenu* panels_menu_ = nullptr;
    QTimer* sun_timer_ = nullptr;
    QTimer* gamepad_timer_ = nullptr;
    std::unique_ptr<SdlGamepad> gamepad_;
    MountJog* mount_jog_ = nullptr;
    std::shared_ptr<hal::IMount> mount_;
    OverlaySettings overlays_;
    std::shared_ptr<const SkyProjection> projection_;
    QString projection_note_;
    bool layout_restored_ = false;
};

}  // namespace cloudscope::desktop
