// cloudscope-bench: measures the frame pipeline on this machine.
//
//   cloudscope-bench                         4K BGR frames, as fast as possible
//   cloudscope-bench --width 4656 --height 3496 --frames 100
//   cloudscope-bench --fps 60                paced like a 60 fps camera
//   cloudscope-bench --min-fps 60            exit code 1 unless at least 60 fps were reached without a drop
//   cloudscope-bench --json
//
// What is measured: one producer thread copies each frame into a pooled buffer and publishes it through a
// FrameHub; a "recorder" consumer receives every frame through the lock-free queue and a "preview" consumer
// takes only the latest. The source image is synthetic (labelled simulated in the frame metadata).
//
// Exit codes: 0 success, 1 the --min-fps requirement was not met, 2 wrong usage, 3 internal error.

#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/common/build_info.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/sim/synthetic_sky.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

namespace {

using namespace cloudscope;
using SteadyClock = std::chrono::steady_clock;

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInternalError = 3;

struct Options {
    int width = 3840;
    int height = 2160;
    int frames = 300;
    double fps = 0.0;      // 0: as fast as possible
    double min_fps = 0.0;  // 0: no requirement
    bool json = false;
};

struct Result {
    double seconds = 0.0;
    double fps = 0.0;
    double gigabytes_per_second = 0.0;
    std::uint64_t published = 0;
    std::uint64_t pool_misses = 0;  // frames the producer could not copy because no buffer was free
    std::uint64_t recorder_received = 0;
    std::uint64_t recorder_dropped = 0;
    std::uint64_t preview_received = 0;
    std::uint64_t preview_skipped = 0;
    std::uint64_t checksum = 0;
    double worst_latency_ms = 0.0;  // publish to recorder, longest
};

bool write(std::FILE* stream, const std::string& text)
{
    return std::fwrite(text.data(), 1, text.size(), stream) == text.size();
}

// Reads one byte per memory page, as a consumer that looks at the whole frame would at least do.
std::uint64_t touch(const Frame& frame)
{
    constexpr std::size_t kPage = 4096;
    std::uint64_t sum = 0;
    const std::span<const std::byte> data = frame.data();
    for (std::size_t offset = 0; offset < data.size(); offset += kPage) {
        sum += std::to_integer<std::uint64_t>(data[offset]);
    }
    return sum;
}

Result run_frames(const Options& options)
{
    sim::SyntheticSkySpec spec;
    spec.width = options.width;
    spec.height = options.height;
    spec.sun_radius_px = options.height / 40.0;
    const auto sky = sim::make_synthetic_sky(spec);
    if (!sky) {
        throw std::runtime_error(sky.error().to_string());
    }
    const cv::Mat& source = sky->image;
    const std::size_t frame_bytes = frame_buffer_bytes(PixelFormat::Bgr8, options.width, options.height);

    constexpr std::size_t kPoolFrames = 8;
    constexpr std::size_t kRecorderQueue = 4;
    FramePool pool(kPoolFrames, frame_bytes);
    FrameHub hub;
    const auto recorder = hub.subscribe("recorder", Delivery::Queue, kRecorderQueue);
    const auto preview = hub.subscribe("preview", Delivery::Latest);
    const SystemClock clock;
    std::atomic<bool> producing{true};
    Result result;

    std::thread recorder_thread([&] {
        while (true) {
            const FramePtr frame = recorder->wait(std::chrono::milliseconds(20));
            if (frame == nullptr) {
                if (!producing) {
                    break;
                }
                continue;
            }
            const double latency_ms =
                std::chrono::duration<double, std::milli>(SteadyClock::now() - frame->info().captured.monotonic)
                    .count();
            result.worst_latency_ms = std::max(result.worst_latency_ms, latency_ms);
            result.checksum += touch(*frame);
            ++result.recorder_received;
        }
    });
    std::thread preview_thread([&] {
        while (true) {
            const FramePtr frame = preview->wait(std::chrono::milliseconds(20));
            if (frame == nullptr) {
                if (!producing) {
                    break;
                }
                continue;
            }
            ++result.preview_received;
        }
    });

    const auto interval =
        options.fps > 0.0 ? std::chrono::duration<double>(1.0 / options.fps) : std::chrono::duration<double>(0.0);
    const SteadyClock::time_point start = SteadyClock::now();
    for (int i = 0; i < options.frames; ++i) {
        if (options.fps > 0.0) {
            std::this_thread::sleep_until(start + std::chrono::duration_cast<SteadyClock::duration>(interval * i));
        }
        const std::shared_ptr<Frame> frame = pool.acquire();
        if (frame == nullptr) {
            ++result.pool_misses;
            continue;
        }
        std::memcpy(frame->buffer().data(), source.data, frame_bytes);
        frame->set_size(frame_bytes);
        frame->info() = {.sequence = static_cast<std::uint64_t>(i),
                         .captured = clock.now(),
                         .width = options.width,
                         .height = options.height,
                         .format = PixelFormat::Bgr8,
                         .stride = static_cast<std::size_t>(options.width) * 3,
                         .simulated = true};
        hub.publish(frame);
    }
    result.seconds = std::chrono::duration<double>(SteadyClock::now() - start).count();
    producing = false;
    recorder_thread.join();
    preview_thread.join();

    result.published = hub.published();
    result.recorder_dropped = recorder->dropped();
    result.preview_skipped = preview->skipped();
    result.fps = static_cast<double>(result.published) / result.seconds;
    result.gigabytes_per_second = result.fps * static_cast<double>(frame_bytes) / 1e9;
    return result;
}

int run(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cloudscope-bench"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Measures the CloudScope frame pipeline on this machine."));
    const QCommandLineOption help_option = parser.addHelpOption();
    const QCommandLineOption width_option(QStringLiteral("width"),
                                          QStringLiteral("Frame width in pixels (default 3840)."),
                                          QStringLiteral("pixels"), QStringLiteral("3840"));
    const QCommandLineOption height_option(QStringLiteral("height"),
                                           QStringLiteral("Frame height in pixels (default 2160)."),
                                           QStringLiteral("pixels"), QStringLiteral("2160"));
    const QCommandLineOption frames_option(QStringLiteral("frames"),
                                           QStringLiteral("Number of frames to produce (default 300)."),
                                           QStringLiteral("count"), QStringLiteral("300"));
    const QCommandLineOption fps_option(
        QStringLiteral("fps"), QStringLiteral("Produce at this rate; 0 means as fast as possible (default 0)."),
        QStringLiteral("rate"), QStringLiteral("0"));
    const QCommandLineOption min_fps_option(
        QStringLiteral("min-fps"),
        QStringLiteral("Fail (exit code 1) unless this rate is reached with no dropped frame."), QStringLiteral("rate"),
        QStringLiteral("0"));
    const QCommandLineOption json_option(QStringLiteral("json"), QStringLiteral("Print the result as JSON."));
    parser.addOptions({width_option, height_option, frames_option, fps_option, min_fps_option, json_option});

    if (!parser.parse(QCoreApplication::arguments()) || !parser.positionalArguments().isEmpty()) {
        const std::string reason =
            parser.errorText().isEmpty() ? "Unexpected argument." : parser.errorText().toStdString();
        write(stderr, reason + "\nTry 'cloudscope-bench --help'.\n");
        return kExitUsage;
    }
    if (parser.isSet(help_option)) {
        return write(stdout, parser.helpText().toStdString()) ? kExitOk : kExitFailed;
    }

    Options options;
    bool width_ok = false;
    bool height_ok = false;
    bool frames_ok = false;
    bool fps_ok = false;
    bool min_fps_ok = false;
    options.width = parser.value(width_option).toInt(&width_ok);
    options.height = parser.value(height_option).toInt(&height_ok);
    options.frames = parser.value(frames_option).toInt(&frames_ok);
    options.fps = parser.value(fps_option).toDouble(&fps_ok);
    options.min_fps = parser.value(min_fps_option).toDouble(&min_fps_ok);
    options.json = parser.isSet(json_option);
    const bool valid = width_ok && height_ok && frames_ok && fps_ok && min_fps_ok && options.width >= 16 &&
                       options.width <= 8192 && options.height >= 16 && options.height <= 8192 && options.frames >= 1 &&
                       options.frames <= 1'000'000 && options.fps >= 0.0 && options.fps <= 10000.0 &&
                       options.min_fps >= 0.0;
    if (!valid) {
        write(stderr, "Invalid value: width and height 16 to 8192, frames 1 to 1000000, rates not negative.\n"
                      "Try 'cloudscope-bench --help'.\n");
        return kExitUsage;
    }

    const Result result = run_frames(options);
    const bool no_loss = result.recorder_dropped == 0 && result.pool_misses == 0;
    const bool passed = options.min_fps <= 0.0 || (result.fps >= options.min_fps && no_loss);
    const BuildInfo& build = build_info();

    std::string report;
    if (options.json) {
        const nlohmann::ordered_json json = {
            {"benchmark", "frames"},
            {"simulated_source", true},
            {"build",
             {{"version", build.version},
              {"git_revision", build.git_revision},
              {"build_type", build.build_type},
              {"compiler", build.compiler},
              {"system", build.system},
              {"architecture", build.architecture}}},
            {"width", options.width},
            {"height", options.height},
            {"format", "BGR8"},
            {"frames_requested", options.frames},
            {"paced_fps", options.fps},
            {"seconds", result.seconds},
            {"fps", result.fps},
            {"copy_gigabytes_per_second", result.gigabytes_per_second},
            {"published", result.published},
            {"pool_misses", result.pool_misses},
            {"recorder_received", result.recorder_received},
            {"recorder_dropped", result.recorder_dropped},
            {"preview_received", result.preview_received},
            {"preview_skipped", result.preview_skipped},
            {"worst_latency_ms", result.worst_latency_ms},
            {"min_fps_required", options.min_fps},
            {"passed", passed},
        };
        report = json.dump(2) + "\n";
    } else {
        report =
            fmt::format("Frame pipeline, {}x{} BGR8 ({:.1f} MB per frame), {} build, synthetic source\n"
                        "  produced      {} frames in {:.2f} s = {:.1f} fps ({:.2f} GB/s copied){}\n"
                        "  recorder      received {}, dropped {} (queue), worst delay {:.2f} ms\n"
                        "  preview       received {}, skipped {} (latest-only: skipping is its job)\n"
                        "  pool misses   {}\n"
                        "  result        {}\n",
                        options.width, options.height,
                        static_cast<double>(frame_buffer_bytes(PixelFormat::Bgr8, options.width, options.height)) / 1e6,
                        build.build_type, result.published, result.seconds, result.fps, result.gigabytes_per_second,
                        options.fps > 0.0 ? fmt::format(", paced at {:.0f} fps", options.fps) : std::string(),
                        result.recorder_received, result.recorder_dropped, result.worst_latency_ms,
                        result.preview_received, result.preview_skipped, result.pool_misses,
                        options.min_fps > 0.0 ? fmt::format("{} (required: at least {:.0f} fps and no dropped frame)",
                                                            passed ? "PASS" : "FAIL", options.min_fps)
                                              : std::string(no_loss ? "no frame lost" : "frames were lost"));
    }
    if (!write(stdout, report)) {
        return kExitFailed;
    }
    return passed ? kExitOk : kExitFailed;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        write(stderr, std::string("Internal error: ") + error.what() + "\n");
    } catch (...) {
        write(stderr, "Internal error of unknown kind.\n");
    }
    return kExitInternalError;
}
