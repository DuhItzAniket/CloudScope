#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/recording.hpp>
#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fitsio.h>
#include <opencv2/imgproc.hpp>

#include <array>
#include <fstream>
#include <string>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using Catch::Matchers::WithinAbs;

namespace {

UtcTime at(std::string_view iso)
{
    const auto parsed = parse_iso8601(iso);
    REQUIRE(parsed);
    return *parsed;
}

// 64x48 BGR picture with a gradient and a white square at the top-left corner, so that orientation shows.
cv::Mat test_picture()
{
    cv::Mat image(48, 64, CV_8UC3);
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            image.at<cv::Vec3b>(y, x) =
                cv::Vec3b(static_cast<std::uint8_t>(x * 4), static_cast<std::uint8_t>(y * 5), 40);
        }
    }
    cv::rectangle(image, cv::Rect(0, 0, 8, 8), cv::Scalar(255, 255, 255), cv::FILLED);
    return image;
}

CaptureRecord test_record()
{
    CaptureRecord record;
    record.info.sequence = 42;
    record.info.captured.utc = at("2026-10-09T10:15:30.123Z");
    record.info.captured.monotonic = MonotonicTime(std::chrono::nanoseconds(123'456'789));
    record.info.captured.source = TimeSource::Host;
    record.info.width = 64;
    record.info.height = 48;
    record.info.format = PixelFormat::Mjpeg;
    record.info.simulated = true;
    record.mode = {.width = 64, .height = 48, .format = PixelFormat::Mjpeg, .fps = 30.0};
    record.camera_id = "sim:camera:sky";
    record.camera_name = "Simulated sky camera";
    record.controls["exposure"] = {.value = 15.625, .automatic = false};
    record.controls["gain"] = {.value = 3.0, .automatic = true};
    record.exposure_ms = 15.625;
    record.gain = 3.0;
    record.site = SiteInfo{.id = "blr-roof", .latitude_deg = 12.97, .longitude_deg = 77.59, .altitude_m = 920.0};
    record.pointing = PointingInfo{.azimuth_deg = 0.0, .elevation_deg = 90.0, .source = "declared_by_operator"};
    record.sun = SunInfo{.azimuth_deg = 252.09, .elevation_deg = 36.0};
    record.statistics = compute_statistics(test_picture());
    record.calibration_id = "cal-2026-10-09";
    record.session_id = "20261009T1000Z-blr-roof";
    return record;
}

}  // namespace

