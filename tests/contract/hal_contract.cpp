#include "hal_contract.hpp"

#include <cloudscope/capture/frame.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace cloudscope::test {

using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::DeviceKind;
using hal::MountCapabilities;
using hal::MountMotion;
using hal::MountPosition;
using hal::MountStatus;
using hal::SensorQuantity;
using hal::Tensor;

void check_device_contract(hal::IDevice& device, DeviceKind kind)
{
    const hal::DeviceInfo& info = device.info();
    CHECK(to_string(info.kind) == to_string(kind));
    CHECK_FALSE(info.name.empty());
    CHECK_FALSE(info.driver.empty());
    // The id starts with the driver's name and a colon: that is how the registry finds the driver.
    CHECK(info.id.starts_with(info.driver + ":"));
    CHECK(info.id.size() > info.driver.size() + 1);

    CHECK_FALSE(device.is_open());
    device.close();  // closing a closed device does nothing
    CHECK_FALSE(device.is_open());

    REQUIRE(outcome(device.open()) == "ok");
    CHECK(device.is_open());
    REQUIRE(outcome(device.open()) == "ok");  // opening an open device does nothing
    CHECK(device.is_open());

    device.close();
    CHECK_FALSE(device.is_open());
    device.close();
    CHECK_FALSE(device.is_open());

    REQUIRE(outcome(device.open()) == "ok");  // and it can be opened again
    CHECK(device.is_open());
    device.close();
}

// ---------------------------------------------------------------------------------------- camera

namespace {

constexpr std::array kAllControls = {
    CameraControl::Exposure,   CameraControl::Gain,      CameraControl::WhiteBalance,
    CameraControl::Brightness, CameraControl::Contrast,  CameraControl::Saturation,
    CameraControl::Gamma,      CameraControl::Sharpness, CameraControl::Focus,
};

// The next frame; a camera that has none ready yet gets several chances.
Expected<void> read_next(hal::ICamera& camera, Frame& frame)
{
    Expected<void> result = camera.read_frame(frame, 500ms);
    for (int attempt = 0; attempt < 10 && !result && result.error().code == ErrorCode::Timeout; ++attempt) {
        result = camera.read_frame(frame, 500ms);
    }
    return result;
}

void check_frame(const Frame& frame, const CameraMode& mode, bool simulated)
{
    const FrameInfo& info = frame.info();
    CHECK(info.width == mode.width);
    CHECK(info.height == mode.height);
    CHECK(to_string(info.format) == to_string(mode.format));
    CHECK(info.simulated == simulated);
    const std::size_t pixel_bytes = bytes_per_pixel(mode.format);
    if (pixel_bytes > 0) {
        CHECK(info.stride >= static_cast<std::size_t>(mode.width) * pixel_bytes);
        CHECK(frame.data().size() == info.stride * static_cast<std::size_t>(mode.height));
    } else {
        CHECK(info.stride == 0);  // compressed: no rows
        CHECK_FALSE(frame.data().empty());
    }
}

}  // namespace

