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
#include <cloudscope/capture/acquisition.hpp>
#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/decode.hpp>
#include <cloudscope/capture/exposure.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>
#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/hal/camera.hpp>
#include <cloudscope/hal/registry.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <fmt/format.h>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
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
#else
#include <fstream>
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
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
}

void print_error(const std::string& text)
{
    std::fputs((text + "\n").c_str(), stderr);
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
        if (line.rfind("VmRSS:", 0) == 0) {
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
            mode.fps = std::stod(text.substr(end + 1, slash == std::string::npos ? std::string::npos : slash - end - 1));
            end = slash;
        }
        if (end != std::string::npos && text[end] == '/') {
            std::string format = text.substr(end + 1);
            std::ranges::transform(format, format.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            const PixelFormat formats[] = {PixelFormat::Gray8, PixelFormat::Gray16, PixelFormat::Bgr8,
                                           PixelFormat::Rgb8,  PixelFormat::Yuyv,   PixelFormat::Mjpeg};
            bool known = false;
            for (const PixelFormat candidate : formats) {
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
    std::string out = "| Control | Range | Step | Default | Unit | Auto | Calibrated | Now |\n|---|---|---|---|---|---|---|---|\n";
    for (const ControlInfo& info : capabilities.controls) {
        const auto now = camera.control(info.control);
        const std::string current = now ? fmt::format("{:g}{}", now->value, now->automatic ? " (auto)" : "")
                                        : std::string(to_string(now.error().code));
        out += fmt::format("| {} | {:g} .. {:g} | {:g} | {:g} | {} | {} | {} | {} |\n", to_string(info.control), info.minimum,
                           info.maximum, info.step, info.default_value, info.unit.empty() ? "-" : info.unit,
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
    print(fmt::format("Camera {} ({}){}\n", id, (*camera)->info().name, (*camera)->info().simulated ? " [simulated]" : ""));
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
    std::optional<Error> error;
};

Measurement measure_mode(hal::ICamera& camera, const CameraMode& mode, double seconds, std::chrono::milliseconds timeout)
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
    print("| Mode | Nominal fps | Measured fps | Frames | Lost | Timeouts | Bytes per frame | Note |\n|---|---|---|---|---|---|---|---|\n");
    int failures = 0;
    for (const CameraMode& mode : capabilities->modes) {
        const Measurement m = measure_mode(**camera, mode, seconds, context.timeout);
        const std::string note = m.error ? m.error->to_string() : (m.fps < 0.8 * mode.fps ? "below nominal" : "");
        failures += m.error ? 1 : 0;
        print(fmt::format("| {} | {:g} | {:.1f} | {} | {} | {} | {} | {} |\n", mode_text(mode), mode.fps, m.fps, m.frames,
                          m.lost, m.timeouts, m.bytes, note));
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
        print(fmt::format("{}: requested {:g}{}, effective {:g}{}, {}\n", to_string(parsed->first), state->requested.value,
                          state->requested.automatic ? " (auto)" : "", state->effective.value,
                          state->effective.automatic ? " (auto)" : "", state->applied ? "applied" : "NOT applied as asked"));
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
    return sum / taken;
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
        print(fmt::format("| {:g} | {:g} | {} | {:.1f} |\n", value, state->effective.value, state->applied ? "yes" : "no", *luma));
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
    print(fmt::format("\nBrightness {} with exposure and changes by {:.1f} levels over the sweep: exposure control {}.\n",
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
                                   std::chrono::seconds report_every, const std::function<void(const std::string&)>& report)
{
    if (auto set = camera.set_mode(mode); !set) {
        return fail(set.error());
    }
    auto hub = std::make_shared<FrameHub>();
    auto viewer = hub->subscribe("viewer", Delivery::Latest);
    std::shared_ptr<hal::ICamera> shared(&camera, [](hal::ICamera*) {});  // not owned here
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
            report(fmt::format("{:>6.0f} s  {:6.1f} fps (recent {:5.1f})  frames {:>7}  lost {:>4}  timeouts {:>3}  misses {:>3}  "
                               "latency {:5.1f}/{:5.1f} ms  decode {:5.1f} ms  luma {:5.1f}  clipped {:5.2f}%  rss {:6.1f} MiB\n",
                               std::chrono::duration<double>(SteadyClock::now() - start).count(), s.fps, s.recent_fps, s.frames,
                               s.lost, s.timeouts, s.pool_misses, s.latency_mean.count() / 1000.0, s.latency_max.count() / 1000.0,
                               summary.decoded ? decode_sum_ms / static_cast<double>(summary.decoded) : 0.0, summary.last.mean,
                               summary.last.clipped_fraction * 100.0, resident_mib()));
        }
    }
    acquisition.stop();
    summary.stats = acquisition.stats();
    summary.decode_ms_mean = summary.decoded ? decode_sum_ms / static_cast<double>(summary.decoded) : 0.0;
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
    print(fmt::format("\nResult: {} frames in {:.1f} s = {:.2f} fps; lost {}, timeouts {}, pool misses {}, errors {}; latency "
                      "mean {:.1f} ms, max {:.1f} ms; decode {:.2f} ms per frame; last frame {} bytes, luma {:.1f}, clipped "
                      "{:.2f}%, noise {:.1f}, sun {}\n",
                      s.frames, s.elapsed.count() / 1000.0, s.fps, s.lost, s.timeouts, s.pool_misses, s.errors,
                      s.latency_mean.count() / 1000.0, s.latency_max.count() / 1000.0, summary->decode_ms_mean,
                      summary->last_bytes, summary->last.mean, summary->last.clipped_fraction * 100.0, summary->last.noise_sigma,
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
    const std::string verdict =
        fmt::format("\nSoak result: {} frames in {:.1f} min = {:.2f} fps (nominal {:g}); lost {} ({:.3f}%), timeouts {}, pool "
                    "misses {}, errors {}; latency mean {:.1f} ms, max {:.1f} ms; resident memory {:.1f} -> {:.1f} MiB ({:+.1f}); "
                    "decode {:.2f} ms per frame.\n",
                    s.frames, s.elapsed.count() / 60000.0, s.fps, mode->fps, s.lost,
                    s.frames + s.lost > 0 ? 100.0 * static_cast<double>(s.lost) / static_cast<double>(s.frames + s.lost) : 0.0,
                    s.timeouts, s.pool_misses, s.errors, s.latency_mean.count() / 1000.0, s.latency_max.count() / 1000.0,
                    rss_start, rss_end, rss_end - rss_start, summary->decode_ms_mean);
    print(verdict);
    if (!report_file.empty()) {
        std::ofstream out(report_file, std::ios::binary);
        out << "# Camera soak test\n\n" << fmt::format("Camera {} ({}), mode {}, {:g} min requested.\n\n", id,
                                                          (*camera)->info().name, mode_text(*mode), minutes);
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
    print(fmt::format("Decode time per frame, median of {} runs (simulated frames):\n\n| Mode | Bytes | Decode (ms) | MPixel/s |\n|---|---|---|---|\n",
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
    const SteadyClock::time_point end = SteadyClock::now() + std::chrono::duration_cast<SteadyClock::duration>(
                                                                 std::chrono::duration<double>(seconds));
    print(fmt::format("Sky auto-exposure in {} (limits {:.3g} .. {:.3g} ms):\n", mode_text(*mode), settings.min_exposure_ms,
                      settings.max_exposure_ms));
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
        print(fmt::format("  exposure {:8.3f} ms  p99 {:5.1f}  clipped {:6.3f}%  {}\n", current, sky_level, sky_clipped * 100.0,
                          decision.reason));
        if (!decision.changed) {
            break;
        }
        const auto state = (*camera)->set_control(CameraControl::Exposure, {.value = decision.exposure_ms, .automatic = false});
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
    print(fmt::format("\n| Method | Exposure (ms) | 99th percentile (outside Sun) | Clipped outside Sun |\n|---|---|---|---|\n"
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
    print(fmt::format("{} master of {} frames in {} -> {}: mean level {:.1f}, per-pixel noise {:.2f}\n", flat ? "Flat" : "Dark",
                      master->frames, mode_text(*mode), out_file, (level[0] + level[1] + level[2]) / 3.0,
                      (noise[0] + noise[1] + noise[2]) / 3.0));
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
            print(fmt::format("Uniformity spread of a flat frame: {:.3f} before, {:.3f} after the gain map; vignetting fit "
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

int run(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CloudScope camera tool"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("list | caps | measure | stream | set | exposure-test | decode-bench | soak"));
    parser.addPositionalArgument(QStringLiteral("arguments"), QStringLiteral("camera id and command arguments"),
                                 QStringLiteral("[id] [name=value ...]"));
    const QCommandLineOption config_option({QStringLiteral("c"), QStringLiteral("config")},
                                           QStringLiteral("Read FILE on top of the standard configuration."),
                                           QStringLiteral("FILE"));
    const QCommandLineOption timeout_option(QStringLiteral("timeout"),
                                            QStringLiteral("Milliseconds to wait for a frame (default 2000)."),
                                            QStringLiteral("MS"), QStringLiteral("2000"));
    const QCommandLineOption mode_option(QStringLiteral("mode"),
                                         QStringLiteral("Mode as WxH@FPS/FORMAT, e.g. 1920x1080@30/MJPEG."),
                                         QStringLiteral("MODE"));
    const QCommandLineOption seconds_option(QStringLiteral("seconds"),
                                            QStringLiteral("Duration in seconds (measure: per mode; stream: total)."),
                                            QStringLiteral("S"));
    const QCommandLineOption minutes_option(QStringLiteral("minutes"), QStringLiteral("Soak duration in minutes (default 60)."),
                                            QStringLiteral("MIN"), QStringLiteral("60"));
    const QCommandLineOption values_option(QStringLiteral("values"), QStringLiteral("Exposure values in ms, comma separated."),
                                           QStringLiteral("LIST"), QStringLiteral("1,2,4,8,16,32,64,128"));
    const QCommandLineOption repeat_option(QStringLiteral("repeat"),
                                           QStringLiteral("Decode-bench repetitions per mode (default 20)."),
                                           QStringLiteral("N"), QStringLiteral("20"));
    const QCommandLineOption report_option(QStringLiteral("report"), QStringLiteral("Soak: write a Markdown report to FILE."),
                                           QStringLiteral("FILE"));
    const QCommandLineOption frames_option(QStringLiteral("frames"), QStringLiteral("Frames to average for a master (default 20)."),
                                           QStringLiteral("N"), QStringLiteral("20"));
    const QCommandLineOption out_option(QStringLiteral("out"), QStringLiteral("Output file of a master (TIFF)."),
                                        QStringLiteral("FILE"));
    const QCommandLineOption dark_option(QStringLiteral("dark"), QStringLiteral("Dark master to subtract (flat)."),
                                         QStringLiteral("FILE"));
    parser.addOptions({config_option, timeout_option, mode_option, seconds_option, minutes_option, values_option,
                       repeat_option, report_option, frames_option, out_option, dark_option});
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        print_error(utf8(parser.helpText()));
        return kExitUsage;
    }
    Context context;
    context.timeout = std::chrono::milliseconds(std::max(parser.value(timeout_option).toInt(), 1));
    std::vector<std::filesystem::path> config_files;
    for (const QString& file : parser.values(config_option)) {
        config_files.emplace_back(std::filesystem::path(file.toStdU16String()));
    }
    if (auto ready = set_up(context, config_files); !ready) {
        print_error(ready.error().to_string());
        return kExitFailed;
    }
    const std::string command = utf8(positional[0]);
    const std::string id = positional.size() > 1 ? utf8(positional[1]) : std::string();
    std::optional<CameraMode> mode;
    if (parser.isSet(mode_option)) {
        mode = parse_mode(utf8(parser.value(mode_option)));
        if (!mode) {
            print_error("--mode must look like 1920x1080@30/MJPEG");
            return kExitUsage;
        }
    }
    const bool needs_id = command != "list" && command != "decode-bench";
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
        return command_measure(context, id, parser.isSet(seconds_option) ? parser.value(seconds_option).toDouble() : 3.0);
    }
    if (command == "stream") {
        return command_stream(context, id, mode, parser.isSet(seconds_option) ? parser.value(seconds_option).toDouble() : 10.0);
    }
    if (command == "set") {
        std::vector<std::string> assignments;
        for (qsizetype i = 2; i < positional.size(); ++i) {
            assignments.push_back(utf8(positional[static_cast<int>(i)]));
        }
        return command_set(context, id, assignments);
    }
    if (command == "exposure-test") {
        return command_exposure_test(context, id, mode, parse_values(utf8(parser.value(values_option))));
    }
    if (command == "decode-bench") {
        return command_decode_bench(context, std::max(parser.value(repeat_option).toInt(), 1));
    }
    if (command == "soak") {
        return command_soak(context, id, mode, parser.value(minutes_option).toDouble(), utf8(parser.value(report_option)));
    }
    if (command == "ae-test") {
        return command_ae_test(context, id, mode, parser.isSet(seconds_option) ? parser.value(seconds_option).toDouble() : 20.0);
    }
    if (command == "dark" || command == "flat") {
        return command_master(context, id, mode, std::max(parser.value(frames_option).toInt(), 2), utf8(parser.value(out_option)),
                              utf8(parser.value(dark_option)), command == "flat");
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
