#include "camera_service.hpp"

#include <cloudscope/common/log.hpp>

#include <QtCore/QTimerEvent>
#include <fmt/format.h>

#include <future>
#include <utility>

namespace cloudscope::desktop {

namespace {

constexpr int kStatsIntervalMs = 500;

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop.camera");
    return instance;
}

}  // namespace

struct CameraService::Shared {
    // The worker alone changes the pointers; other threads read copies of them under the mutex.
    mutable std::mutex mutex;
    std::shared_ptr<hal::ICamera> camera;
    std::shared_ptr<FrameHub> hub;
    std::shared_ptr<Acquisition> acquisition;
    std::optional<hal::CameraMode> wanted_mode;  // restored by a recovery

    [[nodiscard]] std::shared_ptr<hal::ICamera> camera_copy() const
    {
        const std::lock_guard lock(mutex);
        return camera;
    }
    [[nodiscard]] std::shared_ptr<Acquisition> acquisition_copy() const
    {
        const std::lock_guard lock(mutex);
        return acquisition;
    }
};

QString user_message(const Error& error, const std::string& what)
{
    std::string advice;
    switch (error.code) {
    case ErrorCode::NotFound:
        advice = "Check that the camera is connected, then refresh the device list.";
        break;
    case ErrorCode::Unavailable:
        advice = "Another program may be using the camera, or it is not open.";
        break;
    case ErrorCode::Io:
        advice = "Unplug the camera, plug it in again and open it again.";
        break;
    case ErrorCode::Timeout:
        advice = "The camera delivered no frame in time; try another mode or a shorter exposure.";
        break;
    case ErrorCode::InvalidArgument:
    case ErrorCode::Unsupported:
        advice = "The camera does not offer this setting; choose one of the listed values.";
        break;
    default:
        break;
    }
    std::string text = what.empty() ? error.message : what + ": " + error.message;
    if (!advice.empty()) {
        text += " " + advice;
    }
    return QString::fromStdString(text);
}

CameraService::CameraService(hal::DeviceRegistry& registry, const IClock& clock, QObject* parent)
    : QObject(parent),
      registry_(registry),
      clock_(clock),
      shared_(std::make_shared<Shared>()),
      ui_(this),
      worker_("camera", 1)
{
}

CameraService::~CameraService()
{
    if (stats_timer_ != 0) {
        killTimer(stats_timer_);
    }
    const std::shared_ptr<Shared> shared = shared_;
    worker_.post([shared] {
        const std::lock_guard lock(shared->mutex);
        if (shared->acquisition) {
            shared->acquisition->stop();
            shared->acquisition.reset();
        }
        if (shared->camera) {
            shared->camera->close();
            shared->camera.reset();
        }
        shared->hub.reset();
    });
    worker_.wait_idle();
}

void CameraService::post(const std::function<void()>& work)
{
    ++pending_;
    if (pending_ == 1) {
        state_.busy = true;
        Q_EMIT busyChanged(true);
    }
    worker_.post([this, work] {
        work();
        on_ui([this] {
            --pending_;
            if (pending_ == 0) {
                state_.busy = false;
                Q_EMIT busyChanged(false);
            }
        });
    });
}

void CameraService::on_ui(const std::function<void()>& work)
{
    ui_.post(work);
}

void CameraService::report(const Error& error, const std::string& what)
{
    log().warn("{}: {}", what, error.to_string());
    const QString message = user_message(error, what);
    on_ui([this, message] { Q_EMIT failed(message); });
}

Expected<CameraState> CameraService::worker_snapshot() const
{
    CameraState snapshot;
    const std::shared_ptr<hal::ICamera> camera = shared_->camera_copy();
    if (!camera) {
        return snapshot;
    }
    snapshot.device = camera->info();
    auto capabilities = camera->capabilities();
    if (!capabilities) {
        return fail(capabilities.error());
    }
    snapshot.capabilities = std::move(*capabilities);
    auto mode = camera->mode();
    if (!mode) {
        return fail(mode.error());
    }
    snapshot.mode = *mode;
    for (const hal::ControlInfo& info : snapshot.capabilities.controls) {
        auto setting = camera->control(info.control);
        if (!setting) {
            return fail(setting.error());
        }
        snapshot.controls[info.control] = {.requested = *setting, .effective = *setting, .applied = true};
    }
    const std::shared_ptr<Acquisition> acquisition = shared_->acquisition_copy();
    snapshot.streaming = acquisition && acquisition->is_running();
    if (acquisition) {
        snapshot.stats = acquisition->stats();
    }
    return snapshot;
}

void CameraService::publish_state(const CameraState& snapshot)
{
    on_ui([this, snapshot] {
        const bool busy = state_.busy;
        state_ = snapshot;
        state_.busy = busy;
    });
}

