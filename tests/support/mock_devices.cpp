#include "mock_devices.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <numeric>

namespace cloudscope::test {

using hal::AxisLimits;
using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::ControlState;
using hal::DeviceInfo;
using hal::DeviceKind;
using hal::ImuCapabilities;
using hal::ImuSample;
using hal::InferenceCapabilities;
using hal::MountCapabilities;
using hal::MountMotion;
using hal::MountPosition;
using hal::MountStatus;
using hal::not_open;
using hal::SensorQuantity;
using hal::SensorReading;
using hal::Tensor;

namespace {

DeviceInfo mock_info(std::string id, DeviceKind kind, std::string name)
{
    return {.id = std::move(id), .kind = kind, .name = std::move(name), .driver = "mock", .simulated = true};
}

}  // namespace

// ---------------------------------------------------------------------------------------- camera

namespace {

CameraCapabilities mock_camera_capabilities()
{
    return {
        .modes =
            {
                {.width = 640, .height = 480, .format = PixelFormat::Gray8, .fps = 60.0},
                {.width = 320, .height = 240, .format = PixelFormat::Gray8, .fps = 120.0},
                {.width = 640, .height = 480, .format = PixelFormat::Bgr8, .fps = 30.0},
            },
        .controls =
            {
                {.control = CameraControl::Exposure,
                 .minimum = 0.1,
                 .maximum = 1000.0,
                 .step = 0.1,
                 .default_value = 10.0,
                 .unit = "ms",
                 .supports_auto = true,
                 .calibrated = true},
                {.control = CameraControl::Gain,
                 .minimum = 0.0,
                 .maximum = 24.0,
                 .step = 0.5,
                 .default_value = 0.0,
                 .unit = "dB",
                 .supports_auto = false,
                 .calibrated = true},
                {.control = CameraControl::Brightness,
                 .minimum = 0.0,
                 .maximum = 255.0,
                 .step = 1.0,
                 .default_value = 128.0,
                 .unit = "",
                 .supports_auto = false,
                 .calibrated = false},
            },
    };
}

}  // namespace

MockCamera::MockCamera(const IClock& clock, std::string id)
    : clock_(clock),
      info_(mock_info(std::move(id), DeviceKind::Camera, "Mock camera")),
      capabilities_(mock_camera_capabilities()),
      mode_(capabilities_.modes.front())
{
    settings_.reserve(capabilities_.controls.size());
    for (const ControlInfo& control : capabilities_.controls) {
        settings_.push_back({.value = control.default_value, .automatic = false});
    }
}

Expected<void> MockCamera::open()
{
    const std::lock_guard lock(mutex_);
    if (!plugged_in_) {
        return fail(ErrorCode::NotFound, fmt::format("camera '{}' is not connected", info_.id));
    }
    open_ = true;
    return {};
}

void MockCamera::close()
{
    const std::lock_guard lock(mutex_);
    streaming_ = false;
    open_ = false;
}

bool MockCamera::is_open() const
{
    const std::lock_guard lock(mutex_);
    return open_;
}

Expected<CameraCapabilities> MockCamera::capabilities() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return capabilities_;
}

Expected<CameraMode> MockCamera::set_mode(const CameraMode& mode)
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
    mode_ = mode;
    return mode_;
}

Expected<CameraMode> MockCamera::mode() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return mode_;
}

Expected<ControlState> MockCamera::set_control(CameraControl control, ControlSetting setting)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    const auto found = std::ranges::find(capabilities_.controls, control, &ControlInfo::control);
    if (found == capabilities_.controls.end()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info_.id, to_string(control)));
    }
    if (!std::isfinite(setting.value)) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("the value for control '{}' is not a number", to_string(control)));
    }
    // What a real device does with a request: clamp to the range, snap to its step, ignore "auto" if it has none.
    ControlSetting effective;
    effective.value = hal::nearest_setting(*found, setting.value);
    effective.automatic = setting.automatic && found->supports_auto;
    settings_[static_cast<std::size_t>(found - capabilities_.controls.begin())] = effective;

    const bool same_value = std::abs(effective.value - setting.value) < 1e-9;
    const bool applied = effective.automatic == setting.automatic && (effective.automatic || same_value);
    return ControlState{.requested = setting, .effective = effective, .applied = applied};
}

