// The acquisition thread (P022, architecture 4.1): the one thread that reads frames from a camera and hands
// them to everyone else through a FrameHub.
//
//   auto hub = std::make_shared<FrameHub>();
//   Acquisition acquisition(camera, hub, clock);        // the camera is open and in the wanted mode
//   acquisition.start();                                 // starts the camera's stream and the thread
//   auto sub = hub->subscribe("recorder", Delivery::Queue, 8);
//   ...
//   acquisition.stop();
//
// Frames are read straight into pooled buffers (no copy), stamped by the camera with the host time of arrival,
// and published. Nothing waits for a consumer: when every pooled buffer is still held, the next frame is read
// into a scratch buffer and counted as a pool miss (the camera keeps flowing). Frames the camera or its driver
// lost show as gaps in the sequence numbers and are counted as `lost`. An Io error ends acquisition; the
// statistics keep the error so the caller can decide to reopen the camera (FR-CAM-08).
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/capture/frame_hub.hpp"
#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/hal/camera.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace cloudscope {

struct AcquisitionStats {
    bool running = false;
    std::uint64_t frames = 0;       // published
    std::uint64_t lost = 0;         // gaps in the camera's sequence numbers
    std::uint64_t pool_misses = 0;  // read but not published: no pooled buffer was free
    std::uint64_t timeouts = 0;     // read_frame() timeouts (the stream is alive but slow)
    std::uint64_t errors = 0;       // read errors other than timeout; the last one ends acquisition
    std::optional<Error> last_error;
    double fps = 0.0;                       // over the whole run
    double recent_fps = 0.0;                // over the last second of running time
    std::chrono::microseconds latency_mean{0};  // from the frame's arrival (camera clock) to its publication
    std::chrono::microseconds latency_max{0};
    std::chrono::milliseconds elapsed{0};
};

struct AcquisitionOptions {
    std::size_t pool_frames = 6;
    std::chrono::milliseconds read_timeout{500};
};

class Acquisition {
public:
    // The camera must be open; its current mode decides the buffer size. The clock measures latency and must
    // outlive the acquisition.
    Acquisition(std::shared_ptr<hal::ICamera> camera, std::shared_ptr<FrameHub> hub, const IClock& clock,
                AcquisitionOptions options = {});
    ~Acquisition();
    Acquisition(const Acquisition&) = delete;
    Acquisition& operator=(const Acquisition&) = delete;
    Acquisition(Acquisition&&) = delete;
    Acquisition& operator=(Acquisition&&) = delete;

    // Starts the camera's stream and the thread. Unavailable if already running; the camera's own errors
    // otherwise.
    [[nodiscard]] Expected<void> start();
    // Stops the thread and the camera's stream; returns when the thread has ended. May be called twice.
    void stop();
    [[nodiscard]] bool is_running() const;
    [[nodiscard]] AcquisitionStats stats() const;

    [[nodiscard]] const std::shared_ptr<FrameHub>& hub() const { return hub_; }

private:
    void run();

    std::shared_ptr<hal::ICamera> camera_;
    std::shared_ptr<FrameHub> hub_;
    const IClock& clock_;
    AcquisitionOptions options_;
    std::unique_ptr<FramePool> pool_;
    std::unique_ptr<Frame> scratch_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex mutex_;  // guards stats_
    AcquisitionStats stats_;
    MonotonicTime started_;
    MonotonicTime window_started_;
    std::uint64_t window_frames_ = 0;
    double latency_sum_us_ = 0.0;
};

}  // namespace cloudscope
