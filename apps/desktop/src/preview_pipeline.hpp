// From camera frames to pictures on screen (P034, P036): a worker thread takes the newest frame from the hub,
// decodes it, computes the statistics the scopes need, applies the display stretch and the over-exposure
// highlight to a preview copy (never to the frame itself), and hands the result to the UI thread. Frames that
// arrive while one is being processed are skipped, never queued: the preview shows the present (architecture 4.1).
//
// Lives on the UI thread; the worker is its own. frameReady() is emitted on the UI thread, at most once per
// processed frame; latest() returns the newest result at any time.
#pragma once

#include "display_stretch.hpp"

#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/common/executor.hpp>

#include <QtCore/QObject>
#include <QtGui/QImage>
#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace cloudscope::desktop {

struct PreviewFrame {
    QImage image;  // display-ready: stretched, highlighted, red under the night theme
    FrameInfo info;
    std::shared_ptr<const cv::Mat> picture;  // the decoded 8-bit picture as the camera delivered it (BGR or grey)
    std::shared_ptr<const FrameStatistics> statistics;  // of `picture`; may be from an earlier frame
    ChannelHistograms channels{};                       // of `picture`, same age as `statistics`
    std::array<cv::Mat, 3> parade;                      // RGB parade of `picture` (empty when not computed)
    Lut lut{};                                          // the stretch applied to `image`
    int overexposed_pixels = 0;                         // highlighted in `image` (0 when the highlight is off)
    double decode_ms = 0.0;
    double process_ms = 0.0;    // statistics, stretch and conversion
    std::uint64_t skipped = 0;  // frames the preview did not show because it was busy (since start)
    std::uint64_t shown = 0;    // frames processed since start
};

struct PreviewOptions {
    StretchSettings stretch;
    bool night_vision = false;
    bool compute_parade = false;
    std::chrono::milliseconds statistics_interval{200};  // the statistics are recomputed at most this often
};

class PreviewPipeline : public QObject {
    Q_OBJECT

public:
    explicit PreviewPipeline(QObject* parent = nullptr);
    ~PreviewPipeline() override;
    PreviewPipeline(const PreviewPipeline&) = delete;
    PreviewPipeline& operator=(const PreviewPipeline&) = delete;
    PreviewPipeline(PreviewPipeline&&) = delete;
    PreviewPipeline& operator=(PreviewPipeline&&) = delete;

    // Subscribes to the hub (Latest delivery) and starts processing. Stops a previous subscription first.
    void start(const std::shared_ptr<FrameHub>& hub);
    void stop();
    [[nodiscard]] bool is_running() const { return running_.load(); }

    void set_options(const PreviewOptions& options);  // takes effect from the next frame
    [[nodiscard]] PreviewOptions options() const;

    [[nodiscard]] std::shared_ptr<const PreviewFrame> latest() const;
    // Frames shown per second over the last second or so, measured on the UI thread.
    [[nodiscard]] double display_fps() const { return display_fps_; }

    // Processes one frame in the calling thread (for tests and tools; the same code the worker runs).
    [[nodiscard]] std::shared_ptr<const PreviewFrame> process(const FramePtr& frame);

Q_SIGNALS:
    void frameReady();

private:
    void run(const std::shared_ptr<FrameSubscription>& subscription);
    void note_shown();

    mutable std::mutex mutex_;  // guards options_, latest_, statistics cache
    PreviewOptions options_;
    std::shared_ptr<const PreviewFrame> latest_;
    std::shared_ptr<const FrameStatistics> cached_statistics_;
    ChannelHistograms cached_channels_{};
    std::chrono::steady_clock::time_point statistics_time_{};
    std::uint64_t shown_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> notify_pending_{false};
    std::shared_ptr<FrameSubscription> subscription_;
    QtExecutor ui_;
    // Display rate: time stamps of recent frameReady() emissions.
    std::chrono::steady_clock::time_point fps_window_start_{};
    int fps_window_frames_ = 0;
    double display_fps_ = 0.0;
    ThreadPool worker_;  // last: joined first
};

}  // namespace cloudscope::desktop
