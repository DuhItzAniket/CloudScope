#include "camera_panel.hpp"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

constexpr int kSliderResolution = 1000;  // steps for a continuous control

// Slider position <-> value for a control with range and step.
int slider_steps(const hal::ControlInfo& info)
{
    if (info.step > 0.0 && info.maximum > info.minimum) {
        return std::clamp(static_cast<int>(std::lround((info.maximum - info.minimum) / info.step)), 1, 1'000'000);
    }
    return kSliderResolution;
}

int to_slider(const hal::ControlInfo& info, double value)
{
    const int steps = slider_steps(info);
    if (info.maximum <= info.minimum) {
        return 0;
    }
    return std::clamp(static_cast<int>(std::lround((value - info.minimum) / (info.maximum - info.minimum) * steps)), 0,
                      steps);
}

double from_slider(const hal::ControlInfo& info, int position)
{
    const int steps = slider_steps(info);
    return info.minimum + (info.maximum - info.minimum) * static_cast<double>(position) / steps;
}

QString control_title(hal::CameraControl control)
{
    switch (control) {
    case hal::CameraControl::Exposure:
        return CameraPanel::tr("Exposure");
    case hal::CameraControl::Gain:
        return CameraPanel::tr("Gain");
    case hal::CameraControl::WhiteBalance:
        return CameraPanel::tr("White balance");
    case hal::CameraControl::Brightness:
        return CameraPanel::tr("Brightness");
    case hal::CameraControl::Contrast:
        return CameraPanel::tr("Contrast");
    case hal::CameraControl::Saturation:
        return CameraPanel::tr("Saturation");
    case hal::CameraControl::Gamma:
        return CameraPanel::tr("Gamma");
    case hal::CameraControl::Sharpness:
        return CameraPanel::tr("Sharpness");
    case hal::CameraControl::Focus:
        return CameraPanel::tr("Focus");
    }
    const std::string_view name = hal::to_string(control);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

}  // namespace

