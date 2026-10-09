// cloudscope-camtool: the camera subsystem from the command line (P019-P032).
//
//   cloudscope-camtool list                                  cameras of the configured drivers
//   cloudscope-camtool caps <id>                             modes and controls, with current values
//   cloudscope-camtool measure <id> [--seconds 3]            actual frame rate of every mode (table for the docs)
//   cloudscope-camtool stream <id> [--mode WxH@FPS/FMT] [--seconds 10]
//                                                            acquisition thread + decode + statistics, live summary
//   cloudscope-camtool set <id> name=value|auto ...          set controls and show what the camera really did
//   cloudscope-camtool exposure-test <id> [--mode ...] [--values 1,2,4,8,16,32,64]
//                                                            brightness against manual exposure (ms)
//   cloudscope-camtool decode-bench [--repeat 20]            decode time per format and size (simulated frames)
//   cloudscope-camtool ae-test <id> [--mode ...] [--seconds 20]
//                                                            the sky auto-exposure against the camera's own
//   cloudscope-camtool dark <id> [--mode ...] [--frames 20] --out FILE.tiff
//                                                            dark master (cover the lens first)
//   cloudscope-camtool flat <id> [--mode ...] [--frames 20] --out FILE.tiff [--dark FILE.tiff]
//                                                            flat master and gain map (even light, e.g. the sky)
//   cloudscope-camtool soak <id> [--mode ...] --minutes 60 [--report FILE]
//                                                            long capture: fps, lost frames, latency, memory
//   common options: --config FILE (may be repeated), --timeout MS (default 2000)
//
// Exit codes: 0 success, 1 the command failed or a check was not met, 2 wrong usage, 3 internal error.

#include <cloudscope/app/devices.hpp>
#include <cloudscope/calibration/intrinsics.hpp>
#include <cloudscope/capture/acquisition.hpp>
#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/decode.hpp>
#include <cloudscope/capture/exposure.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/capture/sequencer.hpp>
#include <cloudscope/capture/solar.hpp>
#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/capture/video_files.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/registry.hpp>
#include <cloudscope/session/catalogue.hpp>
#include <cloudscope/session/session.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#endif

namespace {

using namespace cloudscope;
using namespace std::chrono_literals;
using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using SteadyClock = std::chrono::steady_clock;

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInternalError = 3;

void print(const std::string& text)
{
    static_cast<void>(std::fputs(text.c_str(), stdout));
    static_cast<void>(std::fflush(stdout));
}

void print_error(const std::string& text)
{
    static_cast<void>(std::fputs((text + "\n").c_str(), stderr));
}

// An integer from text; `fallback` when the text is not one (unlike std::atoi, which hides that).
int parse_int(std::string_view text, int fallback)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() ? value : fallback;
}

std::string utf8(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

// Resident memory of this process in MiB (working set on Windows, VmRSS on Linux); 0 if unknown.
double resident_mib()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            return std::stod(line.substr(6)) / 1024.0;
        }
    }
    return 0.0;
#endif
}

std::string mode_text(const CameraMode& mode)
{
    return fmt::format("{}x{}@{:g}/{}", mode.width, mode.height, mode.fps, to_string(mode.format));
}

