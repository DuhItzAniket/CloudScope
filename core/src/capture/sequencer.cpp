#include "cloudscope/capture/sequencer.hpp"

#include "cloudscope/capture/decode.hpp"
#include "cloudscope/capture/exposure.hpp"
#include "cloudscope/capture/solar.hpp"
#include "cloudscope/capture/statistics.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace cloudscope {

namespace {

constexpr std::chrono::milliseconds kWaitSlice{50};

std::string zero_padded(std::uint32_t value, int width)
{
    return fmt::format("{:0{}}", value, width);
}

}  // namespace

std::string_view to_string(CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Single:
        return "single";
    case CaptureKind::Burst:
        return "burst";
    case CaptureKind::Interval:
        return "interval";
    case CaptureKind::Bracket:
        return "bracket";
    case CaptureKind::Scheduled:
        return "scheduled";
    }
    return "unknown";
}

Expected<CaptureKind> capture_kind_from_string(std::string_view text)
{
    for (const CaptureKind kind :
         {CaptureKind::Single, CaptureKind::Burst, CaptureKind::Interval, CaptureKind::Bracket, CaptureKind::Scheduled}) {
        if (text == to_string(kind)) {
            return kind;
        }
    }
    return fail(ErrorCode::InvalidArgument, fmt::format("unknown capture kind '{}'", text));
}

Expected<std::string> expand_filename(std::string_view pattern, const FilenameFields& fields)
{
    std::string out;
    std::size_t i = 0;
    while (i < pattern.size()) {
        const char c = pattern[i];
        if (c == '}') {
            return fail(ErrorCode::InvalidArgument, fmt::format("unbalanced '}}' in file name template '{}'", pattern));
        }
        if (c != '{') {
            out += c;
            ++i;
            continue;
        }
        const std::size_t close = pattern.find('}', i);
        if (close == std::string_view::npos) {
            return fail(ErrorCode::InvalidArgument, fmt::format("unbalanced '{{' in file name template '{}'", pattern));
        }
        const std::string_view token = pattern.substr(i + 1, close - i - 1);
        if (token == "site") {
            out += fields.site.empty() ? "site" : fields.site;
        } else if (token == "camera") {
            std::string camera = fields.camera.empty() ? "camera" : fields.camera;
            std::ranges::replace(camera, ':', '-');
            out += camera;
        } else if (token == "utc") {
            out += format_file_stamp(fields.utc);
        } else if (token == "date") {
            out += format_file_stamp(fields.utc).substr(0, 8);
        } else if (token == "seq") {
            out += zero_padded(fields.sequence, 6);
        } else if (token == "profile") {
            out += fields.profile.empty() ? "profile" : fields.profile;
        } else if (token == "kind") {
            out += fields.kind.empty() ? "capture" : fields.kind;
        } else {
            return fail(ErrorCode::InvalidArgument, fmt::format("unknown token '{{{}}}' in file name template", token));
        }
        i = close + 1;
    }
    if (out.empty()) {
        return fail(ErrorCode::InvalidArgument, "the file name template is empty");
    }
    return out;
}

Expected<void> validate(const CapturePlan& plan)
{
    if (plan.folder.empty()) {
        return fail(ErrorCode::InvalidArgument, "the plan has no folder");
    }
    switch (plan.kind) {
    case CaptureKind::Single:
        break;
    case CaptureKind::Burst:
        if (plan.count == 0) {
            return fail(ErrorCode::InvalidArgument, "a burst needs a frame count");
        }
        break;
    case CaptureKind::Interval:
        if (plan.count == 0 && !plan.end_at) {
            // allowed: runs until request_stop(); nothing to check
        }
        break;
    case CaptureKind::Bracket:
        if (plan.bracket_stops.empty()) {
            return fail(ErrorCode::InvalidArgument, "a bracket needs at least one stop");
        }
        break;
    case CaptureKind::Scheduled:
        if (!plan.start_at) {
            return fail(ErrorCode::InvalidArgument, "a scheduled run needs a start time");
        }
        if (plan.end_at && *plan.end_at <= *plan.start_at) {
            return fail(ErrorCode::InvalidArgument, "a scheduled run must end after it starts");
        }
        break;
    }
    if (plan.interval < std::chrono::milliseconds::zero()) {
        return fail(ErrorCode::InvalidArgument, "the interval cannot be negative");
    }
    if (plan.settle_frames < 0) {
        return fail(ErrorCode::InvalidArgument, "settle_frames cannot be negative");
    }
    for (const CaptureProfile* profile : {&plan.day, &plan.night}) {
        if (profile->name.empty()) {
            return fail(ErrorCode::InvalidArgument, "a profile needs a name");
        }
        if (profile->jpeg_quality < 1 || profile->jpeg_quality > 100) {
            return fail(ErrorCode::InvalidArgument, fmt::format("profile '{}': JPEG quality must be 1..100", profile->name));
        }
        if (profile->exposure_ms && *profile->exposure_ms <= 0.0) {
            return fail(ErrorCode::InvalidArgument, fmt::format("profile '{}': exposure must be positive", profile->name));
        }
    }
    const auto sample = expand_filename(plan.filename_template, FilenameFields{});
    if (!sample) {
        return fail(sample.error());
    }
    return {};
}

