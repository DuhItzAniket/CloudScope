// The capture sequencer (P029, FR-SEQ-01 to FR-SEQ-06, FR-REC-06/07): single pictures, bursts, interval series,
// exposure brackets and scheduled runs, written as pictures with sidecars, with a day and a night profile chosen by
// the Sun's elevation, file names from a template and a guard against filling the disk.
//
//   auto hub = std::make_shared<FrameHub>();
//   Acquisition acquisition(camera, hub, clock);        // the camera is open and in the wanted mode
//   acquisition.start();
//   Sequencer sequencer(camera, hub, clock);
//   sequencer.set_site({...});                           // optional: enables the Sun-based profile switch and pause
//   sequencer.set_recovery([&] { ... reopen the camera, restart the acquisition ... });
//   CapturePlan plan = ...;
//   auto run = sequencer.run(plan);                      // blocks until the plan is done or request_stop()
//
// The sequencer is a consumer of the acquisition's frame hub like any other, so a preview can run beside it
// (architecture 4.1). It sets camera controls only when a profile asks for a fixed exposure or gain, or when a
// bracket needs an exposure; otherwise the camera's own setting stays.
//
// Interval runs follow a fixed-rate schedule anchored to the first picture: a slow picture does not shift the
// series, and slots that passed while it was taken are skipped and counted, never taken late in a burst (FR-SEQ-02).
#pragma once

#include "cloudscope/capture/frame_hub.hpp"
#include "cloudscope/capture/recording.hpp"
#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/hal/camera.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cloudscope {

enum class CaptureKind : std::uint8_t { Single, Burst, Interval, Bracket, Scheduled };
[[nodiscard]] std::string_view to_string(CaptureKind kind);
[[nodiscard]] Expected<CaptureKind> capture_kind_from_string(std::string_view text);

// How pictures are taken and stored while this profile is active.
struct CaptureProfile {
    std::string name = "day";
    ImageFileFormat format = ImageFileFormat::Png;
    int jpeg_quality = 92;
    std::optional<double> exposure_ms;  // fixed exposure; empty: leave the camera's exposure control alone
    std::optional<double> gain;         // fixed gain; empty: leave it alone
    bool automatic_exposure = false;    // ask the camera for automatic exposure (ignored when exposure_ms is set)
    bool keep_native_jpeg = true;       // MJPEG frames into JPEG files are stored as the camera sent them
};

struct CapturePlan {
    CaptureKind kind = CaptureKind::Single;
    // Single: ignored (one picture). Burst: consecutive frames. Interval: pictures (0 = until stopped, end_at or
    // duration). Bracket: one picture per stop. Scheduled: like Interval, from `start_at`.
    std::uint32_t count = 1;
    std::chrono::milliseconds interval{0};
    std::vector<double> bracket_stops = {-2.0, 0.0, 2.0};  // photographic stops around the current exposure
    std::optional<UtcTime> start_at;                       // Scheduled: when to begin
    std::optional<UtcTime> end_at;                         // when to stop, if before `count`
    std::optional<std::chrono::milliseconds> duration;     // how long to run at most, from the start of run()
    CaptureProfile day;
    CaptureProfile night{.name = "night"};
    double night_below_sun_elevation_deg = -6.0;  // night profile when the Sun is below this (civil dusk)
    // Pause (take nothing, keep waiting) while the Sun is below this; resumes when it rises above. Needs a site.
    std::optional<double> pause_below_sun_elevation_deg;
    std::filesystem::path folder;
    // Tokens: {site} {camera} {utc} (20261009T101530_123Z) {date} (20261009) {seq} (000042) {profile} {kind}.
    std::string filename_template = "{site}_{utc}_{seq}_{profile}";
    std::uintmax_t min_free_bytes = 512ULL * 1024 * 1024;  // the disk guard stops the run below this
    bool write_sidecar = true;
    std::chrono::milliseconds frame_timeout{2000};  // waiting for a frame from the hub
    int settle_frames = 2;                           // frames to skip after an exposure change
    int max_consecutive_failures = 3;                // frame failures in a row that end the run (not counting recoveries)
};

[[nodiscard]] Expected<void> validate(const CapturePlan& plan);

struct FilenameFields {
    std::string site;
    std::string camera;
    UtcTime utc{};
    std::uint32_t sequence = 0;
    std::string profile;
    std::string kind;
};
// Expands a template; InvalidArgument for an unknown token or unbalanced braces. The result has no extension.
[[nodiscard]] Expected<std::string> expand_filename(std::string_view pattern, const FilenameFields& fields);