TEST_CASE("the FITS file carries the agreed keywords and reads back upright", "[capture][recording]")
{
    const TempWorkspace workspace;
    const cv::Mat picture = test_picture();
    const CaptureRecord record = test_record();
    const std::filesystem::path file = workspace.path("frames/frame_000042.fits");

    const auto written = write_picture(picture, ImageFileFormat::Fits, file, record);
    REQUIRE(outcome(written) == "ok");
    CHECK(written->bit_depth == 16);
    CHECK(written->channels == 3);
    CHECK(written->bytes > 0);
    CHECK(written->sha256.size() == 64);

    const auto header = read_fits_header(file);
    REQUIRE(header);
    const auto& keys = *header;
    CHECK(keys.at("DATE-OBS") == "2026-10-09T10:15:30.123");
    CHECK(keys.at("TIMESYS") == "UTC");
    CHECK_THAT(std::stod(keys.at("MJD-OBS")), WithinAbs(61322.42743, 0.00001));
    CHECK_THAT(std::stod(keys.at("EXPTIME")), WithinAbs(0.015625, 1e-9));
    CHECK_THAT(std::stod(keys.at("GAIN")), WithinAbs(3.0, 1e-9));
    CHECK_THAT(std::stod(keys.at("OBSGEO-B")), WithinAbs(12.97, 1e-9));
    CHECK_THAT(std::stod(keys.at("OBSGEO-L")), WithinAbs(77.59, 1e-9));
    CHECK_THAT(std::stod(keys.at("OBSGEO-H")), WithinAbs(920.0, 1e-9));
    CHECK_THAT(std::stod(keys.at("SITELAT")), WithinAbs(12.97, 1e-9));
    CHECK_THAT(std::stod(keys.at("SITELONG")), WithinAbs(77.59, 1e-9));
    CHECK_THAT(std::stod(keys.at("SITEELEV")), WithinAbs(920.0, 1e-9));
    CHECK(keys.at("SWCREATE").rfind("CloudScope ", 0) == 0);
    CHECK_THAT(std::stod(keys.at("CENTALT")), WithinAbs(90.0, 1e-9));
    CHECK_THAT(std::stod(keys.at("CENTAZ")), WithinAbs(0.0, 1e-9));
    CHECK(keys.at("ROWORDER") == "BOTTOM-UP");
    CHECK(keys.at("CALIB") == "cal-2026-10-09");
    CHECK(keys.at("SIMULATE") == "T");
    CHECK(keys.at("DEVICEID") == "sim:camera:sky");
    CHECK(keys.at("NAXIS") == "3");
    CHECK(keys.at("NAXIS1") == "64");
    CHECK(keys.at("NAXIS2") == "48");
    CHECK(keys.at("NAXIS3") == "3");
    CHECK(keys.at("BITPIX") == "16");

    // Through the reader the picture is upright and scaled by 257.
    const auto back = read_fits_image(file);
    REQUIRE(back);
    REQUIRE(back->type() == CV_16UC3);
    cv::Mat expected;
    picture.convertTo(expected, CV_16UC3, 257.0);
    CHECK(cv::norm(*back, expected, cv::NORM_INF) == 0.0);

    // On disk the first row of the first plane is the bottom row of the picture (ROWORDER = BOTTOM-UP): the white
    // square at the top-left of the picture is in the last stored row, not the first.
    std::vector<char> bytes(std::filesystem::file_size(file));
    {
        std::ifstream in(file, std::ios::binary);
        REQUIRE(in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())));
    }
    CHECK(bytes.size() % 2880 == 0);
    fitsfile* fits = nullptr;
    int status = 0;
    void* buffer = bytes.data();
    std::size_t size = bytes.size();
    fits_open_memfile(&fits, "", READONLY, &buffer, &size, 0, nullptr, &status);
    REQUIRE(status == 0);
    std::vector<std::uint16_t> first_row(64);
    std::vector<std::uint16_t> last_row(64);
    int any_null = 0;
    fits_read_img(fits, TUSHORT, 1, 64, nullptr, first_row.data(), &any_null, &status);
    fits_read_img(fits, TUSHORT, 1 + 47 * 64, 64, nullptr, last_row.data(), &any_null, &status);
    fits_close_file(fits, &status);
    REQUIRE(status == 0);
    CHECK(last_row[0] == 65535);      // white square, red plane
    CHECK(first_row[0] == 40 * 257);  // bottom-left pixel: B=0, G=235, R=40 -> red plane 40
}