struct Sequencer::Run {
    const CapturePlan& plan;
    std::shared_ptr<FrameSubscription> subscription;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

Sequencer::Sequencer(std::shared_ptr<hal::ICamera> camera, std::shared_ptr<FrameHub> hub, const IClock& clock)
    : camera_(std::move(camera)), hub_(std::move(hub)), clock_(clock)
{
}

SequencerStats Sequencer::stats() const
{
    const std::lock_guard lock(stats_mutex_);
    return stats_;
}

void Sequencer::request_stop()
{
    stop_requested_.store(true);
}

void Sequencer::note(const Error& error)
{
    const std::lock_guard lock(stats_mutex_);
    stats_.last_error = error;
}

const CaptureProfile& Sequencer::profile_for(const CapturePlan& plan, std::optional<double> sun_elevation_deg) const
{
    if (sun_elevation_deg && *sun_elevation_deg < plan.night_below_sun_elevation_deg) {
        return plan.night;
    }
    return plan.day;
}

Expected<void> Sequencer::apply_profile(const CaptureProfile& profile)
{
    using hal::CameraControl;
    using hal::ControlSetting;
    if (profile.exposure_ms) {
        if (auto set = camera_->set_control(CameraControl::Exposure, ControlSetting{.value = *profile.exposure_ms, .automatic = false});
            !set && set.error().code != ErrorCode::Unsupported) {
            return fail(set.error());
        }
    } else if (profile.automatic_exposure) {
        if (auto set = camera_->set_control(CameraControl::Exposure, ControlSetting{.value = 0.0, .automatic = true});
            !set && set.error().code != ErrorCode::Unsupported) {
            return fail(set.error());
        }
    }
    if (profile.gain) {
        if (auto set = camera_->set_control(CameraControl::Gain, ControlSetting{.value = *profile.gain, .automatic = false});
            !set && set.error().code != ErrorCode::Unsupported) {
            return fail(set.error());
        }
    }
    return {};
}

FramePtr Sequencer::next_frame(Run& run)
{
    // Wait in slices so that request_stop() is honoured within kWaitSlice even when no frame comes.
    auto remaining = run.plan.frame_timeout;
    while (true) {
        const auto slice = std::min(remaining, kWaitSlice);
        if (FramePtr frame = run.subscription->wait(slice)) {
            return frame;
        }
        remaining -= slice;
        if (remaining <= std::chrono::milliseconds::zero() || stop_requested_.load()) {
            return nullptr;
        }
    }
}

bool Sequencer::wait_until(UtcTime time)
{
    while (clock_.now_utc() < time) {
        if (stop_requested_.load()) {
            return false;
        }
        std::this_thread::sleep_for(kWaitSlice);
    }
    return true;
}

bool Sequencer::disk_has_room(const CapturePlan& plan)
{
    std::error_code error;
    const std::filesystem::space_info space = std::filesystem::space(plan.folder, error);
    if (error) {
        return true;  // unknown: do not refuse to work because of a failed query
    }
    return space.available >= plan.min_free_bytes;
}

Expected<CapturedPicture> Sequencer::capture_one(Run& run, const CaptureProfile& profile, std::uint32_t sequence,
                                                 std::optional<double> fixed_exposure_ms)
{
    using hal::CameraControl;
    const FramePtr frame = next_frame(run);
    if (frame == nullptr) {
        return fail(ErrorCode::Timeout, "no frame arrived in time");
    }
    const FrameInfo& info = frame->info();

    CaptureRecord record;
    record.info = info;
    if (auto mode = camera_->mode()) {
        record.mode = *mode;
    }
    record.camera_id = camera_->info().id;
    record.camera_name = camera_->info().name;
    if (auto exposure = camera_->control(CameraControl::Exposure)) {
        record.controls["exposure"] = *exposure;
        record.exposure_ms = exposure->value;
    } else if (fixed_exposure_ms) {
        record.exposure_ms = fixed_exposure_ms;
    }
    if (auto gain = camera_->control(CameraControl::Gain)) {
        record.controls["gain"] = *gain;
        record.gain = gain->value;
    }
    record.site = site_;
    record.pointing = pointing_;
    if (site_) {
        const SunPosition sun = sun_position(info.captured.utc, site_->latitude_deg, site_->longitude_deg);
        record.sun = SunInfo{.azimuth_deg = sun.azimuth_deg, .elevation_deg = sun.elevation_deg};
    }
    record.calibration_id = calibration_id_;
    record.session_id = session_id_;

    const auto name = expand_filename(run.plan.filename_template,
                                      FilenameFields{.site = site_ ? site_->id : std::string(),
                                                     .camera = record.camera_id,
                                                     .utc = info.captured.utc,
                                                     .sequence = sequence,
                                                     .profile = profile.name,
                                                     .kind = std::string(to_string(run.plan.kind))});
    if (!name) {
        return fail(name.error());
    }
    const std::filesystem::path file = run.plan.folder / (*name + std::string(extension(profile.format)));

    Expected<WrittenFile> written = fail(ErrorCode::Internal, "not written");
    if (profile.format == ImageFileFormat::Jpeg && profile.keep_native_jpeg && info.format == PixelFormat::Mjpeg) {
        if (auto decoded = decode_gray8(*frame)) {
            record.statistics = compute_statistics(*decoded);
        }
        written = write_jpeg_bytes(frame->data(), file);
    } else {
        auto decoded = decode_bgr8(*frame);
        if (!decoded) {
            return fail(decoded.error());
        }
        record.statistics = compute_statistics(*decoded);
        written = write_picture(*decoded, profile.format, file, record, profile.jpeg_quality);
    }
    if (!written) {
        return fail(written.error());
    }
    if (run.plan.write_sidecar) {
        if (auto sidecar = write_sidecar(file, sidecar_json(record, *written, profile.format)); !sidecar) {
            return fail(sidecar.error());
        }
    }
    return CapturedPicture{.file = *written, .record = record, .profile = profile.name};
}

Expected<SequencerStats> Sequencer::run(const CapturePlan& plan)
{
    if (auto valid = validate(plan); !valid) {
        return fail(valid.error());
    }
    if (running_.exchange(true)) {
        return fail(ErrorCode::Unavailable, "a capture plan is already running");
    }
    stop_requested_.store(false);
    {
        const std::lock_guard lock(stats_mutex_);
        stats_ = {};
    }
    struct Running {
        std::atomic<bool>& flag;
        ~Running() { flag.store(false); }
    } running_guard{running_};

    std::error_code ignored;
    std::filesystem::create_directories(plan.folder, ignored);

    Run run{.plan = plan,
            .subscription = hub_->subscribe("sequencer", plan.kind == CaptureKind::Burst ? Delivery::Queue : Delivery::Latest,
                                            plan.kind == CaptureKind::Burst ? std::max<std::size_t>(plan.count, 4) : 4)};

    const auto finish = [&](bool by_request, bool by_disk) {
        const std::lock_guard lock(stats_mutex_);
        stats_.stopped_by_request = by_request;
        stats_.stopped_by_disk_guard = by_disk;
        stats_.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - run.started);
        return stats_;
    };
    const auto count_written = [&](const CapturedPicture& picture) {
        {
            const std::lock_guard lock(stats_mutex_);
            ++stats_.written;
            stats_.bytes += picture.file.bytes;
        }
        if (on_picture_) {
            on_picture_(picture);
        }
    };
    const auto count_failed = [&](const Error& error) {
        const std::lock_guard lock(stats_mutex_);
        ++stats_.failed;
        stats_.last_error = error;
    };
    // Skips frames taken before an exposure change reached the sensor.
    const auto settle = [&] {
        for (int i = 0; i < plan.settle_frames; ++i) {
            if (next_frame(run) != nullptr) {
                const std::lock_guard lock(stats_mutex_);
                ++stats_.skipped;
            }
        }
    };

