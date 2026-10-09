#include "cloudscope/capture/recording.hpp"

#include "cloudscope/capture/calibration_frames.hpp"
#include "cloudscope/capture/decode.hpp"
#include "cloudscope/common/build_info.hpp"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QString>
#include <fitsio.h>
#include <fmt/format.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cloudscope {

std::string_view extension(ImageFileFormat format)
{
    switch (format) {
    case ImageFileFormat::Png:
        return ".png";
    case ImageFileFormat::Tiff16:
        return ".tiff";
    case ImageFileFormat::Jpeg:
        return ".jpg";
    case ImageFileFormat::Fits:
        return ".fits";
    }
    return ".bin";
}

std::string_view to_string(ImageFileFormat format)
{
    switch (format) {
    case ImageFileFormat::Png:
        return "png";
    case ImageFileFormat::Tiff16:
        return "tiff16";
    case ImageFileFormat::Jpeg:
        return "jpeg";
    case ImageFileFormat::Fits:
        return "fits";
    }
    return "unknown";
}

Expected<ImageFileFormat> image_format_from_string(std::string_view text)
{
    for (const ImageFileFormat format :
         {ImageFileFormat::Png, ImageFileFormat::Tiff16, ImageFileFormat::Jpeg, ImageFileFormat::Fits}) {
        if (text == to_string(format)) {
            return format;
        }
    }
    if (text == "tiff" || text == "tif") {
        return ImageFileFormat::Tiff16;
    }
    if (text == "jpg") {
        return ImageFileFormat::Jpeg;
    }
    return fail(ErrorCode::InvalidArgument, fmt::format("unknown image format '{}'", text));
}

std::string fits_date_obs(UtcTime time)
{
    // FITS 4.0: "YYYY-MM-DDThh:mm:ss.sss" in the TIMESYS scale, without an offset suffix.
    std::string text = format_iso8601(time);
    text.resize(text.size() - std::string_view("+00:00").size());
    return text;
}

double modified_julian_date(UtcTime time)
{
    return 40587.0 + static_cast<double>(to_unix_ms(time)) / 86'400'000.0;
}

Expected<std::string> file_sha256(const std::filesystem::path& file)
{
    QFile input(QString::fromStdU16String(file.u16string()));
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", file.string()));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) {
        return fail(ErrorCode::Io, fmt::format("could not hash {}", file.string()));
    }
    return hash.result().toHex().toStdString();
}

namespace {

std::filesystem::path with_suffix(const std::filesystem::path& file, const char* suffix)
{
    return std::filesystem::path(file.native() + std::filesystem::path(suffix).native());
}

// "frame.png" -> "frame.part.png": the extension stays, so encoders that look at it still work.
std::filesystem::path partial_name(const std::filesystem::path& file)
{
    return file.parent_path() /
           (file.stem().native() + std::filesystem::path(".part").native() + file.extension().native());
}

// Moves a finished temporary file into place, replacing what was there.
Expected<void> move_into_place(const std::filesystem::path& temporary, const std::filesystem::path& target)
{
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(target, error);
        std::filesystem::rename(temporary, target, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail(ErrorCode::Io, fmt::format("could not move {} into place", temporary.string()));
        }
    }
    return {};
}

std::string fits_message(int status)
{
    std::array<char, FLEN_STATUS> text{};
    fits_get_errstatus(status, text.data());
    return text.data();
}

// A FITS file held in memory. cfitsio names files with narrow C strings and parses them for its own extended
// syntax, so it cannot open every folder a user may choose (non-ASCII names on Windows, brackets); CloudScope
// therefore lets cfitsio work on a memory buffer and moves the bytes to and from disk itself.
class MemoryFits {
public:
    MemoryFits() = default;
    ~MemoryFits()
    {
        close();
        std::free(buffer_);  // NOLINT(cppcoreguidelines-no-malloc)
    }
    MemoryFits(const MemoryFits&) = delete;
    MemoryFits& operator=(const MemoryFits&) = delete;
    MemoryFits(MemoryFits&&) = delete;
    MemoryFits& operator=(MemoryFits&&) = delete;