// "1920x1080@30/MJPEG"; the rate and the format may be left out ("1920x1080").
std::optional<CameraMode> parse_mode(const std::string& text)
{
    CameraMode mode;
    const std::size_t x = text.find('x');
    if (x == std::string::npos) {
        return std::nullopt;
    }
    try {
        mode.width = std::stoi(text.substr(0, x));
        std::size_t end = text.find_first_of("@/", x);
        mode.height = std::stoi(text.substr(x + 1, end == std::string::npos ? std::string::npos : end - x - 1));
        if (end != std::string::npos && text[end] == '@') {
            const std::size_t slash = text.find('/', end);
            mode.fps =
                std::stod(text.substr(end + 1, slash == std::string::npos ? std::string::npos : slash - end - 1));
            end = slash;
        }
        if (end != std::string::npos && text[end] == '/') {
            std::string format = text.substr(end + 1);
            std::ranges::transform(format, format.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            constexpr std::array<PixelFormat, 6> kFormats = {PixelFormat::Gray8, PixelFormat::Gray16,
                                                             PixelFormat::Bgr8,  PixelFormat::Rgb8,
                                                             PixelFormat::Yuyv,  PixelFormat::Mjpeg};
            bool known = false;
            for (const PixelFormat candidate : kFormats) {
                if (format == to_string(candidate)) {
                    mode.format = candidate;
                    known = true;
                }
            }
            if (!known) {
                return std::nullopt;
            }
        } else {
            mode.format = PixelFormat::Mjpeg;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return mode;
}

// The listed mode matching a request: exact, or the same size and format with the highest rate when the rate
// was left out, or the first mode of that size when the format was left out too.
std::optional<CameraMode> match_mode(const CameraCapabilities& capabilities, const std::optional<CameraMode>& wanted)
{
    if (!wanted) {
        return capabilities.modes.empty() ? std::nullopt : std::optional(capabilities.modes.front());
    }
    std::optional<CameraMode> best;
    for (const CameraMode& mode : capabilities.modes) {
        if (mode.width != wanted->width || mode.height != wanted->height) {
            continue;
        }
        if (wanted->fps > 0.0 && std::abs(mode.fps - wanted->fps) > 0.01) {
            continue;
        }
        if (mode.format != wanted->format) {
            continue;
        }
        if (!best || mode.fps > best->fps) {
            best = mode;
        }
    }
    return best;
}

struct Context {
    SystemClock clock;
    hal::DeviceRegistry registry;
    std::chrono::milliseconds timeout{2000};
};

Expected<void> set_up(Context& context, const std::vector<std::filesystem::path>& config_files)
{
    const auto loaded = load_app_config(standard_paths(), config_files);
    if (!loaded) {
        return fail(loaded.error());
    }
    return add_configured_drivers(context.registry, loaded->effective, context.clock);
}

Expected<std::shared_ptr<hal::ICamera>> open_camera(Context& context, const std::string& id)
{
    auto camera = context.registry.create<hal::ICamera>(id);
    if (!camera) {
        return camera;
    }
    if (auto opened = (*camera)->open(); !opened) {
        return fail(opened.error());
    }
    return camera;
}

std::string control_table(const hal::ICamera& camera, const CameraCapabilities& capabilities)
{
    std::string out =
        "| Control | Range | Step | Default | Unit | Auto | Calibrated | Now |\n|---|---|---|---|---|---|---|---|\n";
    for (const ControlInfo& info : capabilities.controls) {
        const auto now = camera.control(info.control);
        const std::string current = now ? fmt::format("{:g}{}", now->value, now->automatic ? " (auto)" : "")
                                        : std::string(to_string(now.error().code));
        out +=
            fmt::format("| {} | {:g} .. {:g} | {:g} | {:g} | {} | {} | {} | {} |\n", to_string(info.control),
                        info.minimum, info.maximum, info.step, info.default_value, info.unit.empty() ? "-" : info.unit,
                        info.supports_auto ? "yes" : "no", info.calibrated ? "yes" : "no", current);
    }
    return out;
}

int command_list(Context& context)
{
    const std::vector<hal::DeviceInfo> cameras = context.registry.enumerate(hal::DeviceKind::Camera);
    if (cameras.empty()) {
        print("Cameras: none\n");
        return kExitOk;
    }
    print("Cameras:\n");
    for (const hal::DeviceInfo& camera : cameras) {
        print(fmt::format("  {:<22} {}{}\n", camera.id, camera.name, camera.simulated ? "  [simulated]" : ""));
    }
    return kExitOk;
}

int command_caps(Context& context, const std::string& id)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    const auto current = (*camera)->mode();
    if (!capabilities || !current) {
        print_error((capabilities ? current.error() : capabilities.error()).to_string());
        return kExitFailed;
    }
    print(fmt::format("Camera {} ({}){}\n", id, (*camera)->info().name,
                      (*camera)->info().simulated ? " [simulated]" : ""));
    print(fmt::format("Current mode: {}\n\nModes ({}):\n", mode_text(*current), capabilities->modes.size()));
    for (const CameraMode& mode : capabilities->modes) {
        print("  " + mode_text(mode) + "\n");
    }
    print("\nControls:\n" + control_table(**camera, *capabilities));
    return kExitOk;
}

struct Measurement {
    CameraMode mode;
    std::uint64_t frames = 0;
    std::uint64_t lost = 0;
    std::uint64_t timeouts = 0;
    double seconds = 0.0;
    double fps = 0.0;
    std::size_t bytes = 0;  // of the last frame
    std::optional<Error> error = std::nullopt;
};

Measurement measure_mode(hal::ICamera& camera, const CameraMode& mode, double seconds,
                         std::chrono::milliseconds timeout)
{
    Measurement result{.mode = mode};
    if (auto set = camera.set_mode(mode); !set) {
        result.error = set.error();
        return result;
    }
    if (auto started = camera.start(); !started) {
        result.error = started.error();
        return result;
    }
    Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
    std::optional<std::uint64_t> expected;
    SteadyClock::time_point first;
    SteadyClock::time_point last;
    const SteadyClock::time_point deadline = SteadyClock::now() + std::chrono::duration_cast<SteadyClock::duration>(
                                                                      std::chrono::duration<double>(seconds + 1.0));
    while (SteadyClock::now() < deadline) {
        const auto read = camera.read_frame(frame, timeout);
        if (!read) {
            if (read.error().code == ErrorCode::Timeout) {
                ++result.timeouts;
                continue;
            }
            result.error = read.error();
            break;
        }
        const SteadyClock::time_point now = SteadyClock::now();
        if (result.frames == 0) {
            first = now;  // the first frame starts the clock: start-up latency is not frame rate
        } else if (expected && frame.info().sequence > *expected) {
            result.lost += frame.info().sequence - *expected;
        }
        expected = frame.info().sequence + 1;
        last = now;
        ++result.frames;
        result.bytes = frame.data().size();
        if (std::chrono::duration<double>(now - first).count() >= seconds) {
            break;
        }
    }
    camera.stop();
    if (result.frames > 1) {
        result.seconds = std::chrono::duration<double>(last - first).count();
        result.fps = result.seconds > 0.0 ? static_cast<double>(result.frames - 1) / result.seconds : 0.0;
    }
    return result;
}

int command_measure(Context& context, const std::string& id, double seconds)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    print(fmt::format("Measured modes of {} ({}), {:g} s each:\n\n", id, (*camera)->info().name, seconds));
    print("| Mode | Nominal fps | Measured fps | Frames | Lost | Timeouts | Bytes per frame | Note "
          "|\n|---|---|---|---|---|---|---|---|\n");
    int failures = 0;
    for (const CameraMode& mode : capabilities->modes) {
        const Measurement m = measure_mode(**camera, mode, seconds, context.timeout);
        std::string note;
        if (m.error.has_value()) {
            note = m.error.value().to_string();
            ++failures;
        } else if (m.fps < 0.8 * mode.fps) {
            note = "below nominal";
        }
        print(fmt::format("| {} | {:g} | {:.1f} | {} | {} | {} | {} | {} |\n", mode_text(mode), mode.fps, m.fps,
                          m.frames, m.lost, m.timeouts, m.bytes, note));
    }
    return failures == 0 ? kExitOk : kExitFailed;
}

Expected<std::pair<CameraControl, ControlSetting>> parse_assignment(const CameraCapabilities& capabilities,
                                                                    const std::string& text)
{
    const std::size_t equals = text.find('=');
    if (equals == std::string::npos) {
        return fail(ErrorCode::InvalidArgument, fmt::format("'{}' is not name=value", text));
    }
    const std::string name = text.substr(0, equals);
    const std::string value = text.substr(equals + 1);
    for (const ControlInfo& info : capabilities.controls) {
        if (name == to_string(info.control)) {
            ControlSetting setting;
            if (value == "auto") {
                setting = {.value = info.default_value, .automatic = true};
            } else {
                try {
                    setting = {.value = std::stod(value), .automatic = false};
                } catch (const std::exception&) {
                    return fail(ErrorCode::InvalidArgument, fmt::format("'{}' is not a number", value));
                }
            }
            return std::pair{info.control, setting};
        }
    }
    return fail(ErrorCode::Unsupported, fmt::format("the camera has no control '{}'", name));
}

int command_set(Context& context, const std::string& id, const std::vector<std::string>& assignments)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    int failures = 0;
    for (const std::string& assignment : assignments) {
        const auto parsed = parse_assignment(*capabilities, assignment);
        if (!parsed) {
            print_error(parsed.error().to_string());
            ++failures;
            continue;
        }
        const auto state = (*camera)->set_control(parsed->first, parsed->second);
        if (!state) {
            print_error(state.error().to_string());
            ++failures;
            continue;
        }
        print(fmt::format("{}: requested {:g}{}, effective {:g}{}, {}\n", to_string(parsed->first),
                          state->requested.value, state->requested.automatic ? " (auto)" : "", state->effective.value,
                          state->effective.automatic ? " (auto)" : "",
                          state->applied ? "applied" : "NOT applied as asked"));
    }
    print("\n" + control_table(**camera, *capabilities));
    return failures == 0 ? kExitOk : kExitFailed;
}

// Mean luma of `count` decoded frames after discarding `skip` (the camera needs a few frames to apply a setting).
Expected<double> mean_luma(hal::ICamera& camera, Frame& frame, int skip, int count, std::chrono::milliseconds timeout)
{
    double sum = 0.0;
    int taken = 0;
    for (int i = 0; i < skip + count + 10 && taken < count; ++i) {
        const auto read = camera.read_frame(frame, timeout);
        if (!read) {
            if (read.error().code == ErrorCode::Timeout) {
                continue;
            }
            return fail(read.error());
        }
        if (i < skip) {
            continue;
        }
        const auto gray = decode_gray8(frame);
        if (!gray) {
            return fail(gray.error());
        }
        sum += compute_statistics(*gray).mean;
        ++taken;
    }
    if (taken == 0) {
        return fail(ErrorCode::Timeout, "no frames arrived");
    }
    return sum / static_cast<double>(taken);
}

int command_exposure_test(Context& context, const std::string& id, const std::optional<CameraMode>& wanted,
                          const std::vector<double>& values)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    const auto exposure = std::ranges::find(capabilities->controls, CameraControl::Exposure, &ControlInfo::control);
    if (exposure == capabilities->controls.end()) {
        print_error("the camera has no exposure control");
        return kExitFailed;
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        print_error("no listed mode matches the request");
        return kExitUsage;
    }
    if (auto set = (*camera)->set_mode(*mode); !set) {
        print_error(set.error().to_string());
        return kExitFailed;
    }
    const auto original = (*camera)->control(CameraControl::Exposure);
    const auto gain = (*camera)->control(CameraControl::Gain);
    if (gain && gain->automatic) {
        (void)(*camera)->set_control(CameraControl::Gain, {.value = gain->value, .automatic = false});
    }
    if (auto started = (*camera)->start(); !started) {
        print_error(started.error().to_string());
        return kExitFailed;
    }
    Frame frame(frame_buffer_bytes(mode->format, mode->width, mode->height));
    print(fmt::format("Exposure response of {} in {} (exposure range {:g} .. {:g} {}):\n\n", id, mode_text(*mode),
                      exposure->minimum, exposure->maximum, exposure->unit));
    print("| Requested (ms) | Effective (ms) | Applied | Mean luma |\n|---|---|---|---|\n");
    std::vector<double> lumas;
    int failures = 0;
    for (const double value : values) {
        const auto state = (*camera)->set_control(CameraControl::Exposure, {.value = value, .automatic = false});
        if (!state) {
            print_error(state.error().to_string());
            ++failures;
            continue;
        }
        const auto luma = mean_luma(**camera, frame, 6, 3, context.timeout);
        if (!luma) {
            print_error(luma.error().to_string());
            ++failures;
            break;
        }
        lumas.push_back(*luma);
        print(fmt::format("| {:g} | {:g} | {} | {:.1f} |\n", value, state->effective.value,
                          state->applied ? "yes" : "no", *luma));
    }
    (*camera)->stop();
    if (original) {
        (void)(*camera)->set_control(CameraControl::Exposure, *original);  // leave the camera as it was found
    }
    if (gain && gain->automatic) {
        (void)(*camera)->set_control(CameraControl::Gain, *gain);
    }
    bool rises = lumas.size() >= 2;
    for (std::size_t i = 1; i < lumas.size(); ++i) {
        rises = rises && lumas[i] >= lumas[i - 1] - 1.0;  // allow noise, demand no fall
    }
    const bool spans = lumas.size() >= 2 && lumas.back() - lumas.front() >= 20.0;
    print(
        fmt::format("\nBrightness {} with exposure and changes by {:.1f} levels over the sweep: exposure control {}.\n",
                    rises ? "rises" : "does not rise", lumas.empty() ? 0.0 : lumas.back() - lumas.front(),
                    rises && spans ? "WORKS" : "is NOT effective"));
    return failures == 0 && rises && spans ? kExitOk : kExitFailed;
}

