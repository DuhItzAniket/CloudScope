#include "cloudscope/common/self_test.hpp"

#include "cloudscope/common/scope_exit.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <fitsio.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <toml++/toml.hpp>
#include <turbojpeg.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cloudscope {

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;

void require(bool condition, std::string_view what)
{
    if (!condition) {
        throw std::runtime_error(std::string(what));
    }
}

// 16-bit test pattern that uses the full value range, so an 8-bit round trip cannot pass by accident.
cv::Mat gradient16()
{
    cv::Mat image(kHeight, kWidth, CV_16UC1);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            image.at<std::uint16_t>(y, x) = static_cast<std::uint16_t>((x * 1021 + y * 257) & 0xFFFF);
        }
    }
    return image;
}

std::string check_qt()
{
    const QString text = QStringLiteral("2026-10-04T12:34:56.789Z");
    const QDateTime time = QDateTime::fromString(text, Qt::ISODateWithMs);
    require(time.isValid(), "ISO 8601 timestamp not parsed");
    require(time.toMSecsSinceEpoch() == 1791117296789LL, "wrong epoch for 2026-10-04T12:34:56.789Z");
    require(time.toUTC().toString(Qt::ISODateWithMs) == text, "ISO 8601 round trip changed the text");
    return "UTC timestamp with milliseconds parsed and formatted";
}

std::string check_opencv()
{
    const cv::Mat image = gradient16();
    for (const char* extension : {".png", ".tiff"}) {
        std::vector<unsigned char> encoded;
        require(cv::imencode(extension, image, encoded), fmt::format("no encoder for {}", extension));
        const cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_UNCHANGED);
        require(!decoded.empty(), fmt::format("no decoder for {}", extension));
        require(decoded.type() == CV_16UC1 && decoded.size() == image.size(),
                fmt::format("{} did not keep 16-bit depth or size", extension));
        require(cv::norm(image, decoded, cv::NORM_INF) == 0.0, fmt::format("{} round trip is not lossless", extension));
    }
    cv::Mat half;
    cv::resize(image, half, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
    require(half.cols == kWidth / 2 && half.rows == kHeight / 2, "resize gave the wrong size");
    return "16-bit PNG and TIFF round trips are lossless";
}

std::string check_turbojpeg()
{
    struct HandleDeleter {
        void operator()(void* handle) const { tjDestroy(handle); }
    };
    struct BufferDeleter {
        void operator()(unsigned char* buffer) const { tjFree(buffer); }
    };
    using Handle = std::unique_ptr<void, HandleDeleter>;

    std::vector<unsigned char> rgb(static_cast<std::size_t>(kWidth) * kHeight * 3);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 3;
            rgb[i] = static_cast<unsigned char>(x * 3);
            rgb[i + 1] = static_cast<unsigned char>(y * 4);
            rgb[i + 2] = static_cast<unsigned char>(x + y);
        }
    }

    const Handle compressor(tjInitCompress());
    require(compressor != nullptr, "compressor not created");
    unsigned char* raw_jpeg = nullptr;
    unsigned long jpeg_size = 0;
    const int compressed = tjCompress2(compressor.get(), rgb.data(), kWidth, 0, kHeight, TJPF_RGB, &raw_jpeg,
                                       &jpeg_size, TJSAMP_444, 95, TJFLAG_ACCURATEDCT);
    const std::unique_ptr<unsigned char, BufferDeleter> jpeg(raw_jpeg);
    require(compressed == 0 && jpeg_size > 0, "compression failed");

    const Handle decompressor(tjInitDecompress());
    require(decompressor != nullptr, "decompressor not created");
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colourspace = 0;
    require(tjDecompressHeader3(decompressor.get(), jpeg.get(), jpeg_size, &width, &height, &subsampling,
                                &colourspace) == 0,
            "header not read");
    require(width == kWidth && height == kHeight, "header reports the wrong size");

    std::vector<unsigned char> decoded(rgb.size());
    require(tjDecompress2(decompressor.get(), jpeg.get(), jpeg_size, decoded.data(), kWidth, 0, kHeight, TJPF_RGB,
                          TJFLAG_ACCURATEDCT) == 0,
            "decompression failed");
    int worst = 0;
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<int>(rgb[i]) - static_cast<int>(decoded[i])));
    }
    require(worst <= 16, fmt::format("decoded image differs by {} grey levels", worst));
    return fmt::format("JPEG round trip, largest error {} of 255 grey levels", worst);
}