    // An empty file to write into.
    [[nodiscard]] Expected<void> create()
    {
        size_ = kFitsBlock;
        buffer_ = std::malloc(size_);  // NOLINT(cppcoreguidelines-no-malloc)
        int status = 0;
        fits_create_memfile(&file_, &buffer_, &size_, 0, std::realloc, &status);
        if (status != 0) {
            return fail(ErrorCode::Io, fmt::format("could not create a FITS file in memory: {}", fits_message(status)));
        }
        return {};
    }

    // The contents of `path`, opened for reading.
    [[nodiscard]] Expected<void> load(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            return fail(ErrorCode::NotFound, fmt::format("could not open {}", path.string()));
        }
        const std::streamsize length = in.tellg();
        if (length < static_cast<std::streamsize>(kFitsBlock)) {
            return fail(ErrorCode::Parse, fmt::format("{} is too short to be a FITS file", path.string()));
        }
        size_ = static_cast<std::size_t>(length);
        buffer_ = std::malloc(size_);  // NOLINT(cppcoreguidelines-no-malloc)
        in.seekg(0);
        if (!in.read(static_cast<char*>(buffer_), length)) {
            return fail(ErrorCode::Io, fmt::format("could not read {}", path.string()));
        }
        int status = 0;
        fits_open_memfile(&file_, "", READONLY, &buffer_, &size_, 0, nullptr, &status);
        if (status != 0) {
            return fail(ErrorCode::Parse,
                        fmt::format("{} is not a FITS file: {}", path.string(), fits_message(status)));
        }
        return {};
    }

    // Finishes the file in memory and writes it to `path`.
    [[nodiscard]] Expected<void> save(const std::filesystem::path& path)
    {
        LONGLONG header_start = 0;
        LONGLONG data_start = 0;
        LONGLONG data_end = 0;
        int status = 0;
        fits_get_hduaddrll(file_, &header_start, &data_start, &data_end, &status);
        if (status != 0) {
            return fail(ErrorCode::Io, fmt::format("could not measure the FITS file: {}", fits_message(status)));
        }
        close();  // pads the last block and flushes into the buffer
        const auto blocks = static_cast<std::size_t>((data_end + static_cast<LONGLONG>(kFitsBlock) - 1) /
                                                     static_cast<LONGLONG>(kFitsBlock));
        const std::size_t length = std::min(blocks * kFitsBlock, size_);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.write(static_cast<const char*>(buffer_), static_cast<std::streamsize>(length))) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", path.string()));
        }
        return {};
    }

    void close()
    {
        if (file_ != nullptr) {
            int status = 0;
            fits_close_file(file_, &status);
            file_ = nullptr;
        }
    }

    [[nodiscard]] fitsfile* file() const { return file_; }

private:
    static constexpr std::size_t kFitsBlock = 2880;
    fitsfile* file_ = nullptr;
    void* buffer_ = nullptr;
    std::size_t size_ = 0;
};

void write_key_string(fitsfile* file, const char* key, const std::string& value, const char* comment, int& status)
{
    std::string text = value.substr(0, 68);
    fits_write_key(file, TSTRING, key, text.data(), comment, &status);
}

void write_key_double(fitsfile* file, const char* key, double value, const char* comment, int& status)
{
    fits_write_key(file, TDOUBLE, key, &value, comment, &status);
}

void write_key_bool(fitsfile* file, const char* key, bool value, const char* comment, int& status)
{
    int logical = value ? 1 : 0;
    fits_write_key(file, TLOGICAL, key, &logical, comment, &status);
}

