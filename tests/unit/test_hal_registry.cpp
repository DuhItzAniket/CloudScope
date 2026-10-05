#include "hal_contract.hpp"
#include "mock_devices.hpp"

#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/inference.hpp>
#include <cloudscope/hal/mount.hpp>
#include <cloudscope/hal/registry.hpp>
#include <cloudscope/hal/sensors.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using hal::DeviceInfo;
using hal::DeviceKind;
using hal::DeviceRegistry;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);

// A driver with a chosen name that offers IMUs "<name>:imu:0" ... "<name>:imu:<count - 1>".
class ImuDriver final : public hal::IDriver {
public:
    ImuDriver(std::string name, int count, const IClock& clock) : name_(std::move(name)), count_(count), clock_(clock)
    {
    }

    [[nodiscard]] std::string_view name() const override { return name_; }

    [[nodiscard]] std::vector<DeviceInfo> enumerate() override
    {
        std::vector<DeviceInfo> devices;
        devices.reserve(static_cast<std::size_t>(count_));
        for (int i = 0; i < count_; ++i) {
            devices.push_back({.id = name_ + ":imu:" + std::to_string(i),
                               .kind = DeviceKind::Imu,
                               .name = "Test IMU",
                               .driver = name_,
                               .simulated = true});
        }
        return devices;
    }

    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view id) override
    {
        for (const DeviceInfo& device : enumerate()) {
            if (device.id == id) {
                return std::make_shared<MockImu>(clock_, device.id);
            }
        }
        return fail(ErrorCode::NotFound, "no such IMU");
    }

private:
    std::string name_;
    int count_;
    const IClock& clock_;
};

// A driver with a bug: it claims success and hands out nothing.
class EmptyHandedDriver final : public hal::IDriver {
public:
    [[nodiscard]] std::string_view name() const override { return "broken"; }
    [[nodiscard]] std::vector<DeviceInfo> enumerate() override { return {}; }
    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view /*id*/) override
    {
        return std::shared_ptr<hal::IDevice>();
    }
};

}  // namespace

TEST_CASE("device kinds, controls, motions and quantities have the names used in files and the API", "[hal]")
{
    CHECK(to_string(DeviceKind::Camera) == "camera");
    CHECK(to_string(DeviceKind::Mount) == "mount");
    CHECK(to_string(DeviceKind::Imu) == "imu");
    CHECK(to_string(DeviceKind::Sensor) == "sensor");
    CHECK(to_string(DeviceKind::Transport) == "transport");
    CHECK(to_string(DeviceKind::Inference) == "inference");

    CHECK(to_string(hal::CameraControl::Exposure) == "exposure");
    CHECK(to_string(hal::CameraControl::Gain) == "gain");
    CHECK(to_string(hal::CameraControl::WhiteBalance) == "white_balance");
    CHECK(to_string(hal::CameraControl::Brightness) == "brightness");
    CHECK(to_string(hal::CameraControl::Contrast) == "contrast");
    CHECK(to_string(hal::CameraControl::Saturation) == "saturation");
    CHECK(to_string(hal::CameraControl::Gamma) == "gamma");
    CHECK(to_string(hal::CameraControl::Sharpness) == "sharpness");
    CHECK(to_string(hal::CameraControl::Focus) == "focus");

    CHECK(to_string(hal::MountMotion::Idle) == "idle");
    CHECK(to_string(hal::MountMotion::Moving) == "moving");
    CHECK(to_string(hal::MountMotion::Stopped) == "stopped");
    CHECK(to_string(hal::MountMotion::Fault) == "fault");

    using hal::SensorQuantity;
    CHECK(to_string(SensorQuantity::Temperature) == "temperature");
    CHECK(unit_of(SensorQuantity::Temperature) == "degC");
    CHECK(to_string(SensorQuantity::RelativeHumidity) == "relative_humidity");
    CHECK(unit_of(SensorQuantity::RelativeHumidity) == "%");
    CHECK(to_string(SensorQuantity::Pressure) == "pressure");
    CHECK(unit_of(SensorQuantity::Pressure) == "hPa");
    CHECK(to_string(SensorQuantity::Illuminance) == "illuminance");
    CHECK(unit_of(SensorQuantity::Illuminance) == "lx");
    CHECK(to_string(SensorQuantity::Rain) == "rain");
    CHECK(unit_of(SensorQuantity::Rain).empty());
    CHECK(to_string(SensorQuantity::Latitude) == "latitude");
    CHECK(unit_of(SensorQuantity::Latitude) == "deg");
    CHECK(to_string(SensorQuantity::Longitude) == "longitude");
    CHECK(unit_of(SensorQuantity::Longitude) == "deg");
    CHECK(to_string(SensorQuantity::Altitude) == "altitude");
    CHECK(unit_of(SensorQuantity::Altitude) == "m");
    CHECK(to_string(SensorQuantity::HorizontalAccuracy) == "horizontal_accuracy");
    CHECK(unit_of(SensorQuantity::HorizontalAccuracy) == "m");
    CHECK(to_string(SensorQuantity::ClockOffset) == "clock_offset");
    CHECK(unit_of(SensorQuantity::ClockOffset) == "s");
}

