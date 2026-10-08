#include "cloudscope/capture/video_files.hpp"

#include "cloudscope/capture/calibration_frames.hpp"

#include <fmt/format.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace cloudscope {

namespace {

constexpr std::size_t kHeaderBytes = 178;
constexpr std::array<char, 14> kFileId = {'L', 'U', 'C', 'A', 'M', '-', 'R', 'E', 'C', 'O', 'R', 'D', 'E', 'R'};
constexpr std::int64_t kTicksPerMillisecond = 10'000;
// Ticks from 0001-01-01 to 1970-01-01 (.NET epoch to Unix epoch).
constexpr std::int64_t kUnixEpochTicks = 621'355'968'000'000'000;

template <class T>
void put(char* at, T value)
{
    if constexpr (std::endian::native == std::endian::big) {
        std::array<char, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(T));
        std::reverse(bytes.begin(), bytes.end());
        std::memcpy(at, bytes.data(), sizeof(T));
    } else {
        std::memcpy(at, &value, sizeof(T));
    }
}

template <class T>
T get(const char* at)
{
    T value{};
    std::memcpy(&value, at, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::array<char, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(T));
        std::reverse(bytes.begin(), bytes.end());
        std::memcpy(&value, bytes.data(), sizeof(T));
    }
    return value;
}

void put_text(char* at, const std::string& text, std::size_t length)
{
    std::memset(at, ' ', length);
    std::memcpy(at, text.data(), std::min(text.size(), length));
}

std::string get_text(const char* at, std::size_t length)
{
    std::string text(at, length);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) {
        text.pop_back();
    }
    return text;
}

std::array<char, kHeaderBytes> encode_header(const SerHeader& header)
{
    std::array<char, kHeaderBytes> bytes{};
    std::memcpy(bytes.data(), kFileId.data(), kFileId.size());
    put<std::int32_t>(bytes.data() + 14, 0);  // LuID
    put<std::int32_t>(bytes.data() + 18, static_cast<std::int32_t>(header.colour));
    put<std::int32_t>(bytes.data() + 22, header.little_endian ? 1 : 0);
    put<std::int32_t>(bytes.data() + 26, header.width);
    put<std::int32_t>(bytes.data() + 30, header.height);
    put<std::int32_t>(bytes.data() + 34, header.bit_depth);
    put<std::int32_t>(bytes.data() + 38, static_cast<std::int32_t>(header.frames));
    put_text(bytes.data() + 42, header.observer, 40);
    put_text(bytes.data() + 82, header.instrument, 40);
    put_text(bytes.data() + 122, header.telescope, 40);
    put<std::int64_t>(bytes.data() + 162, ser_ticks(header.start_utc));  // local time: we write UTC here too
    put<std::int64_t>(bytes.data() + 170, ser_ticks(header.start_utc));
    return bytes;
}

std::size_t bytes_per_frame(const SerHeader& header)
{
    const std::size_t planes = header.colour == SerColour::Mono ? 1 : 3;
    return static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height) * planes *
           (header.bit_depth > 8 ? 2 : 1);
}

}  // namespace

std::int64_t ser_ticks(UtcTime time)
{
    return kUnixEpochTicks + time.time_since_epoch().count() * kTicksPerMillisecond;
}

UtcTime utc_from_ser_ticks(std::int64_t ticks)
{
    return UtcTime(std::chrono::milliseconds((ticks - kUnixEpochTicks) / kTicksPerMillisecond));
}

SerWriter::~SerWriter()
{
    (void)close();
}

Expected<void> SerWriter::open(const std::filesystem::path& file, int width, int height, int cv_type,
                               const std::string& observer, const std::string& instrument, const std::string& telescope)
{
    if (out_.is_open()) {
        return fail(ErrorCode::Unavailable, "the SER writer is already open");
    }
    if (width <= 0 || height <= 0) {
        return fail(ErrorCode::InvalidArgument, "SER frames need a positive size");
    }
    header_ = {};
    switch (cv_type) {
    case CV_8UC1:
        header_.colour = SerColour::Mono;
        header_.bit_depth = 8;
        break;
    case CV_16UC1:
        header_.colour = SerColour::Mono;
        header_.bit_depth = 16;
        break;
    case CV_8UC3:
        header_.colour = SerColour::Bgr;
        header_.bit_depth = 8;
        break;
    default:
        return fail(ErrorCode::Unsupported, "SER takes 8-bit mono, 16-bit mono or 8-bit BGR frames");
    }
    header_.width = width;
    header_.height = height;
    header_.observer = observer;
    header_.instrument = instrument;
    header_.telescope = telescope;
    cv_type_ = cv_type;
    frames_ = 0;
    timestamps_.clear();
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    out_.open(file, std::ios::binary | std::ios::trunc);
    if (!out_) {
        return fail(ErrorCode::Io, fmt::format("could not create {}", file.string()));
    }
    file_ = file;
    const std::array<char, kHeaderBytes> header = encode_header(header_);  // frame count 0 until close()
    out_.write(header.data(), static_cast<std::streamsize>(header.size()));
    return out_ ? Expected<void>{} : fail(ErrorCode::Io, fmt::format("could not write {}", file.string()));
}