Expected<void> write_fits(const cv::Mat& image, const std::filesystem::path& file, const CaptureRecord& record)
{
    if ((image.depth() != CV_8U && image.depth() != CV_16U) || (image.channels() != 1 && image.channels() != 3)) {
        return fail(ErrorCode::InvalidArgument, "FITS needs an 8- or 16-bit image with one or three channels");
    }
    MemoryFits fits;
    if (auto created = fits.create(); !created) {
        return created;
    }
    int status = 0;
    const int channels = image.channels();
    std::array<long, 3> axes = {image.cols, image.rows, channels};
    fits_create_img(fits.file(), USHORT_IMG, channels == 3 ? 3 : 2, axes.data(), &status);
    if (status != 0) {
        return fail(ErrorCode::Io, fmt::format("could not create the FITS image: {}", fits_message(status)));
    }
    // Planes (R, G, B for colour), rows written bottom-up.
    cv::Mat wide;
    if (image.depth() == CV_8U) {
        image.convertTo(wide, CV_MAKETYPE(CV_16U, channels), 257.0);
    } else {
        wide = image;
    }
    std::vector<cv::Mat> planes;
    if (channels == 3) {
        cv::split(wide, planes);
        std::swap(planes[0], planes[2]);  // BGR -> RGB
    } else {
        planes.push_back(wide);
    }
    const std::size_t plane_pixels = static_cast<std::size_t>(image.cols) * static_cast<std::size_t>(image.rows);
    std::vector<std::uint16_t> data(plane_pixels * static_cast<std::size_t>(channels));
    for (std::size_t p = 0; p < planes.size(); ++p) {
        for (int row = 0; row < image.rows; ++row) {
            const int source_row = image.rows - 1 - row;  // bottom-up
            std::memcpy(
                data.data() + p * plane_pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(image.cols),
                planes[p].ptr<std::uint16_t>(source_row), static_cast<std::size_t>(image.cols) * sizeof(std::uint16_t));
        }
    }
    fits_write_img(fits.file(), TUSHORT, 1, static_cast<LONGLONG>(data.size()), data.data(), &status);
    if (status != 0) {
        return fail(ErrorCode::Io, fmt::format("could not write the FITS image: {}", fits_message(status)));
    }

    write_key_string(fits.file(), "DATE-OBS", fits_date_obs(record.info.captured.utc), "UTC time the frame arrived",
                     status);
    write_key_string(fits.file(), "TIMESYS", "UTC", "time scale of DATE-OBS", status);
    write_key_double(fits.file(), "MJD-OBS", modified_julian_date(record.info.captured.utc),
                     "Modified Julian Date of DATE-OBS", status);
    write_key_string(fits.file(), "TIMESRC", std::string(to_string(record.info.captured.source)),
                     "what disciplines the host clock", status);
    if (record.exposure_ms) {
        write_key_double(fits.file(), "EXPTIME", *record.exposure_ms / 1000.0,
                         "[s] exposure time reported by the camera", status);
    }
    if (record.gain) {
        write_key_double(fits.file(), "GAIN", *record.gain, "gain setting reported by the camera (driver scale)",
                         status);
    }
    if (record.site) {
        write_key_double(fits.file(), "OBSGEO-B", record.site->latitude_deg, "[deg] geodetic latitude of the site",
                         status);
        write_key_double(fits.file(), "OBSGEO-L", record.site->longitude_deg,
                         "[deg] longitude of the site, east positive", status);
        write_key_double(fits.file(), "OBSGEO-H", record.site->altitude_m, "[m] altitude of the site", status);
        write_key_double(fits.file(), "SITELAT", record.site->latitude_deg, "[deg] site latitude", status);
        write_key_double(fits.file(), "SITELONG", record.site->longitude_deg, "[deg] site longitude, east positive",
                         status);
        write_key_double(fits.file(), "SITEELEV", record.site->altitude_m, "[m] site elevation above sea level",
                         status);
        write_key_string(fits.file(), "SITEID", record.site->id, "site identifier", status);
    }
    if (record.pointing) {
        write_key_double(fits.file(), "CENTALT", record.pointing->elevation_deg, "[deg] elevation of the optical axis",
                         status);
        write_key_double(fits.file(), "CENTAZ", record.pointing->azimuth_deg, "[deg] azimuth of the optical axis",
                         status);
        write_key_string(fits.file(), "POINTSRC", record.pointing->source, "source of the pointing", status);
    }
    if (record.sun) {
        write_key_double(fits.file(), "SUNALT", record.sun->elevation_deg, "[deg] Sun elevation at DATE-OBS", status);
        write_key_double(fits.file(), "SUNAZ", record.sun->azimuth_deg, "[deg] Sun azimuth at DATE-OBS", status);
    }
    write_key_string(fits.file(), "ROWORDER", "BOTTOM-UP", "first row of the data is the bottom of the picture",
                     status);
    write_key_string(fits.file(), "INSTRUME", record.camera_name, "camera", status);
    write_key_string(fits.file(), "DEVICEID", record.camera_id, "CloudScope device id", status);
    write_key_bool(fits.file(), "SIMULATE", record.info.simulated, "T: synthetic data, not a measurement", status);
    if (!record.calibration_id.empty()) {
        write_key_string(fits.file(), "CALIB", record.calibration_id, "calibration set applied or to apply", status);
    }
    if (!record.session_id.empty()) {
        write_key_string(fits.file(), "SESSION", record.session_id, "CloudScope session", status);
    }
    const BuildInfo& build = build_info();
    write_key_string(fits.file(), "SWCREATE", fmt::format("CloudScope {} ({})", build.version, build.git_revision),
                     "software that wrote the file", status);
    write_key_string(fits.file(), "CREATOR", fmt::format("CloudScope {}", build.version),
                     "software that wrote the file", status);
    write_key_string(fits.file(), "BUNIT", "ADU", "camera data numbers (8-bit data scaled by 257)", status);
    if (status != 0) {
        return fail(ErrorCode::Io, fmt::format("could not write the FITS header: {}", fits_message(status)));
    }
    return fits.save(file);
}

}  // namespace