TEST_CASE("a control value is clamped to its range and moved to the nearest step", "[hal][camera]")
{
    hal::ControlInfo info{.control = hal::CameraControl::Exposure,
                          .minimum = 0.1,
                          .maximum = 1000.0,
                          .step = 0.1,
                          .default_value = 10.0,
                          .unit = "ms",
                          .supports_auto = true,
                          .calibrated = true};
    CHECK(hal::nearest_setting(info, 10.0) == Catch::Approx(10.0).margin(1e-9));
    CHECK(hal::nearest_setting(info, 10.04) == Catch::Approx(10.0).margin(1e-9));
    CHECK(hal::nearest_setting(info, 10.06) == Catch::Approx(10.1).margin(1e-9));
    CHECK(hal::nearest_setting(info, -5.0) == 0.1);
    // The top of the range is reached exactly: 0.1 + 9999 * 0.1 is a hair above 1000 in floating point.
    CHECK(hal::nearest_setting(info, 1000.0) == 1000.0);
    CHECK(hal::nearest_setting(info, 1e9) == 1000.0);

    // A range that is not a whole number of steps ends at its last full step.
    info.minimum = 0.0;
    info.maximum = 10.0;
    info.step = 3.0;
    CHECK(hal::nearest_setting(info, 10.0) == 9.0);
    CHECK(hal::nearest_setting(info, 4.4) == 3.0);
    CHECK(hal::nearest_setting(info, 4.6) == 6.0);

    // No step: any value in the range.
    info.step = 0.0;
    CHECK(hal::nearest_setting(info, 4.4) == 4.4);
    CHECK(hal::nearest_setting(info, 11.0) == 10.0);
}

TEST_CASE("the error for a closed device names the device", "[hal]")
{
    const DeviceInfo info{
        .id = "uvc:0c45:6366", .kind = DeviceKind::Camera, .name = "Arducam", .driver = "uvc", .simulated = false};
    const Error error = hal::not_open(info).value();
    CHECK(to_string(error.code) == "Unavailable");
    CHECK(error.message == "camera 'uvc:0c45:6366' is not open");
}

TEST_CASE("a tensor shape gives its number of elements", "[hal][inference]")
{
    CHECK(hal::element_count({1, 3, 512, 512}) == 786432);
    CHECK(hal::element_count({7}) == 7);
    CHECK(hal::element_count({}) == 0);         // no shape, no data
    CHECK(hal::element_count({2, 0, 4}) == 0);  // an empty dimension
    CHECK(hal::element_count({-1, 12}) == 0);   // an open dimension has no size yet
}

TEST_CASE("the registry accepts drivers with usable, unique names", "[hal][registry]")
{
    const ManualClock clock(kStart);
    DeviceRegistry registry;
    CHECK(registry.driver_names().empty());
    CHECK(registry.enumerate().empty());

    CHECK(outcome(registry.add_driver(nullptr)) == "InvalidArgument");
    for (const char* bad_name : {"", "With:Colon", "UPPER", "has space", "sim:"}) {
        CAPTURE(bad_name);
        CHECK(outcome(registry.add_driver(std::make_shared<ImuDriver>(bad_name, 1, clock))) == "InvalidArgument");
    }

    REQUIRE(registry.add_driver(std::make_shared<ImuDriver>("first", 1, clock)));
    REQUIRE(registry.add_driver(std::make_shared<ImuDriver>("second-2_b", 1, clock)));
    const auto duplicate = registry.add_driver(std::make_shared<ImuDriver>("first", 5, clock));
    CHECK(outcome(duplicate) == "AlreadyExists");
    CHECK(duplicate.error().message == "a driver named 'first' is already registered");

    CHECK(registry.driver_names() == std::vector<std::string>{"first", "second-2_b"});
    CHECK(registry.enumerate().size() == 2);  // the refused driver added nothing
}

TEST_CASE("the registry lists the devices of all drivers, in order, and by kind", "[hal][registry]")
{
    const ManualClock clock(kStart);
    DeviceRegistry registry;
    REQUIRE(registry.add_driver(std::make_shared<MockDriver>(clock)));
    REQUIRE(registry.add_driver(std::make_shared<ImuDriver>("extra", 2, clock)));

    const std::vector<DeviceInfo> all = registry.enumerate();
    std::vector<std::string> ids;
    for (const DeviceInfo& device : all) {
        ids.push_back(device.id);
        CHECK(device.id.starts_with(device.driver + ":"));
        CHECK(device.simulated);
    }
    CHECK(ids == std::vector<std::string>{"mock:camera:0", "mock:mount:0", "mock:imu:0", "mock:sensor:0",
                                          "mock:inference:0", "extra:imu:0", "extra:imu:1"});

    const std::vector<DeviceInfo> imus = registry.enumerate(DeviceKind::Imu);
    REQUIRE(imus.size() == 3);
    CHECK(imus[0].id == "mock:imu:0");
    CHECK(imus[1].id == "extra:imu:0");
    CHECK(imus[2].id == "extra:imu:1");
    CHECK(registry.enumerate(DeviceKind::Camera).size() == 1);
    CHECK(registry.enumerate(DeviceKind::Transport).empty());
}