void check_camera_contract(const std::function<std::shared_ptr<hal::ICamera>()>& make)
{
    const std::shared_ptr<hal::ICamera> camera = make();
    REQUIRE(camera != nullptr);

    SECTION("it follows the rules for all devices")
    {
        check_device_contract(*camera, DeviceKind::Camera);
    }

    SECTION("a closed camera refuses everything that needs the device")
    {
        Frame frame(64);
        CHECK(outcome(camera->capabilities()) == "Unavailable");
        CHECK(outcome(camera->mode()) == "Unavailable");
        CHECK(outcome(camera->set_mode({})) == "Unavailable");
        CHECK(outcome(camera->set_control(CameraControl::Exposure, {})) == "Unavailable");
        CHECK(outcome(camera->control(CameraControl::Exposure)) == "Unavailable");
        CHECK(outcome(camera->start()) == "Unavailable");
        CHECK(outcome(camera->read_frame(frame, 0ms)) == "Unavailable");
        camera->stop();
        CHECK_FALSE(camera->is_streaming());
        CHECK_FALSE(camera->is_open());
    }

    SECTION("its capabilities are complete and consistent")
    {
        REQUIRE(outcome(camera->open()) == "ok");
        const auto capabilities = camera->capabilities();
        REQUIRE(outcome(capabilities) == "ok");

        REQUIRE_FALSE(capabilities->modes.empty());
        for (std::size_t i = 0; i < capabilities->modes.size(); ++i) {
            const CameraMode& mode = capabilities->modes[i];
            CAPTURE(i);
            CHECK(mode.width > 0);
            CHECK(mode.height > 0);
            CHECK(mode.fps > 0.0);
            CHECK(std::ranges::count(capabilities->modes, mode) == 1);  // listed once
        }
        const auto current = camera->mode();
        REQUIRE(outcome(current) == "ok");
        CHECK(std::ranges::count(capabilities->modes, *current) == 1);  // an open camera has a listed mode

        for (const ControlInfo& control : capabilities->controls) {
            CAPTURE(to_string(control.control));
            CHECK(control.minimum <= control.maximum);
            CHECK(control.default_value >= control.minimum);
            CHECK(control.default_value <= control.maximum);
            CHECK(control.step >= 0.0);
            if (!control.calibrated) {
                CHECK(control.unit.empty());  // a unit would claim a calibration that does not exist
            }
            CHECK(std::ranges::count(capabilities->controls, control.control, &ControlInfo::control) == 1);
        }
    }

    SECTION("every listed mode can be selected, and only those")
    {
        REQUIRE(outcome(camera->open()) == "ok");
        const auto capabilities = camera->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        for (const CameraMode& mode : capabilities->modes) {
            CAPTURE(mode.width, mode.height, to_string(mode.format), mode.fps);
            const auto selected = camera->set_mode(mode);
            REQUIRE(outcome(selected) == "ok");
            CHECK(selected->width == mode.width);
            CHECK(selected->height == mode.height);
            CHECK(to_string(selected->format) == to_string(mode.format));
            CHECK(selected->fps > 0.0);
            const auto read_back = camera->mode();
            REQUIRE(outcome(read_back) == "ok");
            CHECK(*read_back == *selected);
        }
        const auto before = camera->mode();
        REQUIRE(outcome(before) == "ok");
        const CameraMode unlisted{.width = 12345, .height = 54321, .format = PixelFormat::Gray8, .fps = 1.0};
        CHECK(outcome(camera->set_mode(unlisted)) == "InvalidArgument");
        const auto after = camera->mode();
        REQUIRE(outcome(after) == "ok");
        CHECK(*after == *before);  // a refused request changes nothing
    }

    SECTION("controls report what is really in effect")
    {
        REQUIRE(outcome(camera->open()) == "ok");
        const auto capabilities = camera->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        for (const ControlInfo& info : capabilities->controls) {
            CAPTURE(to_string(info.control));
            for (const double value : {info.minimum, info.maximum, info.default_value}) {
                CAPTURE(value);
                const ControlSetting request{.value = value, .automatic = false};
                const auto state = camera->set_control(info.control, request);
                REQUIRE(outcome(state) == "ok");
                CHECK(state->requested == request);
                CHECK(state->effective.value >= info.minimum);
                CHECK(state->effective.value <= info.maximum);
                CHECK_FALSE(state->effective.automatic);
                const auto read_back = camera->control(info.control);
                REQUIRE(outcome(read_back) == "ok");
                CHECK(*read_back == state->effective);
            }

            // Out of range is not an error: the device clamps and says that it did not do as asked.
            const double span = info.maximum - info.minimum;
            const auto above =
                camera->set_control(info.control, {.value = info.maximum + span + 1000.0, .automatic = false});
            REQUIRE(outcome(above) == "ok");
            CHECK(above->effective.value <= info.maximum);
            CHECK_FALSE(above->applied);
            const auto below =
                camera->set_control(info.control, {.value = info.minimum - span - 1000.0, .automatic = false});
            REQUIRE(outcome(below) == "ok");
            CHECK(below->effective.value >= info.minimum);
            CHECK_FALSE(below->applied);

            const double not_a_number = std::numeric_limits<double>::quiet_NaN();
            CHECK(outcome(camera->set_control(info.control, {.value = not_a_number, .automatic = false})) ==
                  "InvalidArgument");

            // "Automatic" is honoured only by a control that has it.
            const auto automatic = camera->set_control(info.control, {.value = info.default_value, .automatic = true});
            REQUIRE(outcome(automatic) == "ok");
            CHECK(automatic->effective.automatic == info.supports_auto);
            CHECK(automatic->applied == info.supports_auto);
            const auto manual = camera->set_control(info.control, {.value = info.default_value, .automatic = false});
            REQUIRE(outcome(manual) == "ok");
            CHECK_FALSE(manual->effective.automatic);
        }

        for (const CameraControl control : kAllControls) {
            if (std::ranges::count(capabilities->controls, control, &ControlInfo::control) == 0) {
                CAPTURE(to_string(control));
                CHECK(outcome(camera->set_control(control, {})) == "Unsupported");
                CHECK(outcome(camera->control(control)) == "Unsupported");
            }
        }
    }

    SECTION("it delivers numbered, time-stamped frames of the selected mode")
    {
        REQUIRE(outcome(camera->open()) == "ok");
        const auto capabilities = camera->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const auto selected = camera->mode();
        REQUIRE(outcome(selected) == "ok");
        const CameraMode mode = *selected;
        Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));

        CHECK_FALSE(camera->is_streaming());
        CHECK(outcome(camera->read_frame(frame, 0ms)) == "Unavailable");  // not started

        REQUIRE(outcome(camera->start()) == "ok");
        CHECK(camera->is_streaming());
        CHECK(outcome(camera->start()) == "Unavailable");         // already streaming
        CHECK(outcome(camera->set_mode(mode)) == "Unavailable");  // not while streaming

        FrameInfo previous;
        for (int i = 0; i < 5; ++i) {
            CAPTURE(i);
            REQUIRE(outcome(read_next(*camera, frame)) == "ok");
            check_frame(frame, mode, camera->info().simulated);
            if (i > 0) {
                CHECK(frame.info().sequence > previous.sequence);
                CHECK(frame.info().captured.monotonic >= previous.captured.monotonic);
                CHECK(frame.info().captured.utc >= previous.captured.utc);
            }
            previous = frame.info();
        }

        // A buffer that cannot hold a frame is refused, whether or not a frame is ready.
        Frame tiny(1);
        CHECK(outcome(camera->read_frame(tiny, 0ms)) == "InvalidArgument");

        // Controls may be changed while frames flow.
        if (!capabilities->controls.empty()) {
            const ControlInfo& control = capabilities->controls.front();
            CHECK(outcome(camera->set_control(control.control, {.value = control.default_value, .automatic = false})) ==
                  "ok");
            CHECK(outcome(read_next(*camera, frame)) == "ok");
        }

        camera->stop();
        CHECK_FALSE(camera->is_streaming());
        CHECK(outcome(camera->read_frame(frame, 0ms)) == "Unavailable");
        camera->stop();  // stopping twice does nothing

        // A new start numbers the frames from the beginning again.
        REQUIRE(outcome(camera->start()) == "ok");
        REQUIRE(outcome(read_next(*camera, frame)) == "ok");
        CHECK(frame.info().sequence <= previous.sequence);

        // close() ends streaming, and the camera works again after open().
        camera->close();
        CHECK_FALSE(camera->is_streaming());
        CHECK_FALSE(camera->is_open());
        REQUIRE(outcome(camera->open()) == "ok");
        CHECK_FALSE(camera->is_streaming());
        REQUIRE(outcome(camera->start()) == "ok");
        CHECK(outcome(read_next(*camera, frame)) == "ok");
    }

    SECTION("frames match the mode in every mode")
    {
        REQUIRE(outcome(camera->open()) == "ok");
        const auto capabilities = camera->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        for (const CameraMode& listed : capabilities->modes) {
            CAPTURE(listed.width, listed.height, to_string(listed.format), listed.fps);
            const auto mode = camera->set_mode(listed);
            REQUIRE(outcome(mode) == "ok");
            Frame frame(frame_buffer_bytes(mode->format, mode->width, mode->height));
            REQUIRE(outcome(camera->start()) == "ok");
            REQUIRE(outcome(read_next(*camera, frame)) == "ok");
            check_frame(frame, *mode, camera->info().simulated);
            camera->stop();
        }
    }
}