Expected<WrittenFile> write_picture(const cv::Mat& image, ImageFileFormat format, const std::filesystem::path& file,
                                    const CaptureRecord& record, int jpeg_quality)
{
    if (image.empty() || (image.depth() != CV_8U && image.depth() != CV_16U)) {
        return fail(ErrorCode::InvalidArgument, "the picture must be an 8- or 16-bit image");
    }
    WrittenFile written;
    written.path = file;
    written.width = image.cols;
    written.height = image.rows;
    written.channels = image.channels();
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    const std::filesystem::path partial = partial_name(file);
    switch (format) {
    case ImageFileFormat::Png: {
        written.bit_depth = image.depth() == CV_16U ? 16 : 8;
        if (auto done = write_image(partial, image); !done) {
            return fail(done.error());
        }
        break;
    }
    case ImageFileFormat::Tiff16: {
        cv::Mat wide;
        if (image.depth() == CV_8U) {
            image.convertTo(wide, CV_MAKETYPE(CV_16U, image.channels()), 257.0);
        } else {
            wide = image;
        }
        written.bit_depth = 16;
        if (auto done = write_image(partial, wide); !done) {
            return fail(done.error());
        }
        break;
    }
    case ImageFileFormat::Jpeg: {
        if (image.depth() != CV_8U) {
            return fail(ErrorCode::InvalidArgument, "JPEG takes 8-bit pictures only");
        }
        written.bit_depth = 8;
        std::vector<std::uint8_t> encoded;
        if (!cv::imencode(".jpg", image, encoded, {cv::IMWRITE_JPEG_QUALITY, std::clamp(jpeg_quality, 1, 100)})) {
            return fail(ErrorCode::Io, "JPEG encoding failed");
        }
        std::ofstream out(partial, std::ios::binary);
        if (!out.write(
                reinterpret_cast<const char*>(encoded.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                static_cast<std::streamsize>(encoded.size()))) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", partial.string()));
        }
        break;
    }
    case ImageFileFormat::Fits: {
        written.bit_depth = 16;
        if (auto done = write_fits(image, partial, record); !done) {
            return fail(done.error());
        }
        break;
    }
    }
    if (auto moved = move_into_place(partial, file); !moved) {
        return fail(moved.error());
    }
    std::error_code error;
    written.bytes = std::filesystem::file_size(file, error);
    if (error) {
        return fail(ErrorCode::Io, fmt::format("could not read back the size of {}", file.string()));
    }
    auto hash = file_sha256(file);
    if (!hash) {
        return fail(hash.error());
    }
    written.sha256 = *hash;
    return written;
}

Expected<WrittenFile> write_jpeg_bytes(std::span<const std::byte> jpeg, const std::filesystem::path& file)
{
    const auto info = jpeg_info(jpeg);
    if (!info) {
        return fail(ErrorCode::InvalidArgument, "the frame is not a JPEG");
    }
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    const std::filesystem::path partial = partial_name(file);
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out.write(
                reinterpret_cast<const char*>(jpeg.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                static_cast<std::streamsize>(jpeg.size()))) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", partial.string()));
        }
    }
    if (auto moved = move_into_place(partial, file); !moved) {
        return fail(moved.error());
    }
    WrittenFile written;
    written.path = file;
    written.bytes = jpeg.size();
    written.width = info->width;
    written.height = info->height;
    written.channels = 3;
    written.bit_depth = 8;
    auto hash = file_sha256(file);
    if (!hash) {
        return fail(hash.error());
    }
    written.sha256 = *hash;
    return written;
}

