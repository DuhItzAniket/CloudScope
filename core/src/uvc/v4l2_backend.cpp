// UVC cameras through Video4Linux2 (P019-P022), for Linux laptops and the Raspberry Pi.
//
// Enumeration: every /dev/video* node that reports the video-capture capability (metadata nodes are skipped);
// vendor and product ids come from sysfs (/sys/class/video4linux/videoN/device/../idVendor).
// Modes: VIDIOC_ENUM_FMT, VIDIOC_ENUM_FRAMESIZES and VIDIOC_ENUM_FRAMEINTERVALS (discrete entries).
// Controls: VIDIOC_QUERYCTRL for the standard UVC controls; exposure_time_absolute is in 100 us units and is
// converted to milliseconds; automatic exposure, white balance and focus use their own V4L2 controls.
// Frames: memory-mapped buffers, VIDIOC_DQBUF after poll(); the kernel's own sequence counter and monotonic
// timestamp are used, so lost frames show as gaps in the sequence numbers.
//
// This backend has not yet run against hardware (no Linux machine with a camera in the project at P019); CI
// compiles it, and the Raspberry Pi phase will exercise it.

#include "uvc_backend.hpp"

#include <fmt/format.h>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace cloudscope::uvc {

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the V4L2 ioctl structs are C unions by kernel design

namespace {

using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::ControlState;

constexpr unsigned kBuffers = 4;

int xioctl(int fd, unsigned long request, void* argument)
{
    int result = 0;
    do {
        result = ioctl(fd, request, argument);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    } while (result == -1 && errno == EINTR);
    return result;
}

Unexpected errno_error(ErrorCode code, const std::string& what)
{
    return fail(code,
                fmt::format("{} ({})", what, std::generic_category().message(errno)));  // not strerror: shared buffer
}

std::optional<PixelFormat> format_of(std::uint32_t fourcc)
{
    switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG:
        return PixelFormat::Mjpeg;
    case V4L2_PIX_FMT_YUYV:
        return PixelFormat::Yuyv;
    case V4L2_PIX_FMT_GREY:
        return PixelFormat::Gray8;
    case V4L2_PIX_FMT_Y16:
        return PixelFormat::Gray16;
    case V4L2_PIX_FMT_BGR24:
        return PixelFormat::Bgr8;
    case V4L2_PIX_FMT_RGB24:
        return PixelFormat::Rgb8;
    default:
        return std::nullopt;
    }
}

std::uint32_t fourcc_of(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Mjpeg:
        return V4L2_PIX_FMT_MJPEG;
    case PixelFormat::Yuyv:
        return V4L2_PIX_FMT_YUYV;
    case PixelFormat::Gray8:
        return V4L2_PIX_FMT_GREY;
    case PixelFormat::Gray16:
        return V4L2_PIX_FMT_Y16;
    case PixelFormat::Bgr8:
        return V4L2_PIX_FMT_BGR24;
    case PixelFormat::Rgb8:
        return V4L2_PIX_FMT_RGB24;
    }
    return 0;
}

struct ControlBinding {
    CameraControl control;
    std::uint32_t id;
    std::uint32_t auto_id;  // 0: no automatic mode
    double scale;           // value * scale = calibrated value
    const char* unit;
};

constexpr std::array<ControlBinding, 9> kBindings = {{
    {.control = CameraControl::Exposure,
     .id = V4L2_CID_EXPOSURE_ABSOLUTE,
     .auto_id = V4L2_CID_EXPOSURE_AUTO,
     .scale = 0.1,
     .unit = "ms"},  // 100 us units
    {.control = CameraControl::Gain, .id = V4L2_CID_GAIN, .auto_id = V4L2_CID_AUTOGAIN, .scale = 1.0, .unit = ""},
    {.control = CameraControl::WhiteBalance,
     .id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
     .auto_id = V4L2_CID_AUTO_WHITE_BALANCE,
     .scale = 1.0,
     .unit = "K"},
    {.control = CameraControl::Brightness,
     .id = V4L2_CID_BRIGHTNESS,
     .auto_id = V4L2_CID_AUTOBRIGHTNESS,
     .scale = 1.0,
     .unit = ""},
    {.control = CameraControl::Contrast, .id = V4L2_CID_CONTRAST, .auto_id = 0, .scale = 1.0, .unit = ""},
    {.control = CameraControl::Saturation, .id = V4L2_CID_SATURATION, .auto_id = 0, .scale = 1.0, .unit = ""},
    {.control = CameraControl::Gamma, .id = V4L2_CID_GAMMA, .auto_id = 0, .scale = 1.0, .unit = ""},
    {.control = CameraControl::Sharpness, .id = V4L2_CID_SHARPNESS, .auto_id = 0, .scale = 1.0, .unit = ""},
    {.control = CameraControl::Focus,
     .id = V4L2_CID_FOCUS_ABSOLUTE,
     .auto_id = V4L2_CID_FOCUS_AUTO,
     .scale = 1.0,
     .unit = ""},
}};

