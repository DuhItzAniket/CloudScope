// The two picture sources of the simulated camera: a synthetic sky and the replay of a folder of pictures.

#include "cloudscope/sim/sim_camera.hpp"
#include "cloudscope/sim/synthetic_sky.hpp"

#include <fmt/format.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <turbojpeg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cloudscope::sim {

namespace {

using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlSetting;

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// Stores a BGR picture in a frame, in the pixel format asked for. `encoded` is working memory that is
// kept between frames.
Expected<void> store(const cv::Mat& bgr, PixelFormat format, Frame& frame, std::vector<unsigned char>& encoded)
{
    const std::size_t pixels = static_cast<std::size_t>(bgr.cols) * static_cast<std::size_t>(bgr.rows);
    void* const buffer = frame.buffer().data();
    switch (format) {
    case PixelFormat::Bgr8: {
        cv::Mat out(bgr.rows, bgr.cols, CV_8UC3, buffer);
        if (out.data != bgr.data) {
            bgr.copyTo(out);
        }
        frame.set_size(pixels * 3);
        return {};
    }
    case PixelFormat::Rgb8: {
        cv::Mat out(bgr.rows, bgr.cols, CV_8UC3, buffer);
        cv::cvtColor(bgr, out, cv::COLOR_BGR2RGB);
        frame.set_size(pixels * 3);
        return {};
    }
    case PixelFormat::Gray8: {
        cv::Mat out(bgr.rows, bgr.cols, CV_8UC1, buffer);
        cv::cvtColor(bgr, out, cv::COLOR_BGR2GRAY);
        frame.set_size(pixels);
        return {};
    }
    case PixelFormat::Gray16: {
        cv::Mat grey;
        cv::cvtColor(bgr, grey, cv::COLOR_BGR2GRAY);
        cv::Mat out(bgr.rows, bgr.cols, CV_16UC1, buffer);
        grey.convertTo(out, CV_16U, 257.0);  // 255 becomes 65535
        frame.set_size(pixels * 2);
        return {};
    }
    case PixelFormat::Yuyv: {
        // Y0 U Y1 V per pair of pixels, colour averaged over the pair; full-range BT.601 as in JPEG.
        cv::Mat out(bgr.rows, bgr.cols, CV_8UC2, buffer);
        const auto luma = [](const cv::Vec3b& p) { return 0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2]; };
        for (int y = 0; y < bgr.rows; ++y) {
            const auto* in = bgr.ptr<cv::Vec3b>(y);
            auto* packed = out.ptr<unsigned char>(y);
            for (int x = 0; x + 1 < bgr.cols; x += 2, in += 2, packed += 4) {
                const cv::Vec3b& first = in[0];
                const cv::Vec3b& second = in[1];
                const double blue = (first[0] + second[0]) / 2.0;
                const double green = (first[1] + second[1]) / 2.0;
                const double red = (first[2] + second[2]) / 2.0;
                packed[0] = cv::saturate_cast<unsigned char>(luma(first));
                packed[1] = cv::saturate_cast<unsigned char>(-0.168736 * red - 0.331264 * green + 0.5 * blue + 128.0);
                packed[2] = cv::saturate_cast<unsigned char>(luma(second));
                packed[3] = cv::saturate_cast<unsigned char>(0.5 * red - 0.418688 * green - 0.081312 * blue + 128.0);
            }
        }
        frame.set_size(pixels * 2);
        return {};
    }
    case PixelFormat::Mjpeg: {
        try {
            cv::imencode(".jpg", bgr, encoded, {cv::IMWRITE_JPEG_QUALITY, 90});
        } catch (const cv::Exception& error) {
            return fail(ErrorCode::Internal, fmt::format("JPEG encoding failed: {}", error.what()));
        }
        if (encoded.size() > frame.capacity()) {
            return fail(ErrorCode::Internal, "an encoded frame is larger than the frame buffer");
        }
        std::memcpy(buffer, encoded.data(), encoded.size());
        frame.set_size(encoded.size());
        return {};
    }
    }
    return fail(ErrorCode::Unsupported, "the simulated camera cannot deliver this pixel format");
}