Expected<ControlSetting> MockCamera::control(CameraControl control) const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    const auto found = std::ranges::find(capabilities_.controls, control, &ControlInfo::control);
    if (found == capabilities_.controls.end()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' has no control '{}'", info_.id, to_string(control)));
    }
    return settings_[static_cast<std::size_t>(found - capabilities_.controls.begin())];
}

Expected<void> MockCamera::start()
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (streaming_) {
        return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is already streaming", info_.id));
    }
    if (!plugged_in_) {
        return fail(ErrorCode::Io, fmt::format("camera '{}' was unplugged", info_.id));
    }
    next_sequence_ = 0;
    streaming_ = true;
    return {};
}

void MockCamera::stop()
{
    const std::lock_guard lock(mutex_);
    streaming_ = false;
}

bool MockCamera::is_streaming() const
{
    const std::lock_guard lock(mutex_);
    return streaming_;
}

Expected<void> MockCamera::read_frame(Frame& frame, std::chrono::milliseconds /*timeout*/)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (!streaming_) {
        return fail(ErrorCode::Unavailable, fmt::format("camera '{}' is not streaming", info_.id));
    }
    if (!plugged_in_) {
        streaming_ = false;
        return fail(ErrorCode::Io, fmt::format("camera '{}' was unplugged", info_.id));
    }
    const std::size_t bytes = frame_buffer_bytes(mode_.format, mode_.width, mode_.height);
    if (frame.capacity() < bytes) {
        return fail(ErrorCode::InvalidArgument, fmt::format("a frame of camera '{}' needs {} bytes, the buffer has {}",
                                                            info_.id, bytes, frame.capacity()));
    }
    if (held_) {
        return fail(ErrorCode::Timeout, fmt::format("camera '{}' delivered no frame", info_.id));
    }
    const std::uint64_t sequence = next_sequence_++;
    std::memset(frame.buffer().data(), static_cast<int>(sequence & 0xFFU), bytes);
    frame.set_size(bytes);
    frame.info() = {.sequence = sequence,
                    .captured = clock_.now(),
                    .width = mode_.width,
                    .height = mode_.height,
                    .format = mode_.format,
                    .stride = static_cast<std::size_t>(mode_.width) * bytes_per_pixel(mode_.format),
                    .simulated = true};
    return {};
}

void MockCamera::lose_frames(std::uint64_t count)
{
    const std::lock_guard lock(mutex_);
    next_sequence_ += count;
}

void MockCamera::hold_frames(bool hold)
{
    const std::lock_guard lock(mutex_);
    held_ = hold;
}

void MockCamera::unplug()
{
    const std::lock_guard lock(mutex_);
    plugged_in_ = false;
}

void MockCamera::plug_in()
{
    const std::lock_guard lock(mutex_);
    plugged_in_ = true;
}

// ----------------------------------------------------------------------------------------- mount

MockMount::MockMount(const IClock& clock, std::string id)
    : clock_(clock),
      info_(mock_info(std::move(id), DeviceKind::Mount, "Mock pan-tilt mount")),
      capabilities_{.pan = {.minimum = Degrees(-135.0), .maximum = Degrees(135.0), .max_speed_deg_s = 60.0},
                    .tilt = {.minimum = Degrees(0.0), .maximum = Degrees(90.0), .max_speed_deg_s = 60.0},
                    .position_feedback = false},
      start_{.pan = Degrees(0.0), .tilt = Degrees(45.0)},
      target_(start_)
{
}

Expected<void> MockMount::open()
{
    const std::lock_guard lock(mutex_);
    open_ = true;
    return {};
}

void MockMount::close()
{
    const std::lock_guard lock(mutex_);
    halt();
    open_ = false;
}

bool MockMount::is_open() const
{
    const std::lock_guard lock(mutex_);
    return open_;
}