Expected<void> SerWriter::append(const cv::Mat& frame, UtcTime utc)
{
    if (!out_.is_open()) {
        return fail(ErrorCode::Unavailable, "the SER writer is not open");
    }
    if (frame.type() != cv_type_ || frame.cols != header_.width || frame.rows != header_.height) {
        return fail(ErrorCode::InvalidArgument, "the frame does not match the SER file's size and type");
    }
    if (frames_ == 0) {
        header_.start_utc = utc;
    }
    const std::size_t row_bytes = static_cast<std::size_t>(frame.cols) * frame.elemSize();
    for (int row = 0; row < frame.rows; ++row) {
        out_.write(reinterpret_cast<const char*>(frame.ptr(row)),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                   static_cast<std::streamsize>(row_bytes));
    }
    if (!out_) {
        return fail(ErrorCode::Io, fmt::format("could not write a frame to {}", file_.string()));
    }
    timestamps_.push_back(ser_ticks(utc));
    ++frames_;
    return {};
}

Expected<void> SerWriter::close()
{
    if (!out_.is_open()) {
        return {};
    }
    header_.frames = frames_;
    for (const std::int64_t ticks : timestamps_) {
        std::array<char, 8> bytes{};
        put<std::int64_t>(bytes.data(), ticks);
        out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    out_.seekp(0);
    const std::array<char, kHeaderBytes> header = encode_header(header_);
    out_.write(header.data(), static_cast<std::streamsize>(header.size()));
    const bool ok = static_cast<bool>(out_);
    out_.close();
    timestamps_.clear();
    return ok ? Expected<void>{} : fail(ErrorCode::Io, fmt::format("could not finish {}", file_.string()));
}

Expected<void> SerReader::open(const std::filesystem::path& file)
{
    in_.open(file, std::ios::binary);
    if (!in_) {
        return fail(ErrorCode::NotFound, fmt::format("could not open {}", file.string()));
    }
    std::array<char, kHeaderBytes> bytes{};
    in_.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in_ || std::memcmp(bytes.data(), kFileId.data(), kFileId.size()) != 0) {
        return fail(ErrorCode::Parse, fmt::format("{} is not a SER file", file.string()));
    }
    header_ = {};
    header_.colour = static_cast<SerColour>(get<std::int32_t>(bytes.data() + 18));
    header_.little_endian = get<std::int32_t>(bytes.data() + 22) != 0;
    header_.width = get<std::int32_t>(bytes.data() + 26);
    header_.height = get<std::int32_t>(bytes.data() + 30);
    header_.bit_depth = get<std::int32_t>(bytes.data() + 34);
    header_.frames = static_cast<std::uint32_t>(std::max(get<std::int32_t>(bytes.data() + 38), 0));
    header_.observer = get_text(bytes.data() + 42, 40);
    header_.instrument = get_text(bytes.data() + 82, 40);
    header_.telescope = get_text(bytes.data() + 122, 40);
    header_.start_utc = utc_from_ser_ticks(get<std::int64_t>(bytes.data() + 170));
    if (header_.width <= 0 || header_.height <= 0 || (header_.bit_depth != 8 && header_.bit_depth != 16) ||
        (header_.colour != SerColour::Mono && header_.colour != SerColour::Rgb && header_.colour != SerColour::Bgr)) {
        return fail(ErrorCode::Unsupported, fmt::format("{}: unsupported SER layout", file.string()));
    }
    frame_bytes_ = bytes_per_frame(header_);
    // Trailer: one int64 per frame after the frames, if the file is long enough.
    in_.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(in_.tellg());
    const std::uint64_t data_end = kHeaderBytes + static_cast<std::uint64_t>(frame_bytes_) * header_.frames;
    timestamps_.clear();
    if (size >= data_end + 8ULL * header_.frames) {
        in_.seekg(static_cast<std::streamoff>(data_end));
        for (std::uint32_t i = 0; i < header_.frames; ++i) {
            std::array<char, 8> stamp{};
            in_.read(stamp.data(), 8);
            timestamps_.push_back(get<std::int64_t>(stamp.data()));
        }
    }
    in_.clear();
    return {};
}

