// The camera from the user interface's point of view (P034, P035): one camera at a time, opened, configured and
// streamed through the HAL, with every device call made on a worker thread of its own so that the UI thread never
// does device I/O (architecture 4.1). Results and state changes come back to the UI thread as Qt signals.
//
// Lives on the UI thread. The public functions queue work and return at once; state() is the UI thread's copy of
// what the device last reported. The frame hub and the camera object are shared with other workers (preview
// pipeline, sequencer) under the rules of the HAL header: only this service's worker opens, closes, sets the mode
// or starts and stops the stream.
#pragma once

#include <cloudscope/capture/acquisition.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/common/error.hpp>
#include <cloudscope/common/executor.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/registry.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cloudscope::desktop {

struct CameraState {
    std::optional<hal::DeviceInfo> device = std::nullopt;  // empty: no camera open
    hal::CameraCapabilities capabilities;
    std::optional<hal::CameraMode> mode = std::nullopt;
    std::map<hal::CameraControl, hal::ControlState> controls;  // last request and read-back per control
    bool streaming = false;
    bool busy = false;  // a request is in progress on the worker
    AcquisitionStats stats;
};

class CameraService : public QObject {
    Q_OBJECT

public:
    CameraService(hal::DeviceRegistry& registry, const IClock& clock, QObject* parent = nullptr);
    ~CameraService() override;
    CameraService(const CameraService&) = delete;
    CameraService& operator=(const CameraService&) = delete;
    CameraService(CameraService&&) = delete;
    CameraService& operator=(CameraService&&) = delete;

    // Enumerates the registry's cameras on the worker; devicesChanged() follows.
    void refresh_devices();
    // Opens the camera (closing the current one first); opened() or failed() follows.
    void open(const std::string& device_id);
    void close();
    // Stops the stream if needed, sets the mode, and restarts the stream if it was running.
    void set_mode(const hal::CameraMode& mode);
    void set_control(hal::CameraControl control, hal::ControlSetting setting);
    void start_stream();
    void stop_stream();
    // Re-reads mode and every control from the device (after another party changed them).
    void read_back();

    [[nodiscard]] const CameraState& state() const { return state_; }
    [[nodiscard]] const std::vector<hal::DeviceInfo>& devices() const { return devices_; }
    [[nodiscard]] bool is_open() const { return state_.device.has_value(); }

    // Shared with other consumers. The hub exists while a camera is open; frames flow while streaming.
    [[nodiscard]] std::shared_ptr<FrameHub> hub() const;
    [[nodiscard]] std::shared_ptr<hal::ICamera> camera() const;
    [[nodiscard]] AcquisitionStats acquisition_stats() const;  // straight from the acquisition, any thread

    // For the capture sequencer (P039), called on its thread: brings the camera back after a failure by closing,
    // reopening, restoring the mode and restarting the acquisition. Blocks until done.
    [[nodiscard]] Expected<void> recover_blocking();
    // Runs `work` on the camera worker with the camera object and waits for it (other threads, never the UI).
    [[nodiscard]] Expected<void> with_camera_blocking(const std::function<Expected<void>(hal::ICamera&)>& work);

Q_SIGNALS:
    void devicesChanged();
    void opened();
    void closed();
    void modeChanged();
    void controlChanged(int control);  // hal::CameraControl
    void streamingChanged(bool streaming);
    void statsUpdated();
    void busyChanged(bool busy);
    void failed(QString what);  // a user-readable message; the state reflects what the device still is

private:
    struct Shared;  // the worker's view: camera, hub, acquisition

    void post(const std::function<void()>& work);   // UI -> worker, with the busy flag
    void on_ui(const std::function<void()>& work);  // worker -> UI
    [[nodiscard]] Expected<void> worker_open(const std::string& device_id);
    void worker_close();
    [[nodiscard]] Expected<void> worker_start();
    void worker_stop();
    [[nodiscard]] Expected<CameraState> worker_snapshot() const;
    void publish_state(const CameraState& snapshot);
    void report(const Error& error, const std::string& what);
    void timerEvent(QTimerEvent* event) override;

    hal::DeviceRegistry& registry_;
    const IClock& clock_;
    std::shared_ptr<Shared> shared_;
    QtExecutor ui_;
    CameraState state_;
    std::vector<hal::DeviceInfo> devices_;
    int pending_ = 0;
    int stats_timer_ = 0;
    ThreadPool worker_;  // last: joined first
};

// The error text the UI shows for a device failure: the message plus what to do.
[[nodiscard]] QString user_message(const Error& error, const std::string& what);

}  // namespace cloudscope::desktop
