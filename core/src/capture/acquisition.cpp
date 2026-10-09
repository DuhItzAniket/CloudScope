#include "cloudscope/capture/acquisition.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <utility>

namespace cloudscope {

Acquisition::Acquisition(std::shared_ptr<hal::ICamera> camera, std::shared_ptr<FrameHub> hub, const IClock& clock,
                         AcquisitionOptions options)
    : camera_(std::move(camera)), hub_(std::move(hub)), clock_(clock), options_(options)
{
}

Acquisition::~Acquisition()
{
    stop();
}

Expected<void> Acquisition::start()
{
    {
        const std::lock_guard lock(mutex_);
        if (stats_.running) {
            return fail(ErrorCode::Unavailable, "acquisition is already running");
        }
    }
    const auto mode = camera_->mode();
    if (!mode) {
        return fail(mode.error());
    }
    const std::size_t bytes = frame_buffer_bytes(mode->format, mode->width, mode->height);
    pool_ = std::make_unique<FramePool>(std::max<std::size_t>(options_.pool_frames, 1), bytes);
    scratch_ = std::make_unique<Frame>(bytes);
    if (auto started = camera_->start(); !started) {
        return started;
    }
    {
        const std::lock_guard lock(mutex_);
        stats_ = {};
        stats_.running = true;
    }
    started_ = clock_.now_monotonic();
    window_started_ = started_;
    window_frames_ = 0;
    latency_sum_us_ = 0.0;
    stop_requested_.store(false);
    thread_ = std::thread([this] { run(); });
    return {};
}

void Acquisition::stop()
{
    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    camera_->stop();
    const std::lock_guard lock(mutex_);
    stats_.running = false;
}

bool Acquisition::is_running() const
{
    const std::lock_guard lock(mutex_);
    return stats_.running;
}

AcquisitionStats Acquisition::stats() const
{
    const std::lock_guard lock(mutex_);
    AcquisitionStats copy = stats_;
    const MonotonicTime now = clock_.now_monotonic();
    if (copy.running || copy.frames > 0) {
        copy.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_);
        const double seconds = std::chrono::duration<double>(now - started_).count();
        copy.fps = seconds > 0.0 ? static_cast<double>(copy.frames) / seconds : 0.0;
    }
    if (copy.frames > 0) {
        copy.latency_mean = std::chrono::microseconds(
            static_cast<std::chrono::microseconds::rep>(latency_sum_us_ / static_cast<double>(copy.frames)));
    }
    return copy;
}

void Acquisition::run()
{
    std::optional<std::uint64_t> expected;
    while (!stop_requested_.load()) {
        const std::shared_ptr<Frame> pooled = pool_->acquire();
        Frame& target = pooled ? *pooled : *scratch_;
        const auto read = camera_->read_frame(target, options_.read_timeout);
        if (!read) {
            const std::lock_guard lock(mutex_);
            if (read.error().code == ErrorCode::Timeout) {
                ++stats_.timeouts;
                continue;
            }
            ++stats_.errors;
            stats_.last_error = read.error();
            stats_.running = false;
            return;
        }
        const FrameInfo& info = target.info();
        const MonotonicTime now = clock_.now_monotonic();
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - info.captured.monotonic);
        {
            const std::lock_guard lock(mutex_);
            if (expected && info.sequence > *expected) {
                stats_.lost += info.sequence - *expected;
            }
            expected = info.sequence + 1;
            if (!pooled) {
                ++stats_.pool_misses;
                continue;
            }
            ++stats_.frames;
            latency_sum_us_ += static_cast<double>(latency.count());
            stats_.latency_max = std::max(stats_.latency_max, latency);
            ++window_frames_;
            const double window = std::chrono::duration<double>(now - window_started_).count();
            if (window >= 1.0) {
                stats_.recent_fps = static_cast<double>(window_frames_) / window;
                window_frames_ = 0;
                window_started_ = now;
            }
        }
        hub_->publish(pooled);
    }
    const std::lock_guard lock(mutex_);
    stats_.running = false;
}

}  // namespace cloudscope