    // Picks the profile for now, applies it when it changes.
    const CaptureProfile* active = nullptr;
    const auto choose_profile = [&]() -> const CaptureProfile& {
        std::optional<double> elevation;
        if (site_) {
            elevation = sun_position(clock_.now_utc(), site_->latitude_deg, site_->longitude_deg).elevation_deg;
        }
        const CaptureProfile& wanted = profile_for(plan, elevation);
        if (active == nullptr || active->name != wanted.name) {
            if (auto applied = apply_profile(wanted); !applied) {
                note(applied.error());
            }
            {
                const std::lock_guard lock(stats_mutex_);
                if (active != nullptr) {
                    ++stats_.profile_switches;
                }
                stats_.current_profile = wanted.name;
            }
            const bool changes_exposure = wanted.exposure_ms.has_value() || wanted.automatic_exposure || wanted.gain.has_value();
            active = &wanted;
            if (changes_exposure) {
                settle();
            }
        }
        return wanted;
    };

    if (plan.kind == CaptureKind::Scheduled) {
        if (!wait_until(*plan.start_at)) {
            return finish(true, false);
        }
    }

    if (plan.kind == CaptureKind::Bracket) {
        const CaptureProfile& profile = choose_profile();
        double base_ms = profile.exposure_ms.value_or(0.0);
        std::optional<hal::ControlInfo> exposure_info;
        if (auto caps = camera_->capabilities()) {
            for (const hal::ControlInfo& info : caps->controls) {
                if (info.control == hal::CameraControl::Exposure) {
                    exposure_info = info;
                }
            }
        }
        if (base_ms <= 0.0) {
            if (auto current = camera_->control(hal::CameraControl::Exposure)) {
                base_ms = current->value;
            }
        }
        if (!exposure_info || base_ms <= 0.0) {
            return fail(ErrorCode::Unsupported, "a bracket needs a camera with an exposure control");
        }
        const std::vector<double> exposures = bracket_exposures(base_ms, plan.bracket_stops, exposure_info->minimum, exposure_info->maximum);
        std::uint32_t sequence = 0;
        for (const double exposure_ms : exposures) {
            if (stop_requested_.load()) {
                return finish(true, false);
            }
            if (!disk_has_room(plan)) {
                return finish(false, true);
            }
            if (auto set = camera_->set_control(hal::CameraControl::Exposure, {.value = exposure_ms, .automatic = false}); !set) {
                count_failed(set.error());
                continue;
            }
            settle();
            if (auto picture = capture_one(run, profile, sequence, exposure_ms)) {
                count_written(*picture);
            } else {
                count_failed(picture.error());
            }
            ++sequence;
        }
        // Back to where the bracket started.
        (void)camera_->set_control(hal::CameraControl::Exposure, {.value = base_ms, .automatic = false});
        return finish(false, false);
    }