// ----------------------------------------------------------------------------------------- mount

namespace {

MountStatus status_of(const hal::IMount& mount)
{
    const auto status = mount.status();
    REQUIRE(outcome(status) == "ok");
    return *status;
}

// A point inside the travel of both axes: 0 is the minimum of an axis, 1 its maximum.
MountPosition point(const MountCapabilities& capabilities, double fraction)
{
    return {.pan = capabilities.pan.minimum + ((capabilities.pan.maximum - capabilities.pan.minimum) * fraction),
            .tilt = capabilities.tilt.minimum + ((capabilities.tilt.maximum - capabilities.tilt.minimum) * fraction)};
}

// Degrees between two positions, on the axis where they differ most.
double distance(const MountPosition& a, const MountPosition& b)
{
    return std::max(std::abs((a.pan - b.pan).value()), std::abs((a.tilt - b.tilt).value()));
}

// Of two points well inside the travel, the one further from where the mount is: a move that takes a while.
MountPosition far_target(const MountCapabilities& capabilities, const MountPosition& from)
{
    const MountPosition low = point(capabilities, 0.2);
    const MountPosition high = point(capabilities, 0.8);
    return distance(low, from) > distance(high, from) ? low : high;
}

double usable_speed(const MountCapabilities& capabilities)
{
    return std::min(capabilities.pan.max_speed_deg_s, capabilities.tilt.max_speed_deg_s);
}

// Lets time pass until the mount no longer moves: at most a minute of the mount's time.
MountStatus come_to_rest(const hal::IMount& mount, const MountContract& contract)
{
    for (int step = 0; step < 240; ++step) {
        contract.pass_time(250ms);
        if (status_of(mount).motion != MountMotion::Moving) {
            break;
        }
    }
    const MountStatus status = status_of(mount);
    REQUIRE(to_string(status.motion) != "moving");
    return status;
}

}  // namespace

