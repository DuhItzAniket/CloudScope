// A real USB (UVC) camera behind the HAL camera interface (FR-CAM-01 to FR-CAM-08).
//
// The platform's capture API (Media Foundation on Windows, V4L2 on Linux) is behind a backend; this class holds
// the rules every camera must follow: a closed camera refuses what needs the device, modes must be listed,
// controls must be offered, no mode change while streaming, read-back of what is really in effect.
#pragma once

#include "cloudscope/hal/camera.hpp"

#include <chrono>
#include <memory>
#include <mutex>

namespace cloudscope::uvc {

class IUvcBackend;

class UvcCamera final : public hal::ICamera {
public:
    UvcCamera(hal::DeviceInfo info, std::unique_ptr<IUvcBackend> backend);
    ~UvcCamera() override;
    UvcCamera(const UvcCamera&) = delete;
    UvcCamera& operator=(const UvcCamera&) = delete;
    UvcCamera(UvcCamera&&) = delete;
    UvcCamera& operator=(UvcCamera&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override;

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

private:
    hal::DeviceInfo info_;
    std::unique_ptr<IUvcBackend> backend_;
    mutable std::mutex mutex_;  // guards open_, streaming_, capabilities_ and the backend's control calls
    bool open_ = false;
    bool streaming_ = false;
    hal::CameraCapabilities capabilities_;
};

}  // namespace cloudscope::uvc
