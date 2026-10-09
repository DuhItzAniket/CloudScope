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

namespace {

Expected<void> validate_profile(const CaptureProfile& profile)
{
    if (profile.name.empty()) {
        return fail(ErrorCode::InvalidArgument, "a profile needs a name");
    }
    if (profile.jpeg_quality < 1 || profile.jpeg_quality > 100) {
        return fail(ErrorCode::InvalidArgument, fmt::format("profile '{}': JPEG quality must be 1..100", profile.name));
    }
    if (profile.exposure_ms.has_value() && profile.exposure_ms.value() <= 0.0) {
        return fail(ErrorCode::InvalidArgument, fmt::format("profile '{}': exposure must be positive", profile.name));
    }
    return {};
}

Expected<void> validate_kind(const CapturePlan& plan)
{
    switch (plan.kind) {
    case CaptureKind::Single:
    case CaptureKind::Interval:
        return {};
    case CaptureKind::Burst:
        if (plan.count == 0) {
            return fail(ErrorCode::InvalidArgument, "a burst needs a frame count");
        }
        return {};
    case CaptureKind::Bracket:
        if (plan.bracket_stops.empty()) {
            return fail(ErrorCode::InvalidArgument, "a bracket needs at least one stop");
        }
        return {};
    case CaptureKind::Scheduled:
        if (!plan.start_at.has_value()) {
            return fail(ErrorCode::InvalidArgument, "a scheduled run needs a start time");
        }
        if (plan.end_at.has_value() && plan.end_at.value() <= plan.start_at.value()) {
            return fail(ErrorCode::InvalidArgument, "a scheduled run must end after it starts");
        }
        return {};
    }
    return fail(ErrorCode::InvalidArgument, "unknown capture kind");
}

}  // namespace

Expected<void> validate(const CapturePlan& plan)
{
    if (plan.folder.empty()) {
        return fail(ErrorCode::InvalidArgument, "the plan has no folder");
    }
    if (auto kind = validate_kind(plan); !kind) {
        return kind;
    }
    if (plan.interval < std::chrono::milliseconds::zero()) {
        return fail(ErrorCode::InvalidArgument, "the interval cannot be negative");
    }
    if (plan.duration.has_value() && plan.duration.value() <= std::chrono::milliseconds::zero()) {
        return fail(ErrorCode::InvalidArgument, "the duration must be positive");
    }
    if (plan.settle_frames < 0) {
        return fail(ErrorCode::InvalidArgument, "settle_frames cannot be negative");
    }
    if (plan.max_consecutive_failures < 1) {
        return fail(ErrorCode::InvalidArgument, "max_consecutive_failures must be at least 1");
    }
    for (const CaptureProfile* profile : {&plan.day, &plan.night}) {
        if (auto valid = validate_profile(*profile); !valid) {
            return valid;
        }
    }
    const auto sample = expand_filename(plan.filename_template, FilenameFields{});
    if (!sample) {
        return fail(sample.error());
    }
    return {};
}

