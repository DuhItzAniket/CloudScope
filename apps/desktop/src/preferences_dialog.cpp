#include "preferences_dialog.hpp"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <map>

namespace cloudscope::desktop {

namespace {

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

QString text_of(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QDoubleSpinBox* number_box(QWidget* parent, double minimum, double maximum, int decimals, const QString& suffix,
                           const QString& name, const QString& tip)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setSuffix(suffix);
    box->setAccessibleName(name);
    box->setToolTip(tip);
    return box;
}

QWidget* path_row(QWidget* parent, QLineEdit* edit, bool folder, const QString& caption)
{
    auto* holder = new QWidget(parent);
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton(PreferencesDialog::tr("Browse…"), holder);
    browse->setAccessibleName(caption + PreferencesDialog::tr(" browse"));
    browse->setToolTip(caption);
    row->addWidget(edit, 1);
    row->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, holder, [holder, edit, folder, caption] {
        const QString chosen = folder ? QFileDialog::getExistingDirectory(holder, caption, edit->text())
                                      : QFileDialog::getOpenFileName(holder, caption, edit->text(),
                                                                     PreferencesDialog::tr("Camera model (*.json)"));
        if (!chosen.isEmpty()) {
            edit->setText(chosen);
        }
    });
    return holder;
}

void select_data(QComboBox* box, const QString& value)
{
    box->setCurrentIndex(std::max(box->findData(value), 0));
}

}  // namespace

PreferencesDialog::PreferencesDialog(AppContext& context, const ShortcutRegistry* shortcuts, QWidget* parent)
    : QDialog(parent), context_(context), shortcuts_(shortcuts), message_(new QLabel(this))
{
    setWindowTitle(tr("Preferences"));
    setAccessibleName(QStringLiteral("Preferences dialog"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(build_devices_tab(), tr("Devices"));
    tabs->addTab(build_paths_tab(), tr("Paths"));
    tabs->addTab(build_site_tab(), tr("Site and pointing"));
    tabs->addTab(build_display_tab(), tr("Display and units"));
    tabs->addTab(build_shortcuts_tab(), tr("Shortcuts"));
    layout->addWidget(tabs, 1);
    message_->setAccessibleName(QStringLiteral("Preferences message"));
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    load();
}

QWidget* PreferencesDialog::build_devices_tab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    default_camera_ = new QComboBox(page);
    default_camera_->setEditable(true);
    default_camera_->setAccessibleName(QStringLiteral("Default camera"));
    default_camera_->setToolTip(tr("Camera opened when the application starts; empty for none"));
    default_camera_->addItem(QString());
    for (const hal::DeviceInfo& device : context_.registry().enumerate(hal::DeviceKind::Camera)) {
        default_camera_->addItem(QString::fromStdString(device.id));
    }
    uvc_ = new QCheckBox(tr("Offer USB cameras (UVC)"), page);
    uvc_->setToolTip(tr("camera.uvc: takes effect at the next start"));
    simulation_ = new QCheckBox(tr("Offer simulated devices"), page);
    simulation_->setToolTip(tr("simulation.enabled: takes effect at the next start"));
    form->addRow(tr("Default camera"), default_camera_);
    form->addRow(uvc_);
    form->addRow(simulation_);
    return page;
}

QWidget* PreferencesDialog::build_paths_tab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    session_root_ = new QLineEdit(page);
    session_root_->setAccessibleName(QStringLiteral("Session folder"));
    session_root_->setToolTip(tr("Folder for sessions and the frame catalogue; empty for the Pictures folder"));
    calibration_file_ = new QLineEdit(page);
    calibration_file_->setAccessibleName(QStringLiteral("Calibration file"));
    calibration_file_->setToolTip(tr("Intrinsic calibration (camera model JSON) for the sky overlays; empty for none"));
    form->addRow(tr("Session folder"), path_row(page, session_root_, true, tr("Session folder")));
    form->addRow(tr("Calibration file"), path_row(page, calibration_file_, false, tr("Calibration file")));
    return page;
}