Expected<void> CameraService::worker_open(const std::string& device_id)
{
    worker_close();
    auto created = registry_.create<hal::ICamera>(device_id);
    if (!created && created.error().code == ErrorCode::NotFound) {
        // A camera that was just released by another process can be missing from one enumeration and present in
        // the next; one more look before giving up.
        static_cast<void>(registry_.enumerate(hal::DeviceKind::Camera));
        created = registry_.create<hal::ICamera>(device_id);
    }
    if (!created) {
        return fail(created.error());
    }
    if (auto opened = (*created)->open(); !opened) {
        return fail(opened.error());
    }
    {
        const std::lock_guard lock(shared_->mutex);
        shared_->camera = *created;
        shared_->hub = std::make_shared<FrameHub>();
        shared_->wanted_mode.reset();
    }
    log().info("opened {} ({})", device_id, (*created)->info().name);
    return {};
}

void CameraService::worker_close()
{
    worker_stop();
    std::shared_ptr<hal::ICamera> camera;
    {
        const std::lock_guard lock(shared_->mutex);
        camera.swap(shared_->camera);
        shared_->hub.reset();
        shared_->wanted_mode.reset();
    }
    if (camera) {
        camera->close();
        log().info("closed {}", camera->info().id);
    }
}

Expected<void> CameraService::worker_start()
{
    std::shared_ptr<hal::ICamera> camera;
    std::shared_ptr<FrameHub> hub;
    {
        const std::lock_guard lock(shared_->mutex);
        if (shared_->acquisition && shared_->acquisition->is_running()) {
            return {};
        }
        camera = shared_->camera;
        hub = shared_->hub;
    }
    if (!camera) {
        return fail(ErrorCode::Unavailable, "no camera is open");
    }
    auto acquisition = std::make_shared<Acquisition>(camera, hub, clock_);
    if (auto started = acquisition->start(); !started) {
        return fail(started.error());
    }
    {
        const std::lock_guard lock(shared_->mutex);
        shared_->acquisition = acquisition;
    }
    on_ui([this] {
        if (stats_timer_ == 0) {
            stats_timer_ = startTimer(kStatsIntervalMs);
        }
        state_.streaming = true;
        Q_EMIT streamingChanged(true);
    });
    return {};
}

void CameraService::worker_stop()
{
    std::shared_ptr<Acquisition> acquisition;
    {
        const std::lock_guard lock(shared_->mutex);
        acquisition.swap(shared_->acquisition);
    }
    if (acquisition) {
        acquisition->stop();
        on_ui([this] {
            if (stats_timer_ != 0) {
                killTimer(stats_timer_);
                stats_timer_ = 0;
            }
            state_.streaming = false;
            Q_EMIT streamingChanged(false);
        });
    }
}

void CameraService::refresh_devices()
{
    post([this] {
        std::vector<hal::DeviceInfo> found = registry_.enumerate(hal::DeviceKind::Camera);
        on_ui([this, found = std::move(found)] {
            devices_ = found;
            Q_EMIT devicesChanged();
        });
    });
}

void CameraService::open(const std::string& device_id)
{
    post([this, device_id] {
        if (auto opened = worker_open(device_id); !opened) {
            publish_state(CameraState{});
            on_ui([this] { Q_EMIT closed(); });
            report(opened.error(), "Cannot open " + device_id);
            return;
        }
        auto snapshot = worker_snapshot();
        if (!snapshot) {
            worker_close();
            publish_state(CameraState{});
            on_ui([this] { Q_EMIT closed(); });
            report(snapshot.error(), "Cannot read the capabilities of " + device_id);
            return;
        }
        publish_state(*snapshot);
        on_ui([this] { Q_EMIT opened(); });
    });
}

void CameraService::close()
{
    post([this] {
        worker_close();
        publish_state(CameraState{});
        on_ui([this] { Q_EMIT closed(); });
    });
}

void CameraService::set_mode(const hal::CameraMode& mode)
{
    post([this, mode] {
        const std::shared_ptr<hal::ICamera> camera = shared_->camera_copy();
        if (!camera) {
            report(Error{.code = ErrorCode::Unavailable, .message = "no camera is open"}, "Cannot set the mode");
            return;
        }
        const bool was_streaming = [&] {
            const std::shared_ptr<Acquisition> acquisition = shared_->acquisition_copy();
            return acquisition && acquisition->is_running();
        }();
        worker_stop();
        auto result = camera->set_mode(mode);
        if (!result) {
            report(result.error(), "Cannot set the mode " + fmt::format("{}x{}", mode.width, mode.height));
        } else {
            const std::lock_guard lock(shared_->mutex);
            shared_->wanted_mode = *result;
        }
        if (was_streaming) {
            if (auto started = worker_start(); !started) {
                report(started.error(), "Cannot restart the stream");
            }
        }
        if (auto snapshot = worker_snapshot()) {
            publish_state(*snapshot);
            on_ui([this] { Q_EMIT modeChanged(); });
        } else {
            report(snapshot.error(), "Cannot read the camera back");
        }
    });
}

