// The camera control panel (P035, FR-CAM-03 to FR-CAM-07): devices, modes, every control the camera lists with
// its real range and step, automatic toggles where the device offers them, the value read back from the device
// after each request (marked when the device clamped or ignored it), and named profiles.
//
// Lives on the UI thread; talks to the camera through CameraService only.
#pragma once

#include "camera_profiles.hpp"
#include "camera_service.hpp"

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <map>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;

namespace cloudscope::desktop {

class CameraPanel : public QWidget {
    Q_OBJECT

public:
    CameraPanel(CameraService& service, ProfileStore profiles, QWidget* parent = nullptr);

    // One row of the panel per control; exposed for the tests.
    struct ControlRow {
        hal::ControlInfo info;
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QCheckBox* automatic = nullptr;
        QLabel* effective = nullptr;  // the read-back, with the unit; says when the device did not take the request
    };
    [[nodiscard]] const std::map<hal::CameraControl, ControlRow>& rows() const { return rows_; }
    [[nodiscard]] QComboBox* device_box() const { return device_box_; }
    [[nodiscard]] QComboBox* mode_box() const { return mode_box_; }
    [[nodiscard]] QPushButton* open_button() const { return open_button_; }
    [[nodiscard]] QPushButton* stream_button() const { return stream_button_; }
    [[nodiscard]] QComboBox* profile_box() const { return profile_box_; }
    [[nodiscard]] const std::vector<CameraProfile>& profiles() const { return profiles_; }

    // Saves the current mode and controls under `name` (replacing a profile of that name).
    [[nodiscard]] Expected<void> save_profile(const QString& name);
    [[nodiscard]] Expected<void> delete_profile(const QString& name);
    void apply_profile(const QString& name);  // through the service; the read-back updates the rows

Q_SIGNALS:
    void notice(const QString& text);  // something the user should see (profile applied with changes, ...)

private:
    QWidget* build_device_group();
    QWidget* build_profile_group();
    void connect_service();
    void rebuild_devices();
    void rebuild_for_camera();
    void add_row(const hal::ControlInfo& info);
    void rebuild_profiles();
    void update_mode_box();
    void update_row(hal::CameraControl control);
    void update_stream_state();
    void request_control(hal::CameraControl control);
    [[nodiscard]] static QString value_text(const hal::ControlInfo& info, double value);

    CameraService& service_;
    ProfileStore store_;
    std::vector<CameraProfile> profiles_;
    QComboBox* device_box_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QPushButton* open_button_ = nullptr;
    QLabel* camera_label_ = nullptr;
    QComboBox* mode_box_ = nullptr;
    QPushButton* stream_button_ = nullptr;
    QGroupBox* controls_box_ = nullptr;
    QFormLayout* controls_form_ = nullptr;
    QLabel* no_controls_label_ = nullptr;
    std::map<hal::CameraControl, ControlRow> rows_;
    QComboBox* profile_box_ = nullptr;
    QPushButton* apply_profile_button_ = nullptr;
    QPushButton* save_profile_button_ = nullptr;
    QPushButton* delete_profile_button_ = nullptr;
    bool updating_ = false;
};

}  // namespace cloudscope::desktop