struct StreamSummary {
    AcquisitionStats stats;
    std::uint64_t decoded = 0;
    double decode_ms_mean = 0.0;
    FrameStatistics last;
    std::size_t last_bytes = 0;
};

// Runs the acquisition thread for `seconds`, decoding the latest frame and printing a line every `report_every`.
Expected<StreamSummary> run_stream(Context& context, hal::ICamera& camera, const CameraMode& mode, double seconds,
                                   std::chrono::seconds report_every,
                                   const std::function<void(const std::string&)>& report)
{
    if (auto set = camera.set_mode(mode); !set) {
        return fail(set.error());
    }
    auto hub = std::make_shared<FrameHub>();
    auto viewer = hub->subscribe("viewer", Delivery::Latest);
    const std::shared_ptr<hal::ICamera> shared(&camera, [](hal::ICamera*) {});  // not owned here
    Acquisition acquisition(shared, hub, context.clock, {.pool_frames = 6, .read_timeout = context.timeout});
    if (auto started = acquisition.start(); !started) {
        return fail(started.error());
    }
    StreamSummary summary;
    double decode_sum_ms = 0.0;
    const SteadyClock::time_point start = SteadyClock::now();
    SteadyClock::time_point next_report = start + report_every;
    const SteadyClock::time_point end =
        start + std::chrono::duration_cast<SteadyClock::duration>(std::chrono::duration<double>(seconds));
    while (SteadyClock::now() < end && acquisition.is_running()) {
        const FramePtr frame = viewer->wait(200ms);
        if (frame) {
            const SteadyClock::time_point before = SteadyClock::now();
            const auto image = decode_bgr8(*frame);
            if (image) {
                decode_sum_ms += std::chrono::duration<double, std::milli>(SteadyClock::now() - before).count();
                ++summary.decoded;
                summary.last = compute_statistics(*image);
                summary.last_bytes = frame->data().size();
            }
        }
        if (SteadyClock::now() >= next_report) {
            next_report += report_every;
            const AcquisitionStats s = acquisition.stats();
            report(fmt::format(
                "{:>6.0f} s  {:6.1f} fps (recent {:5.1f})  frames {:>7}  lost {:>4}  timeouts {:>3}  misses {:>3}  "
                "latency {:5.1f}/{:5.1f} ms  decode {:5.1f} ms  luma {:5.1f}  clipped {:5.2f}%  rss {:6.1f} MiB\n",
                std::chrono::duration<double>(SteadyClock::now() - start).count(), s.fps, s.recent_fps, s.frames,
                s.lost, s.timeouts, s.pool_misses, static_cast<double>(s.latency_mean.count()) / 1000.0,
                static_cast<double>(s.latency_max.count()) / 1000.0,
                summary.decoded > 0 ? decode_sum_ms / static_cast<double>(summary.decoded) : 0.0, summary.last.mean,
                summary.last.clipped_fraction * 100.0, resident_mib()));
        }
    }
    acquisition.stop();
    summary.stats = acquisition.stats();
    summary.decode_ms_mean = summary.decoded > 0 ? decode_sum_ms / static_cast<double>(summary.decoded) : 0.0;
    return summary;
}

int command_stream(Context& context, const std::string& id, const std::optional<CameraMode>& wanted, double seconds)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        print_error("no listed mode matches the request");
        return kExitUsage;
    }
    print(fmt::format("Streaming {} in {} for {:g} s\n", id, mode_text(*mode), seconds));
    const auto summary = run_stream(context, **camera, *mode, seconds, 1s, print);
    if (!summary) {
        print_error(summary.error().to_string());
        return kExitFailed;
    }
    const AcquisitionStats& s = summary->stats;
    print(fmt::format(
        "\nResult: {} frames in {:.1f} s = {:.2f} fps; lost {}, timeouts {}, pool misses {}, errors {}; latency "
        "mean {:.1f} ms, max {:.1f} ms; decode {:.2f} ms per frame; last frame {} bytes, luma {:.1f}, clipped "
        "{:.2f}%, noise {:.1f}, sun {}\n",
        s.frames, static_cast<double>(s.elapsed.count()) / 1000.0, s.fps, s.lost, s.timeouts, s.pool_misses, s.errors,
        static_cast<double>(s.latency_mean.count()) / 1000.0, static_cast<double>(s.latency_max.count()) / 1000.0,
        summary->decode_ms_mean, summary->last_bytes, summary->last.mean, summary->last.clipped_fraction * 100.0,
        summary->last.noise_sigma,
        summary->last.sun.found ? fmt::format("at ({:.0f}, {:.0f}) r={:.0f} px", summary->last.sun.x,
                                              summary->last.sun.y, summary->last.sun.radius_px)
                                : std::string("not found")));
    if (s.last_error) {
        print_error(s.last_error->to_string());
    }
    return s.errors == 0 ? kExitOk : kExitFailed;
}

int command_soak(Context& context, const std::string& id, const std::optional<CameraMode>& wanted, double minutes,
                 const std::string& report_file)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        print_error("no listed mode matches the request");
        return kExitUsage;
    }
    std::vector<std::string> lines;
    const auto record = [&](const std::string& line) {
        print(line);
        lines.push_back(line);
    };
    const double rss_start = resident_mib();
    print(fmt::format("Soak test of {} ({}) in {} for {:g} min; resident memory at start {:.1f} MiB\n", id,
                      (*camera)->info().name, mode_text(*mode), minutes, rss_start));
    const auto summary = run_stream(context, **camera, *mode, minutes * 60.0, 30s, record);
    if (!summary) {
        print_error(summary.error().to_string());
        return kExitFailed;
    }
    const AcquisitionStats& s = summary->stats;
    const double rss_end = resident_mib();
    const std::string verdict = fmt::format(
        "\nSoak result: {} frames in {:.1f} min = {:.2f} fps (nominal {:g}); lost {} ({:.3f}%), timeouts {}, pool "
        "misses {}, errors {}; latency mean {:.1f} ms, max {:.1f} ms; resident memory {:.1f} -> {:.1f} MiB ({:+.1f}); "
        "decode {:.2f} ms per frame.\n",
        s.frames, static_cast<double>(s.elapsed.count()) / 60000.0, s.fps, mode->fps, s.lost,
        s.frames + s.lost > 0 ? 100.0 * static_cast<double>(s.lost) / static_cast<double>(s.frames + s.lost) : 0.0,
        s.timeouts, s.pool_misses, s.errors, static_cast<double>(s.latency_mean.count()) / 1000.0,
        static_cast<double>(s.latency_max.count()) / 1000.0, rss_start, rss_end, rss_end - rss_start,
        summary->decode_ms_mean);
    print(verdict);
    if (!report_file.empty()) {
        std::ofstream out(report_file, std::ios::binary);
        out << "# Camera soak test\n\n"
            << fmt::format("Camera {} ({}), mode {}, {:g} min requested.\n\n", id, (*camera)->info().name,
                           mode_text(*mode), minutes);
        out << "```\n";
        for (const std::string& line : lines) {
            out << line;
        }
        out << "```\n" << verdict;
        if (s.last_error) {
            out << "\nEnded by error: " << s.last_error->to_string() << "\n";
        }
    }
    return s.errors == 0 ? kExitOk : kExitFailed;
}