QWidget* PreferencesDialog::build_site_tab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    site_id_ = new QLineEdit(page);
    site_id_->setAccessibleName(QStringLiteral("Site id"));
    site_id_->setToolTip(tr("Letters, digits, - and _: used in file names and sidecars"));
    const QString degree = QStringLiteral("°");
    latitude_ = number_box(page, -90.0, 90.0, 5, degree, QStringLiteral("Latitude"), tr("North positive"));
    longitude_ = number_box(page, -180.0, 180.0, 5, degree, QStringLiteral("Longitude"), tr("East positive"));
    altitude_ =
        number_box(page, -500.0, 9000.0, 1, QStringLiteral(" m"), QStringLiteral("Altitude"), tr("Above sea level"));
    azimuth_ = number_box(page, 0.0, 359.999, 3, degree, QStringLiteral("Pointing azimuth"),
                          tr("Where the camera looks: azimuth from north through east"));
    elevation_ = number_box(page, -90.0, 90.0, 3, degree, QStringLiteral("Pointing elevation"),
                            tr("Where the camera looks: elevation above the horizon (90 = straight up)"));
    roll_ = number_box(page, -180.0, 180.0, 3, degree, QStringLiteral("Pointing roll"),
                       tr("Turn of the image about the optical axis"));
    form->addRow(tr("Site id"), site_id_);
    form->addRow(tr("Latitude"), latitude_);
    form->addRow(tr("Longitude"), longitude_);
    form->addRow(tr("Altitude"), altitude_);
    form->addRow(tr("Pointing azimuth"), azimuth_);
    form->addRow(tr("Pointing elevation"), elevation_);
    form->addRow(tr("Pointing roll"), roll_);
    return page;
}

QWidget* PreferencesDialog::build_display_tab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    theme_ = new QComboBox(page);
    theme_->setAccessibleName(QStringLiteral("Theme"));
    theme_->addItem(tr("Dark"), QStringLiteral("dark"));
    theme_->addItem(tr("Night (red)"), QStringLiteral("night"));
    renderer_ = new QComboBox(page);
    renderer_->setAccessibleName(QStringLiteral("Renderer"));
    renderer_->addItem(tr("Automatic"), QStringLiteral("auto"));
    renderer_->addItem(tr("OpenGL"), QStringLiteral("opengl"));
    renderer_->addItem(tr("Software"), QStringLiteral("raster"));
    renderer_->setToolTip(tr("Live-view renderer; takes effect at the next start"));
    time_display_ = new QComboBox(page);
    time_display_->setAccessibleName(QStringLiteral("Time display"));
    time_display_->addItem(tr("UTC"), QStringLiteral("utc"));
    time_display_->addItem(tr("Local"), QStringLiteral("local"));
    time_display_->addItem(tr("UTC and local"), QStringLiteral("both"));
    temperature_unit_ = new QComboBox(page);
    temperature_unit_->setAccessibleName(QStringLiteral("Temperature unit"));
    temperature_unit_->addItem(tr("Celsius"), QStringLiteral("celsius"));
    temperature_unit_->addItem(tr("Fahrenheit"), QStringLiteral("fahrenheit"));
    log_level_ = new QComboBox(page);
    log_level_->setAccessibleName(QStringLiteral("Log level"));
    for (const char* level : {"trace", "debug", "info", "warn", "error", "critical"}) {
        log_level_->addItem(QString::fromLatin1(level), QString::fromLatin1(level));
    }
    log_level_->setToolTip(tr("logging.level: takes effect at the next start"));
    form->addRow(tr("Theme"), theme_);
    form->addRow(tr("Live-view renderer"), renderer_);
    form->addRow(tr("Times"), time_display_);
    form->addRow(tr("Temperature"), temperature_unit_);
    form->addRow(tr("Log level"), log_level_);
    return page;
}

QWidget* PreferencesDialog::build_shortcuts_tab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    shortcuts_table_ = new QTableWidget(0, 3, page);
    shortcuts_table_->setAccessibleName(QStringLiteral("Shortcuts table"));
    shortcuts_table_->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut"), tr("Default")});
    shortcuts_table_->horizontalHeader()->setStretchLastSection(true);
    shortcuts_table_->verticalHeader()->setVisible(false);
    shortcuts_table_->setToolTip(tr("Edit the Shortcut column in Qt's spelling (Ctrl+Shift+F); empty removes it"));
    layout->addWidget(new QLabel(tr("Double-click a shortcut to change it. Conflicts are reported on Save."), page));
    layout->addWidget(shortcuts_table_, 1);
    return page;
}

