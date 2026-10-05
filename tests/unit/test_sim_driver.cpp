#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/app/devices.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/geometry/rotation.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/mount.hpp>
#include <cloudscope/hal/registry.hpp>
#include <cloudscope/hal/sensors.hpp>
#include <cloudscope/sim/sim_driver.hpp>

#include <QtCore/QtEnvironmentVariables>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using hal::DeviceInfo;
using hal::DeviceKind;
using sim::SimDriver;
using sim::SimulationConfig;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);

// Settings for tests on a manual clock: cameras do not wait for real time.
SimulationConfig unpaced()
{
    SimulationConfig config;
    config.camera.real_time = false;
    return config;
}

std::vector<std::string> ids_of(const std::vector<DeviceInfo>& devices)
{
    std::vector<std::string> ids;
    ids.reserve(devices.size());
    for (const DeviceInfo& device : devices) {
        ids.push_back(device.id);
    }
    return ids;
}

}  // namespace

TEST_CASE("the simulator driver lists and creates the simulated devices", "[sim][driver]")
{
    ManualClock clock(kStart);
    const auto driver = SimDriver::make(clock, unpaced());
    REQUIRE(outcome(driver) == "ok");
    CHECK((*driver)->name() == "sim");

    const std::vector<DeviceInfo> devices = (*driver)->enumerate();
    CHECK(ids_of(devices) == std::vector<std::string>{"sim:camera:sky", "sim:mount:pan-tilt", "sim:imu:head",
                                                      "sim:sensor:gps", "sim:sensor:environment"});
    for (const DeviceInfo& device : devices) {
        CAPTURE(device.id);
        CHECK(device.simulated);  // NFR-DATA-03: never mistaken for a measurement
        CHECK(device.driver == "sim");
        CHECK_THAT(device.name, ContainsSubstring("Simulated"));
    }
    CHECK(to_string(devices[0].kind) == "camera");
    CHECK(to_string(devices[1].kind) == "mount");
    CHECK(to_string(devices[2].kind) == "imu");
    CHECK(to_string(devices[3].kind) == "sensor");
    CHECK(to_string(devices[4].kind) == "sensor");

    hal::DeviceRegistry registry;
    REQUIRE(registry.add_driver(*driver));
    const auto camera = registry.create<hal::ICamera>("sim:camera:sky");
    const auto mount = registry.create<hal::IMount>("sim:mount:pan-tilt");
    const auto imu = registry.create<hal::IImu>("sim:imu:head");
    const auto gps = registry.create<hal::ISensor>("sim:sensor:gps");
    const auto environment = registry.create<hal::ISensor>("sim:sensor:environment");
    REQUIRE(outcome(camera) == "ok");
    REQUIRE(outcome(mount) == "ok");
    REQUIRE(outcome(imu) == "ok");
    REQUIRE(outcome(gps) == "ok");
    REQUIRE(outcome(environment) == "ok");
    CHECK((*camera)->info().id == devices[0].id);
    CHECK((*camera)->info().name == devices[0].name);

    const auto unknown = registry.create<hal::ICamera>("sim:camera:42");
    CHECK(outcome(unknown) == "NotFound");
    CHECK(unknown.error().message == "driver 'sim' has no device 'sim:camera:42'");
    CHECK(outcome(registry.create<hal::ICamera>("sim:camera:replay")) == "NotFound");          // no replay folder set
    CHECK(outcome(registry.create<hal::ICamera>("sim:mount:pan-tilt")) == "InvalidArgument");  // not a camera

    // The devices are views of one rig: the IMU turns when the mount moves.
    REQUIRE((*mount)->open());
    REQUIRE((*imu)->open());
    REQUIRE((*mount)->move_to({.pan = Degrees(90.0), .tilt = Degrees(20.0)}, 60.0));
    clock.advance(10s);
    const auto sample = (*imu)->read();
    REQUIRE(outcome(sample) == "ok");
    const SkyDirection seen = optical_axis(sample->orientation);
    CHECK_THAT(seen.azimuth.value(), WithinAbs(90.0, 1.5));
    CHECK_THAT(seen.elevation.value(), WithinAbs(20.0, 1.5));
    CHECK_THAT((*driver)->rig()->true_pointing().azimuth.value(), WithinAbs(90.0, 1e-9));
}