// ------------------------------------------------------------------------------- synthetic sky

constexpr int kCanvasWidth = 1920;  // the clouds are drawn once, wider than any view, and the view moves over them
constexpr int kCanvasHeight = 720;
constexpr double kReferenceExposureMs = 10.0;  // with 0 dB gain the scene looks as it was drawn
constexpr double kNeutralWhiteBalanceK = 5500.0;
constexpr double kAutoExposureTarget = 110.0;  // mean grey level that automatic exposure steers to
constexpr double kSensorNoiseSigma = 1.5;      // grey levels at 0 dB gain
constexpr int kNoiseTile = 256;

// Positions in the list of controls.
constexpr std::size_t kExposure = 0;
constexpr std::size_t kGain = 1;
constexpr std::size_t kWhiteBalance = 2;
constexpr std::size_t kBrightness = 3;

CameraCapabilities synthetic_capabilities()
{
    return {
        .modes =
            {
                {.width = 640, .height = 480, .format = PixelFormat::Bgr8, .fps = 30.0},
                {.width = 640, .height = 480, .format = PixelFormat::Rgb8, .fps = 30.0},
                {.width = 640, .height = 480, .format = PixelFormat::Gray8, .fps = 30.0},
                {.width = 640, .height = 480, .format = PixelFormat::Gray16, .fps = 30.0},
                {.width = 640, .height = 480, .format = PixelFormat::Yuyv, .fps = 30.0},
                {.width = 640, .height = 480, .format = PixelFormat::Mjpeg, .fps = 30.0},
                {.width = 1280, .height = 720, .format = PixelFormat::Bgr8, .fps = 30.0},
                {.width = 1280, .height = 720, .format = PixelFormat::Mjpeg, .fps = 30.0},
                {.width = 1920, .height = 1080, .format = PixelFormat::Bgr8, .fps = 30.0},
                {.width = 1920, .height = 1080, .format = PixelFormat::Mjpeg, .fps = 30.0},
                {.width = 3840, .height = 2160, .format = PixelFormat::Bgr8, .fps = 15.0},
            },
        .controls =
            {
                {.control = CameraControl::Exposure,
                 .minimum = 0.05,
                 .maximum = 1000.0,
                 .step = 0.05,
                 .default_value = kReferenceExposureMs,
                 .unit = "ms",
                 .supports_auto = true,
                 .calibrated = true},
                {.control = CameraControl::Gain,
                 .minimum = 0.0,
                 .maximum = 24.0,
                 .step = 0.1,
                 .default_value = 0.0,
                 .unit = "dB",
                 .supports_auto = false,
                 .calibrated = true},
                {.control = CameraControl::WhiteBalance,
                 .minimum = 2800.0,
                 .maximum = 10000.0,
                 .step = 50.0,
                 .default_value = kNeutralWhiteBalanceK,
                 .unit = "K",
                 .supports_auto = true,
                 .calibrated = true},
                {.control = CameraControl::Brightness,
                 .minimum = -64.0,
                 .maximum = 64.0,
                 .step = 1.0,
                 .default_value = 0.0,
                 .unit = "",
                 .supports_auto = false,
                 .calibrated = false},
            },
    };
}

class SyntheticSkySource final : public IFrameSource {
public:
    explicit SyntheticSkySource(const SyntheticSkyOptions& options)
        : options_(options),
          capabilities_(synthetic_capabilities()),
          noise_tile_(kNoiseTile, kNoiseTile, CV_16SC3),
          rng_(options.seed)
    {
    }

    [[nodiscard]] const CameraCapabilities& capabilities() const override { return capabilities_; }