const ControlBinding* binding_of(CameraControl control)
{
    for (const ControlBinding& binding : kBindings) {
        if (binding.control == control) {
            return &binding;
        }
    }
    return nullptr;
}

std::optional<std::uint16_t> sysfs_hex(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::string text;
    if (!in || !(in >> text)) {
        return std::nullopt;
    }
    try {
        return static_cast<std::uint16_t>(std::stoul(text, nullptr, 16));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string sysfs_text(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::string text;
    if (in) {
        std::getline(in, text);
    }
    return text;
}

struct MappedBuffer {
    void* start = nullptr;
    std::size_t length = 0;
};

class V4l2Backend final : public IUvcBackend {
public:
    V4l2Backend(UvcDeviceDescriptor descriptor, const IClock& clock) : descriptor_(std::move(descriptor)), clock_(clock)
    {
    }
    ~V4l2Backend() override { V4l2Backend::close(); }
    V4l2Backend(const V4l2Backend&) = delete;
    V4l2Backend& operator=(const V4l2Backend&) = delete;
    V4l2Backend(V4l2Backend&&) = delete;
    V4l2Backend& operator=(V4l2Backend&&) = delete;

    Expected<void> open() override;
    void close() override;
    Expected<CameraCapabilities> capabilities() override { return capabilities_; }
    Expected<CameraMode> mode() override { return mode_; }
    Expected<CameraMode> set_mode(const CameraMode& mode) override;
    Expected<ControlState> set_control(CameraControl control, ControlSetting setting) override;
    Expected<ControlSetting> control(CameraControl control) override;
    Expected<void> start() override;
    void stop() override;
    Expected<void> read_frame(Frame& frame, std::chrono::milliseconds timeout) override;

private:
    Expected<void> read_modes();
    void read_controls();
    [[nodiscard]] Expected<ControlSetting> read_setting(const ControlBinding& binding) const;
    [[nodiscard]] bool automatic_on(const ControlBinding& binding) const;
    [[nodiscard]] Expected<void> apply_mode(const CameraMode& mode);
    void unmap();

    UvcDeviceDescriptor descriptor_;
    const IClock& clock_;
    int fd_ = -1;
    CameraCapabilities capabilities_;
    CameraMode mode_;
    std::vector<MappedBuffer> buffers_;
    bool streaming_ = false;
};

Expected<void> V4l2Backend::open()
{
    if (fd_ >= 0) {
        return {};
    }
    fd_ = ::open(descriptor_.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        const ErrorCode code = errno == ENOENT || errno == ENODEV ? ErrorCode::NotFound
                               : errno == EBUSY                   ? ErrorCode::Unavailable
                                                                  : ErrorCode::Io;
        return errno_error(code, fmt::format("camera '{}' could not be opened", descriptor_.name));
    }
    if (auto modes = read_modes(); !modes) {
        close();
        return modes;
    }
    read_controls();
    return {};
}

void V4l2Backend::close()
{
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    capabilities_ = {};
}

Expected<void> V4l2Backend::read_modes()
{
    capabilities_.modes.clear();
    for (std::uint32_t f = 0;; ++f) {
        v4l2_fmtdesc fmt{};
        fmt.index = f;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_ENUM_FMT, &fmt) < 0) {
            break;
        }
        const std::optional<PixelFormat> format = format_of(fmt.pixelformat);
        if (!format) {
            continue;
        }
        for (std::uint32_t s = 0;; ++s) {
            v4l2_frmsizeenum size{};
            size.index = s;
            size.pixel_format = fmt.pixelformat;
            if (xioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &size) < 0) {
                break;
            }
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                break;
            }
            for (std::uint32_t i = 0;; ++i) {
                v4l2_frmivalenum interval{};
                interval.index = i;
                interval.pixel_format = fmt.pixelformat;
                interval.width = size.discrete.width;
                interval.height = size.discrete.height;
                if (xioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0) {
                    break;
                }
                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE || interval.discrete.numerator == 0) {
                    break;
                }
                const CameraMode mode{.width = static_cast<int>(size.discrete.width),
                                      .height = static_cast<int>(size.discrete.height),
                                      .format = *format,
                                      .fps = static_cast<double>(interval.discrete.denominator) /
                                             static_cast<double>(interval.discrete.numerator)};
                if (std::ranges::find(capabilities_.modes, mode) == capabilities_.modes.end()) {
                    capabilities_.modes.push_back(mode);
                }
            }
        }
    }
    if (capabilities_.modes.empty()) {
        return fail(ErrorCode::Unsupported, fmt::format("camera '{}' offers no usable pixel format", descriptor_.name));
    }
    // The device's current format, if listed; otherwise the first listed mode is applied.
    v4l2_format current{};
    current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mode_ = capabilities_.modes.front();
    if (xioctl(fd_, VIDIOC_G_FMT, &current) == 0) {
        const std::optional<PixelFormat> format = format_of(current.fmt.pix.pixelformat);
        for (const CameraMode& mode : capabilities_.modes) {
            if (format == mode.format && mode.width == static_cast<int>(current.fmt.pix.width) &&
                mode.height == static_cast<int>(current.fmt.pix.height)) {
                mode_ = mode;
                return {};
            }
        }
    }
    return apply_mode(mode_);
}

