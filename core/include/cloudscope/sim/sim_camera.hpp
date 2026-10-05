// The simulated camera (FR-CTL-02): a HAL camera whose pictures come from a frame source.
//
// Two sources exist:
//   synthetic sky   moving clouds and a Sun, with a model of exposure, gain, white balance, brightness, sensor
//                   noise and automatic exposure; modes from 640 x 480 to 3840 x 2160 in the pixel formats
//                   BGR8, GRAY8, GRAY16, YUYV and MJPEG                         device "sim:camera:sky"
//   replay          the JPEG and PNG pictures of a folder, in name order, again and again; no controls;
//                   JPEG files are passed through unchanged in MJPEG mode       device "sim:camera:replay"
//
// The camera behaves like a real one where it matters to the code above it: frames arrive at the mode's rate
// (an exposure longer than the frame period lowers the rate), a reader that falls behind finds a few frames
// buffered and loses the rest, which shows as a gap in the sequence numbers, and the cable can be pulled.
// Every frame is labelled simulated (NFR-DATA-03).
#pragma once

#include "cloudscope/hal/camera.hpp"
#include "cloudscope/sim/sim_devices.hpp"
#include "cloudscope/sim/sim_rig.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace cloudscope::sim {

// Where the pictures of a simulated camera come from. Used by one thread at a time.
class IFrameSource {
public:
    struct Request {
        std::uint64_t index = 0;     // sequence number of the frame; lost frames leave gaps
        double stream_time_s = 0.0;  // seconds since streaming started, on the rig's clock
        // The camera's settings, one per control of capabilities() and in that order. A source with automatic
        // controls writes the value it chose into the entries that are set to automatic.
        std::span<hal::ControlSetting> settings;
    };

    IFrameSource() = default;
    virtual ~IFrameSource() = default;
    IFrameSource(const IFrameSource&) = delete;
    IFrameSource& operator=(const IFrameSource&) = delete;
    IFrameSource(IFrameSource&&) = delete;
    IFrameSource& operator=(IFrameSource&&) = delete;

    [[nodiscard]] virtual const hal::CameraCapabilities& capabilities() const = 0;
    // Called when streaming starts in `mode`, which is one of the modes of capabilities().
    [[nodiscard]] virtual Expected<void> prepare(const hal::CameraMode& mode) = 0;
    // Writes one picture in the prepared mode into `frame` (pixel data and size). The frame's buffer holds
    // at least frame_buffer_bytes() for that mode.
    [[nodiscard]] virtual Expected<void> render(const Request& request, Frame& frame) = 0;
};

struct SyntheticSkyOptions {
    std::uint64_t seed = 1;
    double cloud_fraction = 0.4;
    double cloud_drift = 0.005;  // fraction of the image width per second
    bool sun_visible = true;
    double sun_x = 0.7;
    double sun_y = 0.3;
};

[[nodiscard]] std::unique_ptr<IFrameSource> make_synthetic_sky_source(const SyntheticSkyOptions& options);

// NotFound if the folder cannot be read or holds no JPEG or PNG file; Parse if its first picture cannot be
// decoded. All pictures are delivered at the size of the first one.
[[nodiscard]] Expected<std::unique_ptr<IFrameSource>> make_replay_source(const std::filesystem::path& folder,
                                                                         double fps);

// True if `folder` holds at least one JPEG or PNG file.
[[nodiscard]] bool has_replay_pictures(const std::filesystem::path& folder);

class SimCamera final : public hal::ICamera {
public:
    // `real_time` false: read_frame() never waits; a frame is ready whenever one is asked for.
    SimCamera(std::shared_ptr<SimRig> rig, hal::DeviceInfo info, std::unique_ptr<IFrameSource> source, bool real_time);
    ~SimCamera() override;
    SimCamera(const SimCamera&) = delete;
    SimCamera& operator=(const SimCamera&) = delete;
    SimCamera(SimCamera&&) = delete;
    SimCamera& operator=(SimCamera&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return state_.info(); }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override { return state_.is_open(); }

    [[nodiscard]] Expected<hal::CameraCapabilities> capabilities() const override;
    [[nodiscard]] Expected<hal::CameraMode> set_mode(const hal::CameraMode& mode) override;
    [[nodiscard]] Expected<hal::CameraMode> mode() const override;
    [[nodiscard]] Expected<hal::ControlState> set_control(hal::CameraControl control,
                                                          hal::ControlSetting setting) override;
    [[nodiscard]] Expected<hal::ControlSetting> control(hal::CameraControl control) const override;
    [[nodiscard]] Expected<void> start() override;
    void stop() override;
    [[nodiscard]] bool is_streaming() const override;
    [[nodiscard]] Expected<void> read_frame(Frame& frame, std::chrono::milliseconds timeout) override;

    // How many frames a reader that falls behind still finds waiting; older ones are lost.
    static constexpr std::uint64_t kBufferedFrames = 3;

private:
    using RealTime = std::chrono::steady_clock::time_point;

    [[nodiscard]] std::chrono::steady_clock::duration frame_period() const;  // caller holds mutex_

    SimDeviceState state_;
    std::unique_ptr<IFrameSource> source_;
    bool real_time_;
    mutable std::mutex mutex_;  // guards the members below: a control thread and the reading thread use them
    hal::CameraMode mode_;
    std::vector<hal::ControlSetting> settings_;  // parallel to the source's controls
    bool streaming_ = false;
    std::uint64_t next_sequence_ = 0;
    RealTime next_due_;             // when the next frame is complete (real time; used when real_time_)
    MonotonicTime stream_started_;  // on the rig's clock
};

}  // namespace cloudscope::sim