TEST_CASE("the replay camera is offered when its folder holds pictures", "[sim][driver][replay]")
{
    const ManualClock clock(kStart);
    const TempWorkspace workspace;
    workspace.write("no-pictures/readme.txt", "nothing to show");

    SimulationConfig config = unpaced();
    config.camera.replay_folder = data_dir() / "sky";
    config.camera.replay_fps = 4.0;
    const auto driver = SimDriver::make(clock, config);
    REQUIRE(outcome(driver) == "ok");
    const std::vector<DeviceInfo> devices = (*driver)->enumerate();
    REQUIRE(devices.size() == 6);
    CHECK(devices[1].id == "sim:camera:replay");
    CHECK(to_string(devices[1].kind) == "camera");
    CHECK(devices[1].simulated);

    const auto created = (*driver)->create("sim:camera:replay");
    REQUIRE(outcome(created) == "ok");
    const auto camera = std::dynamic_pointer_cast<hal::ICamera>(*created);
    REQUIRE(camera != nullptr);
    REQUIRE(camera->open());
    CHECK(camera->mode().value() ==
          hal::CameraMode{.width = 400, .height = 400, .format = PixelFormat::Bgr8, .fps = 4.0});
    REQUIRE(camera->start());
    Frame frame(frame_buffer_bytes(PixelFormat::Bgr8, 400, 400));
    CHECK(outcome(camera->read_frame(frame, 0ms)) == "ok");

    // A folder without pictures: no replay camera, and asking for it says why.
    config.camera.replay_folder = workspace.path("no-pictures");
    const auto without = SimDriver::make(clock, config);
    REQUIRE(outcome(without) == "ok");
    CHECK((*without)->enumerate().size() == 5);
    const auto refused = (*without)->create("sim:camera:replay");
    CHECK(outcome(refused) == "NotFound");
    CHECK_THAT(refused.error().message, ContainsSubstring("holds no JPEG or PNG picture"));
}

TEST_CASE("a camera whose cable is pulled disappears from the list of devices", "[sim][driver][fault]")
{
    const ManualClock clock(kStart);
    const auto driver = SimDriver::make(clock, unpaced());
    REQUIRE(outcome(driver) == "ok");
    (*driver)->rig()->set_camera_connected(false);
    CHECK(ids_of((*driver)->enumerate()) ==
          std::vector<std::string>{"sim:mount:pan-tilt", "sim:imu:head", "sim:sensor:gps", "sim:sensor:environment"});
    CHECK(outcome((*driver)->create("sim:camera:sky")) == "NotFound");

    (*driver)->rig()->set_camera_connected(true);
    CHECK((*driver)->enumerate().size() == 5);
    CHECK(outcome((*driver)->create("sim:camera:sky")) == "ok");
}

TEST_CASE("the simulator driver refuses an unusable configuration", "[sim][driver][config]")
{
    const ManualClock clock(kStart);
    SimulationConfig config;
    config.mount.max_acceleration_deg_s2 = 0.0;
    const auto driver = SimDriver::make(clock, config);
    CHECK(outcome(driver) == "InvalidArgument");
    CHECK_THAT(driver.error().message, ContainsSubstring("maximum acceleration"));
}

TEST_CASE("the simulation section of the configuration selects what is simulated", "[sim][config]")
{
    const nlohmann::json& defaults = app_config_format().defaults();
    const auto standard = sim::simulation_settings(defaults);
    REQUIRE(outcome(standard) == "ok");
    CHECK(standard->enabled);
    CHECK(standard->config.seed == 1);
    CHECK(standard->config.camera.replay_folder.empty());
    CHECK(standard->config.camera.replay_fps == 2.0);
    CHECK(standard->config.camera.real_time);

    nlohmann::json changed = defaults;
    changed["simulation"] = {
        {"enabled", false}, {"seed", 42}, {"replay_folder", "C:/data/\u00e9t\u00e9"}, {"replay_fps", 0.5}};
    const auto settings = sim::simulation_settings(changed);
    REQUIRE(outcome(settings) == "ok");
    CHECK_FALSE(settings->enabled);
    CHECK(settings->config.seed == 42);
    CHECK(settings->config.camera.replay_folder == std::filesystem::path(u8"C:/data/\u00e9t\u00e9"));
    CHECK(settings->config.camera.replay_fps == 0.5);

    CHECK(outcome(sim::simulation_settings(nlohmann::json::object())) == "InvalidArgument");
    changed["simulation"]["replay_fps"] = 0.0;  // the schema refuses this earlier; the simulator checks again
    CHECK(outcome(sim::simulation_settings(changed)) == "InvalidArgument");
}

