// The frame catalogue (P030): an SQLite index of every picture CloudScope has written, with the metadata that
// answers "what do we have" without opening a sidecar: time, session, file, exposure, statistics, Sun. Backed by
// Qt SQL (QSQLITE); one Catalogue is used from one thread (Qt SQL connections are thread-bound).
//
//   auto catalogue = Catalogue::open(root / "catalogue.sqlite");
//   catalogue->add_session(session.info());
//   catalogue->add_frame(frame_entry(picture, session.info().id));
//   auto frames = catalogue->frames({.from = t0, .to = t1});
//
// The catalogue is a cache of the files: `index_folder()` rebuilds it from the sidecars, and the retention policy
// removes the oldest pictures (file, sidecar and row) when a size or an age limit is passed (FR-REC-07's
// companion: the disk guard stops a run, retention makes room before the next one).
#pragma once

#include "cloudscope/capture/recording.hpp"
#include "cloudscope/capture/sequencer.hpp"
#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/session/session.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cloudscope {

struct FrameEntry {
    std::int64_t id = 0;  // assigned by the catalogue; 0 before insertion
    std::string session_id;
    std::filesystem::path path;  // the picture, absolute
    UtcTime utc{};
    double mjd = 0.0;
    std::uint64_t sequence = 0;
    std::string format;  // "png", "jpeg", "tiff16", "fits"
    std::uint64_t bytes = 0;
    std::string sha256;
    int width = 0;
    int height = 0;
    std::optional<double> exposure_ms = std::nullopt;
    std::optional<double> gain = std::nullopt;
    std::optional<double> mean = std::nullopt;
    std::optional<double> clipped_fraction = std::nullopt;
    std::optional<double> sun_elevation_deg = std::nullopt;
    std::optional<double> sun_azimuth_deg = std::nullopt;
    std::string profile;
    bool simulated = false;
};

// The entry for a picture the sequencer just wrote.
[[nodiscard]] FrameEntry frame_entry(const CapturedPicture& picture, std::string session_id);
// The entry for a picture from its sidecar (`<picture>.json`); either sidecar schema.
[[nodiscard]] Expected<FrameEntry> frame_entry_from_sidecar(const std::filesystem::path& sidecar,
                                                            std::string session_id);

struct FrameQuery {
    std::optional<std::string> session_id = std::nullopt;
    std::optional<UtcTime> from = std::nullopt;  // inclusive
    std::optional<UtcTime> to = std::nullopt;    // exclusive
    std::size_t limit = 1000;
    bool newest_first = false;
};

struct RetentionPolicy {
    std::optional<std::uintmax_t> max_bytes = std::nullopt;    // keep the newest pictures up to this many bytes
    std::optional<std::chrono::hours> max_age = std::nullopt;  // remove pictures older than this
};

struct RetentionResult {
    std::uint32_t removed_frames = 0;
    std::uint64_t removed_bytes = 0;
    std::uint32_t missing_files = 0;  // rows whose picture was already gone
};

class Catalogue {
public:
    // Opens or creates the database file (and its folder).
    [[nodiscard]] static Expected<std::unique_ptr<Catalogue>> open(const std::filesystem::path& file);
    ~Catalogue();
    Catalogue(const Catalogue&) = delete;
    Catalogue& operator=(const Catalogue&) = delete;
    Catalogue(Catalogue&&) = delete;
    Catalogue& operator=(Catalogue&&) = delete;

    [[nodiscard]] const std::filesystem::path& file() const;

    // Inserts or updates a session (by id).
    [[nodiscard]] Expected<void> add_session(const SessionInfo& info);
    [[nodiscard]] Expected<std::vector<SessionInfo>> sessions() const;
    [[nodiscard]] Expected<std::optional<SessionInfo>> session(const std::string& id) const;

    // Inserts a frame and returns its id; a path already catalogued is replaced.
    [[nodiscard]] Expected<std::int64_t> add_frame(const FrameEntry& frame);
    // Inserts many frames in one transaction.
    [[nodiscard]] Expected<void> add_frames(const std::vector<FrameEntry>& entries);
    [[nodiscard]] Expected<std::vector<FrameEntry>> frames(const FrameQuery& query) const;
    [[nodiscard]] Expected<std::optional<FrameEntry>> frame_at(const std::filesystem::path& path) const;
    [[nodiscard]] Expected<std::uint64_t> count(const std::optional<std::string>& session_id = std::nullopt) const;
    [[nodiscard]] Expected<std::uint64_t> total_bytes() const;

    // Removes the oldest pictures (file, sidecar, row) until the policy holds. With `delete_files` false only
    // the rows go (for a catalogue that mirrors files kept elsewhere).
    [[nodiscard]] Expected<RetentionResult> apply_retention(const RetentionPolicy& policy, UtcTime now,
                                                            bool delete_files = true);

    // Adds every picture of a folder that has a sidecar and is not catalogued yet; returns how many were added.
    [[nodiscard]] Expected<std::uint32_t> index_folder(const std::filesystem::path& folder,
                                                       const std::string& session_id);

private:
    struct Impl;
    explicit Catalogue(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace cloudscope