struct SequencerStats {
    std::uint32_t written = 0;      // pictures on disk
    std::uint32_t failed = 0;       // frames that could not be taken, decoded or written
    std::uint32_t skipped = 0;      // frames skipped for settling after an exposure change
    std::uint32_t missed_slots = 0; // interval slots that passed while a picture was taken (FR-SEQ-02)
    std::uint64_t bytes = 0;        // of the pictures, without sidecars
    std::uint32_t profile_switches = 0;
    std::uint32_t pauses = 0;       // times the run paused for the Sun
    std::uint32_t recoveries = 0;   // times the camera was brought back (FR-SEQ-05)
    bool paused = false;
    std::string current_profile;
    std::optional<Error> last_error;
    bool stopped_by_request = false;
    bool stopped_by_disk_guard = false;
    bool stopped_by_failures = false;  // max_consecutive_failures reached, or the camera could not be recovered
    std::chrono::milliseconds elapsed{0};
};

struct CapturedPicture {
    WrittenFile file;
    CaptureRecord record;
    std::string profile;
};

class Sequencer {
public:
    // Brings the camera back after a failure: typically close/open it, set its mode and restart the acquisition
    // on the same hub. Called with exponential backoff until it succeeds or the run is stopped.
    using Recovery = std::function<Expected<void>()>;

    Sequencer(std::shared_ptr<hal::ICamera> camera, std::shared_ptr<FrameHub> hub, const IClock& clock);

    void set_site(std::optional<SiteInfo> site) { site_ = std::move(site); }
    void set_pointing(std::optional<PointingInfo> pointing) { pointing_ = std::move(pointing); }
    void set_calibration_id(std::string id) { calibration_id_ = std::move(id); }
    void set_session_id(std::string id) { session_id_ = std::move(id); }
    // Called from the run's thread after every picture.
    void set_on_picture(std::function<void(const CapturedPicture&)> callback) { on_picture_ = std::move(callback); }
    void set_recovery(Recovery recovery, std::chrono::milliseconds first_backoff = std::chrono::milliseconds(500),
                      std::chrono::milliseconds max_backoff = std::chrono::seconds(60));

    // Runs the plan to its end and returns what happened. InvalidArgument for a plan that fails validate();
    // Unavailable while a run is in progress. Frame errors are counted, not fatal, until
    // max_consecutive_failures; the disk guard, the end conditions and request_stop() end the run early
    // (reported in the stats, not as errors).
    [[nodiscard]] Expected<SequencerStats> run(const CapturePlan& plan);
    // Asks a running plan to stop after the current frame; may be called from any thread.
    void request_stop();
    [[nodiscard]] bool is_running() const { return running_.load(); }
    [[nodiscard]] SequencerStats stats() const;

    // The profile for a Sun elevation (night below the plan's threshold); the day profile when there is no site.
    [[nodiscard]] const CaptureProfile& profile_for(const CapturePlan& plan, std::optional<double> sun_elevation_deg) const;

private:
    struct Run;
    enum class Outcome : std::uint8_t { Continue, Stopped, Ended };

    [[nodiscard]] Expected<void> apply_profile(const CaptureProfile& profile);
    [[nodiscard]] Expected<CapturedPicture> capture_one(Run& run, const CaptureProfile& profile, std::uint32_t sequence,
                                                        std::optional<double> fixed_exposure_ms);
    [[nodiscard]] FramePtr next_frame(Run& run);
    [[nodiscard]] std::optional<double> sun_elevation_now() const;
    [[nodiscard]] bool ended(const Run& run) const;  // end_at or duration reached
    [[nodiscard]] Outcome wait_until(const Run& run, UtcTime time);
    [[nodiscard]] Outcome pause_for_sun(Run& run);
    [[nodiscard]] Outcome recover(Run& run);
    [[nodiscard]] bool disk_has_room(const CapturePlan& plan);
    void note(const Error& error);

    std::shared_ptr<hal::ICamera> camera_;
    std::shared_ptr<FrameHub> hub_;
    const IClock& clock_;
    std::optional<SiteInfo> site_;
    std::optional<PointingInfo> pointing_;
    std::string calibration_id_;
    std::string session_id_;
    std::function<void(const CapturedPicture&)> on_picture_;
    Recovery recovery_;
    std::chrono::milliseconds first_backoff_{500};
    std::chrono::milliseconds max_backoff_{60'000};

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex stats_mutex_;
    SequencerStats stats_;
};

}  // namespace cloudscope