nlohmann::json sidecar_json(const CaptureRecord& record, const WrittenFile& file, ImageFileFormat format)
{
    nlohmann::json controls = nlohmann::json::object();
    for (const auto& [name, setting] : record.controls) {
        controls[name] = {{"value", setting.value}, {"automatic", setting.automatic}};
    }
    nlohmann::json camera{{"id", record.camera_id},
                          {"name", record.camera_name},
                          {"mode",
                           {{"width", record.mode.width},
                            {"height", record.mode.height},
                            {"format", std::string(to_string(record.mode.format))},
                            {"fps", record.mode.fps}}},
                          {"controls", controls}};
    if (record.exposure_ms) {
        camera["exposure_ms"] = *record.exposure_ms;
    }
    if (record.gain) {
        camera["gain"] = *record.gain;
    }
    if (!record.calibration_id.empty()) {
        camera["calibration_id"] = record.calibration_id;
    }
    const BuildInfo& build = build_info();
    nlohmann::json out{
        {"schema", "cloudscope.frame/1"},
        {"file",
         {{"name", file.path.filename().string()},
          {"format", std::string(to_string(format))},
          {"bytes", file.bytes},
          {"sha256", file.sha256},
          {"width", file.width},
          {"height", file.height},
          {"channels", file.channels},
          {"bit_depth", file.bit_depth}}},
        {"capture",
         {{"utc", format_iso8601(record.info.captured.utc)},
          {"utc_unix_ms", to_unix_ms(record.info.captured.utc)},
          {"mjd", modified_julian_date(record.info.captured.utc)},
          {"monotonic_ns",
           std::chrono::duration_cast<std::chrono::nanoseconds>(record.info.captured.monotonic.time_since_epoch())
               .count()},
          {"sequence", record.info.sequence},
          {"time_source", std::string(to_string(record.info.captured.source))},
          {"simulated", record.info.simulated}}},
        {"camera", camera},
        {"software", {{"name", "CloudScope"}, {"version", build.version}, {"git_revision", build.git_revision}}},
    };
    if (record.site) {
        out["site"] = {{"id", record.site->id},
                       {"latitude_deg", record.site->latitude_deg},
                       {"longitude_deg", record.site->longitude_deg},
                       {"altitude_m", record.site->altitude_m}};
    }
    if (record.pointing) {
        out["pointing"] = {{"azimuth_deg", record.pointing->azimuth_deg},
                           {"elevation_deg", record.pointing->elevation_deg},
                           {"source", record.pointing->source}};
    }
    if (record.sun) {
        out["sun"] = {{"azimuth_deg", record.sun->azimuth_deg}, {"elevation_deg", record.sun->elevation_deg}};
    }
    if (record.statistics) {
        const FrameStatistics& s = *record.statistics;
        nlohmann::json sun{{"found", s.sun.found}};
        if (s.sun.found) {
            sun["x"] = s.sun.x;
            sun["y"] = s.sun.y;
            sun["radius_px"] = s.sun.radius_px;
            sun["area_px"] = s.sun.area_px;
            sun["fill"] = s.sun.fill;
        }
        out["statistics"] = {{"mean", s.mean},
                             {"median", s.median},
                             {"std_dev", s.std_dev},
                             {"clipped_fraction", s.clipped_fraction},
                             {"dark_fraction", s.dark_fraction},
                             {"noise_sigma", s.noise_sigma},
                             {"sharpness", s.sharpness},
                             {"sun", sun}};
    }
    if (!record.session_id.empty()) {
        out["session_id"] = record.session_id;
    }
    return out;
}

Expected<std::filesystem::path> write_sidecar(const std::filesystem::path& picture, const nlohmann::json& sidecar)
{
    std::filesystem::path target = with_suffix(picture, ".json");
    const std::filesystem::path temporary = with_suffix(picture, ".json.part");
    {
        std::ofstream out(temporary, std::ios::binary);
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
        out << sidecar.dump(2) << "\n";
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
    }
    if (auto moved = move_into_place(temporary, target); !moved) {
        return fail(moved.error());
    }
    return target;
}

