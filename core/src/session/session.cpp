#include "cloudscope/session/session.hpp"

#include "cloudscope/common/build_info.hpp"

#include <QtCore/QFile>
#include <QtCore/QString>
#include <fmt/format.h>

#include <fstream>
#include <stdexcept>

namespace cloudscope {

std::string session_id(UtcTime started, std::string_view site_id)
{
    // "20261009T101530_123Z" -> "20261009T101530Z"
    const std::string stamp = format_file_stamp(started);
    return fmt::format("{}Z-{}", stamp.substr(0, 15), site_id.empty() ? std::string_view("site") : site_id);
}

std::filesystem::path session_folder(const std::filesystem::path& root, std::string_view site_id, UtcTime started)
{
    const std::string date = format_iso8601(started).substr(0, 10);  // "2026-10-09"
    const std::string site(site_id.empty() ? std::string_view("site") : site_id);
    return root / std::filesystem::path(reinterpret_cast<const char8_t*>(site.c_str()))  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
           / date / session_id(started, site_id);
}

const JsonSchema& session_schema()
{
    static const JsonSchema schema = [] {
        QFile file(QStringLiteral(":/cloudscope/session.schema.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("the session schema is missing from the resources");
        }
        const QByteArray bytes = file.readAll();
        auto compiled = JsonSchema::compile(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size()));
        if (!compiled) {
            throw std::runtime_error("the session schema does not compile: " + compiled.error().to_string());
        }
        return *compiled;
    }();
    return schema;
}

Expected<SessionInfo> session_info_from_json(const nlohmann::json& manifest, const std::filesystem::path& folder)
{
    const auto issues = session_schema().validate(manifest);
    if (!issues.empty()) {
        return fail(ErrorCode::Validation, fmt::format("session manifest: {}", issues.front().to_string()));
    }
    SessionInfo info;
    info.id = manifest["id"].get<std::string>();
    const nlohmann::json& site = manifest["site"];
    info.site = SiteInfo{.id = site["id"].get<std::string>(),
                         .latitude_deg = site["latitude_deg"].get<double>(),
                         .longitude_deg = site["longitude_deg"].get<double>(),
                         .altitude_m = site["altitude_m"].get<double>()};
    info.camera_id = manifest["camera"]["id"].get<std::string>();
    info.camera_name = manifest["camera"]["name"].get<std::string>();
    const auto started = parse_iso8601(manifest["started_utc"].get<std::string>());
    if (!started) {
        return fail(ErrorCode::Parse, "session manifest: started_utc is not a time");
    }
    info.started = *started;
    if (manifest.contains("ended_utc")) {
        const auto ended = parse_iso8601(manifest["ended_utc"].get<std::string>());
        if (!ended) {
            return fail(ErrorCode::Parse, "session manifest: ended_utc is not a time");
        }
        info.ended = *ended;
    }
    info.frames = manifest["frames"].get<std::uint32_t>();
    info.bytes = manifest["bytes"].get<std::uint64_t>();
    if (manifest.contains("notes")) {
        info.notes = manifest["notes"].get<std::string>();
    }
    info.folder = folder;
    return info;
}

Expected<Session> Session::create(const std::filesystem::path& root, const SiteInfo& site, const hal::DeviceInfo& camera,
                                  UtcTime started)
{
    SessionInfo info;
    info.id = session_id(started, site.id);
    info.site = site;
    if (info.site.id.empty()) {
        info.site.id = "site";
    }
    info.camera_id = camera.id;
    info.camera_name = camera.name;
    info.started = started;
    std::error_code error;
    info.folder = std::filesystem::absolute(session_folder(root, site.id, started), error);
    if (error) {
        return fail(ErrorCode::Io, fmt::format("could not resolve {}", root.string()));
    }
    if (std::filesystem::exists(info.folder / "session.json")) {
        return fail(ErrorCode::AlreadyExists, fmt::format("{} is already a session", info.folder.string()));
    }
    Session session(std::move(info));
    for (const std::filesystem::path& folder :
         {session.frames_folder(), session.calibration_folder(), session.logs_folder()}) {
        std::filesystem::create_directories(folder, error);
        if (error) {
            return fail(ErrorCode::Io, fmt::format("could not create {}", folder.string()));
        }
    }
    if (auto saved = session.save(); !saved) {
        return fail(saved.error());
    }
    return session;
}

Expected<Session> Session::open(const std::filesystem::path& folder)
{
    std::ifstream in(folder / "session.json");
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("{} has no session.json", folder.string()));
    }
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", (folder / "session.json").string(), e.what()));
    }
    std::error_code error;
    auto info = session_info_from_json(manifest, std::filesystem::absolute(folder, error));
    if (!info) {
        return fail(info.error());
    }
    return Session(std::move(*info));
}

Expected<void> Session::record_picture(const WrittenFile& file, std::uint32_t save_every)
{
    ++info_.frames;
    info_.bytes += file.bytes;
    if (save_every > 0 && info_.frames % save_every == 0) {
        return save();
    }
    return {};
}

Expected<void> Session::close(UtcTime ended)
{
    info_.ended = ended;
    return save();
}

nlohmann::json Session::manifest() const
{
    const BuildInfo& build = build_info();
    nlohmann::json out{
        {"schema", "cloudscope.session/1"},
        {"id", info_.id},
        {"site",
         {{"id", info_.site.id},
          {"latitude_deg", info_.site.latitude_deg},
          {"longitude_deg", info_.site.longitude_deg},
          {"altitude_m", info_.site.altitude_m}}},
        {"camera", {{"id", info_.camera_id}, {"name", info_.camera_name}}},
        {"started_utc", format_iso8601(info_.started)},
        {"frames", info_.frames},
        {"bytes", info_.bytes},
        {"layout", {{"frames", "frames"}, {"calibration", "calibration"}, {"logs", "logs"}}},
        {"software", {{"name", "CloudScope"}, {"version", build.version}, {"git_revision", build.git_revision}}},
    };
    if (info_.ended) {
        out["ended_utc"] = format_iso8601(*info_.ended);
    }
    if (!info_.notes.empty()) {
        out["notes"] = info_.notes;
    }
    return out;
}

Expected<void> Session::save() const
{
    const std::filesystem::path target = manifest_path();
    const std::filesystem::path temporary = info_.folder / "session.json.part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
        out << manifest().dump(2) << "\n";
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(target, error);
        std::filesystem::rename(temporary, target, error);
        if (error) {
            return fail(ErrorCode::Io, fmt::format("could not move {} into place", temporary.string()));
        }
    }
    return {};
}

}  // namespace cloudscope
