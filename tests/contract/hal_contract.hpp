// Contract tests: the rules of the HAL interfaces (core/include/cloudscope/hal/*.hpp) in executable form.
//
// Every implementation of an interface runs the matching function: mocks, simulators and, on a machine that
// has the hardware, real drivers. A driver that passes can stand in for any other behind the interface.
//
//   TEST_CASE("the mock camera obeys the camera contract", "[hal][contract]")
//   {
//       ManualClock clock(...);
//       check_camera_contract([&] { return std::make_shared<MockCamera>(clock); });
//   }
//
// The functions use Catch2 sections, so they must be called from inside a TEST_CASE; the factory is called
// once per section and must return a new, closed device each time.
#pragma once

#include <cloudscope/common/units.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/device.hpp>
#include <cloudscope/hal/inference.hpp>
#include <cloudscope/hal/mount.hpp>
#include <cloudscope/hal/sensors.hpp>
#include <cloudscope/hal/transport.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace cloudscope::test {

// The outcome of a call as text: "ok", or the name of its error code ("Unavailable", "InvalidArgument", ...).
template <class T>
[[nodiscard]] std::string outcome(const Expected<T>& result)
{
    return result ? std::string("ok") : std::string(to_string(result.error().code));
}

// The rules for all devices (hal/device.hpp). `device` must be new: closed and never opened. It is left closed.
void check_device_contract(hal::IDevice& device, hal::DeviceKind kind);

void check_camera_contract(const std::function<std::shared_ptr<hal::ICamera>()>& make);

struct MountContract {
    std::function<std::shared_ptr<hal::IMount>()> make;
    // Lets time pass for the mount: advances the clock a simulated mount runs on, or waits for a real one.
    std::function<void(std::chrono::milliseconds)> pass_time;
    // How far from its target a mount at rest may report itself (feedback noise, resolution).
    Degrees tolerance{0.5};
};
void check_mount_contract(const MountContract& contract);

void check_imu_contract(const std::function<std::shared_ptr<hal::IImu>()>& make);

void check_sensor_contract(const std::function<std::shared_ptr<hal::ISensor>()>& make);

// The factory returns the two ends of one link, both closed.
using TransportPair = std::pair<std::shared_ptr<hal::ITransport>, std::shared_ptr<hal::ITransport>>;
void check_transport_contract(const std::function<TransportPair()>& make_pair);

// `model` is a model file the engine can load.
void check_inference_contract(const std::function<std::shared_ptr<hal::IInference>()>& make,
                              const std::filesystem::path& model);

}  // namespace cloudscope::test