void check_mount_contract(const MountContract& contract)
{
    const std::shared_ptr<hal::IMount> mount = contract.make();
    REQUIRE(mount != nullptr);
    const double tolerance = contract.tolerance.value();

    SECTION("it follows the rules for all devices")
    {
        check_device_contract(*mount, DeviceKind::Mount);
    }

    SECTION("a closed mount refuses everything that needs the device")
    {
        CHECK(outcome(mount->capabilities()) == "Unavailable");
        CHECK(outcome(mount->status()) == "Unavailable");
        CHECK(outcome(mount->move_to({}, 1.0)) == "Unavailable");
        CHECK(outcome(mount->clear_emergency_stop()) == "Unavailable");
        mount->stop();  // harmless on a closed mount
        CHECK_FALSE(mount->is_open());
    }

    SECTION("it starts at rest, inside limits that make sense")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        CHECK(capabilities->pan.minimum < capabilities->pan.maximum);
        CHECK(capabilities->tilt.minimum < capabilities->tilt.maximum);
        CHECK(capabilities->pan.max_speed_deg_s > 0.0);
        CHECK(capabilities->tilt.max_speed_deg_s > 0.0);

        const MountStatus status = status_of(*mount);
        CHECK(to_string(status.motion) == "idle");
        CHECK_FALSE(status.emergency_stop);
        CHECK(status.fault.empty());
        CHECK(status.position_measured == capabilities->position_feedback);
        CHECK(status.position.pan.value() >= capabilities->pan.minimum.value() - tolerance);
        CHECK(status.position.pan.value() <= capabilities->pan.maximum.value() + tolerance);
        CHECK(status.position.tilt.value() >= capabilities->tilt.minimum.value() - tolerance);
        CHECK(status.position.tilt.value() <= capabilities->tilt.maximum.value() + tolerance);
    }

    SECTION("it refuses targets and speeds it cannot honour, and stays where it is")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const MountPosition inside = point(*capabilities, 0.5);
        const double speed = usable_speed(*capabilities);
        const double not_a_number = std::numeric_limits<double>::quiet_NaN();

        const std::array bad_targets = {
            MountPosition{.pan = capabilities->pan.maximum + Degrees(1.0), .tilt = inside.tilt},
            MountPosition{.pan = capabilities->pan.minimum - Degrees(1.0), .tilt = inside.tilt},
            MountPosition{.pan = inside.pan, .tilt = capabilities->tilt.maximum + Degrees(1.0)},
            MountPosition{.pan = inside.pan, .tilt = capabilities->tilt.minimum - Degrees(1.0)},
            MountPosition{.pan = Degrees(not_a_number), .tilt = inside.tilt},
            MountPosition{.pan = inside.pan, .tilt = Degrees(not_a_number)},
        };
        for (const MountPosition& target : bad_targets) {
            CAPTURE(target.pan.value(), target.tilt.value());
            CHECK(outcome(mount->move_to(target, speed)) == "InvalidArgument");
        }
        for (const double bad_speed : {0.0, -1.0, speed * 1.5, not_a_number}) {
            CAPTURE(bad_speed);
            CHECK(outcome(mount->move_to(inside, bad_speed)) == "InvalidArgument");
        }
        contract.pass_time(500ms);
        CHECK(to_string(status_of(*mount).motion) == "idle");

        // The limits themselves are allowed positions.
        CHECK(outcome(mount->move_to(point(*capabilities, 0.0), speed)) == "ok");
        CHECK(outcome(mount->move_to(point(*capabilities, 1.0), speed)) == "ok");
    }

    SECTION("it moves to a target and comes to rest there")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const MountStatus before = status_of(*mount);
        const MountPosition target = far_target(*capabilities, before.position);
        const double speed = usable_speed(*capabilities) / 2.0;

        REQUIRE(outcome(mount->move_to(target, speed)) == "ok");
        CHECK(status_of(*mount).target == target);
        contract.pass_time(200ms);
        const MountStatus under_way = status_of(*mount);
        CHECK(to_string(under_way.motion) == "moving");
        CHECK(under_way.time.monotonic >= before.time.monotonic);
        // It cannot be further along than the commanded speed allows (with room for noise).
        CHECK(distance(under_way.position, before.position) <= speed * 0.2 + tolerance + 1.0);

        const MountStatus arrived = come_to_rest(*mount, contract);
        CHECK(to_string(arrived.motion) == "idle");
        CHECK(arrived.target == target);
        CHECK(distance(arrived.position, target) <= tolerance);
        CHECK(arrived.time.monotonic >= under_way.time.monotonic);
    }

    SECTION("stop brings it to rest short of the target, and a new move is accepted")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const MountPosition target = far_target(*capabilities, status_of(*mount).position);
        const double speed = usable_speed(*capabilities) / 2.0;

        REQUIRE(outcome(mount->move_to(target, speed)) == "ok");
        contract.pass_time(200ms);
        REQUIRE(to_string(status_of(*mount).motion) == "moving");
        mount->stop();
        const MountStatus stopped = come_to_rest(*mount, contract);
        CHECK(to_string(stopped.motion) == "stopped");
        CHECK(distance(stopped.position, target) > tolerance);

        contract.pass_time(1000ms);  // and it stays there
        const MountStatus later = status_of(*mount);
        CHECK(to_string(later.motion) == "stopped");
        CHECK(distance(later.position, stopped.position) <= tolerance);

        REQUIRE(outcome(mount->move_to(target, speed)) == "ok");
        const MountStatus arrived = come_to_rest(*mount, contract);
        CHECK(to_string(arrived.motion) == "idle");
        CHECK(distance(arrived.position, target) <= tolerance);
    }

    SECTION("an emergency stop halts it and blocks every move until it is cleared")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const MountPosition target = far_target(*capabilities, status_of(*mount).position);
        const double speed = usable_speed(*capabilities) / 2.0;

        REQUIRE(outcome(mount->move_to(target, speed)) == "ok");
        contract.pass_time(200ms);
        mount->emergency_stop();
        CHECK(status_of(*mount).emergency_stop);
        CHECK(outcome(mount->move_to(target, speed)) == "Unavailable");

        const MountStatus halted = come_to_rest(*mount, contract);
        CHECK(to_string(halted.motion) == "stopped");
        CHECK(halted.emergency_stop);
        CHECK(distance(halted.position, target) > tolerance);

        mount->stop();  // an ordinary stop does not clear it
        CHECK(status_of(*mount).emergency_stop);
        // Neither does closing and opening the device: only the explicit call does (FR-SAF-05).
        mount->close();
        REQUIRE(outcome(mount->open()) == "ok");
        CHECK(status_of(*mount).emergency_stop);
        CHECK(outcome(mount->move_to(target, speed)) == "Unavailable");

        REQUIRE(outcome(mount->clear_emergency_stop()) == "ok");
        CHECK_FALSE(status_of(*mount).emergency_stop);
        REQUIRE(outcome(mount->move_to(target, speed)) == "ok");
        const MountStatus arrived = come_to_rest(*mount, contract);
        CHECK(to_string(arrived.motion) == "idle");
        CHECK(distance(arrived.position, target) <= tolerance);
    }

    SECTION("an emergency stop at rest blocks moves too")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        mount->emergency_stop();
        const MountStatus status = status_of(*mount);
        CHECK(status.emergency_stop);
        CHECK(to_string(status.motion) == "idle");
        CHECK(outcome(mount->move_to(point(*capabilities, 0.5), usable_speed(*capabilities))) == "Unavailable");
        REQUIRE(outcome(mount->clear_emergency_stop()) == "ok");
        CHECK(outcome(mount->move_to(point(*capabilities, 0.5), usable_speed(*capabilities))) == "ok");
    }

    SECTION("closing the mount ends a move")
    {
        REQUIRE(outcome(mount->open()) == "ok");
        const auto capabilities = mount->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        const MountPosition target = far_target(*capabilities, status_of(*mount).position);
        REQUIRE(outcome(mount->move_to(target, usable_speed(*capabilities) / 2.0)) == "ok");
        contract.pass_time(200ms);
        mount->close();
        CHECK(outcome(mount->status()) == "Unavailable");

        REQUIRE(outcome(mount->open()) == "ok");
        const MountStatus after = come_to_rest(*mount, contract);
        CHECK(to_string(after.motion) == "stopped");
        CHECK(distance(after.position, target) > tolerance);
    }
}