namespace {

std::optional<double> json_number(const nlohmann::json& node, const char* key)
{
    if (node.is_object() && node.contains(key) && node[key].is_number()) {
        return node[key].get<double>();
    }
    return std::nullopt;
}

std::string json_text(const nlohmann::json& node, const char* key)
{
    if (node.is_object() && node.contains(key) && node[key].is_string()) {
        return node[key].get<std::string>();
    }
    return {};
}

// The parts that differ between the two schemas: where the file name, hash, size and picture size live.
Expected<void> read_file_part(const nlohmann::json& document, SidecarSummary& out)
{
    if (out.schema == "cloudscope.frame/1") {
        const nlohmann::json& file = document.contains("file") ? document["file"] : nlohmann::json::object();
        out.file_name = json_text(file, "name");
        out.sha256 = json_text(file, "sha256");
        out.bytes = static_cast<std::uint64_t>(json_number(file, "bytes").value_or(0.0));
        out.width = static_cast<int>(json_number(file, "width").value_or(0.0));
        out.height = static_cast<int>(json_number(file, "height").value_or(0.0));
        if (document.contains("camera")) {
            out.exposure_ms = json_number(document["camera"], "exposure_ms");
        }
        return {};
    }
    if (out.schema == "cloudscope.sky_logger.frame/1") {
        out.file_name = json_text(document, "file");
        out.sha256 = json_text(document, "sha256");
        out.bytes = static_cast<std::uint64_t>(json_number(document, "bytes").value_or(0.0));
        if (document.contains("image")) {
            out.width = static_cast<int>(json_number(document["image"], "width").value_or(0.0));
            out.height = static_cast<int>(json_number(document["image"], "height").value_or(0.0));
        }
        // The interim logger stored the driver's raw read-back, not milliseconds: left empty on purpose.
        return {};
    }
    return fail(ErrorCode::Unsupported, fmt::format("unknown sidecar schema '{}'", out.schema));
}

// Site, pointing and Sun have the same shape in both schemas, except the site's coordinate names.
void read_place_part(const nlohmann::json& document, SidecarSummary& out)
{
    if (document.contains("site") && document["site"].is_object()) {
        const nlohmann::json& site = document["site"];
        auto latitude = json_number(site, "latitude_deg");
        if (!latitude.has_value()) {
            latitude = json_number(site, "latitude");
        }
        auto longitude = json_number(site, "longitude_deg");
        if (!longitude.has_value()) {
            longitude = json_number(site, "longitude");
        }
        if (latitude.has_value() && longitude.has_value()) {
            out.site = SiteInfo{.id = json_text(site, "id"),
                                .latitude_deg = latitude.value(),
                                .longitude_deg = longitude.value(),
                                .altitude_m = json_number(site, "altitude_m").value_or(0.0)};
        }
    }
    if (document.contains("pointing") && document["pointing"].is_object()) {
        const nlohmann::json& pointing = document["pointing"];
        const auto azimuth = json_number(pointing, "azimuth_deg");
        const auto elevation = json_number(pointing, "elevation_deg");
        if (azimuth.has_value() && elevation.has_value()) {
            out.pointing = PointingInfo{.azimuth_deg = azimuth.value(),
                                        .elevation_deg = elevation.value(),
                                        .source = json_text(pointing, "source")};
        }
    }
    if (document.contains("sun") && document["sun"].is_object()) {
        const nlohmann::json& sun = document["sun"];
        const auto azimuth = json_number(sun, "azimuth_deg");
        const auto elevation = json_number(sun, "elevation_deg");
        if (azimuth.has_value() && elevation.has_value()) {
            out.sun = SunInfo{.azimuth_deg = azimuth.value(), .elevation_deg = elevation.value()};
        }
    }
}

}  // namespace

