// Preferences (P041): the settings of config.toml the desktop application owns (devices, paths, site, pointing,
// units, display, shortcuts), edited in a dialog and written to the user's configuration file through
// AppContext::update_user_config(), which validates the whole document first: an invalid value is rejected with
// the validator's message and nothing is written.
//
// Lives on the UI thread.
#pragma once

#include "app_context.hpp"
#include "shortcuts.hpp"

#include <QtCore/QString>
#include <QtWidgets/QDialog>

#include <nlohmann/json.hpp>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace cloudscope::desktop {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    PreferencesDialog(AppContext& context, const ShortcutRegistry* shortcuts, QWidget* parent = nullptr);

    // The partial configuration document the controls describe (what Save writes).
    [[nodiscard]] nlohmann::json patch() const;
    // Validates and writes; on failure shows the message in the dialog and returns it.
    [[nodiscard]] Expected<void> save();
    [[nodiscard]] QString last_error() const { return error_text_; }

    // Access for the tests.
    [[nodiscard]] QDoubleSpinBox* latitude() const { return latitude_; }
    [[nodiscard]] QLineEdit* site_id() const { return site_id_; }
    [[nodiscard]] QTableWidget* shortcuts_table() const { return shortcuts_table_; }
    [[nodiscard]] QComboBox* theme_box() const { return theme_; }
    void set_shortcut_text(const QString& action_id, const QString& key_text);

Q_SIGNALS:
    void saved();

private:
    QWidget* build_devices_tab();
    QWidget* build_paths_tab();
    QWidget* build_site_tab();
    QWidget* build_display_tab();
    QWidget* build_shortcuts_tab();
    void load();

    AppContext& context_;
    const ShortcutRegistry* shortcuts_;
    QComboBox* default_camera_ = nullptr;
    QCheckBox* uvc_ = nullptr;
    QCheckBox* simulation_ = nullptr;
    QLineEdit* session_root_ = nullptr;
    QLineEdit* calibration_file_ = nullptr;
    QLineEdit* site_id_ = nullptr;
    QDoubleSpinBox* latitude_ = nullptr;
    QDoubleSpinBox* longitude_ = nullptr;
    QDoubleSpinBox* altitude_ = nullptr;
    QDoubleSpinBox* azimuth_ = nullptr;
    QDoubleSpinBox* elevation_ = nullptr;
    QDoubleSpinBox* roll_ = nullptr;
    QComboBox* time_display_ = nullptr;
    QComboBox* temperature_unit_ = nullptr;
    QComboBox* theme_ = nullptr;
    QComboBox* renderer_ = nullptr;
    QComboBox* log_level_ = nullptr;
    QTableWidget* shortcuts_table_ = nullptr;
    QLabel* message_ = nullptr;
    QString error_text_;
};

}  // namespace cloudscope::desktop