void V4l2Backend::read_controls()
{
    capabilities_.controls.clear();
    for (const ControlBinding& binding : kBindings) {
        v4l2_queryctrl query{};
        query.id = binding.id;
        if (xioctl(fd_, VIDIOC_QUERYCTRL, &query) < 0 || (query.flags & V4L2_CTRL_FLAG_DISABLED) != 0) {
            continue;
        }
        bool has_auto = false;
        if (binding.auto_id != 0) {
            v4l2_queryctrl auto_query{};
            auto_query.id = binding.auto_id;
            has_auto =
                xioctl(fd_, VIDIOC_QUERYCTRL, &auto_query) == 0 && (auto_query.flags & V4L2_CTRL_FLAG_DISABLED) == 0;
        }
        ControlInfo info;
        info.control = binding.control;
        info.minimum = query.minimum * binding.scale;
        info.maximum = query.maximum * binding.scale;
        info.step = std::max(query.step, 0) * binding.scale;
        info.default_value = std::clamp(query.default_value * binding.scale, info.minimum, info.maximum);
        info.unit = binding.unit;
        info.calibrated = binding.unit[0] != '\0';
        info.supports_auto = has_auto;
        capabilities_.controls.push_back(info);
    }
}

Expected<void> V4l2Backend::apply_mode(const CameraMode& mode)
{
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<std::uint32_t>(mode.width);
    fmt.fmt.pix.height = static_cast<std::uint32_t>(mode.height);
    fmt.fmt.pix.pixelformat = fourcc_of(mode.format);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        return errno_error(ErrorCode::Io, fmt::format("camera '{}' refused the mode", descriptor_.name));
    }
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1000;
    parm.parm.capture.timeperframe.denominator = static_cast<std::uint32_t>(std::lround(mode.fps * 1000.0));
    xioctl(fd_, VIDIOC_S_PARM, &parm);  // a device that cannot set the rate keeps its own; the mode's fps is nominal
    mode_ = mode;
    return {};
}

Expected<CameraMode> V4l2Backend::set_mode(const CameraMode& mode)
{
    if (std::ranges::find(capabilities_.modes, mode) == capabilities_.modes.end()) {
        return fail(ErrorCode::InvalidArgument, "mode not listed");
    }
    if (auto applied = apply_mode(mode); !applied) {
        return fail(applied.error());
    }
    return mode_;
}

