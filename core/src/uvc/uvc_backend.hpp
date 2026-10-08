// What the operating system's video-capture API must do for the UVC driver: one implementation per platform
// (Media Foundation on Windows, V4L2 on Linux). The platform-neutral UvcCamera does the HAL bookkeeping (open
// state, mode validation, control existence, read-back rules) and calls these primitives.
//
// Threads: open(), close(), set_mode(), start(), stop() and read_frame() are called by one thread at a time;
// capabilities(), mode(), control() and set_control() may be called by one other thread while read_frame() runs.
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/hal/camera.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cloudscope::uvc {

// A camera the platform API found. `path` is what the platform needs to open it (a symbolic link on Windows, a
// device node on Linux); `vendor_id` and `product_id` come from the USB descriptors when the API exposes them.
struct UvcDeviceDescriptor {
    std::string path;
    std::string name;  // the friendly name the operating system shows
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::string serial;  // empty when the platform does not expose it
};

class IUvcBackend {
public:
    IUvcBackend() = default;
    virtual ~IUvcBackend() = default;
    IUvcBackend(const IUvcBackend&) = delete;
    IUvcBackend& operator=(const IUvcBackend&) = delete;
    IUvcBackend(IUvcBackend&&) = delete;
    IUvcBackend& operator=(IUvcBackend&&) = delete;

    // NotFound if the device has gone, Unavailable if another program holds it, Io for other failures.
    [[nodiscard]] virtual Expected<void> open() = 0;
    virtual void close() = 0;

    // Valid after open(). Modes list only pixel formats CloudScope has a PixelFormat for; controls list only
    // those the device reports a range for.
    [[nodiscard]] virtual Expected<hal::CameraCapabilities> capabilities() = 0;
    [[nodiscard]] virtual Expected<hal::CameraMode> mode() = 0;
    // `mode` is one of capabilities().modes; the camera is not streaming.
    [[nodiscard]] virtual Expected<hal::CameraMode> set_mode(const hal::CameraMode& mode) = 0;

    // `control` is one of capabilities().controls; the value is finite. Returns the state really in effect.
    [[nodiscard]] virtual Expected<hal::ControlState> set_control(hal::CameraControl control,
                                                                  hal::ControlSetting setting) = 0;
    [[nodiscard]] virtual Expected<hal::ControlSetting> control(hal::CameraControl control) = 0;

    [[nodiscard]] virtual Expected<void> start() = 0;
    virtual void stop() = 0;

    // Fills the pixel data and every field of frame.info() except `simulated`; `captured` is the host time at
    // which the frame arrived, read from the clock the backend was made with. The frame has room for
    // frame_buffer_bytes() of the current mode. Timeout, Io as in ICamera::read_frame().
    [[nodiscard]] virtual Expected<void> read_frame(Frame& frame, std::chrono::milliseconds timeout) = 0;
};

// The platform's cameras, in a stable order (sorted by path).
[[nodiscard]] Expected<std::vector<UvcDeviceDescriptor>> enumerate_platform_cameras();

// A closed backend for one of the descriptors above. The clock must outlive the backend.
[[nodiscard]] std::unique_ptr<IUvcBackend> make_platform_backend(const UvcDeviceDescriptor& descriptor,
                                                                 const IClock& clock);

}  // namespace cloudscope::uvc