// ----------------------------------------------------------------------------------- IMU, sensor

void check_imu_contract(const std::function<std::shared_ptr<hal::IImu>()>& make)
{
    const std::shared_ptr<hal::IImu> imu = make();
    REQUIRE(imu != nullptr);

    SECTION("it follows the rules for all devices")
    {
        check_device_contract(*imu, DeviceKind::Imu);
    }

    SECTION("a closed IMU refuses everything that needs the device")
    {
        CHECK(outcome(imu->capabilities()) == "Unavailable");
        CHECK(outcome(imu->read()) == "Unavailable");
    }

    SECTION("its samples are unit quaternions in time order")
    {
        REQUIRE(outcome(imu->open()) == "ok");
        const auto capabilities = imu->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        CHECK(capabilities->max_rate_hz > 0.0);

        std::optional<hal::ImuSample> previous;
        for (int i = 0; i < 3; ++i) {
            const auto sample = imu->read();
            REQUIRE(outcome(sample) == "ok");
            const hal::Quaternion& q = sample->orientation;
            const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
            CHECK(std::abs(norm - 1.0) < 1e-6);
            CHECK(sample->accuracy.value() >= 0.0);
            if (previous) {
                CHECK(sample->time.monotonic >= previous->time.monotonic);
            }
            previous = *sample;
        }
    }
}

