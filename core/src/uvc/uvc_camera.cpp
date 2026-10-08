#include "cloudscope/uvc/uvc_camera.hpp"

#include "uvc_backend.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cloudscope::uvc {

using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::ControlState;
using hal::not_open;

UvcCamera::UvcCamera(hal::DeviceInfo info, std::unique_ptr<IUvcBackend> backend)
    : info_(std::move(info)), backend_(std::move(backend))
{
}

UvcCamera::~UvcCamera()
{
    UvcCamera::close();
}

Expected<void> UvcCamera::open()
{
    const std::lock_guard lock(mutex_);
    if (open_) {
        return {};
    }
    if (auto opened = backend_->open(); !opened) {
        return opened;
    }
    auto capabilities = backend_->capabilities();
    if (!capabilities) {
        backend_->close();
        return fail(capabilities.error());
    }
    capabilities_ = std::move(*capabilities);
    open_ = true;
    return {};
}

void UvcCamera::close()
{
    const std::lock_guard lock(mutex_);
    streaming_ = false;
    backend_->close();
    capabilities_ = {};
    open_ = false;
}

bool UvcCamera::is_open() const
{
    const std::lock_guard lock(mutex_);
    return open_;
}

Expected<CameraCapabilities> UvcCamera::capabilities() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return capabilities_;
}

Expected<CameraMode> UvcCamera::set_mode(const CameraMode& mode)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (streaming_) {
        return fail(ErrorCode::Unavailable,
                    fmt::format("camera '{}' is streaming; stop it before changing the mode", info_.id));
    }
    if (std::ranges::find(capabilities_.modes, mode) == capabilities_.modes.end()) {
        return fail(ErrorCode::InvalidArgument, fmt::format("camera '{}' has no mode {}x{} {} at {} fps", info_.id,
                                                            mode.width, mode.height, to_string(mode.format), mode.fps));
    }
    return backend_->set_mode(mode);
}

Expected<CameraMode> UvcCamera::mode() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return backend_->mode();
}

Expected<ControlState> UvcCamera::set_control(CameraControl control, ControlSetting setting)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (std::ranges::count(capabilities_.controls, control, &ControlInfo::control) == 0) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info_.id, to_string(control)));
    }
    if (!std::isfinite(setting.value)) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("the value for control '{}' is not a number", to_string(control)));
    }
    return backend_->set_control(control, setting);
}

Expected<ControlSetting> UvcCamera::control(CameraControl control) const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (std::ranges::count(capabilities_.controls, control, &ControlInfo::control) == 0) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info_.id, to_string(control)));
    }
    return backend_->control(control);
}

Expected<void> UvcCamera::start()
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (streaming_) {
        return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is already streaming", info_.id));
    }
    if (auto started = backend_->start(); !started) {
        return started;
    }
    streaming_ = true;
    return {};
}

void UvcCamera::stop()
{
    const std::lock_guard lock(mutex_);
    if (streaming_) {
        backend_->stop();
        streaming_ = false;
    }
}

bool UvcCamera::is_streaming() const
{
    const std::lock_guard lock(mutex_);
    return streaming_;
}

Expected<void> UvcCamera::read_frame(Frame& frame, std::chrono::milliseconds timeout)
{
    {
        const std::lock_guard lock(mutex_);
        if (!open_) {
            return not_open(info_);
        }
        if (!streaming_) {
            return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is not streaming", info_.id));
        }
        const auto current = backend_->mode();
        if (!current) {
            return fail(current.error());
        }
        const std::size_t needed = frame_buffer_bytes(current->format, current->width, current->height);
        if (frame.capacity() < needed) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format("a frame of camera '{}' needs {} bytes, the buffer has {}", info_.id, needed,
                                    frame.capacity()));
        }
    }
    // The wait happens outside the lock so that controls stay usable while a frame is awaited.
    auto result = backend_->read_frame(frame, timeout);
    if (!result && result.error().code == ErrorCode::Io) {
        const std::lock_guard lock(mutex_);
        streaming_ = false;
    }
    if (result) {
        frame.info().simulated = false;
    }
    return result;
}

}  // namespace cloudscope::uvc