int command_decode_bench(Context& context, int repeat)
{
    const auto camera = open_camera(context, "sim:camera:sky");
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    print(fmt::format("Decode time per frame, median of {} runs (simulated frames):\n\n| Mode | Bytes | Decode (ms) | "
                      "MPixel/s |\n|---|---|---|---|\n",
                      repeat));
    for (const CameraMode& mode : capabilities->modes) {
        if (!(*camera)->set_mode(mode)) {
            continue;
        }
        if (auto started = (*camera)->start(); !started) {
            print_error(started.error().to_string());
            return kExitFailed;
        }
        Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
        auto read = (*camera)->read_frame(frame, context.timeout);
        for (int i = 0; i < 20 && !read; ++i) {
            read = (*camera)->read_frame(frame, context.timeout);
        }
        (*camera)->stop();
        if (!read) {
            print_error(read.error().to_string());
            continue;
        }
        std::vector<double> times;
        for (int i = 0; i < repeat; ++i) {
            const SteadyClock::time_point before = SteadyClock::now();
            const auto image = decode_bgr8(frame);
            if (!image) {
                print_error(image.error().to_string());
                break;
            }
            times.push_back(std::chrono::duration<double, std::milli>(SteadyClock::now() - before).count());
        }
        if (times.empty()) {
            continue;
        }
        std::ranges::sort(times);
        const double median = times[times.size() / 2];
        const double mpixels = static_cast<double>(mode.width) * mode.height / 1e6;
        print(fmt::format("| {} | {} | {:.2f} | {:.0f} |\n", mode_text(mode), frame.data().size(), median,
                          median > 0.0 ? mpixels / (median / 1000.0) : 0.0));
    }
    return kExitOk;
}

std::vector<double> parse_values(const std::string& text)
{
    std::vector<double> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!item.empty()) {
            values.push_back(std::stod(item));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return values;
}

// Frames decoded to 8-bit BGR, `count` of them after `skip` discarded.
Expected<std::vector<cv::Mat>> grab_pictures(hal::ICamera& camera, const CameraMode& mode, int skip, int count,
                                             std::chrono::milliseconds timeout)
{
    Frame frame(frame_buffer_bytes(mode.format, mode.width, mode.height));
    std::vector<cv::Mat> pictures;
    for (int i = 0; i < skip + count + 20 && static_cast<int>(pictures.size()) < count; ++i) {
        const auto read = camera.read_frame(frame, timeout);
        if (!read) {
            if (read.error().code == ErrorCode::Timeout) {
                continue;
            }
            return fail(read.error());
        }
        if (i < skip) {
            continue;
        }
        auto image = decode_bgr8(frame);
        if (!image) {
            return fail(image.error());
        }
        pictures.push_back(std::move(*image));
    }
    if (static_cast<int>(pictures.size()) < count) {
        return fail(ErrorCode::Timeout, "the camera delivered too few frames");
    }
    return pictures;
}

// The camera's own automatic exposure against the sky controller on the live scene: clipped pixels outside the
// Sun and the metered percentile, each after settling.
int command_ae_test(Context& context, const std::string& id, const std::optional<CameraMode>& wanted, double seconds)
{
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    const auto exposure = std::ranges::find(capabilities->controls, CameraControl::Exposure, &ControlInfo::control);
    if (exposure == capabilities->controls.end()) {
        print_error("the camera has no exposure control");
        return kExitFailed;
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        print_error("no listed mode matches the request");
        return kExitUsage;
    }
    if (auto set = (*camera)->set_mode(*mode); !set) {
        print_error(set.error().to_string());
        return kExitFailed;
    }
    const auto original = (*camera)->control(CameraControl::Exposure);
    if (auto started = (*camera)->start(); !started) {
        print_error(started.error().to_string());
        return kExitFailed;
    }
    const int settle_frames = std::max(5, static_cast<int>(seconds * mode->fps / 2.0));

    // 1. The camera's own automatic exposure.
    (void)(*camera)->set_control(CameraControl::Exposure, {.value = exposure->default_value, .automatic = true});
    auto own = grab_pictures(**camera, *mode, settle_frames, 3, context.timeout);
    if (!own) {
        print_error(own.error().to_string());
        return kExitFailed;
    }
    const cv::Mat own_luma = luma_of(own->back());
    const FrameStatistics own_stats = compute_statistics(own_luma);
    const double own_clipped = clipped_outside_sun(own_luma, own_stats.sun, 2.5);
    const double own_level = sun_aware_percentile(own_luma, own_stats.sun, 0.99, 2.5);
    const auto own_exposure = (*camera)->control(CameraControl::Exposure);

    // 2. The sky controller, stepping the manual exposure.
    SkyExposureSettings settings;
    settings.min_exposure_ms = exposure->minimum;
    settings.max_exposure_ms = std::min(exposure->maximum, 1000.0 / mode->fps);  // keep the frame rate
    const SkyExposureController controller(settings);
    double current = own_exposure ? own_exposure->value : exposure->default_value;
    (void)(*camera)->set_control(CameraControl::Exposure, {.value = current, .automatic = false});
    double sky_clipped = 0.0;
    double sky_level = 0.0;
    const SteadyClock::time_point end =
        SteadyClock::now() + std::chrono::duration_cast<SteadyClock::duration>(std::chrono::duration<double>(seconds));
    print(fmt::format("Sky auto-exposure in {} (limits {:.3g} .. {:.3g} ms):\n", mode_text(*mode),
                      settings.min_exposure_ms, settings.max_exposure_ms));
    while (SteadyClock::now() < end) {
        auto pictures = grab_pictures(**camera, *mode, 4, 1, context.timeout);
        if (!pictures) {
            print_error(pictures.error().to_string());
            return kExitFailed;
        }
        const cv::Mat luma = luma_of(pictures->back());
        const FrameStatistics stats = compute_statistics(luma);
        sky_clipped = clipped_outside_sun(luma, stats.sun, 2.5);
        sky_level = sun_aware_percentile(luma, stats.sun, 0.99, 2.5);
        const ExposureDecision decision = controller.update(pictures->back(), current, 0.0);
        print(fmt::format("  exposure {:8.3f} ms  p99 {:5.1f}  clipped {:6.3f}%  {}\n", current, sky_level,
                          sky_clipped * 100.0, decision.reason));
        if (!decision.changed) {
            break;
        }
        const auto state =
            (*camera)->set_control(CameraControl::Exposure, {.value = decision.exposure_ms, .automatic = false});
        if (!state) {
            print_error(state.error().to_string());
            return kExitFailed;
        }
        current = state->effective.value;
    }
    (*camera)->stop();
    if (original) {
        (void)(*camera)->set_control(CameraControl::Exposure, *original);
    }
    print(fmt::format(
        "\n| Method | Exposure (ms) | 99th percentile (outside Sun) | Clipped outside Sun |\n|---|---|---|---|\n"
        "| camera automatic | {:.3g} | {:.0f} | {:.3f}% |\n| sky controller | {:.3g} | {:.0f} | {:.3f}% |\n",
        own_exposure ? own_exposure->value : 0.0, own_level, own_clipped * 100.0, current, sky_level,
        sky_clipped * 100.0));
    return sky_clipped <= own_clipped ? kExitOk : kExitFailed;
}