    [[nodiscard]] Expected<void> prepare(const CameraMode& mode) override
    {
        if (canvas_.empty()) {
            auto sky = make_synthetic_sky({.width = kCanvasWidth,
                                           .height = kCanvasHeight,
                                           .seed = options_.seed,
                                           .cloud_fraction = options_.cloud_fraction,
                                           .sun_visible = false,  // the Sun is drawn per frame: it does not drift
                                           .sun_x = 0.5,
                                           .sun_y = 0.5,
                                           .sun_radius_px = 12.0,
                                           .noise_sigma = 0.0});
            if (!sky) {
                return fail(sky.error());
            }
            canvas_ = std::move(sky->image);
        }
        mode_ = mode;
        return {};
    }

    [[nodiscard]] Expected<void> render(const Request& request, Frame& frame) override
    {
        const int width = mode_.width;
        const int height = mode_.height;

        // The part of the cloud canvas in view. The view slides to and fro over the canvas: the clouds drift
        // across the picture and come back.
        const int view_width =
            std::min(canvas_.cols, static_cast<int>(std::lround(static_cast<double>(canvas_.rows) * width / height)));
        const int travel = canvas_.cols - view_width;
        int offset = 0;
        if (travel > 0) {
            const double moved = std::abs(options_.cloud_drift) * request.stream_time_s * view_width;
            const double phase = std::fmod(moved, 2.0 * travel);
            offset = static_cast<int>(std::lround(phase <= travel ? phase : 2.0 * travel - phase));
        }
        const cv::Mat view = canvas_(cv::Rect(offset, 0, view_width, canvas_.rows));

        // A BGR8 frame is rendered straight into the frame's memory; other formats are converted at the end.
        work_.create(height, width, CV_8UC3);
        cv::Mat picture =
            mode_.format == PixelFormat::Bgr8 ? cv::Mat(height, width, CV_8UC3, frame.buffer().data()) : work_;
        cv::resize(view, picture, cv::Size(width, height), 0.0, 0.0, cv::INTER_LINEAR);

        // Exposure and gain scale the light; white balance tilts it towards red or blue.
        ControlSetting& exposure = request.settings[kExposure];
        ControlSetting& white_balance = request.settings[kWhiteBalance];
        const double gain = std::pow(10.0, request.settings[kGain].value / 20.0);
        const double scale = exposure.value / kReferenceExposureMs * gain;
        double red = 1.0;
        double blue = 1.0;
        if (white_balance.automatic) {
            white_balance.value = kNeutralWhiteBalanceK;  // automatic white balance keeps the scene neutral
        } else {
            red = std::pow(white_balance.value / kNeutralWhiteBalanceK, 0.6);
            blue = 1.0 / red;
        }
        cv::multiply(picture, cv::Scalar(scale * blue, scale, scale * red), picture);

        // Sensor noise, stronger with gain, and the brightness offset.
        rng_.fill(noise_tile_, cv::RNG::NORMAL, 0.0, 64.0);
        cv::repeat(noise_tile_, (height + kNoiseTile - 1) / kNoiseTile, (width + kNoiseTile - 1) / kNoiseTile, noise_);
        const double sigma = std::min(40.0, kSensorNoiseSigma * gain);
        cv::addWeighted(picture, 1.0, noise_(cv::Rect(0, 0, width, height)), sigma / 64.0,
                        request.settings[kBrightness].value, picture, CV_8U);

        if (options_.sun_visible) {
            draw_sun(picture, scale);
        }

        if (exposure.automatic) {
            // The camera's own automatic exposure: steer the mean level to a target, a part of the way per frame.
            const cv::Scalar mean = cv::mean(picture);
            const double level = std::max(1.0, (mean[0] + mean[1] + mean[2]) / 3.0);
            exposure.value = hal::nearest_setting(capabilities_.controls[kExposure],
                                                  exposure.value * std::sqrt(kAutoExposureTarget / level));
        }
        return store(picture, mode_.format, frame, encoded_);
    }

private:
    // A saturated disc with a glow. The disc is saturated at any exposure, as the real Sun is.
    void draw_sun(cv::Mat& picture, double scale) const
    {
        const double centre_x = options_.sun_x * (picture.cols - 1);
        const double centre_y = options_.sun_y * (picture.rows - 1);
        const double radius = std::max(2.0, 0.025 * picture.rows);
        const double reach = 6.0 * radius;
        const int first_row = std::max(0, static_cast<int>(std::floor(centre_y - reach)));
        const int last_row = std::min(picture.rows - 1, static_cast<int>(std::ceil(centre_y + reach)));
        const int first_column = std::max(0, static_cast<int>(std::floor(centre_x - reach)));
        const int last_column = std::min(picture.cols - 1, static_cast<int>(std::ceil(centre_x + reach)));
        for (int y = first_row; y <= last_row; ++y) {
            auto* row = picture.ptr<cv::Vec3b>(y);
            for (int x = first_column; x <= last_column; ++x) {
                const double distance = std::hypot(x - centre_x, y - centre_y);
                if (distance <= radius) {
                    row[x] = cv::Vec3b(255, 255, 255);
                } else if (distance < reach) {
                    const double falloff = 1.0 - (distance - radius) / (reach - radius);
                    const double glow = 120.0 * falloff * falloff * scale;
                    for (int channel = 0; channel < 3; ++channel) {
                        row[x][channel] = cv::saturate_cast<unsigned char>(row[x][channel] + glow);
                    }
                }
            }
        }
    }