TEST_CASE("PNG, TIFF-16 and JPEG pictures are written and hashed", "[capture][recording]")
{
    const TempWorkspace workspace;
    const cv::Mat picture = test_picture();
    const CaptureRecord record = test_record();

    SECTION("PNG keeps every pixel")
    {
        const auto written = write_picture(picture, ImageFileFormat::Png, workspace.path("a.png"), record);
        REQUIRE(written);
        CHECK(written->bit_depth == 8);
        const auto back = read_image(written->path);
        REQUIRE(back);
        CHECK(cv::norm(*back, picture, cv::NORM_INF) == 0.0);
        const auto hash = file_sha256(written->path);
        REQUIRE(hash);
        CHECK(*hash == written->sha256);
        CHECK(written->bytes == std::filesystem::file_size(written->path));
        CHECK_FALSE(std::filesystem::exists(workspace.path("a.part.png")));
        // Writing again replaces the file.
        const auto again = write_picture(picture, ImageFileFormat::Png, workspace.path("a.png"), record);
        REQUIRE(again);
        CHECK(again->sha256 == written->sha256);
    }
    SECTION("TIFF-16 widens 8-bit data by 257")
    {
        const auto written = write_picture(picture, ImageFileFormat::Tiff16, workspace.path("a.tiff"), record);
        REQUIRE(written);
        CHECK(written->bit_depth == 16);
        const auto back = read_image(written->path);
        REQUIRE(back);
        REQUIRE(back->type() == CV_16UC3);
        cv::Mat expected;
        picture.convertTo(expected, CV_16UC3, 257.0);
        CHECK(cv::norm(*back, expected, cv::NORM_INF) == 0.0);
    }
    SECTION("JPEG is close and refuses 16-bit input")
    {
        const auto written = write_picture(picture, ImageFileFormat::Jpeg, workspace.path("a.jpg"), record, 95);
        REQUIRE(written);
        const auto back = read_image(written->path);
        REQUIRE(back);
        CHECK(cv::norm(*back, picture, cv::NORM_L1) / static_cast<double>(picture.total() * 3) < 4.0);
        cv::Mat wide;
        picture.convertTo(wide, CV_16UC3, 257.0);
        const auto refused = write_picture(wide, ImageFileFormat::Jpeg, workspace.path("b.jpg"), record);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("16-bit grey goes into every format but JPEG")
    {
        cv::Mat grey(48, 64, CV_16UC1);
        cv::randu(grey, 0, 65535);
        for (const ImageFileFormat format : {ImageFileFormat::Png, ImageFileFormat::Tiff16, ImageFileFormat::Fits}) {
            const std::filesystem::path file = workspace.path(std::string("grey") + std::string(extension(format)));
            const auto written = write_picture(grey, format, file, record);
            REQUIRE(outcome(written) == "ok");
            CHECK(written->bit_depth == 16);
            const auto back = format == ImageFileFormat::Fits ? read_fits_image(file) : read_image(file);
            REQUIRE(back);
            CHECK(cv::norm(*back, grey, cv::NORM_INF) == 0.0);
        }
    }
}

TEST_CASE("the sidecar validates against its schema and says what was captured", "[capture][recording]")
{
    const TempWorkspace workspace;
    const CaptureRecord record = test_record();
    const auto written = write_picture(test_picture(), ImageFileFormat::Png, workspace.path("s/frame.png"), record);
    REQUIRE(written);

    const nlohmann::json sidecar = sidecar_json(record, *written, ImageFileFormat::Png);
    const auto issues = sidecar_schema().validate(sidecar);
    for (const auto& issue : issues) {
        INFO(issue.path << ": " << issue.message);
    }
    CHECK(issues.empty());

    CHECK(sidecar["schema"] == "cloudscope.frame/1");
    CHECK(sidecar["file"]["name"] == "frame.png");
    CHECK(sidecar["file"]["format"] == "png");
    CHECK(sidecar["file"]["sha256"] == written->sha256);
    CHECK(sidecar["capture"]["utc"] == "2026-10-09T10:15:30.123+00:00");
    CHECK(sidecar["capture"]["sequence"] == 42);
    CHECK(sidecar["capture"]["time_source"] == "host");
    CHECK(sidecar["capture"]["simulated"] == true);
    CHECK_THAT(sidecar["capture"]["mjd"].get<double>(), WithinAbs(61322.42743, 0.00001));
    CHECK(sidecar["camera"]["id"] == "sim:camera:sky");
    CHECK(sidecar["camera"]["mode"]["format"] == "MJPEG");
    CHECK(sidecar["camera"]["controls"]["gain"]["automatic"] == true);
    CHECK(sidecar["camera"]["calibration_id"] == "cal-2026-10-09");
    CHECK(sidecar["site"]["id"] == "blr-roof");
    CHECK(sidecar["pointing"]["source"] == "declared_by_operator");
    CHECK(sidecar["sun"]["elevation_deg"] == 36.0);
    CHECK(sidecar["statistics"]["sun"]["found"].is_boolean());
    CHECK(sidecar["session_id"] == "20261009T1000Z-blr-roof");
    CHECK(sidecar["software"]["name"] == "CloudScope");

    const auto path = write_sidecar(written->path, sidecar);
    REQUIRE(path);
    CHECK(path->filename() == "frame.png.json");
    std::ifstream in(*path);
    const nlohmann::json back = nlohmann::json::parse(in);
    CHECK(back == sidecar);
    CHECK_FALSE(std::filesystem::exists(workspace.path("s/frame.png.json.part")));

    SECTION("a sidecar missing a required part or carrying an unknown one is rejected")
    {
        nlohmann::json broken = sidecar;
        broken.erase("software");
        CHECK_FALSE(sidecar_schema().validate(broken).empty());
        nlohmann::json extra = sidecar;
        extra["guess"] = 1;
        CHECK_FALSE(sidecar_schema().validate(extra).empty());
        nlohmann::json bad_hash = sidecar;
        bad_hash["file"]["sha256"] = "nope";
        CHECK_FALSE(sidecar_schema().validate(bad_hash).empty());
    }
    SECTION("a record without site, pointing, Sun or statistics still validates")
    {
        CaptureRecord bare = record;
        bare.site.reset();
        bare.pointing.reset();
        bare.sun.reset();
        bare.statistics.reset();
        bare.exposure_ms.reset();
        bare.gain.reset();
        bare.calibration_id.clear();
        bare.session_id.clear();
        const nlohmann::json minimal = sidecar_json(bare, *written, ImageFileFormat::Png);
        CHECK(sidecar_schema().validate(minimal).empty());
        CHECK_FALSE(minimal.contains("site"));
        CHECK_FALSE(minimal["camera"].contains("exposure_ms"));
    }
}

TEST_CASE("sidecars of both schemas are read back", "[capture][recording]")
{
    const TempWorkspace workspace;
    const CaptureRecord record = test_record();
    const auto written = write_picture(test_picture(), ImageFileFormat::Png, workspace.path("r/frame.png"), record);
    REQUIRE(written);
    const auto ours = read_sidecar(sidecar_json(record, *written, ImageFileFormat::Png));
    REQUIRE(ours);
    CHECK(ours->schema == "cloudscope.frame/1");
    CHECK(ours->file_name == "frame.png");
    CHECK(ours->sha256 == written->sha256);
    CHECK(ours->bytes == written->bytes);
    CHECK(ours->utc == record.info.captured.utc);
    CHECK(ours->sequence == 42);
    CHECK(ours->simulated);
    CHECK(ours->exposure_ms.value() == 15.625);
    REQUIRE(ours->site);
    CHECK(ours->site->id == "blr-roof");
    REQUIRE(ours->pointing);
    CHECK(ours->pointing->source == "declared_by_operator");
    REQUIRE(ours->sun);
    CHECK(ours->width == 64);

    // The interim logger's layout (tools/sky_logger, schema cloudscope.sky_logger.frame/1).
    const nlohmann::json legacy = {
        {"schema", "cloudscope.sky_logger.frame/1"},
        {"file", "blr-roof_20261004T121500_250Z.jpg"},
        {"sha256", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"bytes", 123456},
        {"encoding", {{"format", "jpg"}, {"jpeg_quality", 92}}},
        {"capture",
         {{"utc", "2026-10-04T12:15:00.250+00:00"},
          {"utc_unix", 1791108900.25},
          {"sequence", 7},
          {"time_source", "host clock (not GPS-disciplined)"}}},
        {"site", {{"id", "blr-roof"}, {"latitude", 12.97}, {"longitude", 77.59}, {"altitude_m", 920.0}}},
        {"pointing",
         {{"source", "declared_by_operator"},
          {"description", "zenith"},
          {"azimuth_deg", 0.0},
          {"elevation_deg", 90.0}}},
        {"camera", {{"index", 1}, {"backend", "dshow"}}},
        {"image", {{"width", 4656}, {"height", 3496}, {"channels", 3}}},
        {"sun", {{"azimuth_deg", 180.5}, {"elevation_deg", 72.6}}},
    };
    const auto old = read_sidecar(legacy);
    REQUIRE(old);
    CHECK(old->schema == "cloudscope.sky_logger.frame/1");
    CHECK(old->file_name == "blr-roof_20261004T121500_250Z.jpg");
    CHECK(old->bytes == 123456);
    CHECK(old->utc == at("2026-10-04T12:15:00.250Z"));
    CHECK(old->sequence == 7);
    CHECK_FALSE(old->simulated);
    CHECK_FALSE(old->exposure_ms);
    REQUIRE(old->site);
    CHECK(old->site->latitude_deg == 12.97);
    CHECK(old->site->altitude_m == 920.0);
    REQUIRE(old->pointing);
    CHECK(old->pointing->elevation_deg == 90.0);
    REQUIRE(old->sun);
    CHECK(old->sun->elevation_deg == 72.6);
    CHECK(old->width == 4656);

    CHECK_FALSE(
        read_sidecar(nlohmann::json{{"schema", "somebody.else/1"}, {"capture", {{"utc", "2026-10-04T12:15:00Z"}}}}));
    CHECK_FALSE(read_sidecar(nlohmann::json{{"schema", "cloudscope.frame/1"}}));  // no time
    CHECK_FALSE(read_sidecar(nlohmann::json::array()));
}

TEST_CASE("time and hash helpers", "[capture][recording]")
{
    CHECK(fits_date_obs(at("2026-10-09T10:15:30.123Z")) == "2026-10-09T10:15:30.123");
    CHECK(modified_julian_date(from_unix_ms(0)) == 40587.0);
    CHECK_THAT(modified_julian_date(at("2000-01-01T12:00:00Z")), WithinAbs(51544.5, 1e-9));

    const TempWorkspace workspace;
    const std::filesystem::path file = workspace.write("abc.txt", "abc");
    const auto hash = file_sha256(file);
    REQUIRE(hash);
    CHECK(*hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_FALSE(file_sha256(workspace.path("missing.bin")));

    CHECK(image_format_from_string("fits").value() == ImageFileFormat::Fits);
    CHECK(image_format_from_string("tif").value() == ImageFileFormat::Tiff16);
    CHECK(image_format_from_string("jpg").value() == ImageFileFormat::Jpeg);
    CHECK_FALSE(image_format_from_string("bmp"));
    CHECK(extension(ImageFileFormat::Tiff16) == ".tiff");
}
