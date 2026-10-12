#include "gamepad.hpp"

#include <cloudscope/common/log.hpp>

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop.gamepad");
    return instance;
}

double axis_value(SDL_GameController* controller, SDL_GameControllerAxis axis)
{
    const Sint16 raw = SDL_GameControllerGetAxis(controller, axis);
    return std::clamp(static_cast<double>(raw) / 32767.0, -1.0, 1.0);
}

bool button_down(SDL_GameController* controller, SDL_GameControllerButton button)
{
    return SDL_GameControllerGetButton(controller, button) != 0;
}

}  // namespace

struct SdlGamepad::Impl {
    SDL_GameController* controller = nullptr;
    int instance_id = -1;

    void open_first()
    {
        if (controller != nullptr) {
            return;
        }
        const int joysticks = SDL_NumJoysticks();
        for (int i = 0; i < joysticks; ++i) {
            if (SDL_IsGameController(i) == SDL_TRUE) {
                controller = SDL_GameControllerOpen(i);
                if (controller != nullptr) {
                    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
                    instance_id = joystick != nullptr ? SDL_JoystickInstanceID(joystick) : -1;
                    log().info("gamepad connected: {}", SDL_GameControllerName(controller));
                    return;
                }
            }
        }
    }

    void close()
    {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
            instance_id = -1;
        }
    }
};

SdlGamepad::SdlGamepad() : impl_(std::make_unique<Impl>())
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        reason_ = QString::fromUtf8(SDL_GetError());
        log().warn("SDL game controller subsystem not started: {}", SDL_GetError());
        return;
    }
    initialised_ = true;
    impl_->open_first();
}

SdlGamepad::~SdlGamepad()
{
    impl_->close();
    if (initialised_) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

GamepadState SdlGamepad::poll()
{
    GamepadState state;
    if (!initialised_) {
        return state;
    }
    // SDL_Event is a C union: the event type says which member is valid.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            impl_->open_first();
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && event.cdevice.which == impl_->instance_id) {
            log().info("gamepad disconnected");
            impl_->close();
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    if (impl_->controller == nullptr) {
        impl_->open_first();
    }
    SDL_GameController* controller = impl_->controller;
    if (controller == nullptr || SDL_GameControllerGetAttached(controller) == SDL_FALSE) {
        return state;
    }
    state.connected = true;
    state.name = QString::fromUtf8(SDL_GameControllerName(controller));
    state.left_x = axis_value(controller, SDL_CONTROLLER_AXIS_LEFTX);
    state.left_y = axis_value(controller, SDL_CONTROLLER_AXIS_LEFTY);
    state.right_x = axis_value(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    state.right_y = axis_value(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    state.left_trigger = std::max(axis_value(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.0);
    state.right_trigger = std::max(axis_value(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.0);
    state.button_a = button_down(controller, SDL_CONTROLLER_BUTTON_A);
    state.button_b = button_down(controller, SDL_CONTROLLER_BUTTON_B);
    state.button_x = button_down(controller, SDL_CONTROLLER_BUTTON_X);
    state.button_y = button_down(controller, SDL_CONTROLLER_BUTTON_Y);
    state.dpad_up = button_down(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    state.dpad_down = button_down(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    state.dpad_left = button_down(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    state.dpad_right = button_down(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    return state;
}

MountJog::MountJog(std::shared_ptr<hal::IMount> mount, IGamepadSource& source, JogSettings settings, QObject* parent)
    : QObject(parent), mount_(std::move(mount)), source_(source), settings_(settings)
{
}

MountJog::Tick MountJog::tick()
{
    Tick result;
    result.state = source_.poll();
    last_ = result.state;
    Q_EMIT stateChanged(last_);
    if (!mount_ || !result.state.connected) {
        return result;
    }
    const GamepadState& s = result.state;
    if (s.button_b) {
        mount_->emergency_stop();
        result.emergency_stop = true;
        was_moving_ = false;
        return result;
    }
    // The D-pad gives full deflection; the stick gives proportional speed.
    double x = s.left_x;
    double y = s.left_y;
    if (s.dpad_left) {
        x = -1.0;
    } else if (s.dpad_right) {
        x = 1.0;
    }
    if (s.dpad_up) {
        y = -1.0;
    } else if (s.dpad_down) {
        y = 1.0;
    }
    const double dead = std::clamp(settings_.dead_zone, 0.0, 0.9);
    const auto shaped = [dead](double value) {
        const double magnitude = std::abs(value);
        if (magnitude <= dead) {
            return 0.0;
        }
        return std::copysign((magnitude - dead) / (1.0 - dead), value);
    };
    const double pan_rate = shaped(x);
    const double tilt_rate = (settings_.invert_tilt ? 1.0 : -1.0) * shaped(y);  // stick up = tilt up
    if (s.button_a || (pan_rate == 0.0 && tilt_rate == 0.0)) {
        if (was_moving_ || s.button_a) {
            mount_->stop();
            result.stopped = true;
        }
        was_moving_ = false;
        return result;
    }
    auto status = mount_->status();
    auto capabilities = mount_->capabilities();
    if (!status || !capabilities) {
        result.error = QString::fromStdString((status ? capabilities.error() : status.error()).message);
        return result;
    }
    const double speed =
        std::min({settings_.max_speed_deg_s, capabilities->pan.max_speed_deg_s, capabilities->tilt.max_speed_deg_s});
    const double magnitude = std::clamp(std::hypot(pan_rate, tilt_rate), 0.0, 1.0);
    const double reach = speed * magnitude * settings_.step_s;  // degrees of travel this tick aims for
    // Successive ticks continue from the target of the previous one, so that the motion is a steady slew.
    const hal::MountPosition from = was_moving_ ? status->target : status->position;
    const hal::MountPosition target{
        .pan = Degrees(std::clamp(from.pan.value() + pan_rate * reach, capabilities->pan.minimum.value(),
                                  capabilities->pan.maximum.value())),
        .tilt = Degrees(std::clamp(from.tilt.value() + tilt_rate * reach, capabilities->tilt.minimum.value(),
                                   capabilities->tilt.maximum.value()))};
    const double commanded_speed = std::max(speed * magnitude, 0.01);
    if (auto moved = mount_->move_to(target, commanded_speed); !moved) {
        result.error = QString::fromStdString(moved.error().message);
        return result;
    }
    result.moving = true;
    result.target = target;
    was_moving_ = true;
    return result;
}

}  // namespace cloudscope::desktop
