#include "cloudscope/sim/sim_camera.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace cloudscope::sim {

namespace {

using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::not_open;

using SteadyClock = std::chrono::steady_clock;

// Returns when the time has come. A system's timed sleep may come back a little early (seen on Windows in
// P016); a frame must not exist before its time.
void sleep_until_reached(SteadyClock::time_point time)
{
    while (SteadyClock::now() < time) {
        std::this_thread::sleep_until(time);
    }
}

// Position of a control in the source's list, or the list's size if the source does not have it.
std::size_t index_of(const CameraCapabilities& capabilities, CameraControl control)
{
    const auto found = std::ranges::find(capabilities.controls, control, &ControlInfo::control);
    return static_cast<std::size_t>(found - capabilities.controls.begin());
}

}  // namespace

SimCamera::SimCamera(std::shared_ptr<SimRig> rig, hal::DeviceInfo info, std::unique_ptr<IFrameSource> source,
                     bool real_time)
    : state_(std::move(rig), std::move(info)),
      source_(std::move(source)),
      real_time_(real_time),
      mode_(source_->capabilities().modes.front())
{
    const std::vector<ControlInfo>& controls = source_->capabilities().controls;
    settings_.reserve(controls.size());
    for (const ControlInfo& control : controls) {
        settings_.push_back({.value = control.default_value, .automatic = false});
    }
}

SimCamera::~SimCamera()
{
    state_.close();
}

Expected<void> SimCamera::open()
{
    if (!state_.is_open() && !state_.rig().camera_connected()) {
        return fail(ErrorCode::NotFound, fmt::format("camera '{}' is not connected", info().id));
    }
    return state_.open();
}

void SimCamera::close()
{
    const std::lock_guard lock(mutex_);
    streaming_ = false;
    state_.close();
}

Expected<CameraCapabilities> SimCamera::capabilities() const
{
    if (!state_.is_open()) {
        return not_open(info());
    }
    return source_->capabilities();
}

Expected<CameraMode> SimCamera::set_mode(const CameraMode& mode)
{
    const std::lock_guard lock(mutex_);
    if (!state_.is_open()) {
        return not_open(info());
    }
    if (streaming_) {
        return fail(ErrorCode::Unavailable,
                    fmt::format("camera '{}' is streaming; stop it before changing the mode", info().id));
    }
    const std::vector<CameraMode>& modes = source_->capabilities().modes;
    if (std::ranges::find(modes, mode) == modes.end()) {
        return fail(ErrorCode::InvalidArgument, fmt::format("camera '{}' has no mode {}x{} {} at {} fps", info().id,
                                                            mode.width, mode.height, to_string(mode.format), mode.fps));
    }
    mode_ = mode;
    return mode_;
}

Expected<CameraMode> SimCamera::mode() const
{
    const std::lock_guard lock(mutex_);
    if (!state_.is_open()) {
        return not_open(info());
    }
    return mode_;
}

Expected<hal::ControlState> SimCamera::set_control(CameraControl control, ControlSetting setting)
{
    const std::lock_guard lock(mutex_);
    if (!state_.is_open()) {
        return not_open(info());
    }
    const CameraCapabilities& capabilities = source_->capabilities();
    const std::size_t index = index_of(capabilities, control);
    if (index == capabilities.controls.size()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info().id, to_string(control)));
    }
    if (!std::isfinite(setting.value)) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("the value for control '{}' is not a number", to_string(control)));
    }
    const ControlInfo& described = capabilities.controls[index];
    ControlSetting effective;
    effective.automatic = setting.automatic && described.supports_auto;
    // In automatic mode the device chooses: the value stays what it chose last.
    effective.value = effective.automatic ? settings_[index].value : hal::nearest_setting(described, setting.value);
    settings_[index] = effective;

    const bool same_value = std::abs(effective.value - setting.value) <= 1e-9 * std::max(1.0, std::abs(setting.value));
    const bool applied = effective.automatic == setting.automatic && (effective.automatic || same_value);
    return hal::ControlState{.requested = setting, .effective = effective, .applied = applied};
}

Expected<ControlSetting> SimCamera::control(CameraControl control) const
{
    const std::lock_guard lock(mutex_);
    if (!state_.is_open()) {
        return not_open(info());
    }
    const CameraCapabilities& capabilities = source_->capabilities();
    const std::size_t index = index_of(capabilities, control);
    if (index == capabilities.controls.size()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info().id, to_string(control)));
    }
    return settings_[index];
}

