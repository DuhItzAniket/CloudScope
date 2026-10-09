#include "test_support.hpp"

#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/video_files.hpp>
#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;

namespace {

UtcTime at(std::string_view iso)
{
    const auto parsed = parse_iso8601(iso);
    REQUIRE(parsed);
    return *parsed;
}

std::vector<cv::Mat> frames_with_moving_bar(int count, int type)
{
    std::vector<cv::Mat> frames;
    for (int i = 0; i < count; ++i) {
        cv::Mat frame(24, 32, type, cv::Scalar::all(10 + i));
        frame.col(i % 32).setTo(cv::Scalar::all(200));  // a bright column that moves one pixel per frame
        frames.push_back(frame);
    }
    return frames;
}

}  // namespace

TEST_CASE("SER files round-trip frames, header and UTC trailer", "[capture][ser]")
{
    const TempWorkspace workspace;
    const std::filesystem::path file = workspace.path("video/clip.ser");

    SECTION("8-bit BGR")
    {
        const std::vector<cv::Mat> frames = frames_with_moving_bar(5, CV_8UC3);
        SerWriter writer;
        REQUIRE(writer.open(file, 32, 24, CV_8UC3, "Owner", "Arducam B0268", "fisheye"));
        for (int i = 0; i < 5; ++i) {
            REQUIRE(writer.append(frames[static_cast<std::size_t>(i)],
                                  at("2026-10-09T10:00:00Z") + std::chrono::milliseconds(33 * i)));
        }
        REQUIRE(writer.close());
        CHECK(writer.frames() == 5);

        SerReader reader;
        REQUIRE(reader.open(file));
        const SerHeader& header = reader.header();
        CHECK(header.colour == SerColour::Bgr);
        CHECK(header.little_endian);
        CHECK(header.width == 32);
        CHECK(header.height == 24);
        CHECK(header.bit_depth == 8);
        CHECK(header.frames == 5);
        CHECK(header.observer == "Owner");
        CHECK(header.instrument == "Arducam B0268");
        CHECK(header.telescope == "fisheye");
        CHECK(header.start_utc == at("2026-10-09T10:00:00Z"));
        for (std::uint32_t i = 0; i < 5; ++i) {
            const auto frame = reader.frame(i);
            REQUIRE(frame);
            CHECK(cv::norm(*frame, frames[i], cv::NORM_INF) == 0.0);
            const auto stamp = reader.timestamp(i);
            REQUIRE(stamp);
            CHECK(*stamp == at("2026-10-09T10:00:00Z") + std::chrono::milliseconds(33 * i));
        }
        CHECK_FALSE(reader.frame(5));
        // Size check: header + frames + trailer.
        CHECK(std::filesystem::file_size(file) == 178 + 5 * (32 * 24 * 3) + 5 * 8);
    }
    SECTION("16-bit mono, and a file declared big-endian is byte-swapped on reading")
    {
        std::vector<cv::Mat> frames;
        for (int i = 0; i < 3; ++i) {
            cv::Mat frame(24, 32, CV_16UC1);
            cv::randu(frame, 0, 65535);
            frames.push_back(frame);
        }
        SerWriter writer;
        REQUIRE(writer.open(file, 32, 24, CV_16UC1, "", "", ""));
        for (int i = 0; i < 3; ++i) {
            REQUIRE(writer.append(frames[static_cast<std::size_t>(i)],
                                  at("2026-10-09T22:00:00Z") + std::chrono::seconds(i)));
        }
        REQUIRE(writer.close());
        SerReader reader;
        REQUIRE(reader.open(file));
        CHECK(reader.header().bit_depth == 16);
        for (std::uint32_t i = 0; i < 3; ++i) {
            const auto frame = reader.frame(i);
            REQUIRE(frame);
            CHECK(cv::norm(*frame, frames[i], cv::NORM_INF) == 0.0);
        }

        // The file says 0 in the "LittleEndian" field: what Siril, SER Player and FireCapture write for
        // little-endian data (the opposite of the specification's wording).
        std::vector<char> bytes(std::filesystem::file_size(file));
        {
            std::ifstream in(file, std::ios::binary);
            in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        CHECK(bytes[22] == 0);
        CHECK(bytes[23] == 0);
        // Rewrite the file as big-endian: set the field to 1 and swap every 16-bit word of the frame data.
        std::memset(bytes.data() + 22, 0, 4);
        bytes[22] = 1;
        for (std::size_t i = 178; i + 1 < 178 + 3 * 32 * 24 * 2; i += 2) {
            std::swap(bytes[i], bytes[i + 1]);
        }
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        SerReader swapped;
        REQUIRE(swapped.open(file));
        CHECK_FALSE(swapped.header().little_endian);
        for (std::uint32_t i = 0; i < 3; ++i) {
            const auto frame = swapped.frame(i);
            REQUIRE(frame);
            CHECK(cv::norm(*frame, frames[i], cv::NORM_INF) == 0.0);
        }
    }
    SECTION("a mismatched frame is refused and the writer stays usable")
    {
        SerWriter writer;
        REQUIRE(writer.open(file, 32, 24, CV_8UC1, "", "", ""));
        const auto wrong = writer.append(cv::Mat(24, 32, CV_8UC3), at("2026-10-09T10:00:00Z"));
        REQUIRE_FALSE(wrong);
        CHECK(wrong.error().code == ErrorCode::InvalidArgument);
        REQUIRE(writer.append(cv::Mat(24, 32, CV_8UC1, cv::Scalar(7)), at("2026-10-09T10:00:00Z")));
        REQUIRE(writer.close());
        REQUIRE(writer.close());  // twice is fine
        CHECK(writer.frames() == 1);
        CHECK_FALSE(SerWriter().append(cv::Mat(24, 32, CV_8UC1), at("2026-10-09T10:00:00Z")));
    }
    SECTION("ticks")
    {
        CHECK(ser_ticks(from_unix_ms(0)) == 621'355'968'000'000'000LL);
        CHECK(utc_from_ser_ticks(ser_ticks(at("2026-10-09T10:15:30.123Z"))) == at("2026-10-09T10:15:30.123Z"));
    }
}

TEST_CASE("a time-lapse is assembled from a folder of pictures in name order", "[capture][ser]")
{
    const TempWorkspace workspace;
    const std::filesystem::path folder = workspace.path("stills");
    std::filesystem::create_directories(folder);
    const std::vector<cv::Mat> frames = frames_with_moving_bar(6, CV_8UC3);
    for (int i = 0; i < 6; ++i) {
        REQUIRE(write_image(folder / fmt::format("frame_{:03}.png", i), frames[static_cast<std::size_t>(i)]));
    }
    workspace.write("stills/notes.txt", "not a picture");
    // One grey picture of another size: converted to BGR and resized.
    REQUIRE(write_image(folder / "frame_006.png", cv::Mat(12, 16, CV_8UC1, cv::Scalar(99))));

    const auto files = picture_files(folder);
    REQUIRE(files);
    CHECK(files->size() == 7);
    CHECK(files->front().filename() == "frame_000.png");

    const std::filesystem::path ser = workspace.path("out/lapse.ser");
    const auto count = assemble_time_lapse(folder, ser, "B0268");
    REQUIRE(count);
    CHECK(*count == 7);
    SerReader reader;
    REQUIRE(reader.open(ser));
    CHECK(reader.header().instrument == "B0268");
    CHECK(reader.header().frames == 7);
    for (std::uint32_t i = 0; i < 6; ++i) {
        const auto frame = reader.frame(i);
        REQUIRE(frame);
        CHECK(cv::norm(*frame, frames[i], cv::NORM_INF) == 0.0);
    }
    const auto resized = reader.frame(6);
    REQUIRE(resized);
    CHECK(resized->size() == cv::Size(32, 24));
    CHECK(resized->at<cv::Vec3b>(5, 5) == cv::Vec3b(99, 99, 99));
    CHECK_FALSE(assemble_time_lapse(workspace.path("empty"), ser));
}

TEST_CASE("keogram and star trails summarise a sequence", "[capture][keogram]")
{
    const std::vector<cv::Mat> frames = frames_with_moving_bar(32, CV_8UC1);
    const auto keo = keogram(frames);  // centre column = 16
    REQUIRE(keo);
    CHECK(keo->rows == 24);
    CHECK(keo->cols == 32);
    // Column i of the keogram is frame i's centre column: bright only in frame 16, where the bar passes.
    CHECK(keo->at<std::uint8_t>(0, 16) == 200);
    CHECK(keo->at<std::uint8_t>(0, 15) == 10 + 15);
    CHECK(keo->at<std::uint8_t>(23, 3) == 10 + 3);
    const auto edge = keogram(frames, 0);
    REQUIRE(edge);
    CHECK(edge->at<std::uint8_t>(0, 0) == 200);
    CHECK_FALSE(keogram(frames, 32));
    CHECK_FALSE(keogram({}));

    const auto trails = star_trails(frames);
    REQUIRE(trails);
    CHECK(trails->size() == cv::Size(32, 24));
    // Every column was bright in one frame: the maximum is 200 everywhere.
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(*trails, &minimum, &maximum);
    CHECK(minimum == 200.0);
    CHECK(maximum == 200.0);

    std::vector<cv::Mat> mixed = frames;
    mixed.emplace_back(24, 32, CV_8UC3);
    CHECK_FALSE(star_trails(mixed));
    CHECK_FALSE(keogram(mixed));
    const auto colour = keogram(frames_with_moving_bar(4, CV_8UC3));
    REQUIRE(colour);
    CHECK(colour->type() == CV_8UC3);
}