int command_master(Context& context, const std::string& id, const std::optional<CameraMode>& wanted, int frames,
                   const std::string& out_file, const std::string& dark_file, bool flat)
{
    if (out_file.empty()) {
        print_error("--out FILE.tiff is required");
        return kExitUsage;
    }
    const auto camera = open_camera(context, id);
    if (!camera) {
        print_error(camera.error().to_string());
        return kExitFailed;
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        print_error(capabilities.error().to_string());
        return kExitFailed;
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        print_error("no listed mode matches the request");
        return kExitUsage;
    }
    if (auto set = (*camera)->set_mode(*mode); !set) {
        print_error(set.error().to_string());
        return kExitFailed;
    }
    if (auto started = (*camera)->start(); !started) {
        print_error(started.error().to_string());
        return kExitFailed;
    }
    const auto pictures = grab_pictures(**camera, *mode, 5, frames, context.timeout);
    (*camera)->stop();
    if (!pictures) {
        print_error(pictures.error().to_string());
        return kExitFailed;
    }
    const auto master = average_frames(*pictures);
    if (!master) {
        print_error(master.error().to_string());
        return kExitFailed;
    }
    if (auto saved = save_master(*master, out_file); !saved) {
        print_error(saved.error().to_string());
        return kExitFailed;
    }
    const cv::Scalar level = cv::mean(master->mean);
    const cv::Scalar noise = cv::mean(master->stddev);
    print(fmt::format("{} master of {} frames in {} -> {}: mean level {:.1f}, per-pixel noise {:.2f}\n",
                      flat ? "Flat" : "Dark", master->frames, mode_text(*mode), out_file,
                      (level[0] + level[1] + level[2]) / 3.0, (noise[0] + noise[1] + noise[2]) / 3.0));
    if (flat) {
        std::optional<MasterFrame> dark;
        if (!dark_file.empty()) {
            auto loaded = load_master(dark_file);
            if (!loaded) {
                print_error(loaded.error().to_string());
                return kExitFailed;
            }
            dark = std::move(*loaded);
        }
        const auto gain = gain_map(*master, dark ? &*dark : nullptr);
        if (!gain) {
            print_error(gain.error().to_string());
            return kExitFailed;
        }
        const auto model = fit_vignetting(master->mean);
        const auto corrected = apply_calibration(pictures->back(), dark ? dark->mean : cv::Mat(), *gain);
        if (corrected) {
            print(fmt::format(
                "Uniformity spread of a flat frame: {:.3f} before, {:.3f} after the gain map; vignetting fit "
                "g(r) = 1 {:+.3f} r^2 {:+.3f} r^4 (rms residual {:.3f})\n",
                uniformity_spread(pictures->back()), uniformity_spread(*corrected), model ? model->a : 0.0,
                model ? model->b : 0.0, model ? model->rms_residual : 0.0));
        }
        cv::Mat gain16;
        gain->convertTo(gain16, CV_MAKETYPE(CV_16U, gain->channels()), 65535.0 / 5.0);  // gain 0..5 -> 16 bit
        const std::string gain_file = out_file + ".gain.tiff";
        if (auto written = write_image(gain_file, gain16); !written) {
            print_error(written.error().to_string());
            return kExitFailed;
        }
        print("Gain map (16-bit, value / 65535 * 5) -> " + gain_file + "\n");
    }
    return kExitOk;
}

struct SequenceOptions {
    CaptureKind kind = CaptureKind::Single;
    std::uint32_t count = 1;
    std::chrono::milliseconds interval{0};
    std::string format = "png";
    std::string night_format;
    std::optional<double> night_exposure_ms;
    double night_below_deg = -6.0;
    std::vector<double> stops = {-2.0, 0.0, 2.0};
    std::string start;
    std::string end;
    std::string site;  // "LAT,LON,ALT[,ID]"
    std::string folder;
    std::string filename_template = "{site}_{utc}_{seq}_{profile}";
    double min_free_mib = 512.0;
    std::string session_root;  // when set, pictures go into a new session under this root and into its catalogue
};

