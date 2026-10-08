#include "cloudscope/capture/decode.hpp"

#include <fmt/format.h>
#include <opencv2/imgproc.hpp>
#include <turbojpeg.h>

#include <cstring>

namespace cloudscope {

namespace {

// One TurboJPEG decompressor per thread: creating one costs more than decoding a small picture.
class JpegDecoder {
public:
    JpegDecoder() : handle_(tjInitDecompress()) {}
    ~JpegDecoder()
    {
        if (handle_ != nullptr) {
            tjDestroy(handle_);
        }
    }
    JpegDecoder(const JpegDecoder&) = delete;
    JpegDecoder& operator=(const JpegDecoder&) = delete;
    JpegDecoder(JpegDecoder&&) = delete;
    JpegDecoder& operator=(JpegDecoder&&) = delete;

    [[nodiscard]] tjhandle handle() const { return handle_; }

private:
    tjhandle handle_;
};

tjhandle decoder()
{
    thread_local JpegDecoder instance;
    return instance.handle();
}

const unsigned char* bytes_of(std::span<const std::byte> data)
{
    return reinterpret_cast<const unsigned char*>(data.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

std::string jpeg_error(tjhandle handle)
{
    const char* text = handle != nullptr ? tjGetErrorStr2(handle) : nullptr;
    return text != nullptr ? text : "TurboJPEG error";
}

Expected<cv::Mat> decode_mjpeg(std::span<const std::byte> data)
{
    const tjhandle handle = decoder();
    if (handle == nullptr) {
        return fail(ErrorCode::Internal, "TurboJPEG decompressor could not be created");
    }
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colourspace = 0;
    if (tjDecompressHeader3(handle, bytes_of(data), static_cast<unsigned long>(data.size()), &width, &height,
                            &subsampling, &colourspace) != 0) {
        return fail(ErrorCode::Parse, fmt::format("not a JPEG: {}", jpeg_error(handle)));
    }
    cv::Mat image(height, width, CV_8UC3);
    if (tjDecompress2(handle, bytes_of(data), static_cast<unsigned long>(data.size()), image.data, width,
                      static_cast<int>(image.step[0]), height, TJPF_BGR, TJFLAG_FASTDCT) != 0) {
        return fail(ErrorCode::Parse, fmt::format("corrupt JPEG: {}", jpeg_error(handle)));
    }
    return image;
}

Expected<cv::Mat> wrap_uncompressed(const Frame& frame, int type)
{
    const FrameInfo& info = frame.info();
    const std::size_t stride = info.stride != 0 ? info.stride
                                                : static_cast<std::size_t>(info.width) * bytes_per_pixel(info.format);
    const std::size_t needed = stride * static_cast<std::size_t>(info.height);
    if (info.width <= 0 || info.height <= 0 || frame.data().size() < needed) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("a {}x{} {} frame needs {} bytes, the frame holds {}", info.width, info.height,
                                to_string(info.format), needed, frame.data().size()));
    }
    // A Mat over the frame's bytes (read-only use), cloned so that the result owns its memory.
    const cv::Mat view(info.height, info.width, type,
                       const_cast<std::byte*>(frame.data().data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                       stride);
    return view.clone();
}

}  // namespace

Expected<JpegInfo> jpeg_info(std::span<const std::byte> jpeg)
{
    const tjhandle handle = decoder();
    if (handle == nullptr) {
        return fail(ErrorCode::Internal, "TurboJPEG decompressor could not be created");
    }
    JpegInfo info;
    int colourspace = 0;
    if (tjDecompressHeader3(handle, bytes_of(jpeg), static_cast<unsigned long>(jpeg.size()), &info.width,
                            &info.height, &info.subsampling, &colourspace) != 0) {
        return fail(ErrorCode::Parse, fmt::format("not a JPEG: {}", jpeg_error(handle)));
    }
    return info;
}

Expected<cv::Mat> decode_native(const Frame& frame)
{
    switch (frame.info().format) {
    case PixelFormat::Mjpeg:
        return decode_mjpeg(frame.data());
    case PixelFormat::Gray8:
        return wrap_uncompressed(frame, CV_8UC1);
    case PixelFormat::Gray16:
        return wrap_uncompressed(frame, CV_16UC1);
    case PixelFormat::Bgr8:
        return wrap_uncompressed(frame, CV_8UC3);
    case PixelFormat::Rgb8: {
        auto rgb = wrap_uncompressed(frame, CV_8UC3);
        if (!rgb) {
            return rgb;
        }
        cv::cvtColor(*rgb, *rgb, cv::COLOR_RGB2BGR);
        return rgb;
    }
    case PixelFormat::Yuyv: {
        auto yuyv = wrap_uncompressed(frame, CV_8UC2);
        if (!yuyv) {
            return yuyv;
        }
        cv::Mat bgr;
        cv::cvtColor(*yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        return bgr;
    }
    }
    return fail(ErrorCode::Unsupported, "unknown pixel format");
}

Expected<cv::Mat> decode_bgr8(const Frame& frame)
{
    auto native = decode_native(frame);
    if (!native) {
        return native;
    }
    cv::Mat& image = *native;
    if (image.type() == CV_16UC1) {
        cv::Mat eight;
        image.convertTo(eight, CV_8U, 1.0 / 256.0);
        image = eight;
    }
    if (image.channels() == 1) {
        cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
    }
    return image;
}

Expected<cv::Mat> decode_gray8(const Frame& frame)
{
    auto native = decode_native(frame);
    if (!native) {
        return native;
    }
    cv::Mat& image = *native;
    if (image.type() == CV_16UC1) {
        cv::Mat eight;
        image.convertTo(eight, CV_8U, 1.0 / 256.0);
        return eight;
    }
    if (image.channels() == 3) {
        cv::cvtColor(image, image, cv::COLOR_BGR2GRAY);
    }
    return image;
}

}  // namespace cloudscope