Expected<MountCapabilities> MockMount::capabilities() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return capabilities_;
}

Expected<void> MockMount::move_to(MountPosition target, double speed_deg_s)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (emergency_stop_) {
        return fail(ErrorCode::Unavailable,
                    fmt::format("mount '{}' is in emergency stop; clear it before moving", info_.id));
    }
    if (!fault_.empty()) {
        return fail(ErrorCode::Unavailable, fmt::format("mount '{}' reports a fault: {}", info_.id, fault_));
    }
    const auto inside = [](Degrees value, const AxisLimits& limits) {
        return value >= limits.minimum && value <= limits.maximum;  // false for NaN
    };
    if (!inside(target.pan, capabilities_.pan) || !inside(target.tilt, capabilities_.tilt)) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("target pan {:.2f} deg, tilt {:.2f} deg is outside the limits of mount '{}'",
                                target.pan.value(), target.tilt.value(), info_.id));
    }
    const double fastest = std::min(capabilities_.pan.max_speed_deg_s, capabilities_.tilt.max_speed_deg_s);
    const bool usable_speed = speed_deg_s > 0.0 && speed_deg_s <= fastest;  // false for NaN
    if (!usable_speed) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("speed {} deg/s is not within (0, {}] for mount '{}'", speed_deg_s, fastest, info_.id));
    }
    start_ = position_now();
    target_ = target;
    const double distance =
        std::max(std::abs((target_.pan - start_.pan).value()), std::abs((target_.tilt - start_.tilt).value()));
    move_duration_ = std::chrono::duration<double>(distance / speed_deg_s);
    move_started_ = clock_.now_monotonic();
    stopped_ = false;
    return {};
}

void MockMount::stop()
{
    const std::lock_guard lock(mutex_);
    halt();
}

void MockMount::emergency_stop()
{
    const std::lock_guard lock(mutex_);
    halt();
    emergency_stop_ = true;
}

Expected<void> MockMount::clear_emergency_stop()
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    emergency_stop_ = false;
    return {};
}

Expected<MountStatus> MockMount::status() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    MountStatus status;
    status.position = position_now();
    status.position_measured = false;  // like a servo without feedback: this is where it was told to be
    status.target = target_;
    status.emergency_stop = emergency_stop_;
    status.fault = fault_;
    status.time = clock_.now();
    if (!fault_.empty()) {
        status.motion = MountMotion::Fault;
    } else if (stopped_) {
        status.motion = MountMotion::Stopped;
    } else {
        status.motion = status.position == target_ ? MountMotion::Idle : MountMotion::Moving;
    }
    return status;
}

void MockMount::set_fault(std::string fault)
{
    const std::lock_guard lock(mutex_);
    fault_ = std::move(fault);
    if (!fault_.empty()) {
        halt();
    }
}

MountPosition MockMount::position_now() const
{
    if (stopped_ || move_duration_.count() <= 0.0) {
        return stopped_ ? start_ : target_;
    }
    const std::chrono::duration<double> elapsed = clock_.now_monotonic() - move_started_;
    const double fraction = std::clamp(elapsed / move_duration_, 0.0, 1.0);
    if (fraction >= 1.0) {
        return target_;
    }
    return {.pan = start_.pan + ((target_.pan - start_.pan) * fraction),
            .tilt = start_.tilt + ((target_.tilt - start_.tilt) * fraction)};
}

void MockMount::halt()
{
    const MountPosition here = position_now();
    stopped_ = !(here == target_);  // halting at the target is simply being idle
    start_ = here;
    move_duration_ = std::chrono::duration<double>(0.0);
}

// ----------------------------------------------------------------------------------- IMU, sensor

MockImu::MockImu(const IClock& clock, std::string id)
    : clock_(clock), info_(mock_info(std::move(id), DeviceKind::Imu, "Mock IMU"))
{
}

Expected<void> MockImu::open()
{
    open_ = true;
    return {};
}

Expected<ImuCapabilities> MockImu::capabilities() const
{
    if (!open_) {
        return not_open(info_);
    }
    return ImuCapabilities{.absolute_heading = false, .max_rate_hz = 100.0};
}

