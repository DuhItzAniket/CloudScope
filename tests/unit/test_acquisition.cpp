#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/acquisition.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/sim/sim_devices.hpp>
#include <cloudscope/sim/sim_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <thread>

using namespace cloudscope;
using namespace cloudscope::test;
using namespace std::chrono_literals;

namespace {

const UtcTime kStart = from_unix_ms(1'790'000'000'000);

struct Rig {
    SystemClock clock;
    std::shared_ptr<sim::SimDriver> driver;
    std::shared_ptr<hal::ICamera> camera;
    std::shared_ptr<FrameHub> hub = std::make_shared<FrameHub>();

    explicit Rig(bool real_time)
    {
        sim::SimulationConfig config;
        config.camera.real_time = real_time;
        driver = *sim::SimDriver::make(clock, config);
        const auto device = driver->create(sim::kSkyCameraId);
        REQUIRE(outcome(device) == "ok");
        camera = std::dynamic_pointer_cast<hal::ICamera>(*device);
        REQUIRE(camera != nullptr);
        REQUIRE(outcome(camera->open()) == "ok");
        const auto caps = camera->capabilities();
        REQUIRE(outcome(caps) == "ok");
        REQUIRE(outcome(camera->set_mode(caps->modes.front())) == "ok");
    }
};

void wait_for(const Acquisition& acquisition, std::uint64_t frames, std::chrono::milliseconds limit = 5000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (acquisition.stats().frames < frames && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
}

}  // namespace

TEST_CASE("acquisition publishes every frame in order and counts what the camera lost", "[capture][acquisition]")
{
    Rig rig(false);
    auto consumer = rig.hub->subscribe("recorder", Delivery::Queue, 64);
    Acquisition acquisition(rig.camera, rig.hub, rig.clock, {.pool_frames = 8, .read_timeout = 100ms});
    REQUIRE(outcome(acquisition.start()) == "ok");
    CHECK(outcome(acquisition.start()) == "Unavailable");
    CHECK(acquisition.is_running());

    std::uint64_t previous = 0;
    std::uint64_t received = 0;
    for (; received < 20; ++received) {
        const FramePtr frame = consumer->wait(2000ms);
        REQUIRE(frame != nullptr);
        if (received > 0) {
            CHECK(frame->info().sequence == previous + 1);
        }
        previous = frame->info().sequence;
        CHECK(frame->info().simulated);
    }
    // Frames lost in the camera show as a gap and are counted exactly.
    rig.driver->rig()->lose_camera_frames(7);
    const AcquisitionStats before = acquisition.stats();
    wait_for(acquisition, before.frames + 5);
    acquisition.stop();
    CHECK_FALSE(acquisition.is_running());
    const AcquisitionStats after = acquisition.stats();
    CHECK(after.lost == 7);
    CHECK(after.errors == 0);
    CHECK(after.frames >= before.frames + 5);
    CHECK(after.frames == rig.hub->published());
    CHECK(after.fps > 0.0);
    CHECK(after.latency_max >= after.latency_mean);
    CHECK_FALSE(rig.camera->is_streaming());
}

TEST_CASE("a consumer that holds every buffer causes pool misses, never a stall", "[capture][acquisition]")
{
    Rig rig(false);
    auto hoarder = rig.hub->subscribe("hoarder", Delivery::Queue, 64);  // takes nothing: its queue keeps the frames
    Acquisition acquisition(rig.camera, rig.hub, rig.clock, {.pool_frames = 3, .read_timeout = 100ms});
    REQUIRE(outcome(acquisition.start()) == "ok");
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (acquisition.stats().pool_misses < 10 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    acquisition.stop();
    const AcquisitionStats stats = acquisition.stats();
    CHECK(stats.frames == 3);  // the three pooled buffers, then nothing could be published
    CHECK(stats.pool_misses >= 10);
    CHECK(stats.lost == 0);
    CHECK(hoarder->offered() == 3);
}

TEST_CASE("an unplugged camera ends acquisition with the error kept in the statistics", "[capture][acquisition]")
{
    Rig rig(false);
    auto consumer = rig.hub->subscribe("viewer", Delivery::Latest);
    Acquisition acquisition(rig.camera, rig.hub, rig.clock, {.pool_frames = 4, .read_timeout = 100ms});
    REQUIRE(outcome(acquisition.start()) == "ok");
    wait_for(acquisition, 3);
    rig.driver->rig()->set_camera_connected(false);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (acquisition.is_running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(acquisition.is_running());
    const AcquisitionStats stats = acquisition.stats();
    CHECK(stats.errors == 1);
    REQUIRE(stats.last_error.has_value());
    CHECK(stats.last_error->code == ErrorCode::Io);
    acquisition.stop();  // idempotent
    CHECK(consumer->offered() >= 3);
}

TEST_CASE("timeouts are counted while a stalled camera stays connected", "[capture][acquisition]")
{
    Rig rig(true);  // real-time pacing: a stalled camera makes read_frame wait out its timeout
    Acquisition acquisition(rig.camera, rig.hub, rig.clock, {.pool_frames = 4, .read_timeout = 20ms});
    rig.driver->rig()->set_camera_stalled(true);
    REQUIRE(outcome(acquisition.start()) == "ok");
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (acquisition.stats().timeouts < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    acquisition.stop();
    const AcquisitionStats stats = acquisition.stats();
    CHECK(stats.timeouts >= 3);
    CHECK(stats.frames == 0);
    CHECK(stats.errors == 0);
    CHECK(kStart.time_since_epoch().count() > 0);
}