bool V4l2Backend::automatic_on(const ControlBinding& binding) const
{
    if (binding.auto_id == 0) {
        return false;
    }
    v4l2_control automatic{};
    automatic.id = binding.auto_id;
    if (xioctl(fd_, VIDIOC_G_CTRL, &automatic) < 0) {
        return false;
    }
    if (binding.auto_id == V4L2_CID_EXPOSURE_AUTO) {
        return automatic.value != V4L2_EXPOSURE_MANUAL;
    }
    return automatic.value != 0;
}

Expected<ControlSetting> V4l2Backend::read_setting(const ControlBinding& binding) const
{
    v4l2_control value{};
    value.id = binding.id;
    if (xioctl(fd_, VIDIOC_G_CTRL, &value) < 0) {
        return errno_error(ErrorCode::Io, fmt::format("control '{}' could not be read", to_string(binding.control)));
    }
    return ControlSetting{.value = value.value * binding.scale, .automatic = automatic_on(binding)};
}

Expected<ControlState> V4l2Backend::set_control(CameraControl control, ControlSetting setting)
{
    const ControlBinding* binding = binding_of(control);
    const auto info = std::ranges::find(capabilities_.controls, control, &ControlInfo::control);
    if (binding == nullptr || info == capabilities_.controls.end()) {
        return fail(ErrorCode::Unsupported, fmt::format("control '{}' is not offered", to_string(control)));
    }
    const bool want_auto = setting.automatic && info->supports_auto;
    if (binding->auto_id != 0 && info->supports_auto) {
        v4l2_control automatic{};
        automatic.id = binding->auto_id;
        if (binding->auto_id == V4L2_CID_EXPOSURE_AUTO) {
            automatic.value = want_auto ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL;
        } else {
            automatic.value = want_auto ? 1 : 0;
        }
        xioctl(fd_, VIDIOC_S_CTRL, &automatic);
    }
    if (!want_auto) {
        v4l2_control value{};
        value.id = binding->id;
        value.value =
            static_cast<std::int32_t>(std::lround(hal::nearest_setting(*info, setting.value) / binding->scale));
        if (xioctl(fd_, VIDIOC_S_CTRL, &value) < 0) {
            return errno_error(ErrorCode::Io, fmt::format("control '{}' could not be set", to_string(control)));
        }
    }
    const auto effective = read_setting(*binding);
    if (!effective) {
        return fail(effective.error());
    }
    const bool same = std::abs(effective->value - setting.value) <= 1e-9 * std::max(1.0, std::abs(setting.value));
    const bool applied = effective->automatic == setting.automatic && (effective->automatic || same);
    return ControlState{.requested = setting, .effective = *effective, .applied = applied};
}

Expected<ControlSetting> V4l2Backend::control(CameraControl control)
{
    const ControlBinding* binding = binding_of(control);
    if (binding == nullptr || std::ranges::count(capabilities_.controls, control, &ControlInfo::control) == 0) {
        return fail(ErrorCode::Unsupported, fmt::format("control '{}' is not offered", to_string(control)));
    }
    return read_setting(*binding);
}

Expected<void> V4l2Backend::start()
{
    if (streaming_) {
        return {};
    }
    v4l2_requestbuffers request{};
    request.count = kBuffers;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
        return errno_error(ErrorCode::Io, fmt::format("camera '{}' gave no capture buffers", descriptor_.name));
    }
    buffers_.resize(request.count);
    for (std::uint32_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
            unmap();
            return errno_error(ErrorCode::Io, "VIDIOC_QUERYBUF failed");
        }
        void* start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);
        if (start == MAP_FAILED) {
            unmap();
            return errno_error(ErrorCode::Io, "mmap of a capture buffer failed");
        }
        buffers_[i] = {.start = start, .length = buffer.length};
        if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            unmap();
            return errno_error(ErrorCode::Io, "VIDIOC_QBUF failed");
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        unmap();
        return errno_error(ErrorCode::Io, fmt::format("camera '{}' did not start streaming", descriptor_.name));
    }
    streaming_ = true;
    return {};
}

void V4l2Backend::unmap()
{
    for (const MappedBuffer& buffer : buffers_) {
        if (buffer.start != nullptr) {
            munmap(buffer.start, buffer.length);
        }
    }
    buffers_.clear();
    v4l2_requestbuffers release{};
    release.count = 0;
    release.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    release.memory = V4L2_MEMORY_MMAP;
    if (fd_ >= 0) {
        xioctl(fd_, VIDIOC_REQBUFS, &release);
    }
}