void CameraService::set_control(hal::CameraControl control, hal::ControlSetting setting)
{
    post([this, control, setting] {
        const std::shared_ptr<hal::ICamera> camera = shared_->camera_copy();
        if (!camera) {
            report(Error{.code = ErrorCode::Unavailable, .message = "no camera is open"},
                   fmt::format("Cannot set {}", hal::to_string(control)));
            return;
        }
        auto state = camera->set_control(control, setting);
        if (!state) {
            report(state.error(), fmt::format("Cannot set {}", hal::to_string(control)));
            return;
        }
        on_ui([this, control, result = *state] {
            state_.controls[control] = result;
            Q_EMIT controlChanged(static_cast<int>(control));
        });
    });
}

void CameraService::start_stream()
{
    post([this] {
        if (auto started = worker_start(); !started) {
            report(started.error(), "Cannot start the stream");
        }
    });
}

void CameraService::stop_stream()
{
    post([this] { worker_stop(); });
}

void CameraService::read_back()
{
    post([this] {
        if (auto snapshot = worker_snapshot()) {
            publish_state(*snapshot);
            on_ui([this] {
                Q_EMIT modeChanged();
                for (const auto& [control, unused] : state_.controls) {
                    Q_EMIT controlChanged(static_cast<int>(control));
                }
            });
        } else {
            report(snapshot.error(), "Cannot read the camera back");
        }
    });
}

std::shared_ptr<FrameHub> CameraService::hub() const
{
    const std::lock_guard lock(shared_->mutex);
    return shared_->hub;
}

std::shared_ptr<hal::ICamera> CameraService::camera() const
{
    return shared_->camera_copy();
}

AcquisitionStats CameraService::acquisition_stats() const
{
    const std::shared_ptr<Acquisition> acquisition = shared_->acquisition_copy();
    return acquisition ? acquisition->stats() : AcquisitionStats{};
}

Expected<void> CameraService::recover_blocking()
{
    std::future<Expected<void>> done = worker_.submit([this]() -> Expected<void> {
        const std::shared_ptr<hal::ICamera> camera = shared_->camera_copy();
        if (!camera) {
            return fail(ErrorCode::Unavailable, "no camera is open");
        }
        worker_stop();
        camera->close();
        if (auto opened = camera->open(); !opened) {
            return fail(opened.error());
        }
        std::optional<hal::CameraMode> wanted;
        {
            const std::lock_guard lock(shared_->mutex);
            wanted = shared_->wanted_mode;
        }
        if (wanted.has_value()) {
            if (auto mode = camera->set_mode(*wanted); !mode) {
                return fail(mode.error());
            }
        }
        if (auto started = worker_start(); !started) {
            return fail(started.error());
        }
        if (auto snapshot = worker_snapshot()) {
            publish_state(*snapshot);
        }
        log().info("recovered {}", camera->info().id);
        return {};
    });
    return done.get();
}

Expected<void> CameraService::with_camera_blocking(const std::function<Expected<void>(hal::ICamera&)>& work)
{
    std::future<Expected<void>> done = worker_.submit([this, work]() -> Expected<void> {
        const std::shared_ptr<hal::ICamera> camera = shared_->camera_copy();
        if (!camera) {
            return fail(ErrorCode::Unavailable, "no camera is open");
        }
        auto result = work(*camera);
        if (auto snapshot = worker_snapshot()) {
            publish_state(*snapshot);
            on_ui([this] {
                Q_EMIT modeChanged();
                for (const auto& [control, unused] : state_.controls) {
                    Q_EMIT controlChanged(static_cast<int>(control));
                }
            });
        }
        return result;
    });
    return done.get();
}

void CameraService::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != stats_timer_) {
        QObject::timerEvent(event);
        return;
    }
    const AcquisitionStats stats = acquisition_stats();
    state_.stats = stats;
    Q_EMIT statsUpdated();
    if (!stats.running && state_.streaming) {
        // The acquisition ended by itself: the camera failed (FR-CAM-08). Say so once.
        state_.streaming = false;
        killTimer(stats_timer_);
        stats_timer_ = 0;
        Q_EMIT streamingChanged(false);
        if (stats.last_error.has_value()) {
            Q_EMIT failed(user_message(*stats.last_error, "The stream stopped"));
        }
    }
}

}  // namespace cloudscope::desktop
