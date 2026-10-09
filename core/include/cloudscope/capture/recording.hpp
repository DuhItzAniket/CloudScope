// Recording of single pictures (P027, FR-REC-01 to FR-REC-04): PNG, 16-bit TIFF, JPEG and FITS, each with a
// JSON sidecar that says everything known about the frame.
//
// FITS files carry the keyword set agreed in docs/research/P006_competitive_analysis.md (DATE-OBS in UTC, TIMESYS,
// MJD-OBS, EXPTIME, GAIN, OBSGEO-B/L/H, SITELAT/SITELONG, CENTALT/CENTAZ, ROWORDER, calibration id) so that Siril,
// ASTAP and DS9 read time, place and pointing without a sidecar; the image rows are written bottom-up with
// ROWORDER = 'BOTTOM-UP', which every viewer shows upright. WCS keywords come with the intrinsic calibration (P031).
//
// The sidecar (`<picture>.json`, schema "cloudscope.frame/1", core/resources/sidecar.schema.json) records the
// file (bytes, SHA-256), the capture time with its source, the camera and the controls in effect, site, pointing,
// Sun position and frame statistics when known, and the software that wrote it. Nothing in it is a guess: values
// the camera reported are stored as reported, and simulated frames are marked.
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/capture/statistics.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/common/json_schema.hpp"
#include "cloudscope/hal/camera.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cloudscope {

struct SiteInfo {
    std::string id;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
};

struct PointingInfo {
    double azimuth_deg = 0.0;
    double elevation_deg = 0.0;
    std::string source;  // "declared_by_operator", "mount", "imu"
};

struct SunInfo {
    double azimuth_deg = 0.0;
    double elevation_deg = 0.0;
};

// Everything known about a frame at the moment it is written.
struct CaptureRecord {
    FrameInfo info;
    hal::CameraMode mode;
    std::string camera_id;
    std::string camera_name;
    std::map<std::string, hal::ControlSetting> controls;  // by control name ("exposure", ...), as read back
    std::optional<double> exposure_ms = std::nullopt;     // the effective exposure, if the camera has the control
    std::optional<double> gain = std::nullopt;
    std::optional<SiteInfo> site = std::nullopt;
    std::optional<PointingInfo> pointing = std::nullopt;
    std::optional<SunInfo> sun = std::nullopt;
    std::optional<FrameStatistics> statistics = std::nullopt;
    std::string calibration_id;  // of the dark/flat/intrinsic calibration applied or to apply; empty if none
    std::string session_id;
};

enum class ImageFileFormat : std::uint8_t { Png, Tiff16, Jpeg, Fits };

[[nodiscard]] std::string_view extension(ImageFileFormat format);  // ".png", ".tiff", ".jpg", ".fits"
[[nodiscard]] std::string_view to_string(ImageFileFormat format);  // "png", "tiff16", "jpeg", "fits"
[[nodiscard]] Expected<ImageFileFormat> image_format_from_string(std::string_view text);

struct WrittenFile {
    std::filesystem::path path;
    std::uint64_t bytes = 0;
    std::string sha256;  // lower-case hex
    int width = 0;
    int height = 0;
    int channels = 0;
    int bit_depth = 0;
};

// Writes `image` (8-bit 1/3 channels, or 16-bit 1 channel; 16-bit 3 channels for TIFF/FITS) as `format` to `file`.
// JPEG takes 8-bit only; TIFF16 and FITS widen 8-bit data to 16 bits (x257) so that the file's scale is honest.
// The record supplies the FITS keywords. Files are written to `<name>.part<ext>` and renamed into place, so a
// reader never sees a half-written picture; an existing file is replaced.
[[nodiscard]] Expected<WrittenFile> write_picture(const cv::Mat& image, ImageFileFormat format,
                                                  const std::filesystem::path& file, const CaptureRecord& record,
                                                  int jpeg_quality = 92);

// Stores a JPEG as the camera sent it (an MJPEG frame), without decoding: the lossless, fastest way to keep what
// a UVC camera produced. InvalidArgument if the bytes are not a JPEG.
[[nodiscard]] Expected<WrittenFile> write_jpeg_bytes(std::span<const std::byte> jpeg,
                                                     const std::filesystem::path& file);

// The sidecar document for a written picture.
[[nodiscard]] nlohmann::json sidecar_json(const CaptureRecord& record, const WrittenFile& file, ImageFileFormat format);

// Writes `<picture>.json` next to the picture (through a temporary file, so a crash never leaves a half-written sidecar).
[[nodiscard]] Expected<std::filesystem::path> write_sidecar(const std::filesystem::path& picture,
                                                            const nlohmann::json& sidecar);

// The sidecar schema compiled from the resources; valid for the whole program.
[[nodiscard]] const JsonSchema& sidecar_schema();

// What a sidecar says, whichever schema wrote it: "cloudscope.frame/1" (this module) or the interim logger's
// "cloudscope.sky_logger.frame/1" (tools/sky_logger, FR-REC-08). Fields a document does not have stay empty.
struct SidecarSummary {
    std::string schema;
    std::string file_name;
    std::string sha256;
    std::uint64_t bytes = 0;
    UtcTime utc{};
    std::uint64_t sequence = 0;
    bool simulated = false;
    std::optional<double> exposure_ms = std::nullopt;
    std::optional<SiteInfo> site = std::nullopt;
    std::optional<PointingInfo> pointing = std::nullopt;
    std::optional<SunInfo> sun = std::nullopt;
    int width = 0;
    int height = 0;
};
// Parse for a document of neither schema or without the fields both schemas require.
[[nodiscard]] Expected<SidecarSummary> read_sidecar(const nlohmann::json& document);

// DATE-OBS as FITS 4.0 wants it: "2026-10-09T10:15:30.123" (UTC, no offset suffix; TIMESYS says the scale).
[[nodiscard]] std::string fits_date_obs(UtcTime time);
// Modified Julian Date of a UTC time.
[[nodiscard]] double modified_julian_date(UtcTime time);
// SHA-256 of a file, lower-case hex; NotFound if it cannot be read.
[[nodiscard]] Expected<std::string> file_sha256(const std::filesystem::path& file);

// Reading back, for tests and tools: the FITS header as keyword -> value text, and the image as OpenCV sees it
// (top-down, BGR for three planes, CV_8U or CV_16U).
[[nodiscard]] Expected<std::map<std::string, std::string>> read_fits_header(const std::filesystem::path& file);
[[nodiscard]] Expected<cv::Mat> read_fits_image(const std::filesystem::path& file);

}  // namespace cloudscope