void check_sensor_contract(const std::function<std::shared_ptr<hal::ISensor>()>& make)
{
    const std::shared_ptr<hal::ISensor> sensor = make();
    REQUIRE(sensor != nullptr);

    SECTION("it follows the rules for all devices")
    {
        check_device_contract(*sensor, DeviceKind::Sensor);
    }

    SECTION("a closed sensor refuses everything that needs the device")
    {
        CHECK(outcome(sensor->quantities()) == "Unavailable");
        CHECK(outcome(sensor->read()) == "Unavailable");
    }

    SECTION("its readings are of the quantities it lists, one each, with plausible values")
    {
        REQUIRE(outcome(sensor->open()) == "ok");
        const auto quantities = sensor->quantities();
        REQUIRE(outcome(quantities) == "ok");
        REQUIRE_FALSE(quantities->empty());
        for (const SensorQuantity quantity : *quantities) {
            CAPTURE(to_string(quantity));
            CHECK(std::ranges::count(*quantities, quantity) == 1);
            CHECK_FALSE(to_string(quantity).empty());
        }

        std::optional<MonotonicTime> previous;
        for (int i = 0; i < 2; ++i) {
            const auto readings = sensor->read();
            REQUIRE(outcome(readings) == "ok");
            for (const hal::SensorReading& reading : *readings) {
                CAPTURE(to_string(reading.quantity), reading.value);
                CHECK(std::ranges::count(*quantities, reading.quantity) == 1);
                CHECK(std::ranges::count(*readings, reading.quantity, &hal::SensorReading::quantity) == 1);
                CHECK(std::isfinite(reading.value));
                switch (reading.quantity) {
                case SensorQuantity::RelativeHumidity:
                    CHECK(reading.value >= 0.0);
                    CHECK(reading.value <= 100.0);
                    break;
                case SensorQuantity::Rain:
                    CHECK((reading.value == 0.0 || reading.value == 1.0));
                    break;
                case SensorQuantity::Latitude:
                    CHECK(std::abs(reading.value) <= 90.0);
                    break;
                case SensorQuantity::Longitude:
                    CHECK(std::abs(reading.value) <= 180.0);
                    break;
                case SensorQuantity::Pressure:
                    CHECK(reading.value > 0.0);
                    break;
                case SensorQuantity::Illuminance:
                case SensorQuantity::HorizontalAccuracy:
                    CHECK(reading.value >= 0.0);
                    break;
                case SensorQuantity::Temperature:
                    CHECK(reading.value > -273.15);
                    break;
                case SensorQuantity::Altitude:
                case SensorQuantity::ClockOffset:
                    break;
                }
                if (previous) {
                    CHECK(reading.time.monotonic >= *previous);
                }
            }
            if (!readings->empty()) {
                previous = readings->front().time.monotonic;
            }
        }
    }
}