void PreferencesDialog::load()
{
    const DesktopSettings& s = context_.settings();
    const nlohmann::json& effective = context_.config().effective;
    default_camera_->setCurrentText(QString::fromStdString(s.default_camera));
    uvc_->setChecked(effective.value("camera", nlohmann::json::object()).value("uvc", true));
    simulation_->setChecked(effective.value("simulation", nlohmann::json::object()).value("enabled", true));
    const std::string raw_root = effective.at("desktop").at("session_root").get<std::string>();
    session_root_->setText(QString::fromStdString(raw_root));
    session_root_->setPlaceholderText(path_text(default_session_root()));
    calibration_file_->setText(path_text(s.calibration_file));
    site_id_->setText(QString::fromStdString(s.site.id));
    latitude_->setValue(s.site.latitude_deg);
    longitude_->setValue(s.site.longitude_deg);
    altitude_->setValue(s.site.altitude_m);
    azimuth_->setValue(s.pointing.azimuth_deg);
    elevation_->setValue(s.pointing.elevation_deg);
    roll_->setValue(s.pointing.roll_deg);
    select_data(theme_, text_of(to_string(s.theme)));
    select_data(renderer_, QString::fromStdString(s.renderer));
    select_data(time_display_, QString::fromStdString(s.time_display));
    select_data(temperature_unit_, QString::fromStdString(s.temperature_unit));
    select_data(log_level_, QString::fromStdString(effective.at("logging").value("level", std::string("info"))));
    shortcuts_table_->setRowCount(0);
    if (shortcuts_ != nullptr) {
        for (const ActionSpec& spec : shortcuts_->specs()) {
            const int row = shortcuts_table_->rowCount();
            shortcuts_table_->insertRow(row);
            auto* action = new QTableWidgetItem(spec.text + QStringLiteral("  [") + spec.id + QStringLiteral("]"));
            action->setFlags(action->flags() & ~Qt::ItemIsEditable);
            action->setData(Qt::UserRole, spec.id);
            auto* current = new QTableWidgetItem(shortcut_text(shortcuts_->effective(spec.id)));
            auto* fallback = new QTableWidgetItem(shortcut_text(spec.shortcut));
            fallback->setFlags(fallback->flags() & ~Qt::ItemIsEditable);
            shortcuts_table_->setItem(row, 0, action);
            shortcuts_table_->setItem(row, 1, current);
            shortcuts_table_->setItem(row, 2, fallback);
        }
        shortcuts_table_->resizeColumnsToContents();
    }
}

void PreferencesDialog::set_shortcut_text(const QString& action_id, const QString& key_text)
{
    for (int row = 0; row < shortcuts_table_->rowCount(); ++row) {
        if (shortcuts_table_->item(row, 0)->data(Qt::UserRole).toString() == action_id) {
            shortcuts_table_->item(row, 1)->setText(key_text);
            return;
        }
    }
}

nlohmann::json PreferencesDialog::patch() const
{
    nlohmann::json desktop{
        {"theme", theme_->currentData().toString().toStdString()},
        {"renderer", renderer_->currentData().toString().toStdString()},
        {"session_root", session_root_->text().trimmed().toStdString()},
        {"default_camera", default_camera_->currentText().trimmed().toStdString()},
        {"time_display", time_display_->currentData().toString().toStdString()},
        {"temperature_unit", temperature_unit_->currentData().toString().toStdString()},
        {"calibration_file", calibration_file_->text().trimmed().toStdString()},
        {"site",
         {{"id", site_id_->text().trimmed().toStdString()},
          {"latitude_deg", latitude_->value()},
          {"longitude_deg", longitude_->value()},
          {"altitude_m", altitude_->value()}}},
        {"pointing",
         {{"azimuth_deg", azimuth_->value()}, {"elevation_deg", elevation_->value()}, {"roll_deg", roll_->value()}}}};
    nlohmann::json shortcuts = nlohmann::json::object();
    for (int row = 0; row < shortcuts_table_->rowCount(); ++row) {
        const QString id = shortcuts_table_->item(row, 0)->data(Qt::UserRole).toString();
        const QString current = shortcuts_table_->item(row, 1)->text().trimmed();
        const QString fallback = shortcuts_table_->item(row, 2)->text().trimmed();
        if (current != fallback) {
            shortcuts[id.toStdString()] = current.toStdString();
        }
    }
    desktop["shortcuts"] = shortcuts;
    return nlohmann::json{{"desktop", desktop},
                          {"camera", {{"uvc", uvc_->isChecked()}}},
                          {"simulation", {{"enabled", simulation_->isChecked()}}},
                          {"logging", {{"level", log_level_->currentData().toString().toStdString()}}}};
}

Expected<void> PreferencesDialog::save()
{
    error_text_.clear();
    const nlohmann::json document = patch();
    // Shortcut overrides are checked here with the registry's list, before the schema sees the document, so that
    // the message names the action.
    if (shortcuts_ != nullptr) {
        std::map<std::string, std::string> overrides;
        for (const auto& [id, key] : document.at("desktop").at("shortcuts").items()) {
            overrides[id] = key.get<std::string>();
        }
        if (auto parsed = ShortcutRegistry::parse_overrides(overrides, shortcuts_->specs()); !parsed) {
            error_text_ = QString::fromStdString(parsed.error().message);
            message_->setText(tr("Not saved: %1").arg(error_text_));
            return fail(parsed.error());
        }
    }
    auto written = context_.update_user_config(document);
    if (!written) {
        error_text_ = QString::fromStdString(written.error().message);
        message_->setText(tr("Not saved: %1").arg(error_text_));
        return fail(written.error());
    }
    QString note = tr("Saved to %1.").arg(path_text(context_.paths().user_config));
    for (const std::string& line : *written) {
        note += QStringLiteral("\n") + QString::fromStdString(line);
    }
    message_->setText(note);
    Q_EMIT saved();
    return {};
}

}  // namespace cloudscope::desktop