CameraPanel::CameraPanel(CameraService& service, ProfileStore profiles, QWidget* parent)
    : QWidget(parent),
      service_(service),
      store_(std::move(profiles)),
      controls_box_(new QGroupBox(tr("Controls (values read back from the device)"), this)),
      controls_form_(new QFormLayout(controls_box_)),
      no_controls_label_(new QLabel(tr("Open a camera to see its controls."), controls_box_))
{
    setAccessibleName(QStringLiteral("Camera panel"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(build_device_group());
    controls_form_->addRow(no_controls_label_);
    layout->addWidget(controls_box_);
    layout->addWidget(build_profile_group());
    layout->addStretch(1);
    connect_service();
    rebuild_devices();
    rebuild_for_camera();
    rebuild_profiles();
}

QWidget* CameraPanel::build_device_group()
{
    auto* group = new QGroupBox(tr("Device"), this);
    auto* layout = new QVBoxLayout(group);
    auto* device_row = new QHBoxLayout();
    device_box_ = new QComboBox(group);
    device_box_->setAccessibleName(QStringLiteral("Camera device"));
    device_box_->setToolTip(tr("Cameras CloudScope can use; simulated ones are marked"));
    refresh_button_ = new QPushButton(tr("Refresh"), group);
    refresh_button_->setToolTip(tr("Look for cameras again"));
    open_button_ = new QPushButton(tr("Open"), group);
    open_button_->setToolTip(tr("Open the selected camera, or close the open one"));
    device_row->addWidget(device_box_, 1);
    device_row->addWidget(refresh_button_);
    device_row->addWidget(open_button_);
    layout->addLayout(device_row);
    camera_label_ = new QLabel(tr("No camera open"), group);
    camera_label_->setAccessibleName(QStringLiteral("Camera state"));
    layout->addWidget(camera_label_);
    auto* mode_row = new QHBoxLayout();
    mode_box_ = new QComboBox(group);
    mode_box_->setAccessibleName(QStringLiteral("Camera mode"));
    mode_box_->setToolTip(tr("Resolution, pixel format and rate the camera offers (FR-CAM-03)"));
    stream_button_ = new QPushButton(tr("Start"), group);
    stream_button_->setToolTip(tr("Start or stop the live stream"));
    mode_row->addWidget(new QLabel(tr("Mode"), group));
    mode_row->addWidget(mode_box_, 1);
    mode_row->addWidget(stream_button_);
    layout->addLayout(mode_row);

    connect(refresh_button_, &QPushButton::clicked, this, [this] { service_.refresh_devices(); });
    connect(open_button_, &QPushButton::clicked, this, [this] {
        if (service_.is_open()) {
            service_.close();
        } else if (device_box_->currentIndex() >= 0) {
            service_.open(device_box_->currentData().toString().toStdString());
        }
    });
    connect(mode_box_, &QComboBox::activated, this, [this](int index) {
        if (updating_ || !service_.is_open() || index < 0) {
            return;
        }
        const auto& modes = service_.state().capabilities.modes;
        if (static_cast<std::size_t>(index) < modes.size()) {
            service_.set_mode(modes[static_cast<std::size_t>(index)]);
        }
    });
    connect(stream_button_, &QPushButton::clicked, this, [this] {
        if (service_.state().streaming) {
            service_.stop_stream();
        } else {
            service_.start_stream();
        }
    });
    return group;
}

QWidget* CameraPanel::build_profile_group()
{
    auto* group = new QGroupBox(tr("Profiles"), this);
    auto* row = new QHBoxLayout(group);
    profile_box_ = new QComboBox(group);
    profile_box_->setAccessibleName(QStringLiteral("Camera profile"));
    profile_box_->setToolTip(tr("Saved mode and control values (FR-CAM-07)"));
    apply_profile_button_ = new QPushButton(tr("Apply"), group);
    apply_profile_button_->setToolTip(tr("Apply the selected profile to the open camera"));
    save_profile_button_ = new QPushButton(tr("Save as…"), group);
    save_profile_button_->setToolTip(tr("Save the current mode and controls under a name"));
    delete_profile_button_ = new QPushButton(tr("Delete"), group);
    delete_profile_button_->setToolTip(tr("Delete the selected profile"));
    row->addWidget(profile_box_, 1);
    row->addWidget(apply_profile_button_);
    row->addWidget(save_profile_button_);
    row->addWidget(delete_profile_button_);

    connect(apply_profile_button_, &QPushButton::clicked, this, [this] { apply_profile(profile_box_->currentText()); });
    connect(save_profile_button_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save profile"), tr("Profile name:"), QLineEdit::Normal,
                                                   profile_box_->currentText(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (auto saved = save_profile(name.trimmed()); !saved) {
                Q_EMIT notice(tr("Profile not saved: %1").arg(QString::fromStdString(saved.error().message)));
            }
        }
    });
    connect(delete_profile_button_, &QPushButton::clicked, this, [this] {
        if (auto deleted = delete_profile(profile_box_->currentText()); !deleted) {
            Q_EMIT notice(tr("Profile not deleted: %1").arg(QString::fromStdString(deleted.error().message)));
        }
    });
    return group;
}

void CameraPanel::connect_service()
{
    connect(&service_, &CameraService::devicesChanged, this, &CameraPanel::rebuild_devices);
    connect(&service_, &CameraService::opened, this, &CameraPanel::rebuild_for_camera);
    connect(&service_, &CameraService::closed, this, &CameraPanel::rebuild_for_camera);
    connect(&service_, &CameraService::modeChanged, this, [this] {
        update_mode_box();
        for (const auto& [control, row] : rows_) {
            update_row(control);
        }
    });
    connect(&service_, &CameraService::controlChanged, this,
            [this](int control) { update_row(static_cast<hal::CameraControl>(control)); });
    connect(&service_, &CameraService::streamingChanged, this, [this](bool) { update_stream_state(); });
    connect(&service_, &CameraService::busyChanged, this, [this](bool busy) {
        open_button_->setEnabled(!busy);
        stream_button_->setEnabled(!busy && service_.is_open());
        apply_profile_button_->setEnabled(!busy && service_.is_open() && profile_box_->count() > 0);
    });
}

void CameraPanel::rebuild_devices()
{
    updating_ = true;
    const QString selected = device_box_->currentData().toString();
    device_box_->clear();
    for (const hal::DeviceInfo& device : service_.devices()) {
        QString text = QString::fromStdString(device.name) + QStringLiteral("  [") + QString::fromStdString(device.id) +
                       QStringLiteral("]");
        if (device.simulated) {
            text += tr("  (simulated)");
        }
        device_box_->addItem(text, QString::fromStdString(device.id));
    }
    const int index = device_box_->findData(selected);
    if (index >= 0) {
        device_box_->setCurrentIndex(index);
    }
    updating_ = false;
    open_button_->setEnabled(device_box_->count() > 0 || service_.is_open());
}

void CameraPanel::rebuild_for_camera()
{
    const CameraState& state = service_.state();
    updating_ = true;
    while (controls_form_->rowCount() > 0) {
        controls_form_->removeRow(0);
    }
    rows_.clear();
    no_controls_label_ = nullptr;
    if (!state.device.has_value()) {
        camera_label_->setText(tr("No camera open"));
        no_controls_label_ = new QLabel(tr("Open a camera to see its controls."), controls_box_);
        controls_form_->addRow(no_controls_label_);
        open_button_->setText(tr("Open"));
        mode_box_->clear();
        mode_box_->setEnabled(false);
        stream_button_->setEnabled(false);
        updating_ = false;
        update_stream_state();
        return;
    }
    camera_label_->setText(QString::fromStdString(state.device->name) +
                           (state.device->simulated ? tr(" (simulated)") : QString()));
    open_button_->setText(tr("Close"));
    mode_box_->setEnabled(true);
    stream_button_->setEnabled(true);
    update_mode_box();
    if (state.capabilities.controls.empty()) {
        no_controls_label_ = new QLabel(tr("This camera has no adjustable control."), controls_box_);
        controls_form_->addRow(no_controls_label_);
    }
    for (const hal::ControlInfo& info : state.capabilities.controls) {
        add_row(info);
    }
    updating_ = false;
    update_stream_state();
}

void CameraPanel::add_row(const hal::ControlInfo& info)
{
    ControlRow row{.info = info};
    auto* holder = new QWidget(controls_box_);
    auto* row_layout = new QHBoxLayout(holder);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row.slider = new QSlider(Qt::Horizontal, holder);
    row.slider->setRange(0, slider_steps(info));
    row.slider->setAccessibleName(control_title(info.control) + tr(" slider"));
    row.spin = new QDoubleSpinBox(holder);
    row.spin->setRange(info.minimum, info.maximum);
    row.spin->setSingleStep(info.step > 0.0 ? info.step : (info.maximum - info.minimum) / 100.0);
    row.spin->setDecimals(info.step >= 1.0 ? 0 : 3);
    row.spin->setSuffix(info.unit.empty() ? QString() : QStringLiteral(" ") + QString::fromStdString(info.unit));
    row.spin->setAccessibleName(control_title(info.control));
    row.spin->setToolTip(info.calibrated ? tr("%1 in %2, from %3 to %4")
                                               .arg(control_title(info.control), QString::fromStdString(info.unit))
                                               .arg(info.minimum)
                                               .arg(info.maximum)
                                         : tr("%1 on the driver's own scale, from %2 to %3 (not calibrated)")
                                               .arg(control_title(info.control))
                                               .arg(info.minimum)
                                               .arg(info.maximum));
    row_layout->addWidget(row.slider, 1);
    row_layout->addWidget(row.spin);
    if (info.supports_auto) {
        row.automatic = new QCheckBox(tr("Auto"), holder);
        row.automatic->setAccessibleName(control_title(info.control) + tr(" automatic"));
        row.automatic->setToolTip(tr("Let the camera set %1 by itself").arg(control_title(info.control)));
        row_layout->addWidget(row.automatic);
    }
    row.effective = new QLabel(holder);
    row.effective->setAccessibleName(control_title(info.control) + tr(" read-back"));
    row.effective->setMinimumWidth(90);
    row_layout->addWidget(row.effective);
    controls_form_->addRow(control_title(info.control), holder);
    const hal::CameraControl control = info.control;
    connect(row.slider, &QSlider::sliderReleased, this, [this, control] { request_control(control); });
    connect(row.slider, &QSlider::valueChanged, this, [this, control](int position) {
        if (updating_) {
            return;
        }
        const ControlRow& r = rows_.at(control);
        updating_ = true;
        r.spin->setValue(from_slider(r.info, position));
        updating_ = false;
        if (!r.slider->isSliderDown()) {
            request_control(control);
        }
    });
    connect(row.spin, &QDoubleSpinBox::editingFinished, this, [this, control] { request_control(control); });
    if (row.automatic != nullptr) {
        connect(row.automatic, &QCheckBox::toggled, this, [this, control](bool) {
            if (!updating_) {
                request_control(control);
            }
        });
    }
    rows_[info.control] = row;
    update_row(info.control);
}

void CameraPanel::update_mode_box()
{
    const CameraState& state = service_.state();
    updating_ = true;
    mode_box_->clear();
    int current = -1;
    for (std::size_t i = 0; i < state.capabilities.modes.size(); ++i) {
        const hal::CameraMode& mode = state.capabilities.modes[i];
        mode_box_->addItem(QString::fromStdString(mode_text(mode)));
        if (state.mode.has_value() && mode == *state.mode) {
            current = static_cast<int>(i);
        }
    }
    if (current >= 0) {
        mode_box_->setCurrentIndex(current);
    } else if (state.mode.has_value()) {
        // The device is in a mode it did not list: show it, marked.
        mode_box_->addItem(QString::fromStdString(mode_text(*state.mode)) + tr(" (not listed)"));
        mode_box_->setCurrentIndex(mode_box_->count() - 1);
    }
    updating_ = false;
}

QString CameraPanel::value_text(const hal::ControlInfo& info, double value)
{
    const int decimals = info.step >= 1.0 ? 0 : 3;
    QString text = QString::number(value, 'f', decimals);
    if (!info.unit.empty()) {
        text += QStringLiteral(" ") + QString::fromStdString(info.unit);
    }
    return text;
}

void CameraPanel::update_row(hal::CameraControl control)
{
    const auto found = rows_.find(control);
    if (found == rows_.end()) {
        return;
    }
    const ControlRow& row = found->second;
    const CameraState& state = service_.state();
    const auto known = state.controls.find(control);
    if (known == state.controls.end()) {
        return;
    }
    const hal::ControlState& cs = known->second;
    const bool was_updating = updating_;
    updating_ = true;
    row.slider->setValue(to_slider(row.info, cs.effective.value));
    row.spin->setValue(cs.effective.value);
    if (row.automatic != nullptr) {
        row.automatic->setChecked(cs.effective.automatic);
    }
    const bool manual = !cs.effective.automatic;
    row.slider->setEnabled(manual);
    row.spin->setEnabled(manual);
    QString text = value_text(row.info, cs.effective.value);
    if (cs.effective.automatic) {
        text += tr(" (auto)");
    }
    if (!cs.applied) {
        text = tr("device set %1 (asked %2)").arg(text, value_text(row.info, cs.requested.value));
        row.effective->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    } else {
        row.effective->setStyleSheet(QString());
    }
    row.effective->setText(text);
    row.effective->setToolTip(cs.applied ? tr("Value read back from the device")
                                         : tr("The device clamped, rounded away or ignored the request (FR-CAM-04)"));
    updating_ = was_updating;
}

void CameraPanel::update_stream_state()
{
    const CameraState& state = service_.state();
    stream_button_->setText(state.streaming ? tr("Stop") : tr("Start"));
    stream_button_->setEnabled(state.device.has_value() && !state.busy);
    apply_profile_button_->setEnabled(state.device.has_value() && !state.busy && profile_box_->count() > 0);
}

void CameraPanel::request_control(hal::CameraControl control)
{
    if (updating_) {
        return;
    }
    const auto found = rows_.find(control);
    if (found == rows_.end()) {
        return;
    }
    const ControlRow& row = found->second;
    const hal::ControlSetting setting{.value = row.spin->value(),
                                      .automatic = row.automatic != nullptr && row.automatic->isChecked()};
    service_.set_control(control, setting);
}

void CameraPanel::rebuild_profiles()
{
    auto loaded = store_.load();
    if (!loaded) {
        Q_EMIT notice(tr("Camera profiles not read: %1").arg(QString::fromStdString(loaded.error().message)));
        profiles_.clear();
    } else {
        profiles_ = std::move(*loaded);
    }
    const QString selected = profile_box_->currentText();
    profile_box_->clear();
    for (const CameraProfile& profile : profiles_) {
        profile_box_->addItem(QString::fromStdString(profile.name));
    }
    const int index = profile_box_->findText(selected);
    if (index >= 0) {
        profile_box_->setCurrentIndex(index);
    }
    const bool any = profile_box_->count() > 0;
    apply_profile_button_->setEnabled(any && service_.is_open());
    delete_profile_button_->setEnabled(any);
}

Expected<void> CameraPanel::save_profile(const QString& name)
{
    const CameraState& state = service_.state();
    if (!state.device.has_value()) {
        return fail(ErrorCode::Unavailable, "open a camera first");
    }
    CameraProfile profile{
        .name = name.toStdString(), .camera_id = state.device->id, .mode = state.mode, .controls = {}};
    for (const auto& [control, cs] : state.controls) {
        profile.controls[std::string(hal::to_string(control))] = cs.effective;
    }
    std::erase_if(profiles_, [&](const CameraProfile& p) { return p.name == profile.name; });
    profiles_.push_back(profile);
    std::ranges::sort(profiles_, [](const CameraProfile& a, const CameraProfile& b) { return a.name < b.name; });
    if (auto saved = store_.save(profiles_); !saved) {
        return fail(saved.error());
    }
    rebuild_profiles();
    profile_box_->setCurrentIndex(profile_box_->findText(name));
    return {};
}

Expected<void> CameraPanel::delete_profile(const QString& name)
{
    const std::size_t before = profiles_.size();
    std::erase_if(profiles_, [&](const CameraProfile& p) { return p.name == name.toStdString(); });
    if (profiles_.size() == before) {
        return fail(ErrorCode::NotFound, "no profile named \"" + name.toStdString() + "\"");
    }
    if (auto saved = store_.save(profiles_); !saved) {
        return fail(saved.error());
    }
    rebuild_profiles();
    return {};
}

void CameraPanel::apply_profile(const QString& name)
{
    const auto found =
        std::ranges::find_if(profiles_, [&](const CameraProfile& p) { return p.name == name.toStdString(); });
    if (found == profiles_.end() || !service_.is_open()) {
        return;
    }
    const CameraProfile& profile = *found;
    // The service applies controls one by one so that every read-back reaches the rows; the mode goes first.
    if (profile.mode.has_value()) {
        service_.set_mode(*profile.mode);
    }
    for (const auto& [control_name, setting] : profile.controls) {
        const auto control = camera_control_from_string(control_name);
        if (!control) {
            continue;
        }
        const bool offered =
            std::ranges::any_of(service_.state().capabilities.controls,
                                [&](const hal::ControlInfo& info) { return info.control == *control; });
        if (offered) {
            service_.set_control(*control, setting);
        } else {
            Q_EMIT notice(
                tr("Profile %1: this camera has no %2 control").arg(name, QString::fromStdString(control_name)));
        }
    }
    Q_EMIT notice(tr("Profile %1 applied; the read-back column shows what the camera took").arg(name));
}

}  // namespace cloudscope::desktop
