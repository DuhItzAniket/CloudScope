#include "preview_pipeline.hpp"

#include "image_tools.hpp"

#include <cloudscope/capture/decode.hpp>
#include <cloudscope/common/log.hpp>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cloudscope::desktop {

namespace {

constexpr std::chrono::milliseconds kWaitSlice{100};
constexpr std::int64_t kStatisticsPixels = 2'500'000;  // statistics are computed on a copy reduced to this size
const cv::Vec3b kHighlightColour(255, 0, 255);         // magenta: a colour the sky never has

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop.preview");
    return instance;
}

double ms_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

PreviewPipeline::PreviewPipeline(QObject* parent) : QObject(parent), ui_(this), worker_("preview", 1) {}

PreviewPipeline::~PreviewPipeline()
{
    stop();
}

void PreviewPipeline::start(const std::shared_ptr<FrameHub>& hub)
{
    stop();
    if (!hub) {
        return;
    }
    const std::shared_ptr<FrameSubscription> subscription = hub->subscribe("preview", Delivery::Latest);
    stop_requested_.store(false);
    running_.store(true);
    {
        const std::lock_guard lock(mutex_);
        subscription_ = subscription;
        shown_ = 0;
    }
    worker_.post([this, subscription] { run(subscription); });
}

void PreviewPipeline::stop()
{
    if (!running_.load()) {
        return;
    }
    stop_requested_.store(true);
    worker_.wait_idle();
    running_.store(false);
    const std::lock_guard lock(mutex_);
    subscription_.reset();
}

void PreviewPipeline::set_options(const PreviewOptions& options)
{
    const std::lock_guard lock(mutex_);
    options_ = options;
}

PreviewOptions PreviewPipeline::options() const
{
    const std::lock_guard lock(mutex_);
    return options_;
}

std::shared_ptr<const PreviewFrame> PreviewPipeline::latest() const
{
    const std::lock_guard lock(mutex_);
    return latest_;
}

void PreviewPipeline::run(const std::shared_ptr<FrameSubscription>& subscription)
{
    while (!stop_requested_.load()) {
        const FramePtr frame = subscription->wait(kWaitSlice);
        if (!frame) {
            continue;
        }
        const std::shared_ptr<const PreviewFrame> result = process(frame);
        if (!result) {
            continue;
        }
        if (!notify_pending_.exchange(true)) {
            ui_.post([this] {
                notify_pending_.store(false);
                note_shown();
                Q_EMIT frameReady();
            });
        }
    }
    running_.store(false);
}

std::shared_ptr<const PreviewFrame> PreviewPipeline::process(const FramePtr& frame)
{
    PreviewOptions options;
    std::shared_ptr<const FrameStatistics> statistics;
    ChannelHistograms channels{};
    bool statistics_due = false;
    std::uint64_t skipped = 0;
    {
        const std::lock_guard lock(mutex_);
        options = options_;
        statistics = cached_statistics_;
        channels = cached_channels_;
        const auto now = std::chrono::steady_clock::now();
        statistics_due = !cached_statistics_ || now - statistics_time_ >= options_.statistics_interval;
        if (subscription_) {
            skipped = subscription_->skipped();
        }
    }
    const auto started = std::chrono::steady_clock::now();
    auto decoded = decode_bgr8(*frame);
    if (!decoded) {
        log().debug("frame {} not decoded: {}", frame->info().sequence, decoded.error().to_string());
        return nullptr;
    }
    auto result = std::make_shared<PreviewFrame>();
    result->info = frame->info();
    result->decode_ms = ms_since(started);
    const auto processing = std::chrono::steady_clock::now();
    auto picture = std::make_shared<const cv::Mat>(std::move(*decoded));
    result->picture = picture;

    if (statistics_due) {
        // The scopes need shares and percentiles, which a reduced copy gives as well: above a few megapixels the
        // statistics (Laplacian, noise estimate) would otherwise take longer than a frame period.
        const std::int64_t pixels = static_cast<std::int64_t>(picture->cols) * picture->rows;
        cv::Mat for_statistics = *picture;
        if (pixels > kStatisticsPixels) {
            const double scale = std::sqrt(static_cast<double>(kStatisticsPixels) / static_cast<double>(pixels));
            cv::resize(*picture, for_statistics, cv::Size(), scale, scale, cv::INTER_AREA);
        }
        statistics = std::make_shared<const FrameStatistics>(compute_statistics(for_statistics));
        channels = channel_histograms(for_statistics);
        const std::lock_guard lock(mutex_);
        cached_statistics_ = statistics;
        cached_channels_ = channels;
        statistics_time_ = std::chrono::steady_clock::now();
    }
    result->statistics = statistics;
    result->channels = channels;
    if (options.compute_parade) {
        result->parade = rgb_parade(*picture);
    }

    // The preview copy: stretch, highlight, night vision. The decoded picture stays as it is.
    const Histogram luma_histogram = statistics ? statistics->histogram : Histogram{};
    result->lut = stretch_lut(options.stretch, luma_histogram);
    cv::Mat preview = is_identity(result->lut) ? *picture : apply_lut(*picture, result->lut);
    if (options.stretch.highlight_overexposed) {
        if (preview.data == picture->data) {
            preview = picture->clone();
        }
        result->overexposed_pixels =
            highlight_overexposed(preview, luma_of(*picture), options.stretch.clip_threshold, kHighlightColour);
    }
    if (options.night_vision) {
        preview = night_vision(preview);
    }
    result->image = to_qimage(preview);
    result->process_ms = ms_since(processing);
    result->skipped = skipped;
    {
        const std::lock_guard lock(mutex_);
        ++shown_;
        result->shown = shown_;
        latest_ = result;
    }
    return result;
}

void PreviewPipeline::note_shown()
{
    const auto now = std::chrono::steady_clock::now();
    if (fps_window_frames_ == 0) {
        fps_window_start_ = now;
    }
    ++fps_window_frames_;
    const double elapsed_s = std::chrono::duration<double>(now - fps_window_start_).count();
    if (elapsed_s >= 1.0) {
        display_fps_ = static_cast<double>(fps_window_frames_ - 1) / elapsed_s;
        fps_window_start_ = now;
        fps_window_frames_ = 1;
    }
}

}  // namespace cloudscope::desktop