Expected<ImuSample> MockImu::read()
{
    if (!open_) {
        return not_open(info_);
    }
    return ImuSample{.time = clock_.now(), .orientation = orientation_, .accuracy = Degrees(1.0), .calibrated = true};
}

MockSensor::MockSensor(const IClock& clock, std::string id)
    : clock_(clock),
      info_(mock_info(std::move(id), DeviceKind::Sensor, "Mock environment sensor")),
      values_{{SensorQuantity::Temperature, 25.0},
              {SensorQuantity::RelativeHumidity, 40.0},
              {SensorQuantity::Pressure, 1013.25}}
{
}

Expected<void> MockSensor::open()
{
    open_ = true;
    return {};
}

Expected<std::vector<SensorQuantity>> MockSensor::quantities() const
{
    if (!open_) {
        return not_open(info_);
    }
    std::vector<SensorQuantity> list;
    list.reserve(values_.size());
    for (const auto& [quantity, value] : values_) {
        list.push_back(quantity);
    }
    return list;
}

Expected<std::vector<SensorReading>> MockSensor::read()
{
    if (!open_) {
        return not_open(info_);
    }
    const Timestamp now = clock_.now();
    std::vector<SensorReading> readings;
    for (const auto& [quantity, value] : values_) {
        if (value) {
            readings.push_back({.quantity = quantity, .value = *value, .time = now});
        }
    }
    return readings;
}

void MockSensor::set_value(SensorQuantity quantity, std::optional<double> value)
{
    const auto found = std::ranges::find(values_, quantity, &std::pair<SensorQuantity, std::optional<double>>::first);
    if (found != values_.end()) {
        found->second = value;
    }
}

// ------------------------------------------------------------------------------------- transport

PipeTransport::PipeTransport(std::string id, std::shared_ptr<Channel> incoming, std::shared_ptr<Channel> outgoing)
    : info_(mock_info(std::move(id), DeviceKind::Transport, "In-memory pipe")),
      incoming_(std::move(incoming)),
      outgoing_(std::move(outgoing))
{
}

std::pair<std::shared_ptr<PipeTransport>, std::shared_ptr<PipeTransport>> PipeTransport::make_pair()
{
    auto a_to_b = std::make_shared<Channel>();
    auto b_to_a = std::make_shared<Channel>();
    return {std::shared_ptr<PipeTransport>(new PipeTransport("mock:transport:a", b_to_a, a_to_b)),
            std::shared_ptr<PipeTransport>(new PipeTransport("mock:transport:b", a_to_b, b_to_a))};
}

Expected<void> PipeTransport::open()
{
    const std::lock_guard lock(incoming_->mutex);
    incoming_->reader_open = true;
    return {};
}

void PipeTransport::close()
{
    {
        const std::lock_guard lock(incoming_->mutex);
        incoming_->reader_open = false;
        incoming_->bytes.clear();
    }
    incoming_->changed.notify_all();
}

bool PipeTransport::is_open() const
{
    const std::lock_guard lock(incoming_->mutex);
    return incoming_->reader_open;
}

Expected<void> PipeTransport::write(std::span<const std::byte> data)
{
    if (!is_open()) {
        return not_open(info_);
    }
    {
        const std::lock_guard lock(outgoing_->mutex);
        if (!outgoing_->reader_open) {
            return fail(ErrorCode::Io, fmt::format("the other end of transport '{}' is closed", info_.id));
        }
        outgoing_->bytes.insert(outgoing_->bytes.end(), data.begin(), data.end());
    }
    outgoing_->changed.notify_all();
    return {};
}

Expected<std::size_t> PipeTransport::read(std::span<std::byte> buffer, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(incoming_->mutex);
    if (!incoming_->reader_open) {
        return not_open(info_);
    }
    incoming_->changed.wait_for(lock, timeout, [this] { return !incoming_->bytes.empty() || !incoming_->reader_open; });
    if (!incoming_->reader_open) {
        return not_open(info_);  // closed from another thread while waiting
    }
    const std::size_t count = std::min(buffer.size(), incoming_->bytes.size());
    const auto end = incoming_->bytes.begin() + static_cast<std::ptrdiff_t>(count);
    std::copy(incoming_->bytes.begin(), end, buffer.begin());
    incoming_->bytes.erase(incoming_->bytes.begin(), end);
    return count;
}