SteadyClock::duration SimCamera::frame_period() const
{
    double seconds = 1.0 / mode_.fps;
    // A sensor cannot deliver frames faster than it exposes them.
    const CameraCapabilities& capabilities = source_->capabilities();
    const std::size_t exposure = index_of(capabilities, CameraControl::Exposure);
    if (exposure != capabilities.controls.size() && capabilities.controls[exposure].unit == "ms") {
        seconds = std::max(seconds, settings_[exposure].value / 1000.0);
    }
    return std::chrono::duration_cast<SteadyClock::duration>(std::chrono::duration<double>(seconds));
}

Expected<void> SimCamera::start()
{
    const std::lock_guard lock(mutex_);
    if (!state_.is_open()) {
        return not_open(info());
    }
    if (streaming_) {
        return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is already streaming", info().id));
    }
    if (!state_.rig().camera_connected()) {
        return fail(ErrorCode::Io, fmt::format("camera '{}' was disconnected", info().id));
    }
    if (auto prepared = source_->prepare(mode_); !prepared) {
        return fail(prepared.error().with_context(fmt::format("camera '{}'", info().id)));
    }
    next_sequence_ = 0;
    next_due_ = SteadyClock::now() + frame_period();
    stream_started_ = state_.rig().clock().now_monotonic();
    streaming_ = true;
    return {};
}

void SimCamera::stop()
{
    const std::lock_guard lock(mutex_);
    streaming_ = false;
}

bool SimCamera::is_streaming() const
{
    const std::lock_guard lock(mutex_);
    return streaming_;
}

Expected<void> SimCamera::read_frame(Frame& frame, std::chrono::milliseconds timeout)
{
    SimRig& rig = state_.rig();
    const auto no_frame = [this] {
        return fail(ErrorCode::Timeout, fmt::format("camera '{}' delivered no frame in time", info().id));
    };

    std::uint64_t sequence = 0;
    CameraMode mode;
    std::vector<ControlSetting> settings;
    double stream_time_s = 0.0;
    {
        std::unique_lock lock(mutex_);
        if (!state_.is_open()) {
            return not_open(info());
        }
        if (!streaming_) {
            return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is not streaming", info().id));
        }
        const std::size_t needed = frame_buffer_bytes(mode_.format, mode_.width, mode_.height);
        if (frame.capacity() < needed) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format("a frame of camera '{}' needs {} bytes, the buffer has {}", info().id, needed,
                                    frame.capacity()));
        }
        if (!rig.camera_connected()) {
            streaming_ = false;
            return fail(ErrorCode::Io, fmt::format("camera '{}' was disconnected", info().id));
        }

        const SteadyClock::duration period = frame_period();
        if (real_time_) {
            const RealTime now = SteadyClock::now();
            const RealTime deadline = now + timeout;
            if (rig.camera_stalled() || next_due_ > deadline) {
                // Nothing will arrive within the timeout: wait it out, as a real driver would.
                lock.unlock();
                sleep_until_reached(deadline);
                return no_frame();
            }
            if (next_due_ > now) {
                const RealTime due = next_due_;
                lock.unlock();
                sleep_until_reached(due);
                lock.lock();
                if (!streaming_) {
                    return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is not streaming", info().id));
                }
            } else {
                // Frames have been waiting. The driver keeps a few; older ones are lost.
                const auto waiting = static_cast<std::uint64_t>((now - next_due_) / period) + 1;
                if (waiting > kBufferedFrames) {
                    const std::uint64_t lost = waiting - kBufferedFrames;
                    next_sequence_ += lost;
                    next_due_ += period * static_cast<SteadyClock::rep>(lost);
                }
            }
        } else if (rig.camera_stalled()) {
            return no_frame();
        }
        next_sequence_ += rig.take_lost_camera_frames();
        sequence = next_sequence_++;
        next_due_ += period;
        mode = mode_;
        settings = settings_;
        stream_time_s = std::chrono::duration<double>(rig.clock().now_monotonic() - stream_started_).count();
    }

    // The picture is made outside the lock, so that controls stay responsive while a frame is rendered.
    const IFrameSource::Request request{.index = sequence, .stream_time_s = stream_time_s, .settings = settings};
    if (auto rendered = source_->render(request, frame); !rendered) {
        const std::lock_guard lock(mutex_);
        streaming_ = false;
        return fail(rendered.error().with_context(fmt::format("camera '{}'", info().id)));
    }
    {
        // What automatic controls chose becomes the value read back, unless the control was changed meanwhile.
        const std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < settings_.size(); ++i) {
            if (settings_[i].automatic && settings[i].automatic) {
                settings_[i].value = settings[i].value;
            }
        }
    }
    frame.info() = {.sequence = sequence,
                    .captured = rig.clock().now(),
                    .width = mode.width,
                    .height = mode.height,
                    .format = mode.format,
                    .stride = static_cast<std::size_t>(mode.width) * bytes_per_pixel(mode.format),
                    .simulated = true};
    return {};
}

}  // namespace cloudscope::sim
