// HAL: cameras (FR-CAM-01 to FR-CAM-08, FR-CAM-12).
//
// A camera offers modes (size, pixel format, rate) and controls (exposure, gain, ...). What it offers is
// discovered at run time (capabilities), never assumed; a user interface shows only what is listed.
//
// Read-back rule (FR-CAM-04): setting a mode or a control returns what is really in effect afterwards.
// Devices clamp, round or ignore requests; the caller is told, and must show the effective value.
//
// Frames are pulled, not pushed: after start(), the caller's acquisition thread calls read_frame() in a loop
// and the camera fills the frame it is handed. A camera therefore brings no thread of its own into the
// program, and one acquisition loop (capture pipeline, P022) serves every driver.
//
// Threads: read_frame() is called by one thread. While it runs, one other thread may call capabilities(),
// mode(), control(), set_control() and is_streaming(). open(), close(), set_mode(), start() and stop() must
// not overlap with a read_frame() call.
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/hal/device.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope::hal {

struct CameraMode {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Gray8;
    double fps = 0.0;  // nominal frames per second of this mode

    friend bool operator==(const CameraMode&, const CameraMode&) = default;
};

enum class CameraControl : std::uint8_t {
    Exposure,  // exposure time
    Gain,
    WhiteBalance,  // colour temperature
    Brightness,
    Contrast,
    Saturation,
    Gamma,
    Sharpness,
    Focus,
};

// "exposure", "gain", "white_balance", ...: the names used in profiles, sidecars and the API.
[[nodiscard]] std::string_view to_string(CameraControl control);

struct ControlInfo {
    CameraControl control = CameraControl::Exposure;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;  // smallest change the device makes; 0 if continuous
    double default_value = 0.0;
    std::string unit;            // "ms", "dB", "K"; empty when `calibrated` is false
    bool supports_auto = false;  // the device can set this control by itself
    bool calibrated = false;     // true: values are in `unit`; false: the driver's own scale (NFR-DATA-02)
};

// What a control with this range and step makes of a requested value: clamped to the range, then moved to
// the nearest step counted from the minimum. Never outside [minimum, maximum].
[[nodiscard]] double nearest_setting(const ControlInfo& info, double value);

struct ControlSetting {
    double value = 0.0;
    bool automatic = false;  // true: the device chooses; `value` is then what it chose last, if known

    friend bool operator==(const ControlSetting&, const ControlSetting&) = default;
};

// The answer to a set_control() call.
struct ControlState {
    ControlSetting requested;
    ControlSetting effective;  // read back from the device after the request
    bool applied = false;      // false: the device clamped, rounded away or ignored the request
};

struct CameraCapabilities {
    std::vector<CameraMode> modes;      // never empty for an open camera
    std::vector<ControlInfo> controls;  // may be empty: a camera without any adjustable control
};

class ICamera : public IDevice {
public:
    [[nodiscard]] virtual Expected<CameraCapabilities> capabilities() const = 0;

    // Selects one of the modes listed in the capabilities and returns the mode in effect.
    // InvalidArgument for a mode that is not listed; Unavailable while streaming.
    [[nodiscard]] virtual Expected<CameraMode> set_mode(const CameraMode& mode) = 0;
    // An open camera always has a mode; after open() it is the device's own choice.
    [[nodiscard]] virtual Expected<CameraMode> mode() const = 0;

    // A value outside the range is not an error: the device clamps it and the answer says so.
    // Unsupported for a control the camera does not list; InvalidArgument for a value that is not a number.
    [[nodiscard]] virtual Expected<ControlState> set_control(CameraControl control, ControlSetting setting) = 0;
    [[nodiscard]] virtual Expected<ControlSetting> control(CameraControl control) const = 0;

    // Starts streaming in the current mode; frames are numbered from 0 again. Unavailable if already streaming.
    [[nodiscard]] virtual Expected<void> start() = 0;
    // Ends streaming; does nothing if the camera is not streaming.
    virtual void stop() = 0;
    [[nodiscard]] virtual bool is_streaming() const = 0;

    // Waits up to `timeout` for the next frame and writes it into `frame`: the pixel data, its size, and every
    // field of frame.info() (`captured` is the host time at which the frame arrived; a gap in `sequence` means
    // frames were lost before this call). The frame needs a capacity of at least frame_buffer_bytes() for the
    // current mode.
    //   Timeout          no frame arrived in time; the stream is still running, call again
    //   Unavailable      the camera is not open or not streaming
    //   InvalidArgument  the frame's buffer is too small
    //   Io               the device failed or was unplugged: streaming has ended; close() and open() again
    // After an error the content of `frame` is unspecified.
    [[nodiscard]] virtual Expected<void> read_frame(Frame& frame, std::chrono::milliseconds timeout) = 0;
};

}  // namespace cloudscope::hal