Expected<SidecarSummary> read_sidecar(const nlohmann::json& document)
{
    if (!document.is_object() || !document.contains("schema") || !document["schema"].is_string()) {
        return fail(ErrorCode::Parse, "not a CloudScope sidecar: no schema");
    }
    SidecarSummary out;
    out.schema = document["schema"].get<std::string>();
    const nlohmann::json& capture = document.contains("capture") ? document["capture"] : nlohmann::json::object();
    const std::string utc_text = json_text(capture, "utc");
    const auto utc = parse_iso8601(utc_text);
    if (!utc) {
        return fail(ErrorCode::Parse,
                    fmt::format("sidecar capture.utc '{}' is not an ISO 8601 time with offset", utc_text));
    }
    out.utc = *utc;
    if (const auto sequence = json_number(capture, "sequence"); sequence.has_value()) {
        out.sequence = static_cast<std::uint64_t>(std::max(0.0, sequence.value()));
    }
    if (capture.contains("simulated") && capture["simulated"].is_boolean()) {
        out.simulated = capture["simulated"].get<bool>();
    }
    if (auto file = read_file_part(document, out); !file) {
        return fail(file.error());
    }
    read_place_part(document, out);
    return out;
}

const JsonSchema& sidecar_schema()
{
    static const JsonSchema schema = [] {
        QFile file(QStringLiteral(":/cloudscope/sidecar.schema.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("the sidecar schema is missing from the resources");
        }
        const QByteArray bytes = file.readAll();
        auto compiled = JsonSchema::compile(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size()));
        if (!compiled) {
            throw std::runtime_error("the sidecar schema does not compile: " + compiled.error().to_string());
        }
        return *compiled;
    }();
    return schema;
}

Expected<std::map<std::string, std::string>> read_fits_header(const std::filesystem::path& file)
{
    MemoryFits fits;
    if (auto loaded = fits.load(file); !loaded) {
        return fail(loaded.error());
    }
    const std::string name = file.string();
    int status = 0;
    int count = 0;
    fits_get_hdrspace(fits.file(), &count, nullptr, &status);
    std::map<std::string, std::string> header;
    for (int i = 1; i <= count && status == 0; ++i) {
        std::array<char, FLEN_KEYWORD> key{};
        std::array<char, FLEN_VALUE> value{};
        std::array<char, FLEN_COMMENT> comment{};
        fits_read_keyn(fits.file(), i, key.data(), value.data(), comment.data(), &status);
        std::string text = value.data();
        if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
            text = text.substr(1, text.size() - 2);
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
        }
        header[key.data()] = text;
    }
    if (status != 0) {
        return fail(ErrorCode::Parse, fmt::format("could not read the header of {}: {}", name, fits_message(status)));
    }
    return header;
}

Expected<cv::Mat> read_fits_image(const std::filesystem::path& file)
{
    MemoryFits fits;
    if (auto loaded = fits.load(file); !loaded) {
        return fail(loaded.error());
    }
    const std::string name = file.string();
    int status = 0;
    int bitpix = 0;
    int naxis = 0;
    std::array<long, 3> axes = {0, 0, 1};
    fits_get_img_param(fits.file(), 3, &bitpix, &naxis, axes.data(), &status);
    if (status != 0 || naxis < 2 || naxis > 3) {
        return fail(ErrorCode::Parse, fmt::format("{} is not a 2- or 3-axis image", name));
    }
    const int width = static_cast<int>(axes[0]);
    const int height = static_cast<int>(axes[1]);
    const int planes = naxis == 3 ? static_cast<int>(axes[2]) : 1;
    const std::size_t plane_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint16_t> data(plane_pixels * static_cast<std::size_t>(planes));
    int any_null = 0;
    fits_read_img(fits.file(), TUSHORT, 1, static_cast<LONGLONG>(data.size()), nullptr, data.data(), &any_null,
                  &status);
    if (status != 0) {
        return fail(ErrorCode::Parse, fmt::format("could not read the image of {}: {}", name, fits_message(status)));
    }
    std::vector<cv::Mat> channel_mats;
    for (int p = 0; p < planes; ++p) {
        cv::Mat plane(height, width, CV_16UC1);
        for (int row = 0; row < height; ++row) {
            std::memcpy(plane.ptr<std::uint16_t>(height - 1 - row),
                        data.data() + static_cast<std::size_t>(p) * plane_pixels +
                            static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                        static_cast<std::size_t>(width) * sizeof(std::uint16_t));
        }
        channel_mats.push_back(plane);
    }
    if (planes == 3) {
        std::swap(channel_mats[0], channel_mats[2]);  // RGB planes -> BGR
        cv::Mat merged;
        cv::merge(channel_mats, merged);
        return merged;
    }
    return channel_mats.front();
}

}  // namespace cloudscope
