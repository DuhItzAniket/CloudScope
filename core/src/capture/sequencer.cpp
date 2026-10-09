#include "cloudscope/capture/sequencer.hpp"

#include "cloudscope/capture/decode.hpp"
#include "cloudscope/capture/exposure.hpp"
#include "cloudscope/capture/solar.hpp"
#include "cloudscope/capture/statistics.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <thread>

namespace cloudscope {

namespace {

using SteadyClock = std::chrono::steady_clock;
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
    for (const CaptureKind kind : {CaptureKind::Single, CaptureKind::Burst, CaptureKind::Interval, CaptureKind::Bracket,
                                   CaptureKind::Scheduled}) {
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
    case CaptureKind::Interval:
        break;
    case CaptureKind::Burst:
        if (plan.count == 0) {
            return fail(ErrorCode::InvalidArgument, "a burst needs a frame count");
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
    if (plan.duration && *plan.duration <= std::chrono::milliseconds::zero()) {
        return fail(ErrorCode::InvalidArgument, "the duration must be positive");
    }
    if (plan.settle_frames < 0) {
        return fail(ErrorCode::InvalidArgument, "settle_frames cannot be negative");
    }
    if (plan.max_consecutive_failures < 1) {
        return fail(ErrorCode::InvalidArgument, "max_consecutive_failures must be at least 1");
    }
    for (const CaptureProfile* profile : {&plan.day, &plan.night}) {
        if (profile->name.empty()) {
            return fail(ErrorCode::InvalidArgument, "a profile needs a name");
        }
        if (profile->jpeg_quality < 1 || profile->jpeg_quality > 100) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format("profile '{}': JPEG quality must be 1..100", profile->name));
        }
        if (profile->exposure_ms && *profile->exposure_ms <= 0.0) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format("profile '{}': exposure must be positive", profile->name));
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
    SteadyClock::time_point started = SteadyClock::now();
    const CaptureProfile* active = nullptr;
    int consecutive_failures = 0;
};

Sequencer::Sequencer(std::shared_ptr<hal::ICamera> camera, std::shared_ptr<FrameHub> hub, const IClock& clock)
    : camera_(std::move(camera)), hub_(std::move(hub)), clock_(clock)
{
}

void Sequencer::set_recovery(Recovery recovery, std::chrono::milliseconds first_backoff,
                             std::chrono::milliseconds max_backoff)
{
    recovery_ = std::move(recovery);
    first_backoff_ = std::max(first_backoff, std::chrono::milliseconds(1));
    max_backoff_ = std::max(max_backoff, first_backoff_);
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

std::optional<double> Sequencer::sun_elevation_now() const
{
    if (!site_) {
        return std::nullopt;
    }
    return sun_position(clock_.now_utc(), site_->latitude_deg, site_->longitude_deg).elevation_deg;
}

Expected<void> Sequencer::apply_profile(const CaptureProfile& profile)
{
    using hal::CameraControl;
    using hal::ControlSetting;
    if (profile.exposure_ms) {
        if (auto set = camera_->set_control(CameraControl::Exposure,
                                            ControlSetting{.value = *profile.exposure_ms, .automatic = false});
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
        if (auto set =
                camera_->set_control(CameraControl::Gain, ControlSetting{.value = *profile.gain, .automatic = false});
            !set && set.error().code != ErrorCode::Unsupported) {
            return fail(set.error());
        }
    }
    return {};
}

FramePtr Sequencer::next_frame(Run& current)
{
    // Wait in slices so that request_stop() is honoured within kWaitSlice even when no frame comes.
    auto remaining = current.plan.frame_timeout;
    while (true) {
        const auto slice = std::min(remaining, kWaitSlice);
        if (FramePtr frame = current.subscription->wait(slice)) {
            return frame;
        }
        remaining -= slice;
        if (remaining <= std::chrono::milliseconds::zero() || stop_requested_.load()) {
            return nullptr;
        }
    }
}

bool Sequencer::ended(const Run& current) const
{
    if (current.plan.end_at && clock_.now_utc() >= *current.plan.end_at) {
        return true;
    }
    return current.plan.duration && SteadyClock::now() - current.started >= *current.plan.duration;
}

Sequencer::Outcome Sequencer::wait_until(const Run& current, UtcTime time)
{
    while (clock_.now_utc() < time) {
        if (stop_requested_.load()) {
            return Outcome::Stopped;
        }
        if (ended(current)) {
            return Outcome::Ended;
        }
        std::this_thread::sleep_for(kWaitSlice);
    }
    return Outcome::Continue;
}

Sequencer::Outcome Sequencer::pause_for_sun(Run& current)
{
    if (!current.plan.pause_below_sun_elevation_deg || !site_) {
        return Outcome::Continue;
    }
    bool paused = false;
    while (true) {
        const std::optional<double> elevation = sun_elevation_now();
        if (!elevation || *elevation >= *current.plan.pause_below_sun_elevation_deg) {
            break;
        }
        if (!paused) {
            paused = true;
            const std::lock_guard lock(stats_mutex_);
            stats_.paused = true;
            ++stats_.pauses;
        }
        if (stop_requested_.load()) {
            return Outcome::Stopped;
        }
        if (ended(current)) {
            return Outcome::Ended;
        }
        std::this_thread::sleep_for(kWaitSlice);
    }
    if (paused) {
        const std::lock_guard lock(stats_mutex_);
        stats_.paused = false;
    }
    return Outcome::Continue;
}

Sequencer::Outcome Sequencer::recover(Run& current)
{
    if (!recovery_) {
        return Outcome::Ended;
    }
    std::chrono::milliseconds backoff = first_backoff_;
    while (true) {
        // Wait the backoff in slices, watching for a stop.
        for (auto waited = std::chrono::milliseconds::zero(); waited < backoff; waited += kWaitSlice) {
            if (stop_requested_.load()) {
                return Outcome::Stopped;
            }
            std::this_thread::sleep_for(std::min(kWaitSlice, backoff - waited));
        }
        if (ended(current)) {
            return Outcome::Ended;
        }
        if (auto recovered = recovery_(); recovered) {
            const std::lock_guard lock(stats_mutex_);
            ++stats_.recoveries;
            current.consecutive_failures = 0;
            current.active = nullptr;  // the profile's controls must be applied again to the reopened camera
            return Outcome::Continue;
        } else {
            note(recovered.error());
        }
        backoff = std::min(backoff * 2, max_backoff_);
    }
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

Expected<CapturedPicture> Sequencer::capture_one(Run& current, const CaptureProfile& profile, std::uint32_t sequence,
                                                 std::optional<double> fixed_exposure_ms)
{
    using hal::CameraControl;
    const FramePtr frame = next_frame(current);
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

    const auto name = expand_filename(current.plan.filename_template,
                                      FilenameFields{.site = site_ ? site_->id : std::string(),
                                                     .camera = record.camera_id,
                                                     .utc = info.captured.utc,
                                                     .sequence = sequence,
                                                     .profile = profile.name,
                                                     .kind = std::string(to_string(current.plan.kind))});
    if (!name) {
        return fail(name.error());
    }
    const std::filesystem::path file = current.plan.folder / (*name + std::string(extension(profile.format)));

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
    if (current.plan.write_sidecar) {
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

    Run current{.plan = plan,
                .subscription =
                    hub_->subscribe("sequencer", plan.kind == CaptureKind::Burst ? Delivery::Queue : Delivery::Latest,
                                    plan.kind == CaptureKind::Burst ? std::max<std::size_t>(plan.count, 4) : 4)};

    enum class Reason : std::uint8_t { Done, Request, DiskGuard, Failures };
    const auto finish = [&](Reason reason) {
        const std::lock_guard lock(stats_mutex_);
        stats_.stopped_by_request = reason == Reason::Request;
        stats_.stopped_by_disk_guard = reason == Reason::DiskGuard;
        stats_.stopped_by_failures = reason == Reason::Failures;
        stats_.paused = false;
        stats_.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - current.started);
        return stats_;
    };
    const auto outcome_reason = [](Outcome outcome) {
        return outcome == Outcome::Stopped ? Reason::Request : Reason::Done;
    };
    const auto count_written = [&](const CapturedPicture& picture) {
        current.consecutive_failures = 0;
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
        ++current.consecutive_failures;
        const std::lock_guard lock(stats_mutex_);
        ++stats_.failed;
        stats_.last_error = error;
    };
    // Skips frames taken before an exposure change reached the sensor.
    const auto settle = [&] {
        for (int i = 0; i < plan.settle_frames; ++i) {
            if (next_frame(current) != nullptr) {
                const std::lock_guard lock(stats_mutex_);
                ++stats_.skipped;
            }
        }
    };
    // Picks the profile for now, applies it when it changes.
    const auto choose_profile = [&]() -> const CaptureProfile& {
        const CaptureProfile& wanted = profile_for(plan, sun_elevation_now());
        if (current.active == nullptr || current.active->name != wanted.name) {
            if (auto applied = apply_profile(wanted); !applied) {
                note(applied.error());
            }
            {
                const std::lock_guard lock(stats_mutex_);
                if (current.active != nullptr) {
                    ++stats_.profile_switches;
                }
                stats_.current_profile = wanted.name;
            }
            const bool changes_exposure =
                wanted.exposure_ms.has_value() || wanted.automatic_exposure || wanted.gain.has_value();
            current.active = &wanted;
            if (changes_exposure) {
                settle();
            }
        }
        return wanted;
    };
    // After a failed picture: give up, recover the camera, or just carry on.
    const auto after_failure = [&](const Error& error) -> std::optional<Reason> {
        count_failed(error);
        if (error.code == ErrorCode::Timeout && !camera_->is_streaming()) {
            switch (recover(current)) {
            case Outcome::Continue:
                return std::nullopt;
            case Outcome::Stopped:
                return Reason::Request;
            case Outcome::Ended:
                return recovery_ ? Reason::Done : Reason::Failures;
            }
        }
        if (current.consecutive_failures >= plan.max_consecutive_failures) {
            return Reason::Failures;
        }
        return std::nullopt;
    };

    if (plan.kind == CaptureKind::Scheduled) {
        if (const Outcome waited = wait_until(current, *plan.start_at); waited != Outcome::Continue) {
            return finish(outcome_reason(waited));
        }
    }

    if (plan.kind == CaptureKind::Bracket) {
        if (const Outcome paused = pause_for_sun(current); paused != Outcome::Continue) {
            return finish(outcome_reason(paused));
        }
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
            if (auto exposure_now = camera_->control(hal::CameraControl::Exposure)) {
                base_ms = exposure_now->value;
            }
        }
        if (!exposure_info || base_ms <= 0.0) {
            return fail(ErrorCode::Unsupported, "a bracket needs a camera with an exposure control");
        }
        const std::vector<double> exposures =
            bracket_exposures(base_ms, plan.bracket_stops, exposure_info->minimum, exposure_info->maximum);
        std::uint32_t sequence = 0;
        for (const double exposure_ms : exposures) {
            if (stop_requested_.load()) {
                return finish(Reason::Request);
            }
            if (!disk_has_room(plan)) {
                return finish(Reason::DiskGuard);
            }
            if (auto set =
                    camera_->set_control(hal::CameraControl::Exposure, {.value = exposure_ms, .automatic = false});
                !set) {
                count_failed(set.error());
                continue;
            }
            settle();
            if (auto picture = capture_one(current, profile, sequence, exposure_ms)) {
                count_written(*picture);
            } else {
                count_failed(picture.error());
            }
            ++sequence;
        }
        // Back to where the bracket started.
        (void)camera_->set_control(hal::CameraControl::Exposure, {.value = base_ms, .automatic = false});
        return finish(Reason::Done);
    }

    const std::uint32_t wanted = plan.kind == CaptureKind::Single ? 1 : plan.count;
    const bool until_stopped = wanted == 0;
    const bool paced = (plan.kind == CaptureKind::Interval || plan.kind == CaptureKind::Scheduled) &&
                       plan.interval > std::chrono::milliseconds::zero();
    std::optional<SteadyClock::time_point> next_due;
    std::uint32_t sequence = 0;
    while (until_stopped || sequence < wanted) {
        if (stop_requested_.load()) {
            return finish(Reason::Request);
        }
        if (ended(current)) {
            break;
        }
        if (const Outcome paused = pause_for_sun(current); paused != Outcome::Continue) {
            return finish(outcome_reason(paused));
        }
        if (paced) {
            const SteadyClock::time_point now = SteadyClock::now();
            if (!next_due) {
                next_due = now;
            } else if (now > *next_due + plan.interval) {
                // Slots passed while the last picture was taken: skip them, do not catch up in a burst.
                const auto missed = static_cast<std::uint32_t>((now - *next_due) / plan.interval);
                *next_due += plan.interval * static_cast<SteadyClock::rep>(missed);
                const std::lock_guard lock(stats_mutex_);
                stats_.missed_slots += missed;
            }
            while (SteadyClock::now() < *next_due) {
                if (stop_requested_.load()) {
                    return finish(Reason::Request);
                }
                std::this_thread::sleep_for(std::min(
                    kWaitSlice, std::chrono::duration_cast<std::chrono::milliseconds>(*next_due - SteadyClock::now()) +
                                    std::chrono::milliseconds(1)));
            }
            *next_due += plan.interval;
            // The newest frame, not one that waited in the queue while we slept.
            (void)current.subscription->try_take();
        }
        if (!disk_has_room(plan)) {
            return finish(Reason::DiskGuard);
        }
        const CaptureProfile& profile = choose_profile();
        if (auto picture = capture_one(current, profile, sequence, std::nullopt)) {
            count_written(*picture);
            ++sequence;
        } else if (const auto reason = after_failure(picture.error())) {
            return finish(*reason);
        }
    }
    return finish(Reason::Done);
}

}  // namespace cloudscope
