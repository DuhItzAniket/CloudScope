// Recording II (P028, FR-REC-05 to FR-REC-07): SER video with UTC timestamps, time-lapse assembly, keograms and
// star trails.
//
// SER (version 3) is the raw-frame video format astronomy software reads (SER Player, Siril, PIPP, AutoStakkert):
// a 178-byte header, the frames as they come from the camera (8- or 16-bit mono, 8-bit BGR), and a trailer with
// one UTC timestamp per frame. The header's endianness field has been written both ways by different programs;
// CloudScope writes little-endian data and marks it so, and reads whatever a file declares.
//
// A keogram is the centre column of every frame laid side by side over time; star trails are the per-pixel maximum
// over frames. Both are made from a sequence in memory or from a folder of pictures.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace cloudscope {

enum class SerColour : std::int32_t { Mono = 0, Rgb = 100, Bgr = 101 };

struct SerHeader {
    SerColour colour = SerColour::Mono;
    bool little_endian = true;
    int width = 0;
    int height = 0;
    int bit_depth = 8;  // per plane: 8 or 16
    std::uint32_t frames = 0;
    std::string observer;
    std::string instrument;
    std::string telescope;
    UtcTime start_utc{};
};

class SerWriter {
public:
    SerWriter() = default;
    ~SerWriter();
    SerWriter(const SerWriter&) = delete;
    SerWriter& operator=(const SerWriter&) = delete;
    SerWriter(SerWriter&&) = delete;
    SerWriter& operator=(SerWriter&&) = delete;

    // Starts a file for frames of this size and type (CV_8UC1, CV_16UC1 or CV_8UC3 BGR).
    [[nodiscard]] Expected<void> open(const std::filesystem::path& file, int width, int height, int cv_type,
                                      const std::string& observer, const std::string& instrument,
                                      const std::string& telescope);
    // A frame of the opened size and type with its UTC time. InvalidArgument otherwise.
    [[nodiscard]] Expected<void> append(const cv::Mat& frame, UtcTime utc);
    // Writes the frame count and the timestamp trailer and closes the file. Safe to call twice.
    [[nodiscard]] Expected<void> close();

    [[nodiscard]] std::uint32_t frames() const { return frames_; }
    [[nodiscard]] bool is_open() const { return out_.is_open(); }

private:
    std::ofstream out_;
    std::filesystem::path file_;
    SerHeader header_;
    int cv_type_ = 0;
    std::uint32_t frames_ = 0;
    std::vector<std::int64_t> timestamps_;
};

class SerReader {
public:
    [[nodiscard]] Expected<void> open(const std::filesystem::path& file);
    [[nodiscard]] const SerHeader& header() const { return header_; }
    // Frame `index` (0-based) as an OpenCV image of the file's type, and its UTC time (from the trailer; the
    // header's start time if the file has no trailer).
    [[nodiscard]] Expected<cv::Mat> frame(std::uint32_t index);
    [[nodiscard]] Expected<UtcTime> timestamp(std::uint32_t index) const;

private:
    std::ifstream in_;
    SerHeader header_;
    std::vector<std::int64_t> timestamps_;
    std::size_t frame_bytes_ = 0;
};

// .NET ticks (100 ns since 0001-01-01) <-> UTC, as SER stores times.
[[nodiscard]] std::int64_t ser_ticks(UtcTime time);
[[nodiscard]] UtcTime utc_from_ser_ticks(std::int64_t ticks);

// The pictures of a folder (JPEG, PNG, TIFF), in name order; NotFound if there is none.
[[nodiscard]] Expected<std::vector<std::filesystem::path>> picture_files(const std::filesystem::path& folder);

// A SER file from a folder of pictures (decoded to 8-bit BGR, resized to the first picture's size when needed);
// the frame times come from the files' modification times. Returns the number of frames written.
[[nodiscard]] Expected<std::uint32_t> assemble_time_lapse(const std::filesystem::path& folder,
                                                          const std::filesystem::path& ser_file,
                                                          const std::string& instrument = "");

// Keogram: column `column` (default: the centre) of every frame, side by side in time order. Frames may be 8-bit
// grey or BGR of one size; the result has the frames' type, width = frame count, height = frame height.
[[nodiscard]] Expected<cv::Mat> keogram(const std::vector<cv::Mat>& frames, int column = -1);

// Star trails: per-pixel maximum over frames of one size and type.
[[nodiscard]] Expected<cv::Mat> star_trails(const std::vector<cv::Mat>& frames);

// Runs `visit` over the pictures of a folder in name order (decoded as stored); stops at the first error.
[[nodiscard]] Expected<void> for_each_picture(const std::filesystem::path& folder,
                                              const std::function<Expected<void>(const cv::Mat&, const std::filesystem::path&)>& visit);

}  // namespace cloudscope