// Everything one run() carries between its steps.
struct Sequencer::Run {
    const CapturePlan& plan;
    std::shared_ptr<FrameSubscription> subscription;
    SteadyClock::time_point started = SteadyClock::now();
    const CaptureProfile* active = nullptr;
    int consecutive_failures = 0;
    std::uint32_t sequence = 0;
    std::optional<SteadyClock::time_point> next_due = std::nullopt;
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

const CaptureProfile& Sequencer::profile_for(const CapturePlan& plan, std::optional<double> sun_elevation_deg)
{
    if (sun_elevation_deg.has_value() && sun_elevation_deg.value() < plan.night_below_sun_elevation_deg) {
        return plan.night;
    }
    return plan.day;
}

std::optional<double> Sequencer::sun_elevation_now() const
{
    if (!site_.has_value()) {
        return std::nullopt;
    }
    return sun_position(clock_.now_utc(), site_->latitude_deg, site_->longitude_deg).elevation_deg;
}

Expected<void> Sequencer::apply_profile(const CaptureProfile& profile)
{
    using hal::CameraControl;
    using hal::ControlSetting;
    const auto apply = [&](CameraControl control, ControlSetting setting) -> Expected<void> {
        if (auto set = camera_->set_control(control, setting); !set && set.error().code != ErrorCode::Unsupported) {
            return fail(set.error());
        }
        return {};
    };
    if (profile.exposure_ms.has_value()) {
        if (auto set = apply(CameraControl::Exposure, {.value = profile.exposure_ms.value(), .automatic = false});
            !set) {
            return set;
        }
    } else if (profile.automatic_exposure) {
        if (auto set = apply(CameraControl::Exposure, {.value = 0.0, .automatic = true}); !set) {
            return set;
        }
    }
    if (profile.gain.has_value()) {
        return apply(CameraControl::Gain, {.value = profile.gain.value(), .automatic = false});
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
    if (current.plan.end_at.has_value() && clock_.now_utc() >= current.plan.end_at.value()) {
        return true;
    }
    return current.plan.duration.has_value() && SteadyClock::now() - current.started >= current.plan.duration.value();
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
    if (!current.plan.pause_below_sun_elevation_deg.has_value() || !site_.has_value()) {
        return Outcome::Continue;
    }
    bool paused = false;
    while (true) {
        const std::optional<double> elevation = sun_elevation_now();
        if (!elevation.has_value() || elevation.value() >= current.plan.pause_below_sun_elevation_deg.value()) {
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
        const auto recovered = recovery_();
        if (recovered) {
            const std::lock_guard lock(stats_mutex_);
            ++stats_.recoveries;
            current.consecutive_failures = 0;
            current.active = nullptr;  // the profile's controls must be applied again to the reopened camera
            return Outcome::Continue;
        }
        note(recovered.error());
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
        record.controls.insert_or_assign("exposure",
                                         *exposure);  // not operator[]: GCC 15 -O3 stringop-overflow false positive
        record.exposure_ms = exposure->value;
    } else if (fixed_exposure_ms.has_value()) {
        record.exposure_ms = fixed_exposure_ms;
    }
    if (auto gain = camera_->control(CameraControl::Gain)) {
        record.controls.insert_or_assign("gain", *gain);  // not operator[]: GCC 15 -O3 stringop-overflow false positive
        record.gain = gain->value;
    }
    record.site = site_;
    record.pointing = pointing_;
    if (site_.has_value()) {
        const SunPosition sun = sun_position(info.captured.utc, site_->latitude_deg, site_->longitude_deg);
        record.sun = SunInfo{.azimuth_deg = sun.azimuth_deg, .elevation_deg = sun.elevation_deg};
    }
    record.calibration_id = calibration_id_;
    record.session_id = session_id_;

    const auto name = expand_filename(current.plan.filename_template,
                                      FilenameFields{.site = site_.has_value() ? site_->id : std::string(),
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

SequencerStats Sequencer::finish(const Run& current, Reason reason)
{
    const std::lock_guard lock(stats_mutex_);
    stats_.stopped_by_request = reason == Reason::Request;
    stats_.stopped_by_disk_guard = reason == Reason::DiskGuard;
    stats_.stopped_by_failures = reason == Reason::Failures;
    stats_.paused = false;
    stats_.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - current.started);
    return stats_;
}

Sequencer::Reason Sequencer::reason_of(Outcome outcome)
{
    return outcome == Outcome::Stopped ? Reason::Request : Reason::Done;
}

void Sequencer::count_written(Run& current, const CapturedPicture& picture)
{
    current.consecutive_failures = 0;
    {
        const std::lock_guard lock(stats_mutex_);
        ++stats_.written;
        stats_.bytes += picture.file.bytes;
    }
    if (on_picture_) {
        on_picture_(picture);
    }
}

void Sequencer::count_failed(Run& current, const Error& error)
{
    ++current.consecutive_failures;
    const std::lock_guard lock(stats_mutex_);
    ++stats_.failed;
    stats_.last_error = error;
}

// Skips frames taken before an exposure change reached the sensor.
void Sequencer::settle(Run& current)
{
    for (int i = 0; i < current.plan.settle_frames; ++i) {
        if (next_frame(current) != nullptr) {
            const std::lock_guard lock(stats_mutex_);
            ++stats_.skipped;
        }
    }
}

// Picks the profile for now, applies it when it changes.
const CaptureProfile& Sequencer::choose_profile(Run& current)
{
    const CaptureProfile& wanted = profile_for(current.plan, sun_elevation_now());
    if (current.active != nullptr && current.active->name == wanted.name) {
        return wanted;
    }
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
        settle(current);
    }
    return wanted;
}

// After a failed picture: give up, recover the camera, or just carry on (no reason).
std::optional<Sequencer::Reason> Sequencer::after_failure(Run& current, const Error& error)
{
    count_failed(current, error);
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
    if (current.consecutive_failures >= current.plan.max_consecutive_failures) {
        return Reason::Failures;
    }
    return std::nullopt;
}

// Keeps the cadence of a paced run anchored to the first picture: a late picture does not shift the series, and
// slots that passed while it was taken are skipped and counted (FR-SEQ-02).
Sequencer::Outcome Sequencer::wait_for_slot(Run& current)
{
    const SteadyClock::time_point now = SteadyClock::now();
    if (!current.next_due.has_value()) {
        current.next_due = now;
    } else if (now > current.next_due.value() + current.plan.interval) {
        const auto missed = static_cast<std::uint32_t>((now - current.next_due.value()) / current.plan.interval);
        current.next_due.value() += current.plan.interval * static_cast<SteadyClock::rep>(missed);
        const std::lock_guard lock(stats_mutex_);
        stats_.missed_slots += missed;
    }
    while (SteadyClock::now() < current.next_due.value()) {
        if (stop_requested_.load()) {
            return Outcome::Stopped;
        }
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(current.next_due.value() - SteadyClock::now());
        std::this_thread::sleep_for(std::min(kWaitSlice, left + std::chrono::milliseconds(1)));
    }
    current.next_due.value() += current.plan.interval;
    // The newest frame, not one that waited in the queue while we slept.
    static_cast<void>(current.subscription->try_take());
    return Outcome::Continue;
}

Expected<SequencerStats> Sequencer::run_bracket(Run& current)
{
    const CapturePlan& plan = current.plan;
    if (const Outcome paused = pause_for_sun(current); paused != Outcome::Continue) {
        return finish(current, reason_of(paused));
    }
    const CaptureProfile& profile = choose_profile(current);
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
    if (!exposure_info.has_value() || base_ms <= 0.0) {
        return fail(ErrorCode::Unsupported, "a bracket needs a camera with an exposure control");
    }
    const std::vector<double> exposures =
        bracket_exposures(base_ms, plan.bracket_stops, exposure_info->minimum, exposure_info->maximum);
    for (const double exposure_ms : exposures) {
        if (stop_requested_.load()) {
            return finish(current, Reason::Request);
        }
        if (!disk_has_room(plan)) {
            return finish(current, Reason::DiskGuard);
        }
        if (auto set = camera_->set_control(hal::CameraControl::Exposure, {.value = exposure_ms, .automatic = false});
            !set) {
            count_failed(current, set.error());
            continue;
        }
        settle(current);
        if (auto picture = capture_one(current, profile, current.sequence, exposure_ms)) {
            count_written(current, *picture);
        } else {
            count_failed(current, picture.error());
        }
        ++current.sequence;
    }
    // Back to where the bracket started.
    static_cast<void>(camera_->set_control(hal::CameraControl::Exposure, {.value = base_ms, .automatic = false}));
    return finish(current, Reason::Done);
}

Expected<SequencerStats> Sequencer::run_series(Run& current)
{
    const CapturePlan& plan = current.plan;
    const std::uint32_t wanted = plan.kind == CaptureKind::Single ? 1 : plan.count;
    const bool until_stopped = wanted == 0;
    const bool paced = (plan.kind == CaptureKind::Interval || plan.kind == CaptureKind::Scheduled) &&
                       plan.interval > std::chrono::milliseconds::zero();
    while (until_stopped || current.sequence < wanted) {
        if (stop_requested_.load()) {
            return finish(current, Reason::Request);
        }
        if (ended(current)) {
            break;
        }
        if (const Outcome paused = pause_for_sun(current); paused != Outcome::Continue) {
            return finish(current, reason_of(paused));
        }
        if (paced && wait_for_slot(current) == Outcome::Stopped) {
            return finish(current, Reason::Request);
        }
        if (!disk_has_room(plan)) {
            return finish(current, Reason::DiskGuard);
        }
        const CaptureProfile& profile = choose_profile(current);
        if (auto picture = capture_one(current, profile, current.sequence, std::nullopt)) {
            count_written(current, *picture);
            ++current.sequence;
        } else if (const auto reason = after_failure(current, picture.error()); reason.has_value()) {
            return finish(current, reason.value());
        }
    }
    return finish(current, Reason::Done);
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
    const RunningGuard running_guard(running_);

    std::error_code ignored;
    std::filesystem::create_directories(plan.folder, ignored);

    const bool burst = plan.kind == CaptureKind::Burst;
    Run current{.plan = plan,
                .subscription = hub_->subscribe("sequencer", burst ? Delivery::Queue : Delivery::Latest,
                                                burst ? std::max<std::size_t>(plan.count, 4) : 4)};

    if (plan.kind == CaptureKind::Scheduled && plan.start_at.has_value()) {
        if (const Outcome waited = wait_until(current, plan.start_at.value()); waited != Outcome::Continue) {
            return finish(current, reason_of(waited));
        }
    }
    if (plan.kind == CaptureKind::Bracket) {
        return run_bracket(current);
    }
    return run_series(current);
}

}  // namespace cloudscope