std::string check_cfitsio()
{
    const cv::Mat image = gradient16();
    const long pixel_count = static_cast<long>(kWidth) * kHeight;

    fitsfile* file = nullptr;
    int status = 0;
    fits_create_file(&file, "mem://cloudscope-self-test", &status);
    require(status == 0 && file != nullptr, "in-memory FITS file not created");
    const ScopeExit close_file([file] {
        int ignored = 0;
        fits_close_file(file, &ignored);
    });

    std::array<long, 2> axes = {kWidth, kHeight};
    fits_create_img(file, USHORT_IMG, 2, axes.data(), &status);
    std::string date_obs = "2026-10-04T12:34:56.789";  // CFITSIO takes a non-const pointer
    fits_update_key(file, TSTRING, "DATE-OBS", date_obs.data(), "UTC start of exposure", &status);
    fits_write_img(file, TUSHORT, 1, pixel_count, image.data, &status);
    require(status == 0, fmt::format("write failed, CFITSIO status {}", status));

    std::array<char, FLEN_VALUE> value{};
    fits_read_key(file, TSTRING, "DATE-OBS", value.data(), nullptr, &status);
    require(status == 0 && std::string_view(value.data()) == date_obs, "DATE-OBS keyword not read back");

    std::vector<std::uint16_t> decoded(static_cast<std::size_t>(pixel_count));
    int any_null = 0;
    fits_read_img(file, TUSHORT, 1, pixel_count, nullptr, decoded.data(), &any_null, &status);
    require(status == 0, fmt::format("read failed, CFITSIO status {}", status));
    const cv::Mat decoded_image(kHeight, kWidth, CV_16UC1, decoded.data());
    require(cv::norm(image, decoded_image, cv::NORM_INF) == 0.0, "pixel data changed");
    return "16-bit FITS image and DATE-OBS keyword written and read back";
}

std::string check_toml()
{
    using namespace std::string_view_literals;
    const toml::table table = toml::parse("[camera]\nname = \"B0268\"\nexposure_ms = 12.5\n"sv);
    require(table["camera"]["name"].value<std::string>() == "B0268", "string value not read");
    require(table["camera"]["exposure_ms"].value<double>() == 12.5, "float value not read");
    return "TOML document parsed";
}

std::string check_json()
{
    const nlohmann::json document = nlohmann::json::parse(R"({"site":"BLR01","frames":[1,2,3]})");
    require(document.at("site") == "BLR01" && document.at("frames").size() == 3, "values not read");
    require(nlohmann::json::parse(document.dump()) == document, "round trip changed the document");
    return "JSON document parsed and serialised";
}

std::string check_spdlog()
{
    std::ostringstream stream;
    spdlog::logger logger("self-test", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
    logger.set_pattern("%l %v");
    logger.info("frame {} exposure {:.1f} ms", 7, 12.5);
    logger.flush();
    std::string line = stream.str();
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    require(line == "info frame 7 exposure 12.5 ms", fmt::format("unexpected log line '{}'", line));
    require(fmt::format("{:>6.2f}", std::numbers::pi) == "  3.14", "number formatting wrong");
    return "log record formatted and delivered to a sink";
}

SelfTestResult run_check(const char* name, std::string (*check)())
{
    try {
        return {.name = name, .passed = true, .detail = check()};
    } catch (const std::exception& error) {
        return {.name = name, .passed = false, .detail = error.what()};
    } catch (...) {
        return {.name = name, .passed = false, .detail = "unknown exception"};
    }
}

}  // namespace

std::vector<SelfTestResult> run_self_test()
{
    return {
        run_check("qt-core", check_qt),
        run_check("opencv", check_opencv),
        run_check("libjpeg-turbo", check_turbojpeg),
        run_check("cfitsio", check_cfitsio),
        run_check("toml++", check_toml),
        run_check("nlohmann-json", check_json),
        run_check("spdlog-fmt", check_spdlog),
    };
}

bool all_passed(const std::vector<SelfTestResult>& results)
{
    return std::ranges::all_of(results, &SelfTestResult::passed);
}

}  // namespace cloudscope
