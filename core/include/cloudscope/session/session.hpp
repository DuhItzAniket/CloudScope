// Observation sessions (P030): the folder layout a run of captures lives in, and its manifest.
//
//   <root>/<site-id>/<YYYY-MM-DD>/<session-id>/
//       session.json        manifest, schema "cloudscope.session/1" (core/resources/session.schema.json)
//       frames/             pictures and their sidecars (P027), named by the sequencer's template (P029)
//       calibration/        dark/flat masters and the intrinsic calibration used (P026, P031)
//       logs/               the application's log for the session
//
// The session id is the UTC start time to the second plus the site id ("20261009T101530Z-blr-roof"): sortable,
// unique per site, readable. The manifest is rewritten through a temporary file whenever it changes, so a crash
// leaves the previous version, never half of one.
#pragma once

#include "cloudscope/capture/recording.hpp"
#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/common/json_schema.hpp"
#include "cloudscope/hal/device.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cloudscope {

struct SessionInfo {
    std::string id;
    SiteInfo site;
    std::string camera_id;
    std::string camera_name;
    UtcTime started{};
    std::optional<UtcTime> ended = std::nullopt;
    std::uint32_t frames = 0;  // pictures recorded through record_picture()
    std::uint64_t bytes = 0;   // of those pictures
    std::string notes;
    std::filesystem::path folder;  // absolute
};

[[nodiscard]] std::string session_id(UtcTime started, std::string_view site_id);
// <root>/<site-id>/<YYYY-MM-DD>/<session-id>
[[nodiscard]] std::filesystem::path session_folder(const std::filesystem::path& root, std::string_view site_id,
                                                   UtcTime started);

class Session {
public:
    // Creates the folders and the first manifest. AlreadyExists if the folder is already a session.
    [[nodiscard]] static Expected<Session> create(const std::filesystem::path& root, const SiteInfo& site,
                                                  const hal::DeviceInfo& camera, UtcTime started);
    // Reads an existing session's manifest.
    [[nodiscard]] static Expected<Session> open(const std::filesystem::path& folder);

    [[nodiscard]] const SessionInfo& info() const { return info_; }
    [[nodiscard]] std::filesystem::path frames_folder() const { return info_.folder / "frames"; }
    [[nodiscard]] std::filesystem::path calibration_folder() const { return info_.folder / "calibration"; }
    [[nodiscard]] std::filesystem::path logs_folder() const { return info_.folder / "logs"; }
    [[nodiscard]] std::filesystem::path manifest_path() const { return info_.folder / "session.json"; }

    // Counts a picture written into the session (the manifest is saved every `save_every` pictures and on close).
    [[nodiscard]] Expected<void> record_picture(const WrittenFile& file, std::uint32_t save_every = 50);
    void set_notes(std::string notes) { info_.notes = std::move(notes); }
    // Marks the end and saves the manifest.
    [[nodiscard]] Expected<void> close(UtcTime ended);
    [[nodiscard]] Expected<void> save() const;
    [[nodiscard]] nlohmann::json manifest() const;

private:
    explicit Session(SessionInfo info) : info_(std::move(info)) {}
    SessionInfo info_;
};

[[nodiscard]] const JsonSchema& session_schema();
[[nodiscard]] Expected<SessionInfo> session_info_from_json(const nlohmann::json& manifest,
                                                           const std::filesystem::path& folder);

}  // namespace cloudscope
