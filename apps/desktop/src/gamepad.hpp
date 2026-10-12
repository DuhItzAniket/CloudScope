// Gamepad input (P043, FR-DSP-10): SDL2's game-controller API read on a timer, and the mapping of the sticks to
// mount motion (a jog: the left stick sets the pan and tilt speed, A stops, B is the emergency stop). The source
// is an interface so that the tests drive the jog with a fake controller.
//
// Lives on the UI thread; the mount is called from there (the HAL allows every mount function from any thread).
#pragma once

#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/mount.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>

#include <cstdint>
#include <memory>

namespace cloudscope::desktop {

struct GamepadState {
    bool connected = false;
    QString name;
    double left_x = 0.0;  // -1 (left) .. 1 (right)
    double left_y = 0.0;  // -1 (up) .. 1 (down), as SDL reports it
    double right_x = 0.0;
    double right_y = 0.0;
    double left_trigger = 0.0;  // 0 .. 1
    double right_trigger = 0.0;
    bool button_a = false;
    bool button_b = false;
    bool button_x = false;
    bool button_y = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
};

class IGamepadSource {
public:
    IGamepadSource() = default;
    virtual ~IGamepadSource() = default;
    IGamepadSource(const IGamepadSource&) = delete;
    IGamepadSource& operator=(const IGamepadSource&) = delete;
    IGamepadSource(IGamepadSource&&) = delete;
    IGamepadSource& operator=(IGamepadSource&&) = delete;

    [[nodiscard]] virtual GamepadState poll() = 0;
};

// SDL2: the first game controller found; hot-plugging is followed. poll() pumps SDL's events.
class SdlGamepad final : public IGamepadSource {
public:
    SdlGamepad();
    ~SdlGamepad() override;
    SdlGamepad(const SdlGamepad&) = delete;
    SdlGamepad& operator=(const SdlGamepad&) = delete;
    SdlGamepad(SdlGamepad&&) = delete;
    SdlGamepad& operator=(SdlGamepad&&) = delete;

    [[nodiscard]] bool available() const { return initialised_; }  // false: SDL could not start (and why in reason())
    [[nodiscard]] QString reason() const { return reason_; }
    [[nodiscard]] GamepadState poll() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialised_ = false;
    QString reason_;
};

struct JogSettings {
    double dead_zone = 0.15;        // stick deflection below this is rest
    double max_speed_deg_s = 20.0;  // at full deflection
    double step_s = 0.25;           // how far ahead (in seconds of motion) each tick aims the target
    bool invert_tilt = false;       // true: stick up tilts down
};

// Turns gamepad states into mount commands on every tick(). The caller ticks it (a QTimer in the application,
// the test by hand) so that timing is under control.
class MountJog : public QObject {
    Q_OBJECT

public:
    MountJog(std::shared_ptr<hal::IMount> mount, IGamepadSource& source, JogSettings settings = {},
             QObject* parent = nullptr);

    // Reads the controller and commands the mount. Returns what it did, for the UI and the tests.
    struct Tick {
        GamepadState state;
        bool moving = false;
        bool stopped = false;
        bool emergency_stop = false;
        hal::MountPosition target;
        QString error;  // the mount refused the command
    };
    Tick tick();

    [[nodiscard]] const JogSettings& settings() const { return settings_; }
    void set_settings(const JogSettings& settings) { settings_ = settings; }
    [[nodiscard]] const GamepadState& last_state() const { return last_; }

Q_SIGNALS:
    void stateChanged(const GamepadState& state);

private:
    std::shared_ptr<hal::IMount> mount_;
    IGamepadSource& source_;
    JogSettings settings_;
    GamepadState last_;
    bool was_moving_ = false;
};

}  // namespace cloudscope::desktop