Expected<cv::Mat> SerReader::frame(std::uint32_t index)
{
    if (!in_.is_open()) {
        return fail(ErrorCode::Unavailable, "no SER file is open");
    }
    if (index >= header_.frames) {
        return fail(ErrorCode::InvalidArgument, fmt::format("frame {} of {}", index, header_.frames));
    }
    const int type = header_.bit_depth == 16 ? CV_16UC1 : (header_.colour == SerColour::Mono ? CV_8UC1 : CV_8UC3);
    cv::Mat frame(header_.height, header_.width, type);
    in_.seekg(static_cast<std::streamoff>(kHeaderBytes + static_cast<std::uint64_t>(frame_bytes_) * index));
    in_.read(reinterpret_cast<char*>(frame.data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
             static_cast<std::streamsize>(frame_bytes_));
    if (!in_) {
        in_.clear();
        return fail(ErrorCode::Parse, fmt::format("frame {} is truncated", index));
    }
    if (header_.bit_depth == 16 && header_.little_endian == (std::endian::native == std::endian::big)) {
        // The file's byte order differs from the host's: swap.
        auto* words = frame.ptr<std::uint16_t>();
        for (std::size_t i = 0; i < frame.total(); ++i) {
            words[i] = static_cast<std::uint16_t>((words[i] >> 8) | (words[i] << 8));
        }
    }
    if (header_.colour == SerColour::Rgb) {
        cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
    }
    return frame;
}

Expected<UtcTime> SerReader::timestamp(std::uint32_t index) const
{
    if (index >= header_.frames) {
        return fail(ErrorCode::InvalidArgument, fmt::format("frame {} of {}", index, header_.frames));
    }
    if (index < timestamps_.size()) {
        return utc_from_ser_ticks(timestamps_[index]);
    }
    return header_.start_utc;
}

Expected<std::vector<std::filesystem::path>> picture_files(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".tif" || extension == ".tiff") {
            files.push_back(entry.path());
        }
    }
    if (error) {
        return fail(ErrorCode::NotFound, fmt::format("could not read folder {}", folder.string()));
    }
    if (files.empty()) {
        return fail(ErrorCode::NotFound, fmt::format("no pictures in {}", folder.string()));
    }
    std::ranges::sort(files);
    return files;
}

Expected<void> for_each_picture(const std::filesystem::path& folder,
                                const std::function<Expected<void>(const cv::Mat&, const std::filesystem::path&)>& visit)
{
    const auto files = picture_files(folder);
    if (!files) {
        return fail(files.error());
    }
    for (const std::filesystem::path& file : *files) {
        const auto image = read_image(file);
        if (!image) {
            return fail(image.error());
        }
        if (auto visited = visit(*image, file); !visited) {
            return visited;
        }
    }
    return {};
}

Expected<std::uint32_t> assemble_time_lapse(const std::filesystem::path& folder, const std::filesystem::path& ser_file,
                                            const std::string& instrument)
{
    SerWriter writer;
    cv::Size size;
    const auto result = for_each_picture(folder, [&](const cv::Mat& picture, const std::filesystem::path& file) -> Expected<void> {
        cv::Mat bgr;
        if (picture.channels() == 1) {
            cv::cvtColor(picture, bgr, cv::COLOR_GRAY2BGR);
        } else if (picture.channels() == 4) {
            cv::cvtColor(picture, bgr, cv::COLOR_BGRA2BGR);
        } else {
            bgr = picture;
        }
        if (bgr.depth() != CV_8U) {
            bgr.convertTo(bgr, CV_8UC3, 1.0 / 256.0);
        }
        if (!writer.is_open()) {
            size = bgr.size();
            if (auto opened = writer.open(ser_file, size.width, size.height, CV_8UC3, "", instrument, ""); !opened) {
                return opened;
            }
        } else if (bgr.size() != size) {
            cv::resize(bgr, bgr, size, 0, 0, cv::INTER_AREA);
        }
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(file, error);
        UtcTime utc{};
        if (!error) {
            const auto system_time = std::chrono::clock_cast<std::chrono::system_clock>(modified);
            utc = std::chrono::time_point_cast<std::chrono::milliseconds>(system_time);
        }
        return writer.append(bgr, utc);
    });
    if (!result) {
        (void)writer.close();
        return fail(result.error());
    }
    if (auto closed = writer.close(); !closed) {
        return fail(closed.error());
    }
    return writer.frames();
}

Expected<cv::Mat> keogram(const std::vector<cv::Mat>& frames, int column)
{
    if (frames.empty()) {
        return fail(ErrorCode::InvalidArgument, "no frames");
    }
    const cv::Mat& first = frames.front();
    const int use = column < 0 ? first.cols / 2 : column;
    if (use >= first.cols) {
        return fail(ErrorCode::InvalidArgument, "column outside the frame");
    }
    cv::Mat out(first.rows, static_cast<int>(frames.size()), first.type());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].size() != first.size() || frames[i].type() != first.type()) {
            return fail(ErrorCode::InvalidArgument, "frames differ in size or type");
        }
        frames[i].col(use).copyTo(out.col(static_cast<int>(i)));
    }
    return out;
}

Expected<cv::Mat> star_trails(const std::vector<cv::Mat>& frames)
{
    if (frames.empty()) {
        return fail(ErrorCode::InvalidArgument, "no frames");
    }
    cv::Mat out = frames.front().clone();
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].size() != out.size() || frames[i].type() != out.type()) {
            return fail(ErrorCode::InvalidArgument, "frames differ in size or type");
        }
        cv::max(out, frames[i], out);
    }
    return out;
}

}  // namespace cloudscope