TEST_CASE("a configuration file is checked against the rules for the simulation section", "[sim][config]")
{
    const TempWorkspace workspace;
    const AppPaths paths{.system_config = workspace.path("system/config.toml"),
                         .user_config = workspace.path("user/config.toml"),
                         .log_directory = workspace.path("logs")};

    workspace.write("user/config.toml",
                    "schema_version = 1\n[simulation]\nenabled = false\nseed = 7\nreplay_fps = 5\n");
    const auto loaded = load_app_config(paths);
    REQUIRE(outcome(loaded) == "ok");
    CHECK(loaded->effective.at("simulation").at("enabled") == false);
    CHECK(loaded->effective.at("simulation").at("seed") == 7);
    CHECK(loaded->effective.at("simulation").at("replay_fps") == 5);  // a whole number is a number
    CHECK(loaded->effective.at("simulation")
              .at("replay_folder")
              .get<std::string>()
              .empty());  // from the built-in defaults

    workspace.write("user/config.toml",
                    "schema_version = 1\n[simulation]\nseed = -1\nreplay_fps = 500\nreplay_dir = \"x\"\n");
    const auto refused = load_app_config(paths);
    REQUIRE(outcome(refused) == "Validation");
    CHECK_THAT(refused.error().message, ContainsSubstring("simulation.seed: must be at least 0"));
    CHECK_THAT(refused.error().message, ContainsSubstring("simulation.replay_fps: must be at most 120"));
    CHECK_THAT(refused.error().message, ContainsSubstring("simulation.replay_dir: unknown key"));
}

TEST_CASE("the application's registry holds the simulators unless they are switched off", "[sim][config][registry]")
{
    const ManualClock clock(kStart);
    nlohmann::json effective = app_config_format().defaults();

    hal::DeviceRegistry registry;
    REQUIRE(outcome(add_configured_drivers(registry, effective, clock)) == "ok");
    CHECK(registry.driver_names() == std::vector<std::string>{"sim"});
    CHECK(registry.enumerate().size() == 5);
    CHECK(registry.enumerate(DeviceKind::Camera).size() == 1);
    CHECK(outcome(add_configured_drivers(registry, effective, clock)) == "AlreadyExists");  // not twice

    effective["simulation"]["enabled"] = false;
    hal::DeviceRegistry without;
    REQUIRE(outcome(add_configured_drivers(without, effective, clock)) == "ok");
    CHECK(without.driver_names().empty());
    CHECK(without.enumerate().empty());

    CHECK(outcome(add_configured_drivers(without, nlohmann::json::object(), clock)) == "InvalidArgument");
}

