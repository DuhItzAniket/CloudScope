#include "cloudscope/session/catalogue.hpp"

#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <fmt/format.h>

#include <atomic>
#include <fstream>

namespace cloudscope {

namespace {

QString qstring(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString qstring(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

std::string utf8(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::filesystem::path path_of(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

// One spelling per file: absolute, normalised, with the platform's separators. Paths are compared as text in
// SQL, so "C:/data/a.jpg" and "C:\\data\\a.jpg" must become the same string before they are stored or looked up.
QString path_text(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    absolute = absolute.lexically_normal();
    absolute.make_preferred();
    return qstring(absolute);
}

QVariant optional_variant(const std::optional<double>& value)
{
    return value ? QVariant(*value) : QVariant(QMetaType(QMetaType::Double));
}

std::optional<double> optional_double(const QVariant& value)
{
    return value.isNull() ? std::nullopt : std::optional<double>(value.toDouble());
}

Error sql_error(const QSqlQuery& query, std::string_view what)
{
    return Error{.code = ErrorCode::Io, .message = fmt::format("catalogue: {}: {}", what, utf8(query.lastError().text()))};
}

constexpr const char* kFrameColumns =
    "id, session_id, path, utc_ms, mjd, sequence, format, bytes, sha256, width, height, exposure_ms, gain, mean, "
    "clipped_fraction, sun_elevation_deg, sun_azimuth_deg, profile, simulated";

FrameEntry frame_from_row(const QSqlQuery& query)
{
    FrameEntry frame;
    frame.id = query.value(0).toLongLong();
    frame.session_id = utf8(query.value(1).toString());
    frame.path = path_of(query.value(2).toString());
    frame.utc = from_unix_ms(query.value(3).toLongLong());
    frame.mjd = query.value(4).toDouble();
    frame.sequence = query.value(5).toULongLong();
    frame.format = utf8(query.value(6).toString());
    frame.bytes = query.value(7).toULongLong();
    frame.sha256 = utf8(query.value(8).toString());
    frame.width = query.value(9).toInt();
    frame.height = query.value(10).toInt();
    frame.exposure_ms = optional_double(query.value(11));
    frame.gain = optional_double(query.value(12));
    frame.mean = optional_double(query.value(13));
    frame.clipped_fraction = optional_double(query.value(14));
    frame.sun_elevation_deg = optional_double(query.value(15));
    frame.sun_azimuth_deg = optional_double(query.value(16));
    frame.profile = utf8(query.value(17).toString());
    frame.simulated = query.value(18).toInt() != 0;
    return frame;
}

void bind_frame(QSqlQuery& query, const FrameEntry& frame)
{
    query.addBindValue(qstring(frame.session_id));
    query.addBindValue(path_text(frame.path));
    query.addBindValue(static_cast<qlonglong>(to_unix_ms(frame.utc)));
    query.addBindValue(frame.mjd);
    query.addBindValue(static_cast<qulonglong>(frame.sequence));
    query.addBindValue(qstring(frame.format));
    query.addBindValue(static_cast<qulonglong>(frame.bytes));
    query.addBindValue(qstring(frame.sha256));
    query.addBindValue(frame.width);
    query.addBindValue(frame.height);
    query.addBindValue(optional_variant(frame.exposure_ms));
    query.addBindValue(optional_variant(frame.gain));
    query.addBindValue(optional_variant(frame.mean));
    query.addBindValue(optional_variant(frame.clipped_fraction));
    query.addBindValue(optional_variant(frame.sun_elevation_deg));
    query.addBindValue(optional_variant(frame.sun_azimuth_deg));
    query.addBindValue(qstring(frame.profile));
    query.addBindValue(frame.simulated ? 1 : 0);
}

constexpr const char* kInsertFrame =
    "INSERT OR REPLACE INTO frames (session_id, path, utc_ms, mjd, sequence, format, bytes, sha256, width, height, "
    "exposure_ms, gain, mean, clipped_fraction, sun_elevation_deg, sun_azimuth_deg, profile, simulated) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

SessionInfo session_from_row(const QSqlQuery& query)
{
    SessionInfo session;
    session.id = utf8(query.value(0).toString());
    session.site = SiteInfo{.id = utf8(query.value(1).toString()),
                            .latitude_deg = query.value(2).toDouble(),
                            .longitude_deg = query.value(3).toDouble(),
                            .altitude_m = query.value(4).toDouble()};
    session.camera_id = utf8(query.value(5).toString());
    session.camera_name = utf8(query.value(6).toString());
    session.started = from_unix_ms(query.value(7).toLongLong());
    if (!query.value(8).isNull()) {
        session.ended = from_unix_ms(query.value(8).toLongLong());
    }
    session.frames = query.value(9).toUInt();
    session.bytes = query.value(10).toULongLong();
    session.notes = utf8(query.value(11).toString());
    session.folder = path_of(query.value(12).toString());
    return session;
}

constexpr const char* kSessionColumns =
    "id, site_id, latitude_deg, longitude_deg, altitude_m, camera_id, camera_name, started_ms, ended_ms, frames, bytes, "
    "notes, folder";

std::atomic<int> connection_counter{0};

}  // namespace

struct Catalogue::Impl {
    QString connection_name;
    QSqlDatabase db;
    std::filesystem::path file;

    ~Impl()
    {
        if (db.isValid()) {
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connection_name);
        }
    }

    Expected<void> exec(const char* sql)
    {
        QSqlQuery query(db);
        if (!query.exec(QString::fromLatin1(sql))) {
            return fail(sql_error(query, sql));
        }
        return {};
    }
};

FrameEntry frame_entry(const CapturedPicture& picture, std::string session_id)
{
    FrameEntry frame;
    frame.session_id = std::move(session_id);
    std::error_code error;
    frame.path = std::filesystem::absolute(picture.file.path, error);
    if (error) {
        frame.path = picture.file.path;
    }
    frame.utc = picture.record.info.captured.utc;
    frame.mjd = modified_julian_date(frame.utc);
    frame.sequence = picture.record.info.sequence;
    const std::string extension = picture.file.path.extension().string();
    frame.format = extension == ".png" ? "png" : extension == ".jpg" ? "jpeg" : extension == ".fits" ? "fits" : "tiff16";
    frame.bytes = picture.file.bytes;
    frame.sha256 = picture.file.sha256;
    frame.width = picture.file.width;
    frame.height = picture.file.height;
    frame.exposure_ms = picture.record.exposure_ms;
    frame.gain = picture.record.gain;
    if (picture.record.statistics) {
        frame.mean = picture.record.statistics->mean;
        frame.clipped_fraction = picture.record.statistics->clipped_fraction;
    }
    if (picture.record.sun) {
        frame.sun_elevation_deg = picture.record.sun->elevation_deg;
        frame.sun_azimuth_deg = picture.record.sun->azimuth_deg;
    }
    frame.profile = picture.profile;
    frame.simulated = picture.record.info.simulated;
    return frame;
}

Expected<FrameEntry> frame_entry_from_sidecar(const std::filesystem::path& sidecar, std::string session_id)
{
    std::ifstream in(sidecar);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", sidecar.string()));
    }
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", sidecar.string(), e.what()));
    }
    const auto summary = read_sidecar(document);
    if (!summary) {
        return fail(summary.error());
    }
    FrameEntry frame;
    frame.session_id = std::move(session_id);
    // "<picture>.json" sits next to its picture.
    std::filesystem::path picture = sidecar;
    picture.replace_extension();
    if (!summary->file_name.empty()) {
        picture = sidecar.parent_path() / std::filesystem::path(reinterpret_cast<const char8_t*>(summary->file_name.c_str()));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
    std::error_code error;
    frame.path = std::filesystem::absolute(picture, error);
    frame.utc = summary->utc;
    frame.mjd = modified_julian_date(frame.utc);
    frame.sequence = summary->sequence;
    const std::string extension = picture.extension().string();
    frame.format = extension == ".png" ? "png" : (extension == ".jpg" || extension == ".jpeg") ? "jpeg" : extension == ".fits" ? "fits" : "tiff16";
    frame.bytes = summary->bytes;
    frame.sha256 = summary->sha256;
    frame.width = summary->width;
    frame.height = summary->height;
    frame.exposure_ms = summary->exposure_ms;
    if (summary->sun) {
        frame.sun_elevation_deg = summary->sun->elevation_deg;
        frame.sun_azimuth_deg = summary->sun->azimuth_deg;
    }
    if (document.contains("statistics") && document["statistics"].is_object()) {
        const nlohmann::json& statistics = document["statistics"];
        if (statistics.contains("mean") && statistics["mean"].is_number()) {
            frame.mean = statistics["mean"].get<double>();
        }
        if (statistics.contains("clipped_fraction") && statistics["clipped_fraction"].is_number()) {
            frame.clipped_fraction = statistics["clipped_fraction"].get<double>();
        }
    }
    if (document.contains("camera") && document["camera"].is_object() && document["camera"].contains("gain") &&
        document["camera"]["gain"].is_number()) {
        frame.gain = document["camera"]["gain"].get<double>();
    }
    frame.simulated = summary->simulated;
    return frame;
}

Catalogue::Catalogue(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Catalogue::~Catalogue() = default;

const std::filesystem::path& Catalogue::file() const
{
    return impl_->file;
}

Expected<std::unique_ptr<Catalogue>> Catalogue::open(const std::filesystem::path& file)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        return fail(ErrorCode::Unsupported, "the QSQLITE driver is not available (Qt SQL plugin missing)");
    }
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    auto impl = std::make_unique<Impl>();
    impl->connection_name = QStringLiteral("cloudscope-catalogue-%1").arg(connection_counter.fetch_add(1));
    impl->db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), impl->connection_name);
    impl->db.setDatabaseName(qstring(file));
    impl->file = file;
    if (!impl->db.open()) {
        const std::string text = utf8(impl->db.lastError().text());
        return fail(ErrorCode::Io, fmt::format("could not open catalogue {}: {}", file.string(), text));
    }
    for (const char* statement :
         {"PRAGMA journal_mode = WAL", "PRAGMA synchronous = NORMAL", "PRAGMA foreign_keys = ON",
          "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
          "INSERT OR IGNORE INTO meta (key, value) VALUES ('schema_version', '1')",
          "CREATE TABLE IF NOT EXISTS sessions (id TEXT PRIMARY KEY, site_id TEXT NOT NULL, latitude_deg REAL, "
          "longitude_deg REAL, altitude_m REAL, camera_id TEXT, camera_name TEXT, started_ms INTEGER NOT NULL, "
          "ended_ms INTEGER, frames INTEGER NOT NULL DEFAULT 0, bytes INTEGER NOT NULL DEFAULT 0, notes TEXT, folder TEXT)",
          "CREATE TABLE IF NOT EXISTS frames (id INTEGER PRIMARY KEY, session_id TEXT NOT NULL, path TEXT NOT NULL UNIQUE, "
          "utc_ms INTEGER NOT NULL, mjd REAL, sequence INTEGER, format TEXT, bytes INTEGER NOT NULL DEFAULT 0, sha256 TEXT, "
          "width INTEGER, height INTEGER, exposure_ms REAL, gain REAL, mean REAL, clipped_fraction REAL, "
          "sun_elevation_deg REAL, sun_azimuth_deg REAL, profile TEXT, simulated INTEGER NOT NULL DEFAULT 0)",
          "CREATE INDEX IF NOT EXISTS frames_utc ON frames (utc_ms)",
          "CREATE INDEX IF NOT EXISTS frames_session_utc ON frames (session_id, utc_ms)"}) {
        if (auto done = impl->exec(statement); !done) {
            return fail(done.error());
        }
    }
    return std::unique_ptr<Catalogue>(new Catalogue(std::move(impl)));
}