void V4l2Backend::stop()
{
    if (!streaming_) {
        return;
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
    unmap();
    streaming_ = false;
}

Expected<void> V4l2Backend::read_frame(Frame& frame, std::chrono::milliseconds timeout)
{
    if (!streaming_) {
        return fail(ErrorCode::Unavailable, "not streaming");
    }
    pollfd waiter{};
    waiter.fd = fd_;
    waiter.events = POLLIN;
    const int ready = poll(&waiter, 1, static_cast<int>(std::min<long long>(timeout.count(), 1'000'000)));
    if (ready == 0) {
        return fail(ErrorCode::Timeout, fmt::format("camera '{}' delivered no frame in time", descriptor_.name));
    }
    if (ready < 0 || (waiter.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        stop();
        return errno_error(ErrorCode::Io, fmt::format("camera '{}' failed while streaming", descriptor_.name));
    }
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {
            return fail(ErrorCode::Timeout, fmt::format("camera '{}' delivered no frame in time", descriptor_.name));
        }
        stop();
        return errno_error(ErrorCode::Io, fmt::format("camera '{}' failed while streaming", descriptor_.name));
    }
    const Timestamp captured = clock_.now();
    const std::size_t stride = static_cast<std::size_t>(mode_.width) * bytes_per_pixel(mode_.format);
    const std::size_t needed =
        mode_.format == PixelFormat::Mjpeg ? buffer.bytesused : stride * static_cast<std::size_t>(mode_.height);
    Expected<void> result;
    if (frame.capacity() < needed || buffer.bytesused < needed) {
        result = fail(ErrorCode::InvalidArgument,
                      fmt::format("a frame of {} bytes does not fit ({} bytes of buffer, {} bytes from the camera)",
                                  needed, frame.capacity(), buffer.bytesused));
    } else {
        std::memcpy(frame.buffer().data(), buffers_[buffer.index].start, needed);
        frame.set_size(needed);
    }
    xioctl(fd_, VIDIOC_QBUF, &buffer);
    if (!result) {
        return result;
    }
    frame.info() = {.sequence = buffer.sequence,
                    .captured = captured,
                    .width = mode_.width,
                    .height = mode_.height,
                    .format = mode_.format,
                    .stride = mode_.format == PixelFormat::Mjpeg ? 0 : stride,
                    .simulated = false};
    return {};
}

}  // namespace

Expected<std::vector<UvcDeviceDescriptor>> enumerate_platform_cameras()
{
    std::vector<UvcDeviceDescriptor> devices;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator("/dev", error)) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("video")) {
            continue;
        }
        const int fd = ::open(entry.path().c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        v4l2_capability capability{};
        const bool ok = xioctl(fd, VIDIOC_QUERYCAP, &capability) == 0;
        ::close(fd);
        if (!ok) {
            continue;
        }
        const std::uint32_t caps =
            (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? capability.device_caps : capability.capabilities;
        if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0 || (caps & V4L2_CAP_STREAMING) == 0) {
            continue;
        }
        UvcDeviceDescriptor descriptor;
        descriptor.path = entry.path().string();
        descriptor.name =
            reinterpret_cast<const char*>(capability.card);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        const std::filesystem::path sysfs = std::filesystem::path("/sys/class/video4linux") / name / "device";
        for (const std::filesystem::path& base : {sysfs / "..", sysfs}) {
            if (const auto vendor = sysfs_hex(base / "idVendor")) {
                descriptor.vendor_id = *vendor;
                descriptor.product_id = sysfs_hex(base / "idProduct").value_or(0);
                descriptor.serial = sysfs_text(base / "serial");
                break;
            }
        }
        devices.push_back(std::move(descriptor));
    }
    std::ranges::sort(devices, {}, &UvcDeviceDescriptor::path);
    return devices;
}

std::unique_ptr<IUvcBackend> make_platform_backend(const UvcDeviceDescriptor& descriptor, const IClock& clock)
{
    return std::make_unique<V4l2Backend>(descriptor, clock);
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

}  // namespace cloudscope::uvc