    const std::uint32_t wanted = plan.kind == CaptureKind::Single ? 1 : plan.count;
    const bool until_stopped = wanted == 0;
    const bool paced = plan.kind == CaptureKind::Interval || plan.kind == CaptureKind::Scheduled;
    auto next_due = std::chrono::steady_clock::now();
    for (std::uint32_t sequence = 0; until_stopped || sequence < wanted; ++sequence) {
        if (stop_requested_.load()) {
            return finish(true, false);
        }
        if (plan.end_at && clock_.now_utc() >= *plan.end_at) {
            break;
        }
        if (paced && plan.interval > std::chrono::milliseconds::zero()) {
            // Keep the cadence anchored to the first picture; a late picture does not shift the series.
            while (std::chrono::steady_clock::now() < next_due) {
                if (stop_requested_.load()) {
                    return finish(true, false);
                }
                std::this_thread::sleep_for(std::min(kWaitSlice, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                     next_due - std::chrono::steady_clock::now())));
            }
            next_due += plan.interval;
            // The newest frame, not one that waited in the queue while we slept.
            (void)run.subscription->try_take();
        }
        if (!disk_has_room(plan)) {
            return finish(false, true);
        }
        const CaptureProfile& profile = choose_profile();
        if (auto picture = capture_one(run, profile, sequence, std::nullopt)) {
            count_written(*picture);
        } else {
            count_failed(picture.error());
            if (picture.error().code == ErrorCode::Timeout && !camera_->is_streaming()) {
                break;  // the camera is gone; nothing more will come
            }
        }
    }
    return finish(false, false);
}

}  // namespace cloudscope