// ------------------------------------------------------------------------------------- transport

namespace {

// 0, 1, 2, ... 250, 0, 1, ...: a pattern in which a lost, repeated or swapped byte shows.
std::vector<std::byte> pattern(std::size_t count)
{
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>(i % 251);
    }
    return bytes;
}

// Reads until `count` bytes have arrived or the link has been silent for five seconds.
std::vector<std::byte> receive(hal::ITransport& transport, std::size_t count)
{
    std::vector<std::byte> received;
    std::array<std::byte, 1500> buffer{};
    int silent_reads = 0;
    while (received.size() < count && silent_reads < 10) {
        const auto got = transport.read(buffer, 500ms);
        REQUIRE(outcome(got) == "ok");
        REQUIRE(*got <= buffer.size());
        silent_reads = *got == 0 ? silent_reads + 1 : 0;
        received.insert(received.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*got));
    }
    return received;
}

}  // namespace

void check_transport_contract(const std::function<TransportPair()>& make_pair)
{
    const auto [a, b] = make_pair();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    SECTION("both ends follow the rules for all devices")
    {
        check_device_contract(*a, DeviceKind::Transport);
        check_device_contract(*b, DeviceKind::Transport);
    }

    SECTION("a closed transport refuses to read and write")
    {
        std::array<std::byte, 8> buffer{};
        CHECK(outcome(a->write(buffer)) == "Unavailable");
        CHECK(outcome(a->read(buffer, 0ms)) == "Unavailable");
    }

    SECTION("bytes arrive at the other end complete and in order, in both directions")
    {
        REQUIRE(outcome(a->open()) == "ok");
        REQUIRE(outcome(b->open()) == "ok");
        const std::vector<std::byte> message = pattern(300);

        REQUIRE(outcome(a->write(message)) == "ok");
        CHECK(receive(*b, message.size()) == message);
        REQUIRE(outcome(b->write(message)) == "ok");
        CHECK(receive(*a, message.size()) == message);

        // With nothing on the way, a read waits for its timeout and reports no bytes.
        std::array<std::byte, 16> buffer{};
        const auto nothing = b->read(buffer, 20ms);
        REQUIRE(outcome(nothing) == "ok");
        CHECK(*nothing == 0);

        // Writing nothing is allowed and sends nothing.
        CHECK(outcome(a->write({})) == "ok");

        // A small buffer gets its fill; the rest waits for the next read.
        REQUIRE(outcome(a->write(std::span(message).first(10))) == "ok");
        std::vector<std::byte> collected;
        std::array<std::byte, 4> small{};
        for (int reads = 0; reads < 20 && collected.size() < 10; ++reads) {
            const auto got = b->read(small, 500ms);
            REQUIRE(outcome(got) == "ok");
            REQUIRE(*got <= small.size());
            collected.insert(collected.end(), small.begin(), small.begin() + static_cast<std::ptrdiff_t>(*got));
        }
        CHECK(collected == std::vector<std::byte>(message.begin(), message.begin() + 10));
    }

    SECTION("a large transfer survives a writer and a reader on different threads")
    {
        REQUIRE(outcome(a->open()) == "ok");
        REQUIRE(outcome(b->open()) == "ok");
        const std::vector<std::byte> data = pattern(std::size_t{256} * 1024);

        for (const auto& [sender, receiver] : {std::pair(a, b), std::pair(b, a)}) {
            bool written = true;
            std::thread writer([&data, &written, sender = sender] {
                const std::span<const std::byte> all(data);
                for (std::size_t offset = 0; offset < all.size(); offset += 4099) {  // not a multiple of any buffer
                    written =
                        written && sender->write(all.subspan(offset, std::min<std::size_t>(4099, all.size() - offset)));
                }
            });
            const std::vector<std::byte> received = receive(*receiver, data.size());
            writer.join();
            CHECK(written);
            CHECK(received.size() == data.size());
            CHECK(received == data);
        }
    }

    SECTION("closing an end makes a read that is waiting there return at once")
    {
        REQUIRE(outcome(a->open()) == "ok");
        REQUIRE(outcome(b->open()) == "ok");
        Expected<std::size_t> result = std::size_t{1};  // replaced by what the read returns
        const auto started = std::chrono::steady_clock::now();
        std::thread reader([&result, end = b] {
            std::array<std::byte, 8> buffer{};
            result = end->read(buffer, 30s);
        });
        // The pause only makes it likely that the reader is already waiting. The rule holds either way:
        // a read on a closed transport returns at once too.
        std::this_thread::sleep_for(50ms);
        b->close();
        reader.join();
        CHECK(std::chrono::steady_clock::now() - started < 10s);
        CHECK((!result || *result == 0));  // refused or empty-handed, but not left waiting
    }
}