// ------------------------------------------------------------------------------------- inference

MockInference::MockInference(std::string id)
    : info_(mock_info(std::move(id), DeviceKind::Inference, "Mock inference engine"))
{
}

Expected<void> MockInference::open()
{
    open_ = true;
    return {};
}

void MockInference::close()
{
    open_ = false;
    loaded_ = false;
}

Expected<void> MockInference::load(const std::filesystem::path& model)
{
    if (!open_) {
        return not_open(info_);
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return fail(ErrorCode::NotFound, fmt::format("model file {} does not exist", model.string()));
    }
    loaded_ = true;
    return {};
}

Expected<InferenceCapabilities> MockInference::capabilities() const
{
    if (!open_) {
        return not_open(info_);
    }
    if (!loaded_) {
        return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
    }
    return InferenceCapabilities{.provider = "mock",
                                 .inputs = {{.name = "values", .shape = {-1, 4}}},
                                 .outputs = {{.name = "sum", .shape = {-1, 1}}, {.name = "doubled", .shape = {-1, 4}}}};
}

Expected<std::vector<Tensor>> MockInference::run(const std::vector<Tensor>& inputs)
{
    if (!open_) {
        return not_open(info_);
    }
    if (!loaded_) {
        return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
    }
    if (inputs.size() != 1 || inputs.front().name != "values") {
        return fail(ErrorCode::InvalidArgument, "the model takes exactly one input named 'values'");
    }
    const Tensor& values = inputs.front();
    if (values.shape.size() != 2 || values.shape[1] != 4 || values.shape[0] < 1 ||
        values.data.size() != hal::element_count(values.shape)) {
        return fail(ErrorCode::InvalidArgument, "input 'values' must have shape [N, 4] with N * 4 elements");
    }
    const std::int64_t batch = values.shape[0];
    Tensor sum{.name = "sum", .shape = {batch, 1}, .data = {}};
    Tensor doubled{.name = "doubled", .shape = {batch, 4}, .data = {}};
    for (std::int64_t row = 0; row < batch; ++row) {
        const auto begin = values.data.begin() + (row * 4);
        sum.data.push_back(std::accumulate(begin, begin + 4, 0.0F));
        std::transform(begin, begin + 4, std::back_inserter(doubled.data), [](float value) { return value * 2.0F; });
    }
    return std::vector<Tensor>{std::move(sum), std::move(doubled)};
}

// ---------------------------------------------------------------------------------------- driver

std::vector<DeviceInfo> MockDriver::enumerate()
{
    return {mock_info("mock:camera:0", DeviceKind::Camera, "Mock camera"),
            mock_info("mock:mount:0", DeviceKind::Mount, "Mock pan-tilt mount"),
            mock_info("mock:imu:0", DeviceKind::Imu, "Mock IMU"),
            mock_info("mock:sensor:0", DeviceKind::Sensor, "Mock environment sensor"),
            mock_info("mock:inference:0", DeviceKind::Inference, "Mock inference engine")};
}

Expected<std::shared_ptr<hal::IDevice>> MockDriver::create(std::string_view id)
{
    if (id == "mock:camera:0") {
        return std::make_shared<MockCamera>(clock_);
    }
    if (id == "mock:mount:0") {
        return std::make_shared<MockMount>(clock_);
    }
    if (id == "mock:imu:0") {
        return std::make_shared<MockImu>(clock_);
    }
    if (id == "mock:sensor:0") {
        return std::make_shared<MockSensor>(clock_);
    }
    if (id == "mock:inference:0") {
        return std::make_shared<MockInference>();
    }
    return fail(ErrorCode::NotFound, fmt::format("driver 'mock' has no device '{}'", id));
}

}  // namespace cloudscope::test