Expected<void> Catalogue::add_session(const SessionInfo& session)
{
    QSqlQuery query(impl_->db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sessions (id, site_id, latitude_deg, longitude_deg, altitude_m, camera_id, camera_name, "
        "started_ms, ended_ms, frames, bytes, notes, folder) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(qstring(session.id));
    query.addBindValue(qstring(session.site.id));
    query.addBindValue(session.site.latitude_deg);
    query.addBindValue(session.site.longitude_deg);
    query.addBindValue(session.site.altitude_m);
    query.addBindValue(qstring(session.camera_id));
    query.addBindValue(qstring(session.camera_name));
    query.addBindValue(static_cast<qlonglong>(to_unix_ms(session.started)));
    query.addBindValue(session.ended ? QVariant(static_cast<qlonglong>(to_unix_ms(*session.ended)))
                                     : QVariant(QMetaType(QMetaType::LongLong)));
    query.addBindValue(session.frames);
    query.addBindValue(static_cast<qulonglong>(session.bytes));
    query.addBindValue(qstring(session.notes));
    query.addBindValue(qstring(session.folder));
    if (!query.exec()) {
        return fail(sql_error(query, "add session"));
    }
    return {};
}

Expected<std::vector<SessionInfo>> Catalogue::sessions() const
{
    QSqlQuery query(impl_->db);
    if (!query.exec(QStringLiteral("SELECT %1 FROM sessions ORDER BY started_ms").arg(QString::fromLatin1(kSessionColumns)))) {
        return fail(sql_error(query, "list sessions"));
    }
    std::vector<SessionInfo> out;
    while (query.next()) {
        out.push_back(session_from_row(query));
    }
    return out;
}

Expected<std::optional<SessionInfo>> Catalogue::session(const std::string& id) const
{
    QSqlQuery query(impl_->db);
    query.prepare(QStringLiteral("SELECT %1 FROM sessions WHERE id = ?").arg(QString::fromLatin1(kSessionColumns)));
    query.addBindValue(qstring(id));
    if (!query.exec()) {
        return fail(sql_error(query, "find session"));
    }
    if (!query.next()) {
        return std::optional<SessionInfo>();
    }
    return std::optional<SessionInfo>(session_from_row(query));
}

Expected<std::int64_t> Catalogue::add_frame(const FrameEntry& frame)
{
    QSqlQuery query(impl_->db);
    query.prepare(QString::fromLatin1(kInsertFrame));
    bind_frame(query, frame);
    if (!query.exec()) {
        return fail(sql_error(query, "add frame"));
    }
    return static_cast<std::int64_t>(query.lastInsertId().toLongLong());
}

Expected<void> Catalogue::add_frames(const std::vector<FrameEntry>& frames)
{
    if (!impl_->db.transaction()) {
        return fail(ErrorCode::Io, "catalogue: could not begin a transaction");
    }
    QSqlQuery query(impl_->db);
    query.prepare(QString::fromLatin1(kInsertFrame));
    for (const FrameEntry& frame : frames) {
        bind_frame(query, frame);
        if (!query.exec()) {
            const Error error = sql_error(query, "add frames");
            impl_->db.rollback();
            return fail(error);
        }
    }
    if (!impl_->db.commit()) {
        return fail(ErrorCode::Io, "catalogue: could not commit");
    }
    return {};
}

Expected<std::vector<FrameEntry>> Catalogue::frames(const FrameQuery& wanted) const
{
    QString sql = QStringLiteral("SELECT %1 FROM frames WHERE 1 = 1").arg(QString::fromLatin1(kFrameColumns));
    if (wanted.session_id) {
        sql += QStringLiteral(" AND session_id = ?");
    }
    if (wanted.from) {
        sql += QStringLiteral(" AND utc_ms >= ?");
    }
    if (wanted.to) {
        sql += QStringLiteral(" AND utc_ms < ?");
    }
    sql += wanted.newest_first ? QStringLiteral(" ORDER BY utc_ms DESC, id DESC") : QStringLiteral(" ORDER BY utc_ms ASC, id ASC");
    sql += QStringLiteral(" LIMIT ?");
    QSqlQuery query(impl_->db);
    query.setForwardOnly(true);  // no result cache: rows are read once, straight into FrameEntry
    query.prepare(sql);
    if (wanted.session_id) {
        query.addBindValue(qstring(*wanted.session_id));
    }
    if (wanted.from) {
        query.addBindValue(static_cast<qlonglong>(to_unix_ms(*wanted.from)));
    }
    if (wanted.to) {
        query.addBindValue(static_cast<qlonglong>(to_unix_ms(*wanted.to)));
    }
    query.addBindValue(static_cast<qlonglong>(wanted.limit));
    if (!query.exec()) {
        return fail(sql_error(query, "query frames"));
    }
    std::vector<FrameEntry> out;
    while (query.next()) {
        out.push_back(frame_from_row(query));
    }
    return out;
}

Expected<std::optional<FrameEntry>> Catalogue::frame_at(const std::filesystem::path& path) const
{
    QSqlQuery query(impl_->db);
    query.prepare(QStringLiteral("SELECT %1 FROM frames WHERE path = ?").arg(QString::fromLatin1(kFrameColumns)));
    query.addBindValue(path_text(path));
    if (!query.exec()) {
        return fail(sql_error(query, "find frame"));
    }
    if (!query.next()) {
        return std::optional<FrameEntry>();
    }
    return std::optional<FrameEntry>(frame_from_row(query));
}

Expected<std::uint64_t> Catalogue::count(const std::optional<std::string>& session_id) const
{
    QSqlQuery query(impl_->db);
    if (session_id) {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM frames WHERE session_id = ?"));
        query.addBindValue(qstring(*session_id));
    } else {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM frames"));
    }
    if (!query.exec() || !query.next()) {
        return fail(sql_error(query, "count frames"));
    }
    return query.value(0).toULongLong();
}

Expected<std::uint64_t> Catalogue::total_bytes() const
{
    QSqlQuery query(impl_->db);
    if (!query.exec(QStringLiteral("SELECT COALESCE(SUM(bytes), 0) FROM frames")) || !query.next()) {
        return fail(sql_error(query, "sum bytes"));
    }
    return query.value(0).toULongLong();
}

Expected<RetentionResult> Catalogue::apply_retention(const RetentionPolicy& policy, UtcTime now, bool delete_files)
{
    RetentionResult result;
    if (!policy.max_bytes && !policy.max_age) {
        return result;
    }
    const auto total = total_bytes();
    if (!total) {
        return fail(total.error());
    }
    std::uint64_t remaining = *total;
    const std::int64_t cutoff_ms = policy.max_age ? to_unix_ms(now - *policy.max_age) : std::numeric_limits<std::int64_t>::min();

    struct Victim {
        std::int64_t id;
        std::filesystem::path path;
        std::uint64_t bytes;
    };
    std::vector<Victim> victims;
    {
        QSqlQuery query(impl_->db);
        if (!query.exec(QStringLiteral("SELECT id, path, bytes, utc_ms FROM frames ORDER BY utc_ms ASC, id ASC"))) {
            return fail(sql_error(query, "retention scan"));
        }
        while (query.next()) {
            const std::int64_t utc_ms = query.value(3).toLongLong();
            const bool too_old = policy.max_age && utc_ms < cutoff_ms;
            const bool too_big = policy.max_bytes && remaining > *policy.max_bytes;
            if (!too_old && !too_big) {
                if (!policy.max_bytes || remaining <= *policy.max_bytes) {
                    break;  // the rest is newer and within budget
                }
                continue;
            }
            const auto bytes = query.value(2).toULongLong();
            victims.push_back(Victim{.id = query.value(0).toLongLong(), .path = path_of(query.value(1).toString()), .bytes = bytes});
            remaining -= std::min<std::uint64_t>(remaining, bytes);
        }
    }
    if (victims.empty()) {
        return result;
    }
    if (!impl_->db.transaction()) {
        return fail(ErrorCode::Io, "catalogue: could not begin a transaction");
    }
    QSqlQuery remove(impl_->db);
    remove.prepare(QStringLiteral("DELETE FROM frames WHERE id = ?"));
    for (const Victim& victim : victims) {
        if (delete_files) {
            std::error_code error;
            if (!std::filesystem::remove(victim.path, error)) {
                ++result.missing_files;
            }
            std::filesystem::remove(std::filesystem::path(victim.path.native() + std::filesystem::path(".json").native()), error);
        }
        remove.addBindValue(static_cast<qlonglong>(victim.id));
        if (!remove.exec()) {
            const Error error = sql_error(remove, "retention delete");
            impl_->db.rollback();
            return fail(error);
        }
        ++result.removed_frames;
        result.removed_bytes += victim.bytes;
    }
    if (!impl_->db.commit()) {
        return fail(ErrorCode::Io, "catalogue: could not commit");
    }
    return result;
}

Expected<std::uint32_t> Catalogue::index_folder(const std::filesystem::path& folder, const std::string& session_id)
{
    std::error_code error;
    std::vector<FrameEntry> entries;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        std::filesystem::path picture = entry.path();
        picture.replace_extension();  // "<name>.png.json" -> "<name>.png"
        if (picture.extension().empty() || !std::filesystem::exists(picture)) {
            continue;  // a manifest or an orphan sidecar
        }
        if (auto known = frame_at(picture); known && *known) {
            continue;
        }
        auto frame = frame_entry_from_sidecar(entry.path(), session_id);
        if (!frame) {
            continue;  // not a frame sidecar
        }
        entries.push_back(std::move(*frame));
    }
    if (error) {
        return fail(ErrorCode::NotFound, fmt::format("could not read folder {}", folder.string()));
    }
    if (auto added = add_frames(entries); !added) {
        return fail(added.error());
    }
    return static_cast<std::uint32_t>(entries.size());
}

}  // namespace cloudscope