// ------------------------------------------------------------------------------------- inference

void check_inference_contract(const std::function<std::shared_ptr<hal::IInference>()>& make,
                              const std::filesystem::path& model)
{
    const std::shared_ptr<hal::IInference> engine = make();
    REQUIRE(engine != nullptr);
    const std::filesystem::path missing = model.parent_path() / "no-such-model.onnx";

    SECTION("it follows the rules for all devices")
    {
        check_device_contract(*engine, DeviceKind::Inference);
    }

    SECTION("a closed engine refuses everything that needs the device")
    {
        CHECK(outcome(engine->load(model)) == "Unavailable");
        CHECK(outcome(engine->capabilities()) == "Unavailable");
        CHECK(outcome(engine->run({})) == "Unavailable");
    }

    SECTION("without a model it can neither describe nor run one")
    {
        REQUIRE(outcome(engine->open()) == "ok");
        CHECK(outcome(engine->capabilities()) == "Unavailable");
        CHECK(outcome(engine->run({})) == "Unavailable");
        CHECK(outcome(engine->load(missing)) == "NotFound");
        CHECK(outcome(engine->capabilities()) == "Unavailable");
    }

    SECTION("a loaded model is described, runs, and checks its inputs")
    {
        REQUIRE(outcome(engine->open()) == "ok");
        REQUIRE(outcome(engine->load(model)) == "ok");
        const auto capabilities = engine->capabilities();
        REQUIRE(outcome(capabilities) == "ok");
        CHECK_FALSE(capabilities->provider.empty());
        REQUIRE_FALSE(capabilities->inputs.empty());
        REQUIRE_FALSE(capabilities->outputs.empty());

        std::set<std::string> names;
        std::vector<Tensor> inputs;
        for (const hal::TensorInfo& input : capabilities->inputs) {
            CAPTURE(input.name);
            CHECK_FALSE(input.name.empty());
            CHECK(names.insert(input.name).second);  // names are unique
            REQUIRE_FALSE(input.shape.empty());
            Tensor tensor{.name = input.name, .shape = input.shape, .data = {}};
            for (std::int64_t& dimension : tensor.shape) {
                CHECK((dimension == -1 || dimension > 0));
                dimension = dimension == -1 ? 1 : dimension;  // an open dimension: use one
            }
            tensor.data.assign(hal::element_count(tensor.shape), 0.5F);
            inputs.push_back(std::move(tensor));
        }

        const auto outputs = engine->run(inputs);
        REQUIRE(outcome(outputs) == "ok");
        REQUIRE(outputs->size() == capabilities->outputs.size());
        for (std::size_t i = 0; i < outputs->size(); ++i) {
            const Tensor& output = (*outputs)[i];
            const hal::TensorInfo& declared = capabilities->outputs[i];
            CAPTURE(declared.name);
            CHECK(output.name == declared.name);
            REQUIRE(output.shape.size() == declared.shape.size());
            for (std::size_t d = 0; d < declared.shape.size(); ++d) {
                CHECK(output.shape[d] > 0);
                if (declared.shape[d] != -1) {
                    CHECK(output.shape[d] == declared.shape[d]);
                }
            }
            CHECK(output.data.size() == hal::element_count(output.shape));
        }

        // Inputs that do not fit the model are refused.
        CHECK(outcome(engine->run({})) == "InvalidArgument");
        std::vector<Tensor> renamed = inputs;
        renamed.front().name += "-unknown";
        CHECK(outcome(engine->run(renamed)) == "InvalidArgument");
        std::vector<Tensor> truncated = inputs;
        truncated.front().data.pop_back();
        CHECK(outcome(engine->run(truncated)) == "InvalidArgument");

        // A failed load leaves the loaded model in place.
        CHECK(outcome(engine->load(missing)) == "NotFound");
        CHECK(outcome(engine->run(inputs)) == "ok");

        // close() unloads the model.
        engine->close();
        REQUIRE(outcome(engine->open()) == "ok");
        CHECK(outcome(engine->capabilities()) == "Unavailable");
    }
}

}  // namespace cloudscope::test