TEST_CASE("the registry creates a device as the interface that was asked for", "[hal][registry]")
{
    const ManualClock clock(kStart);
    DeviceRegistry registry;
    REQUIRE(registry.add_driver(std::make_shared<MockDriver>(clock)));
    REQUIRE(registry.add_driver(std::make_shared<EmptyHandedDriver>()));

    const auto camera = registry.create<hal::ICamera>("mock:camera:0");
    REQUIRE(outcome(camera) == "ok");
    CHECK((*camera)->info().id == "mock:camera:0");
    CHECK_FALSE((*camera)->is_open());  // devices are handed out closed

    CHECK(outcome(registry.create<hal::IMount>("mock:mount:0")) == "ok");
    CHECK(outcome(registry.create<hal::IImu>("mock:imu:0")) == "ok");
    CHECK(outcome(registry.create<hal::ISensor>("mock:sensor:0")) == "ok");
    CHECK(outcome(registry.create<hal::IInference>("mock:inference:0")) == "ok");
    CHECK(outcome(registry.create<hal::IDevice>("mock:imu:0")) == "ok");  // any device is an IDevice

    // Each call makes a new object.
    const auto again = registry.create<hal::ICamera>("mock:camera:0");
    REQUIRE(outcome(again) == "ok");
    CHECK(camera->get() != again->get());

    const auto wrong_kind = registry.create<hal::IMount>("mock:camera:0");
    CHECK(outcome(wrong_kind) == "InvalidArgument");
    CHECK(wrong_kind.error().message ==
          "device 'mock:camera:0' is a camera; it cannot be used as another kind of device");

    const auto no_driver = registry.create<hal::ICamera>("uvc:0c45:6366");
    CHECK(outcome(no_driver) == "NotFound");
    CHECK(no_driver.error().message == "no driver 'uvc' for device 'uvc:0c45:6366'");
    CHECK(outcome(registry.create<hal::ICamera>("no-colon-at-all")) == "NotFound");
    CHECK(outcome(registry.create<hal::ICamera>("")) == "NotFound");

    const auto no_device = registry.create<hal::ICamera>("mock:camera:7");
    CHECK(outcome(no_device) == "NotFound");
    CHECK(no_device.error().message == "driver 'mock' has no device 'mock:camera:7'");

    // A driver that returns "success" without a device is a bug in the driver, reported as such.
    CHECK(outcome(registry.create<hal::IDevice>("broken:anything")) == "Internal");
}

TEST_CASE("a device from the registry works like one made directly", "[hal][registry]")
{
    ManualClock clock(kStart);
    DeviceRegistry registry;
    REQUIRE(registry.add_driver(std::make_shared<MockDriver>(clock)));
    check_mount_contract({.make =
                              [&registry] {
                                  auto mount = registry.create<hal::IMount>("mock:mount:0");
                                  REQUIRE(outcome(mount) == "ok");
                                  return *mount;
                              },
                          .pass_time = [&clock](std::chrono::milliseconds time) { clock.advance(time); },
                          .tolerance = Degrees(1e-9)});
}

TEST_CASE("the registry can be used from several threads while drivers are added", "[hal][registry][threads]")
{
    const ManualClock clock(kStart);
    DeviceRegistry registry;
    REQUIRE(registry.add_driver(std::make_shared<MockDriver>(clock)));
    std::atomic<bool> failed{false};
    std::atomic<bool> stop{false};

    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int reader = 0; reader < 4; ++reader) {
        readers.emplace_back([&] {
            while (!stop) {
                if (registry.enumerate().size() < 5 || registry.driver_names().empty() ||
                    !registry.create<hal::ISensor>("mock:sensor:0")) {
                    failed = true;
                }
            }
        });
    }
    for (int i = 0; i < 50; ++i) {
        if (!registry.add_driver(std::make_shared<ImuDriver>("extra" + std::to_string(i), 1, clock))) {
            failed = true;
        }
    }
    stop = true;
    for (std::thread& reader : readers) {
        reader.join();
    }
    CHECK_FALSE(failed);
    CHECK(registry.driver_names().size() == 51);
    CHECK(registry.enumerate(DeviceKind::Imu).size() == 51);  // the mock's one and fifty more
}
