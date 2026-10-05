// Mock devices: the smallest implementations of the HAL interfaces that obey their contracts.
// They exist to test code that uses devices, and to prove that the interfaces can be implemented as written.
// Realistic behaviour (image content, motion profiles, noise, latency) belongs to the simulators in the core
// library; these mocks are deliberately plain, never wait, and are fully controllable from a test.
#pragma once

#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/inference.hpp>
#include <cloudscope/hal/mount.hpp>
#include <cloudscope/hal/registry.hpp>
#include <cloudscope/hal/sensors.hpp>
#include <cloudscope/hal/transport.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cloudscope::test {

// Delivers a frame on every read, at once. Every byte of a frame is the low byte of its sequence number.
class MockCamera final : public hal::ICamera {
public:
    explicit MockCamera(const IClock& clock, std::string id = "mock:camera:0");

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

    // What a test can make happen:
    // The next `count` frames are lost before they can be read; the sequence numbers show the gap.
    void lose_frames(std::uint64_t count);
    // While held, no frame arrives: read_frame() reports Timeout (at once; the mock never waits).
    void hold_frames(bool hold);
    // Unplugged: reads fail with Io and open() with NotFound until plug_in().
    void unplug();
    void plug_in();

private:
    const IClock& clock_;
    hal::DeviceInfo info_;
    hal::CameraCapabilities capabilities_;
    mutable std::mutex mutex_;
    hal::CameraMode mode_;
    std::vector<hal::ControlSetting> settings_;  // parallel to capabilities_.controls
    bool open_ = false;
    bool streaming_ = false;
    bool held_ = false;
    bool plugged_in_ = true;
    std::uint64_t next_sequence_ = 0;
};

// Moves in a straight line at the commanded speed and stops dead. Its position is computed from the clock
// it is given, so a test with a ManualClock decides how much time has passed.
class MockMount final : public hal::IMount {
public:
    explicit MockMount(const IClock& clock, std::string id = "mock:mount:0");

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override;

    [[nodiscard]] Expected<hal::MountCapabilities> capabilities() const override;
    [[nodiscard]] Expected<void> move_to(hal::MountPosition target, double speed_deg_s) override;
    void stop() override;
    void emergency_stop() override;
    [[nodiscard]] Expected<void> clear_emergency_stop() override;
    [[nodiscard]] Expected<hal::MountStatus> status() const override;

    // What a test can make happen: the device reports a fault (motion ends, moves are refused) until an
    // empty text clears it.
    void set_fault(std::string fault);

private:
    [[nodiscard]] hal::MountPosition position_now() const;  // caller holds mutex_
    void halt();                                            // caller holds mutex_

    const IClock& clock_;
    hal::DeviceInfo info_;
    hal::MountCapabilities capabilities_;
    mutable std::mutex mutex_;
    bool open_ = false;
    bool emergency_stop_ = false;
    bool stopped_ = false;
    std::string fault_;
    hal::MountPosition start_;
    hal::MountPosition target_;
    MonotonicTime move_started_;
    std::chrono::duration<double> move_duration_{0.0};
};

class MockImu final : public hal::IImu {
public:
    explicit MockImu(const IClock& clock, std::string id = "mock:imu:0");

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override { open_ = false; }
    [[nodiscard]] bool is_open() const override { return open_; }
    [[nodiscard]] Expected<hal::ImuCapabilities> capabilities() const override;
    [[nodiscard]] Expected<hal::ImuSample> read() override;

    void set_orientation(hal::Quaternion orientation) { orientation_ = orientation; }

private:
    const IClock& clock_;
    hal::DeviceInfo info_;
    bool open_ = false;
    hal::Quaternion orientation_;
};

class MockSensor final : public hal::ISensor {
public:
    explicit MockSensor(const IClock& clock, std::string id = "mock:sensor:0");

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override { open_ = false; }
    [[nodiscard]] bool is_open() const override { return open_; }
    [[nodiscard]] Expected<std::vector<hal::SensorQuantity>> quantities() const override;
    [[nodiscard]] Expected<std::vector<hal::SensorReading>> read() override;

    // nullopt: the quantity is listed but has no value at the moment.
    void set_value(hal::SensorQuantity quantity, std::optional<double> value);

private:
    const IClock& clock_;
    hal::DeviceInfo info_;
    bool open_ = false;
    std::vector<std::pair<hal::SensorQuantity, std::optional<double>>> values_;
};

// One end of an in-memory link: what is written here is read at the other end, in order.
class PipeTransport final : public hal::ITransport {
public:
    // Two connected ends with the ids "mock:transport:a" and "mock:transport:b".
    [[nodiscard]] static std::pair<std::shared_ptr<PipeTransport>, std::shared_ptr<PipeTransport>> make_pair();

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] Expected<void> write(std::span<const std::byte> data) override;
    [[nodiscard]] Expected<std::size_t> read(std::span<std::byte> buffer, std::chrono::milliseconds timeout) override;

private:
    // The bytes travelling in one direction.
    struct Channel {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::byte> bytes;
        bool reader_open = false;
    };
    PipeTransport(std::string id, std::shared_ptr<Channel> incoming, std::shared_ptr<Channel> outgoing);

    hal::DeviceInfo info_;
    std::shared_ptr<Channel> incoming_;  // this end reads from here
    std::shared_ptr<Channel> outgoing_;  // the other end reads from here
};

// A "model" with one input "values" of shape [-1, 4] and two outputs: "sum" [-1, 1] and "doubled" [-1, 4].
// load() accepts any existing file.
class MockInference final : public hal::IInference {
public:
    explicit MockInference(std::string id = "mock:inference:0");

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override { return open_; }
    [[nodiscard]] Expected<void> load(const std::filesystem::path& model) override;
    [[nodiscard]] Expected<hal::InferenceCapabilities> capabilities() const override;
    [[nodiscard]] Expected<std::vector<hal::Tensor>> run(const std::vector<hal::Tensor>& inputs) override;

private:
    hal::DeviceInfo info_;
    bool open_ = false;
    bool loaded_ = false;
};

// Offers one camera, mount, IMU, sensor and inference engine under the driver name "mock".
class MockDriver final : public hal::IDriver {
public:
    explicit MockDriver(const IClock& clock) : clock_(clock) {}

    [[nodiscard]] std::string_view name() const override { return "mock"; }
    [[nodiscard]] std::vector<hal::DeviceInfo> enumerate() override;
    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view id) override;

private:
    const IClock& clock_;
};

}  // namespace cloudscope::test