TEST_CASE("a whole simulated rig runs without any hardware", "[sim][integration][realtime][threads]")
{
    // What the application will do with real devices, done with simulated ones: frames flow from the camera
    // through the frame hub to two consumers while the mount moves and the sensors are read.
    const SystemClock clock;
    SimulationConfig config;
    config.gps.time_to_first_fix = 0s;
    config.imu.absolute_heading = true;
    const auto driver = SimDriver::make(clock, config);
    REQUIRE(outcome(driver) == "ok");
    hal::DeviceRegistry registry;
    REQUIRE(registry.add_driver(*driver));

    const auto camera = registry.create<hal::ICamera>("sim:camera:sky").value();
    const auto mount = registry.create<hal::IMount>("sim:mount:pan-tilt").value();
    const auto imu = registry.create<hal::IImu>("sim:imu:head").value();
    const auto gps = registry.create<hal::ISensor>("sim:sensor:gps").value();
    const auto environment = registry.create<hal::ISensor>("sim:sensor:environment").value();
    for (hal::IDevice* device :
         std::vector<hal::IDevice*>{camera.get(), mount.get(), imu.get(), gps.get(), environment.get()}) {
        REQUIRE(outcome(device->open()) == "ok");
    }

    const hal::CameraMode mode{.width = 640, .height = 480, .format = PixelFormat::Gray8, .fps = 30.0};
    REQUIRE(outcome(camera->set_mode(mode)) == "ok");
    FramePool pool(8, frame_buffer_bytes(mode.format, mode.width, mode.height));
    FrameHub hub;
    const auto recorder = hub.subscribe("recorder", Delivery::Queue, 64);
    const auto preview = hub.subscribe("preview", Delivery::Latest);

    // The acquisition loop: pull a frame from the camera, hand it to the hub.
    std::atomic<bool> stop{false};
    std::atomic<bool> camera_failed{false};
    std::uint64_t frames_read = 0;
    std::uint64_t pool_misses = 0;
    REQUIRE(outcome(camera->start()) == "ok");
    std::thread acquisition([&] {
        Frame spare(frame_buffer_bytes(mode.format, mode.width, mode.height));
        while (!stop) {
            std::shared_ptr<Frame> frame = pool.acquire();
            const auto read = camera->read_frame(frame ? *frame : spare, 500ms);
            if (!read) {
                camera_failed = camera_failed || read.error().code != ErrorCode::Timeout;
                continue;
            }
            ++frames_read;
            if (frame) {
                hub.publish(frame);
            } else {
                ++pool_misses;  // every buffer was with a consumer: the frame is read and dropped
            }
        }
    });

    // Meanwhile: point the mount somewhere else and watch it arrive, one frame at a time.
    const hal::MountPosition target{.pan = Degrees(40.0), .tilt = Degrees(70.0)};
    REQUIRE(outcome(mount->move_to(target, 60.0)) == "ok");
    std::uint64_t received = 0;
    std::uint64_t last_sequence = 0;
    bool in_order = true;
    bool arrived = false;
    const auto began = std::chrono::steady_clock::now();
    while ((received < 20 || !arrived) && std::chrono::steady_clock::now() - began < 30s) {
        const FramePtr frame = recorder->wait(1000ms);
        if (frame == nullptr) {
            continue;
        }
        in_order = in_order && (received == 0 || frame->info().sequence > last_sequence) && frame->info().simulated &&
                   frame->info().width == 640 && frame->data().size() == std::size_t{640} * 480;
        last_sequence = frame->info().sequence;
        ++received;
        const auto status = mount->status();
        arrived = status && status->motion == hal::MountMotion::Idle && status->position == target;
    }
    stop = true;
    acquisition.join();
    camera->stop();

    CHECK(received >= 20);
    CHECK(in_order);
    CHECK(arrived);
    CHECK_FALSE(camera_failed);
    // Every frame is accounted for: read = published + dropped for want of a buffer.
    CHECK(hub.published() + pool_misses == frames_read);
    CHECK(recorder->offered() == hub.published());
    CHECK(preview->try_take() != nullptr);  // the display consumer has the newest frame waiting

    // The sensors agree with the simulated world.
    const auto sample = imu->read();
    REQUIRE(outcome(sample) == "ok");
    const SkyDirection seen = optical_axis(sample->orientation);
    CHECK_THAT(seen.azimuth.value(), WithinAbs(40.0, 1.5));
    CHECK_THAT(seen.elevation.value(), WithinAbs(70.0, 1.5));
    const auto fix = gps->read();
    REQUIRE(outcome(fix) == "ok");
    REQUIRE(fix->size() == 5);
    CHECK_THAT(fix->at(0).value, WithinAbs(12.97, 0.001));
    const auto weather = environment->read();
    REQUIRE(outcome(weather) == "ok");
    CHECK(weather->size() == 5);
}

TEST_CASE("a picture of the simulated sky can be written for people to look at", "[sim][camera]")
{
    // Set CLOUDSCOPE_TEST_ARTIFACTS to a folder to keep the picture (the phase document shows one).
    const ManualClock clock(kStart);
    const auto driver = SimDriver::make(clock, unpaced());
    REQUIRE(outcome(driver) == "ok");
    const auto created = (*driver)->create("sim:camera:sky");
    REQUIRE(outcome(created) == "ok");
    const auto camera = std::dynamic_pointer_cast<hal::ICamera>(*created);
    REQUIRE(camera != nullptr);
    REQUIRE(camera->open());
    const hal::CameraMode mode{.width = 1280, .height = 720, .format = PixelFormat::Mjpeg, .fps = 30.0};
    REQUIRE(outcome(camera->set_mode(mode)) == "ok");
    REQUIRE(outcome(camera->start()) == "ok");
    Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
    REQUIRE(outcome(camera->read_frame(frame, 0ms)) == "ok");

    const cv::Mat decoded = cv::imdecode(
        cv::Mat(1, static_cast<int>(frame.data().size()), CV_8UC1, frame.buffer().data()), cv::IMREAD_COLOR);
    CHECK(decoded.size() == cv::Size(1280, 720));

    const QString folder = qEnvironmentVariable("CLOUDSCOPE_TEST_ARTIFACTS");
    if (!folder.isEmpty()) {
        const std::filesystem::path file = std::filesystem::path(folder.toStdU16String()) / "simulated-sky.jpg";
        std::ofstream stream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(frame.data().data()),
                     static_cast<std::streamsize>(frame.data().size()));
        CHECK(stream.good());
    }
}
