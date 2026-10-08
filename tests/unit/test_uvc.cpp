// The UVC driver: ids, configuration, enumeration on this machine, and (hidden, needs a camera) the HAL
// contract and a real capture.
//
//   cloudscope-unit-tests "[hardware]"      runs the hidden tests against the first camera the driver lists

#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/app/devices.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/registry.hpp>
#include <cloudscope/uvc/uvc_driver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::StartsWith;
using hal::DeviceInfo;
using hal::DeviceKind;

TEST_CASE("UVC device ids name the model and count cameras of one model", "[uvc]")
{
    CHECK(uvc::device_id(0x0c45, 0x636d, 1) == "uvc:0c45:636d:1");
    CHECK(uvc::device_id(0x04f2, 0xb7b6, 2) == "uvc:04f2:b7b6:2");
    CHECK(uvc::device_id(0, 0, 1) == "uvc:0000:0000:1");  // a camera without USB ids is still addressable
}

TEST_CASE("the [camera] section is read from the effective configuration", "[uvc][config]")
{
    const auto on = uvc::camera_settings(nlohmann::json{{"camera", {{"uvc", true}}}});
    REQUIRE(on.has_value());
    CHECK(on->uvc);
    const auto off = uvc::camera_settings(nlohmann::json{{"camera", {{"uvc", false}}}});
    REQUIRE(off.has_value());
    CHECK_FALSE(off->uvc);
    const auto missing = uvc::camera_settings(nlohmann::json::object());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("the UVC driver enumerates the computer's cameras with well-formed ids", "[uvc]")
{
    SystemClock clock;
    const auto driver = uvc::UvcDriver::make(clock);
    REQUIRE(driver->name() == "uvc");
    const std::vector<DeviceInfo> cameras = driver->enumerate();  // may be empty on a machine without a camera
    for (const DeviceInfo& camera : cameras) {
        CAPTURE(camera.id, camera.name);
        CHECK_THAT(camera.id, StartsWith("uvc:"));
        CHECK(camera.kind == DeviceKind::Camera);
        CHECK(camera.driver == "uvc");
        CHECK_FALSE(camera.simulated);
        CHECK_FALSE(camera.name.empty());
        CHECK(std::ranges::count(cameras, camera.id, &DeviceInfo::id) == 1);
    }
    CHECK(outcome(driver->create("uvc:ffff:ffff:9")) == "NotFound");
}

TEST_CASE("the configured drivers include the UVC driver unless [camera] uvc is off", "[uvc][config]")
{
    SystemClock clock;
    const auto defaults = load_app_config({}, {}, nlohmann::json::object());
    REQUIRE(defaults.has_value());

    hal::DeviceRegistry with;
    REQUIRE(outcome(add_configured_drivers(with, defaults->effective, clock)) == "ok");
    CHECK(std::ranges::count(with.driver_names(), std::string("uvc")) == 1);

    hal::DeviceRegistry without;
    const auto off = load_app_config({}, {}, nlohmann::json{{"camera", {{"uvc", false}}}});
    REQUIRE(off.has_value());
    REQUIRE(outcome(add_configured_drivers(without, off->effective, clock)) == "ok");
    CHECK(std::ranges::count(without.driver_names(), std::string("uvc")) == 0);
}

// ---------------------------------------------------------------------------------------------- with hardware

namespace {

std::shared_ptr<uvc::UvcDriver> driver_with_a_camera(const IClock& clock, std::string& id)
{
    auto driver = uvc::UvcDriver::make(clock);
    const std::vector<DeviceInfo> cameras = driver->enumerate();
    if (cameras.empty()) {
        return nullptr;
    }
    // Prefer an external camera (the laptop's own webcam has the vendor id of the laptop maker).
    const auto external = std::ranges::find_if(cameras, [](const DeviceInfo& c) { return c.id.find("uvc:04f2") != 0; });
    id = (external != cameras.end() ? *external : cameras.front()).id;
    return driver;
}

}  // namespace

TEST_CASE("a real UVC camera obeys the camera contract", "[.][hardware][uvc]")
{
    SystemClock clock;
    std::string id;
    const auto driver = driver_with_a_camera(clock, id);
    REQUIRE(driver != nullptr);
    CAPTURE(id);
    test::check_camera_contract([&] {
        auto camera = driver->create(id);
        REQUIRE(outcome(camera) == "ok");
        return std::dynamic_pointer_cast<hal::ICamera>(*camera);
    });
}

TEST_CASE("a real UVC camera streams numbered frames of the chosen mode", "[.][hardware][uvc]")
{
    SystemClock clock;
    std::string id;
    const auto driver = driver_with_a_camera(clock, id);
    REQUIRE(driver != nullptr);
    auto created = driver->create(id);
    REQUIRE(outcome(created) == "ok");
    const auto camera = std::dynamic_pointer_cast<hal::ICamera>(*created);
    REQUIRE(outcome(camera->open()) == "ok");
    const auto capabilities = camera->capabilities();
    REQUIRE(outcome(capabilities) == "ok");
    REQUIRE_FALSE(capabilities->modes.empty());
    // The smallest listed mode keeps the test short.
    const hal::CameraMode mode = *std::ranges::min_element(
        capabilities->modes, {}, [](const hal::CameraMode& m) { return static_cast<long>(m.width) * m.height; });
    REQUIRE(outcome(camera->set_mode(mode)) == "ok");
    const auto started = camera->start();
    const std::string start_note = started ? std::string("started") : started.error().to_string();
    INFO(start_note);
    REQUIRE(outcome(started) == "ok");
    Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
    std::uint64_t previous = 0;
    int received = 0;
    for (int i = 0; i < 40 && received < 10; ++i) {
        const auto read = camera->read_frame(frame, 2000ms);
        if (!read && read.error().code == ErrorCode::Timeout) {
            continue;
        }
        REQUIRE(outcome(read) == "ok");
        CHECK(frame.info().width == mode.width);
        CHECK(frame.info().height == mode.height);
        CHECK(frame.info().format == mode.format);
        CHECK_FALSE(frame.info().simulated);
        CHECK(frame.data().size() > 0);
        if (received > 0) {
            CHECK(frame.info().sequence > previous);
        }
        previous = frame.info().sequence;
        ++received;
    }
    CHECK(received == 10);
    camera->stop();
    camera->close();
}