    SyntheticSkyOptions options_;
    CameraCapabilities capabilities_;
    CameraMode mode_;
    cv::Mat canvas_;      // the clouds, BGR
    cv::Mat work_;        // one frame, BGR
    cv::Mat noise_tile_;  // fresh noise for each frame, repeated over the picture
    cv::Mat noise_;
    cv::RNG rng_;
    std::vector<unsigned char> encoded_;
};

// -------------------------------------------------------------------------------------- replay

// ".jpg" for "PICTURE.JPG".
std::string lower_extension(const std::filesystem::path& file)
{
    std::string extension = utf8(file.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return extension;
}

bool is_jpeg(const std::filesystem::path& file)
{
    const std::string extension = lower_extension(file);
    return extension == ".jpg" || extension == ".jpeg";
}

bool is_picture(const std::filesystem::path& file)
{
    return is_jpeg(file) || lower_extension(file) == ".png";
}

// The JPEG and PNG files directly in a folder, in name order. Empty if the folder cannot be read.
std::vector<std::filesystem::path> list_pictures(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::directory_iterator entry(folder, error);
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        if (entry->is_regular_file(error) && is_picture(entry->path())) {
            files.push_back(entry->path());
        }
        entry.increment(error);
    }
    std::ranges::sort(files);
    return files;
}

Expected<std::string> read_file(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    std::string bytes;
    if (stream) {
        stream.seekg(0, std::ios::end);
        const std::streamoff size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        if (size > 0) {
            bytes.resize(static_cast<std::size_t>(size));
            stream.read(bytes.data(), size);
        }
    }
    if (!stream || bytes.empty()) {
        return fail(ErrorCode::Io, fmt::format("{} cannot be read", utf8(file)));
    }
    return bytes;
}

// The content of a picture file as a row of bytes that OpenCV and TurboJPEG accept, without copying it.
cv::Mat as_row(std::string& bytes)
{
    return cv::Mat(1, static_cast<int>(bytes.size()), CV_8UC1, bytes.data());
}

// Width and height stored in a JPEG file; an empty size if it is not a readable JPEG.
cv::Size jpeg_size(const cv::Mat& bytes)
{
    struct Destroy {
        void operator()(void* handle) const { tjDestroy(handle); }
    };
    const std::unique_ptr<void, Destroy> handle(tjInitDecompress());
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colour_space = 0;
    if (!handle ||
        tjDecompressHeader3(handle.get(), bytes.ptr<unsigned char>(), static_cast<unsigned long>(bytes.total()), &width,
                            &height, &subsampling, &colour_space) != 0) {
        return {};
    }
    return {width, height};
}