std::optional<SiteInfo> parse_site(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (parts.size() < 3) {
        return std::nullopt;
    }
    try {
        SiteInfo site;
        site.latitude_deg = std::stod(parts[0]);
        site.longitude_deg = std::stod(parts[1]);
        site.altitude_m = std::stod(parts[2]);
        site.id = parts.size() > 3 ? parts[3] : "site";
        return site;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// record (one picture) and sequence (any plan): the camera streams through an acquisition thread and the
// sequencer consumes its hub, as the application will.
// The capture plan the options describe; InvalidArgument with the message to show when they do not make one.
Expected<CapturePlan> plan_from(const SequenceOptions& options)
{
    CapturePlan plan;
    plan.kind = options.kind;
    plan.count = options.count;
    plan.interval = options.interval;
    plan.bracket_stops = options.stops;
    plan.folder = std::filesystem::path(QString::fromStdString(options.folder).toStdU16String());
    plan.filename_template = options.filename_template;
    plan.night_below_sun_elevation_deg = options.night_below_deg;
    plan.min_free_bytes = static_cast<std::uintmax_t>(std::max(options.min_free_mib, 0.0) * 1024.0 * 1024.0);
    const auto day_format = image_format_from_string(options.format);
    if (!day_format) {
        return fail(day_format.error());
    }
    plan.day.format = *day_format;
    plan.night.format = *day_format;
    if (!options.night_format.empty()) {
        const auto night_format = image_format_from_string(options.night_format);
        if (!night_format) {
            return fail(night_format.error());
        }
        plan.night.format = *night_format;
    }
    plan.night.exposure_ms = options.night_exposure_ms;
    if (!options.start.empty()) {
        const auto start = parse_iso8601(options.start);
        if (!start) {
            return fail(ErrorCode::InvalidArgument,
                        "--start must be ISO 8601 with an offset, e.g. 2026-10-10T00:30:00Z");
        }
        plan.start_at = *start;
    }
    if (!options.end.empty()) {
        const auto end = parse_iso8601(options.end);
        if (!end) {
            return fail(ErrorCode::InvalidArgument, "--end must be ISO 8601 with an offset");
        }
        plan.end_at = *end;
    }
    if (auto valid = validate(plan); !valid) {
        return fail(valid.error());
    }
    return plan;
}

// A camera opened and set to the wanted (or current) mode.
struct OpenedCamera {
    std::shared_ptr<hal::ICamera> camera;
    CameraMode mode;
};

Expected<OpenedCamera> open_in_mode(Context& context, const std::string& id, const std::optional<CameraMode>& wanted)
{
    auto camera = open_camera(context, id);
    if (!camera) {
        return fail(camera.error());
    }
    const auto capabilities = (*camera)->capabilities();
    if (!capabilities) {
        return fail(capabilities.error());
    }
    const std::optional<CameraMode> mode = match_mode(*capabilities, wanted);
    if (!mode) {
        return fail(ErrorCode::InvalidArgument, "no listed mode matches the request");
    }
    if (auto set = (*camera)->set_mode(*mode); !set) {
        return fail(set.error());
    }
    return OpenedCamera{.camera = std::move(*camera), .mode = *mode};
}

// A new session under `root` with its catalogue, for `sequence --session`.
struct SessionBundle {
    std::optional<Session> session;
    std::unique_ptr<Catalogue> catalogue;
};

Expected<SessionBundle> open_session_bundle(const std::string& session_root, const std::optional<SiteInfo>& site,
                                            const hal::DeviceInfo& camera, const IClock& clock)
{
    SessionBundle bundle;
    const std::filesystem::path root(QString::fromStdString(session_root).toStdU16String());
    auto created = Session::create(
        root, site.value_or(SiteInfo{.id = "site", .latitude_deg = 0.0, .longitude_deg = 0.0, .altitude_m = 0.0}),
        camera, clock.now_utc());
    if (!created) {
        return fail(created.error());
    }
    bundle.session = std::move(*created);
    auto opened = Catalogue::open(root / "catalogue.sqlite");
    if (!opened) {
        return fail(opened.error());
    }
    bundle.catalogue = std::move(*opened);
    if (auto added = bundle.catalogue->add_session(bundle.session->info()); !added) {
        return fail(added.error());
    }
    print(fmt::format("Session {} in {}\n", bundle.session->info().id, bundle.session->info().folder.string()));
    return bundle;
}

std::string exposure_text(const std::optional<double>& exposure_ms)
{
    return exposure_ms.has_value() ? fmt::format("{:.3g} ms", exposure_ms.value()) : std::string("n/a");
}

void report_picture(const CapturedPicture& picture, double seconds)
{
    const FrameStatistics statistics = picture.record.statistics.value_or(FrameStatistics{});
    print(fmt::format("{:7.1f} s  {}  {:>9} bytes  profile {}  exposure {}  luma {:.1f}  clipped {:.2f}%\n", seconds,
                      picture.file.path.filename().string(), picture.file.bytes, picture.profile,
                      exposure_text(picture.record.exposure_ms), statistics.mean, statistics.clipped_fraction * 100.0));
}

void report_sequence(const SequencerStats& stats, const AcquisitionStats& acquired)
{
    print(fmt::format(
        "\nResult: {} pictures written ({:.1f} MiB), {} failed, {} settling frames skipped, {} profile switch(es), "
        "{:.1f} s; acquisition {} frames, {} lost, {} timeouts{}{}\n",
        stats.written, static_cast<double>(stats.bytes) / (1024.0 * 1024.0), stats.failed, stats.skipped,
        stats.profile_switches, static_cast<double>(stats.elapsed.count()) / 1000.0, acquired.frames, acquired.lost,
        acquired.timeouts, stats.stopped_by_disk_guard ? "; stopped by the disk guard" : "",
        stats.stopped_by_request ? "; stopped on request" : ""));
    if (stats.last_error.has_value()) {
        print("Last error: " + stats.last_error.value().to_string() + "\n");
    }
}

int command_sequence(Context& context, const std::string& id, const std::optional<CameraMode>& wanted,
                     const SequenceOptions& options)
{
    if (options.folder.empty()) {
        print_error("--out FOLDER is required");
        return kExitUsage;
    }
    auto plan = plan_from(options);
    if (!plan) {
        print_error(plan.error().to_string());
        return kExitUsage;
    }
    const std::optional<SiteInfo> site = parse_site(options.site);
    if (!options.site.empty() && !site) {
        print_error("--site must be LAT,LON,ALT[,ID]");
        return kExitUsage;
    }
    const auto opened = open_in_mode(context, id, wanted);
    if (!opened) {
        print_error(opened.error().to_string());
        return opened.error().code == ErrorCode::InvalidArgument ? kExitUsage : kExitFailed;
    }
    const std::shared_ptr<hal::ICamera>& camera = opened->camera;
    const CameraMode mode = opened->mode;
    SessionBundle bundle;
    if (!options.session_root.empty()) {
        auto session = open_session_bundle(options.session_root, site, camera->info(), context.clock);
        if (!session) {
            print_error(session.error().to_string());
            return kExitFailed;
        }
        bundle = std::move(*session);
        if (bundle.session.has_value()) {
            plan->folder = bundle.session->frames_folder();
        }
    }
    auto hub = std::make_shared<FrameHub>();
    Acquisition acquisition(camera, hub, context.clock, {.pool_frames = 6, .read_timeout = context.timeout});
    if (auto started = acquisition.start(); !started) {
        print_error(started.error().to_string());
        return kExitFailed;
    }
    Sequencer sequencer(camera, hub, context.clock);
    sequencer.set_site(site);
    if (bundle.session.has_value()) {
        sequencer.set_session_id(bundle.session->info().id);
    }
    // FR-SEQ-05: after a camera failure, reopen it and restart the acquisition on the same hub.
    sequencer.set_recovery([&]() -> Expected<void> {
        acquisition.stop();
        camera->close();
        if (auto reopened = camera->open(); !reopened) {
            return reopened;
        }
        if (auto set = camera->set_mode(mode); !set) {
            return fail(set.error());
        }
        return acquisition.start();
    });
    plan->frame_timeout = std::max(context.timeout, std::chrono::milliseconds(500));
    if (site.has_value()) {
        const SunPosition sun = sun_position(context.clock.now_utc(), site->latitude_deg, site->longitude_deg);
        print(fmt::format("Site {} ({:.2f}, {:.2f}): Sun elevation {:.1f} deg, azimuth {:.1f} deg, {}; night profile "
                          "below {:.1f} deg\n",
                          site->id, site->latitude_deg, site->longitude_deg, sun.elevation_deg, sun.azimuth_deg,
                          to_string(sky_period(sun.elevation_deg)), plan->night_below_sun_elevation_deg));
    }
    print(fmt::format("{} in {} -> {}\n", to_string(plan->kind), mode_text(mode), options.folder));
    const SteadyClock::time_point started = SteadyClock::now();
    sequencer.set_on_picture([&](const CapturedPicture& picture) {
        if (bundle.session.has_value()) {
            static_cast<void>(bundle.session->record_picture(picture.file));
            if (auto added = bundle.catalogue->add_frame(frame_entry(picture, bundle.session->info().id)); !added) {
                print_error(added.error().to_string());
            }
        }
        report_picture(picture, std::chrono::duration<double>(SteadyClock::now() - started).count());
    });
    const auto stats = sequencer.run(*plan);
    acquisition.stop();
    camera->stop();
    if (bundle.session.has_value()) {
        if (auto closed = bundle.session->close(context.clock.now_utc()); !closed) {
            print_error(closed.error().to_string());
        }
        if (auto updated = bundle.catalogue->add_session(bundle.session->info()); !updated) {
            print_error(updated.error().to_string());
        }
        print(fmt::format("Session closed: {} pictures catalogued in {}\n", bundle.session->info().frames,
                          bundle.catalogue->file().string()));
    }
    if (!stats) {
        print_error(stats.error().to_string());
        return kExitFailed;
    }
    report_sequence(*stats, acquisition.stats());
    return stats->written > 0 && stats->failed == 0 && !stats->stopped_by_disk_guard ? kExitOk : kExitFailed;
}

struct CalibrateOptions {
    BoardSpec board;
    LensModel lens = LensModel::Fisheye;
    int views = 15;
    double seconds = 120.0;
    std::string from_folder;  // pictures on disk instead of the camera
    std::string out_file;
};

// The intrinsic calibration assistant: detects the checkerboard in live frames (or in pictures of a folder),
// keeps the views that cover new parts of the image, fits the model and writes the camera-model file.
// Offers one picture to the assistant and says what happened.
void consider_view(std::optional<CaptureAssistant>& assistant, const cv::Mat& image, const std::string& source,
                   const BoardSpec& board)
{
    const auto detected = detect_checkerboard(image, board);
    if (!detected) {
        print(fmt::format("  {}: no board\n", source));
        return;
    }
    if (!assistant.has_value()) {
        assistant.emplace(detected->image_size, 4);
    }
    std::string reason;
    const bool kept = assistant->accept(*detected, &reason);
    print(fmt::format("  {}: board found, {} ({}); views {} coverage {:.0f}%\n", source, kept ? "kept" : "skipped",
                      reason, assistant->views(), assistant->coverage() * 100.0));
}

Expected<void> collect_views_from_folder(std::optional<CaptureAssistant>& assistant, const std::string& from_folder,
                                         const BoardSpec& board)
{
    const std::filesystem::path folder(QString::fromStdString(from_folder).toStdU16String());
    const auto files = picture_files(folder);
    if (!files) {
        return fail(files.error());
    }
    for (const std::filesystem::path& file : *files) {
        const auto image = read_image(file);
        if (!image) {
            print_error(image.error().to_string());
            continue;
        }
        consider_view(assistant, *image, file.filename().string(), board);
    }
    return {};
}

// Looks at the live picture once a second until enough views are in or the time is up; returns the camera's name.
Expected<std::string> collect_views_from_camera(Context& context, const std::string& id,
                                                const std::optional<CameraMode>& wanted,
                                                const CalibrateOptions& options,
                                                std::optional<CaptureAssistant>& assistant)
{
    const auto opened = open_in_mode(context, id, wanted);
    if (!opened) {
        return fail(opened.error());
    }
    const std::shared_ptr<hal::ICamera>& camera = opened->camera;
    if (auto started = camera->start(); !started) {
        return fail(started.error());
    }
    print(fmt::format(
        "Show the {}x{} board ({} mm squares) to the camera; move it to the edges and tilt it. {} views wanted, "
        "{:.0f} s at most.\n",
        options.board.columns, options.board.rows, options.board.square_mm, options.views, options.seconds));
    Frame frame(frame_buffer_bytes(opened->mode.format, opened->mode.width, opened->mode.height));
    const SteadyClock::time_point deadline =
        SteadyClock::now() + std::chrono::milliseconds(static_cast<long long>(options.seconds * 1000.0));
    SteadyClock::time_point next_look = SteadyClock::now();
    while (SteadyClock::now() < deadline && (!assistant.has_value() || assistant->views() < options.views)) {
        if (auto read = camera->read_frame(frame, context.timeout); !read) {
            if (read.error().code != ErrorCode::Timeout) {
                print_error(read.error().to_string());
                break;
            }
            continue;
        }
        if (SteadyClock::now() < next_look) {
            continue;  // a look every second: the operator needs time to move the board
        }
        next_look = SteadyClock::now() + 1s;
        if (const auto image = decode_gray8(frame)) {
            consider_view(assistant, *image, fmt::format("frame {}", frame.info().sequence), options.board);
        }
    }
    camera->stop();
    return camera->info().name;
}

int command_calibrate(Context& context, const std::string& id, const std::optional<CameraMode>& wanted,
                      const CalibrateOptions& options)
{
    if (options.out_file.empty()) {
        print_error("--out FILE.json is required");
        return kExitUsage;
    }
    std::optional<CaptureAssistant> assistant;
    std::string camera_id = id;
    std::string camera_name;
    if (!options.from_folder.empty()) {
        camera_id = id.empty() ? "folder" : id;
        if (auto collected = collect_views_from_folder(assistant, options.from_folder, options.board); !collected) {
            print_error(collected.error().to_string());
            return kExitFailed;
        }
    } else {
        auto name = collect_views_from_camera(context, id, wanted, options, assistant);
        if (!name) {
            print_error(name.error().to_string());
            return name.error().code == ErrorCode::InvalidArgument ? kExitUsage : kExitFailed;
        }
        camera_name = std::move(*name);
    }

    if (!assistant.has_value() || assistant->views() < 3) {
        print_error("fewer than three usable views: no calibration");
        return kExitFailed;
    }
    const auto fitted = fit_intrinsics(assistant->accepted(), options.board, options.lens);
    if (!fitted) {
        print_error(fitted.error().to_string());
        return kExitFailed;
    }
    CameraModel model = *fitted;
    model.camera_id = camera_id;
    model.camera_name = camera_name;
    model.calibrated = context.clock.now_utc();
    model.calibration_id = "intrinsics-" + format_file_stamp(model.calibrated).substr(0, 15) + "Z";
    const std::filesystem::path out(QString::fromStdString(options.out_file).toStdU16String());
    if (auto saved = save_camera_model(model, out); !saved) {
        print_error(saved.error().to_string());
        return kExitFailed;
    }
    print(fmt::format("\n{} model from {} views: fx {:.2f} fy {:.2f} cx {:.2f} cy {:.2f}, distortion [{}], rms {:.3f} "
                      "px, coverage {:.0f}% -> {}\n",
                      to_string(model.model), model.views, model.fx, model.fy, model.cx, model.cy,
                      fmt::join(model.distortion, ", "), model.rms_px, assistant->coverage() * 100.0,
                      options.out_file));
    return model.rms_px < 1.0 ? kExitOk : kExitFailed;
}

// The command-line options, declared once and read by the command parsers below.
struct Options {
    QCommandLineOption config_option{{QStringLiteral("c"), QStringLiteral("config")},
                                     QStringLiteral("Read FILE on top of the standard configuration."),
                                     QStringLiteral("FILE")};
    QCommandLineOption timeout_option{QStringLiteral("timeout"),
                                      QStringLiteral("Milliseconds to wait for a frame (default 2000)."),
                                      QStringLiteral("MS"), QStringLiteral("2000")};
    QCommandLineOption mode_option{QStringLiteral("mode"),
                                   QStringLiteral("Mode as WxH@FPS/FORMAT, e.g. 1920x1080@30/MJPEG."),
                                   QStringLiteral("MODE")};
    QCommandLineOption seconds_option{QStringLiteral("seconds"),
                                      QStringLiteral("Duration in seconds (measure: per mode; stream: total)."),
                                      QStringLiteral("S")};
    QCommandLineOption minutes_option{QStringLiteral("minutes"),
                                      QStringLiteral("Soak duration in minutes (default 60)."), QStringLiteral("MIN"),
                                      QStringLiteral("60")};
    QCommandLineOption values_option{QStringLiteral("values"),
                                     QStringLiteral("Exposure values in ms, comma separated."), QStringLiteral("LIST"),
                                     QStringLiteral("1,2,4,8,16,32,64,128")};
    QCommandLineOption repeat_option{QStringLiteral("repeat"),
                                     QStringLiteral("Decode-bench repetitions per mode (default 20)."),
                                     QStringLiteral("N"), QStringLiteral("20")};
    QCommandLineOption report_option{QStringLiteral("report"), QStringLiteral("Soak: write a Markdown report to FILE."),
                                     QStringLiteral("FILE")};
    QCommandLineOption frames_option{QStringLiteral("frames"),
                                     QStringLiteral("Frames to average for a master (default 20)."),
                                     QStringLiteral("N"), QStringLiteral("20")};
    QCommandLineOption out_option{QStringLiteral("out"), QStringLiteral("Output file of a master (TIFF)."),
                                  QStringLiteral("FILE")};
    QCommandLineOption dark_option{QStringLiteral("dark"), QStringLiteral("Dark master to subtract (flat)."),
                                   QStringLiteral("FILE")};
    QCommandLineOption kind_option{
        QStringLiteral("kind"),
        QStringLiteral("Sequence: single | burst | interval | bracket | scheduled (default interval)."),
        QStringLiteral("KIND"), QStringLiteral("interval")};
    QCommandLineOption count_option{QStringLiteral("count"),
                                    QStringLiteral("Sequence: pictures to take (0 = until stopped or --end)."),
                                    QStringLiteral("N"), QStringLiteral("10")};
    QCommandLineOption interval_option{QStringLiteral("interval"),
                                       QStringLiteral("Sequence: milliseconds between pictures."), QStringLiteral("MS"),
                                       QStringLiteral("1000")};
    QCommandLineOption format_option{QStringLiteral("format"),
                                     QStringLiteral("Picture format: png | tiff16 | jpeg | fits."),
                                     QStringLiteral("FORMAT"), QStringLiteral("png")};
    QCommandLineOption night_format_option{QStringLiteral("night-format"),
                                           QStringLiteral("Picture format of the night profile (default: same)."),
                                           QStringLiteral("FORMAT")};
    QCommandLineOption night_exposure_option{QStringLiteral("night-exposure"),
                                             QStringLiteral("Fixed exposure in ms for the night profile."),
                                             QStringLiteral("MS")};
    QCommandLineOption night_below_option{
        QStringLiteral("night-below"),
        QStringLiteral("Sun elevation (deg) below which the night profile applies (default -6)."),
        QStringLiteral("DEG"), QStringLiteral("-6")};
    QCommandLineOption stops_option{QStringLiteral("stops"),
                                    QStringLiteral("Bracket stops, comma separated (default -2,0,2)."),
                                    QStringLiteral("LIST"), QStringLiteral("-2,0,2")};
    QCommandLineOption start_option{QStringLiteral("start"),
                                    QStringLiteral("Scheduled: start time, ISO 8601 with offset."),
                                    QStringLiteral("TIME")};
    QCommandLineOption end_option{QStringLiteral("end"), QStringLiteral("Sequence: end time, ISO 8601 with offset."),
                                  QStringLiteral("TIME")};
    QCommandLineOption site_option{QStringLiteral("site"),
                                   QStringLiteral("Site as LAT,LON,ALT[,ID] for the Sun position."),
                                   QStringLiteral("SITE")};
    QCommandLineOption template_option{QStringLiteral("template"),
                                       QStringLiteral("File name template (default {site}_{utc}_{seq}_{profile})."),
                                       QStringLiteral("T"), QStringLiteral("{site}_{utc}_{seq}_{profile}")};
    QCommandLineOption min_free_option{QStringLiteral("min-free"),
                                       QStringLiteral("Disk guard: stop below this many MiB free (default 512)."),
                                       QStringLiteral("MIB"), QStringLiteral("512")};
    QCommandLineOption session_option{
        QStringLiteral("session"), QStringLiteral("Record into a new session under ROOT (folder layout + catalogue)."),
        QStringLiteral("ROOT")};
    QCommandLineOption board_option{QStringLiteral("board"),
                                    QStringLiteral("Calibrate: inner corners as CxR (default 9x6)."),
                                    QStringLiteral("CxR"), QStringLiteral("9x6")};
    QCommandLineOption square_option{QStringLiteral("square"),
                                     QStringLiteral("Calibrate: square side in mm (default 25)."), QStringLiteral("MM"),
                                     QStringLiteral("25")};
    QCommandLineOption lens_option{QStringLiteral("lens"),
                                   QStringLiteral("Calibrate: fisheye | pinhole (default fisheye)."),
                                   QStringLiteral("LENS"), QStringLiteral("fisheye")};
    QCommandLineOption views_option{QStringLiteral("views"),
                                    QStringLiteral("Calibrate: views to collect (default 15)."), QStringLiteral("N"),
                                    QStringLiteral("15")};
    QCommandLineOption from_option{QStringLiteral("from"),
                                   QStringLiteral("Calibrate: use the pictures of FOLDER instead of a camera."),
                                   QStringLiteral("FOLDER")};

    void add_to(QCommandLineParser& parser) const
    {
        parser.addOptions(
            {config_option,       timeout_option,        mode_option,        seconds_option,  minutes_option,
             values_option,       repeat_option,         report_option,      frames_option,   out_option,
             dark_option,         kind_option,           count_option,       interval_option, format_option,
             night_format_option, night_exposure_option, night_below_option, stops_option,    start_option,
             end_option,          site_option,           template_option,    min_free_option, session_option,
             board_option,        square_option,         lens_option,        views_option,    from_option});
    }
};

Expected<SequenceOptions> sequence_options(const QCommandLineParser& parser, const Options& opts, bool record)
{
    SequenceOptions options;
    if (record) {
        options.kind = CaptureKind::Single;
    } else {
        const auto kind = capture_kind_from_string(utf8(parser.value(opts.kind_option)));
        if (!kind) {
            return fail(kind.error());
        }
        options.kind = *kind;
    }
    options.count = static_cast<std::uint32_t>(std::max(parser.value(opts.count_option).toInt(), 0));
    options.interval = std::chrono::milliseconds(std::max(parser.value(opts.interval_option).toInt(), 0));
    options.format = utf8(parser.value(opts.format_option));
    options.night_format = utf8(parser.value(opts.night_format_option));
    if (parser.isSet(opts.night_exposure_option)) {
        options.night_exposure_ms = parser.value(opts.night_exposure_option).toDouble();
    }
    options.night_below_deg = parser.value(opts.night_below_option).toDouble();
    options.stops = parse_values(utf8(parser.value(opts.stops_option)));
    options.start = utf8(parser.value(opts.start_option));
    options.end = utf8(parser.value(opts.end_option));
    options.site = utf8(parser.value(opts.site_option));
    options.folder = utf8(parser.value(opts.out_option));
    options.filename_template = utf8(parser.value(opts.template_option));
    options.min_free_mib = parser.value(opts.min_free_option).toDouble();
    options.session_root = utf8(parser.value(opts.session_option));
    if (options.folder.empty() && options.session_root.empty()) {
        return fail(ErrorCode::InvalidArgument, "--out FOLDER or --session ROOT is required");
    }
    if (options.folder.empty()) {
        options.folder = options.session_root;  // replaced by the session's frames folder
    }
    return options;
}

Expected<CalibrateOptions> calibrate_options(const QCommandLineParser& parser, const Options& opts)
{
    CalibrateOptions options;
    const std::string board = utf8(parser.value(opts.board_option));
    const std::size_t x = board.find('x');
    if (x == std::string::npos) {
        return fail(ErrorCode::InvalidArgument, "--board must look like 9x6");
    }
    options.board.columns = parse_int(std::string_view(board).substr(0, x), 0);
    options.board.rows = parse_int(std::string_view(board).substr(x + 1), 0);
    if (options.board.columns < 3 || options.board.rows < 3) {
        return fail(ErrorCode::InvalidArgument, "--board must name at least 3x3 inner corners");
    }
    options.board.square_mm = parser.value(opts.square_option).toDouble();
    const auto lens = lens_model_from_string(utf8(parser.value(opts.lens_option)));
    if (!lens) {
        return fail(lens.error());
    }
    options.lens = *lens;
    options.views = std::max(parser.value(opts.views_option).toInt(), 3);
    options.seconds = parser.isSet(opts.seconds_option) ? parser.value(opts.seconds_option).toDouble() : 120.0;
    options.from_folder = utf8(parser.value(opts.from_option));
    options.out_file = utf8(parser.value(opts.out_option));
    return options;
}

int run(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CloudScope camera tool"));
    parser.addHelpOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("list | caps | measure | stream | set | exposure-test | decode-bench | soak | "
                       "ae-test | dark | flat | record | sequence | calibrate"));
    parser.addPositionalArgument(QStringLiteral("arguments"), QStringLiteral("camera id and command arguments"),
                                 QStringLiteral("[id] [name=value ...]"));
    const Options opts;
    opts.add_to(parser);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        print_error(utf8(parser.helpText()));
        return kExitUsage;
    }
    Context context;
    context.timeout = std::chrono::milliseconds(std::max(parser.value(opts.timeout_option).toInt(), 1));
    std::vector<std::filesystem::path> config_files;
    for (const QString& file : parser.values(opts.config_option)) {
        config_files.emplace_back(file.toStdU16String());
    }
    if (auto ready = set_up(context, config_files); !ready) {
        print_error(ready.error().to_string());
        return kExitFailed;
    }
    const std::string command = utf8(positional[0]);
    const std::string id = positional.size() > 1 ? utf8(positional[1]) : std::string();
    std::optional<CameraMode> mode;
    if (parser.isSet(opts.mode_option)) {
        mode = parse_mode(utf8(parser.value(opts.mode_option)));
        if (!mode) {
            print_error("--mode must look like 1920x1080@30/MJPEG");
            return kExitUsage;
        }
    }
    const bool needs_id =
        command != "list" && command != "decode-bench" && !(command == "calibrate" && parser.isSet(opts.from_option));
    if (needs_id && id.empty()) {
        print_error("this command needs a camera id (see: list)");
        return kExitUsage;
    }
    if (command == "list") {
        return command_list(context);
    }
    if (command == "caps") {
        return command_caps(context, id);
    }
    if (command == "measure") {
        return command_measure(context, id,
                               parser.isSet(opts.seconds_option) ? parser.value(opts.seconds_option).toDouble() : 3.0);
    }
    if (command == "stream") {
        return command_stream(context, id, mode,
                              parser.isSet(opts.seconds_option) ? parser.value(opts.seconds_option).toDouble() : 10.0);
    }
    if (command == "set") {
        std::vector<std::string> assignments;
        for (qsizetype i = 2; i < positional.size(); ++i) {
            assignments.push_back(utf8(positional[static_cast<int>(i)]));
        }
        return command_set(context, id, assignments);
    }
    if (command == "exposure-test") {
        return command_exposure_test(context, id, mode, parse_values(utf8(parser.value(opts.values_option))));
    }
    if (command == "decode-bench") {
        return command_decode_bench(context, std::max(parser.value(opts.repeat_option).toInt(), 1));
    }
    if (command == "soak") {
        return command_soak(context, id, mode, parser.value(opts.minutes_option).toDouble(),
                            utf8(parser.value(opts.report_option)));
    }
    if (command == "ae-test") {
        return command_ae_test(context, id, mode,
                               parser.isSet(opts.seconds_option) ? parser.value(opts.seconds_option).toDouble() : 20.0);
    }
    if (command == "dark" || command == "flat") {
        return command_master(context, id, mode, std::max(parser.value(opts.frames_option).toInt(), 2),
                              utf8(parser.value(opts.out_option)), utf8(parser.value(opts.dark_option)),
                              command == "flat");
    }
    if (command == "record" || command == "sequence") {
        const auto options = sequence_options(parser, opts, command == "record");
        if (!options) {
            print_error(options.error().to_string());
            return kExitUsage;
        }
        return command_sequence(context, id, mode, *options);
    }
    if (command == "calibrate") {
        const auto options = calibrate_options(parser, opts);
        if (!options) {
            print_error(options.error().to_string());
            return kExitUsage;
        }
        return command_calibrate(context, id, mode, *options);
    }
    print_error("unknown command '" + command + "'");
    return kExitUsage;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);  // no plugin chatter in Debug builds
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("cloudscope-camtool"));
        return run(app);
    } catch (const std::exception& error) {
        print_error(std::string("internal error: ") + error.what());
        return kExitInternalError;
    }
}