Expected<cv::Mat> decode(std::string& bytes, const std::filesystem::path& file)
{
    cv::Mat picture;
    try {
        // The stored pixel grid, not rotated by an orientation tag: a camera stream has no such tag.
        picture = cv::imdecode(as_row(bytes), cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
    } catch (const cv::Exception&) {
        picture.release();
    }
    if (picture.empty()) {
        return fail(ErrorCode::Parse, fmt::format("{} is not a picture that can be decoded", utf8(file)));
    }
    return picture;
}

class ReplaySource final : public IFrameSource {
public:
    ReplaySource(std::vector<std::filesystem::path> files, cv::Size size, double fps)
        : files_(std::move(files)), size_(size)
    {
        capabilities_.modes = {
            {.width = size.width, .height = size.height, .format = PixelFormat::Bgr8, .fps = fps},
            {.width = size.width, .height = size.height, .format = PixelFormat::Mjpeg, .fps = fps},
        };
    }

    [[nodiscard]] const CameraCapabilities& capabilities() const override { return capabilities_; }

    [[nodiscard]] Expected<void> prepare(const CameraMode& mode) override
    {
        mode_ = mode;
        return {};
    }

    [[nodiscard]] Expected<void> render(const Request& request, Frame& frame) override
    {
        const std::filesystem::path& file = files_[request.index % files_.size()];
        auto bytes = read_file(file);
        if (!bytes) {
            return fail(bytes.error());
        }
        if (mode_.format == PixelFormat::Mjpeg && is_jpeg(file) && bytes->size() <= frame.capacity() &&
            jpeg_size(as_row(*bytes)) == size_) {
            // A JPEG file of the right size is what an MJPEG camera would send: pass it on untouched.
            std::memcpy(frame.buffer().data(), bytes->data(), bytes->size());
            frame.set_size(bytes->size());
            return {};
        }
        auto picture = decode(*bytes, file);
        if (!picture) {
            return fail(picture.error());
        }
        if (picture->size() != size_) {
            cv::resize(*picture, resized_, size_, 0.0, 0.0, cv::INTER_AREA);
            return store(resized_, mode_.format, frame, encoded_);
        }
        return store(*picture, mode_.format, frame, encoded_);
    }

private:
    std::vector<std::filesystem::path> files_;
    cv::Size size_;
    CameraCapabilities capabilities_;
    CameraMode mode_;
    cv::Mat resized_;
    std::vector<unsigned char> encoded_;
};

}  // namespace

std::unique_ptr<IFrameSource> make_synthetic_sky_source(const SyntheticSkyOptions& options)
{
    return std::make_unique<SyntheticSkySource>(options);
}

bool has_replay_pictures(const std::filesystem::path& folder)
{
    // Stops at the first picture: this is asked every time the devices are listed, and a folder may hold
    // a day of pictures.
    std::error_code error;
    std::filesystem::directory_iterator entry(folder, error);
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        if (entry->is_regular_file(error) && is_picture(entry->path())) {
            return true;
        }
        entry.increment(error);
    }
    return false;
}

Expected<std::unique_ptr<IFrameSource>> make_replay_source(const std::filesystem::path& folder, double fps)
{
    std::vector<std::filesystem::path> files = list_pictures(folder);
    if (files.empty()) {
        return fail(ErrorCode::NotFound,
                    fmt::format("replay folder {} cannot be read or holds no JPEG or PNG picture", utf8(folder)));
    }
    if (!(fps > 0.0)) {
        return fail(ErrorCode::InvalidArgument, "the replay rate must be positive");
    }
    auto bytes = read_file(files.front());
    if (!bytes) {
        return fail(bytes.error());
    }
    auto first = decode(*bytes, files.front());
    if (!first) {
        return fail(first.error());
    }
    return std::unique_ptr<IFrameSource>(std::make_unique<ReplaySource>(std::move(files), first->size(), fps));
}

}  // namespace cloudscope::sim
