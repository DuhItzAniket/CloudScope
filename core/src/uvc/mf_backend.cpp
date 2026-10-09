// UVC cameras through Windows Media Foundation (P019-P022).
//
// Enumeration: MFEnumDeviceSources lists video-capture devices with a friendly name and a symbolic link; the
// link carries the USB vendor and product ids ("...#vid_0c45&pid_636d&mi_00#...").
// Modes: the source reader's native media types (subtype, frame size, frame rate), with converters disabled so
// that the camera's own formats are seen, not what Media Foundation could convert them into.
// Controls: the DirectShow interfaces IAMCameraControl (exposure, focus) and IAMVideoProcAmp (brightness,
// contrast, saturation, gain, white balance, gamma, sharpness) that a capture media source exposes.
// Exposure is reported by the driver on a log2(seconds) scale; it is converted to milliseconds here, and the
// read-back shows the power of two the camera really uses.
// Frames: the source reader works asynchronously; its callback keeps the newest three samples and drops older
// ones. read_frame() copies one sample into the caller's frame. Lost frames are seen as gaps in the sample
// timestamps (one frame period apart) and counted into the sequence number, so a consumer sees a gap.

#include "cloudscope/capture/frame.hpp"
#include "uvc_backend.hpp"

#include <fmt/format.h>

#include <windows.h>

#include <dshow.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace cloudscope::uvc {

namespace {

using hal::CameraCapabilities;
using hal::CameraControl;
using hal::CameraMode;
using hal::ControlInfo;
using hal::ControlSetting;
using hal::ControlState;
using Microsoft::WRL::ComPtr;

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr std::size_t kBufferedSamples = 3;

std::string utf8(const wchar_t* text)
{
    if (text == nullptr) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(length - 1, 0)), L'\0');
    if (length > 1) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), length);
    }
    return out;
}

std::string hresult_text(HRESULT hr)
{
    return fmt::format("HRESULT 0x{:08x}", static_cast<unsigned long>(hr));
}

Unexpected com_error(ErrorCode code, const std::string& what, HRESULT hr)
{
    return fail(code, fmt::format("{} ({})", what, hresult_text(hr)));
}

// "vid_0c45&pid_636d" inside a symbolic link, in either case.
std::optional<std::uint16_t> hex_after(const std::string& link, const std::string& key)
{
    std::string lower(link);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::size_t at = lower.find(key);
    if (at == std::string::npos || at + key.size() + 4 > lower.size()) {
        return std::nullopt;
    }
    try {
        return static_cast<std::uint16_t>(std::stoul(lower.substr(at + key.size(), 4), nullptr, 16));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Media Foundation and COM, started once per process and never shut down while the program runs: cameras are
// used until exit, and MFShutdown while a reader exists would be an error.
bool ensure_media_foundation()
{
    static const bool started = [] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
            return false;
        }
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
    }();
    return started;
}

struct SubtypeMapping {
    GUID subtype;
    PixelFormat format;
};

const SubtypeMapping kSubtypes[] = {
    {MFVideoFormat_MJPG, PixelFormat::Mjpeg},
    {MFVideoFormat_YUY2, PixelFormat::Yuyv},
    {MFVideoFormat_RGB24, PixelFormat::Bgr8},
};

std::optional<PixelFormat> format_of(const GUID& subtype)
{
    for (const SubtypeMapping& mapping : kSubtypes) {
        if (IsEqualGUID(mapping.subtype, subtype)) {
            return mapping.format;
        }
    }
    return std::nullopt;
}

// One camera control as the DirectShow interfaces know it.
struct ControlBinding {
    CameraControl control;
    bool camera_control;  // true: IAMCameraControl, false: IAMVideoProcAmp
    long property;
    bool log2_exposure;  // DirectShow exposure is log2(seconds)
    const char* unit;    // "" when the scale is the driver's own
};

const ControlBinding kBindings[] = {
    {CameraControl::Exposure, true, CameraControl_Exposure, true, "ms"},
    {CameraControl::Focus, true, CameraControl_Focus, false, ""},
    {CameraControl::Gain, false, VideoProcAmp_Gain, false, ""},
    {CameraControl::WhiteBalance, false, VideoProcAmp_WhiteBalance, false, "K"},
    {CameraControl::Brightness, false, VideoProcAmp_Brightness, false, ""},
    {CameraControl::Contrast, false, VideoProcAmp_Contrast, false, ""},
    {CameraControl::Saturation, false, VideoProcAmp_Saturation, false, ""},
    {CameraControl::Gamma, false, VideoProcAmp_Gamma, false, ""},
    {CameraControl::Sharpness, false, VideoProcAmp_Sharpness, false, ""},
};

const ControlBinding* binding_of(CameraControl control)
{
    for (const ControlBinding& binding : kBindings) {
        if (binding.control == control) {
            return &binding;
        }
    }
    return nullptr;
}

double exposure_ms_from_log2(long value)
{
    return std::ldexp(1000.0, static_cast<int>(value));
}

long log2_from_exposure_ms(double ms, long minimum, long maximum)
{
    const double exponent = std::log2(std::max(ms, 1e-9) / 1000.0);
    return std::clamp(static_cast<long>(std::lround(exponent)), minimum, maximum);
}

bool nearly(double a, double b)
{
    return std::abs(a - b) <= 1e-9 * std::max({1.0, std::abs(a), std::abs(b)});
}

// A sample that arrived, with the host time of its arrival.
struct Arrival {
    ComPtr<IMFSample> sample;
    LONGLONG device_time = 0;  // 100 ns units, the source's clock
    Timestamp captured;
};

// Receives samples from the source reader on Media Foundation's thread.
class SampleSink final : public IMFSourceReaderCallback {
public:
    explicit SampleSink(const IClock& clock) : clock_(clock) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override
    {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == __uuidof(IMFSourceReaderCallback) || riid == __uuidof(IUnknown)) {
            *out = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++references_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG left = static_cast<ULONG>(--references_);
        if (left == 0) {
            delete this;
        }
        return left;
    }

    // IMFSourceReaderCallback
    STDMETHODIMP OnReadSample(HRESULT status, DWORD /*stream*/, DWORD flags, LONGLONG timestamp,
                              IMFSample* sample) override
    {
        {
            const std::lock_guard lock(mutex_);
            if (FAILED(status)) {
                error_ = status;
            } else if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
                error_ = MF_E_END_OF_STREAM;
            } else if (sample != nullptr) {
                if (arrivals_.size() >= kBufferedSamples) {
                    arrivals_.pop_front();
                    ++overflow_;
                }
                arrivals_.push_back({.sample = sample, .device_time = timestamp, .captured = clock_.now()});
            }
        }
        ready_.notify_all();
        if (SUCCEEDED(status) && (flags & MF_SOURCE_READERF_ENDOFSTREAM) == 0 && reader_ != nullptr &&
            running_.load()) {
            reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return S_OK;
    }
    STDMETHODIMP OnFlush(DWORD /*stream*/) override
    {
        {
            const std::lock_guard lock(mutex_);
            flushed_ = true;
        }
        ready_.notify_all();
        return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD /*stream*/, IMFMediaEvent* /*event*/) override { return S_OK; }

    // The reader whose samples arrive here (not owned); set once after the reader is made.
    void attach(IMFSourceReader* reader)
    {
        const std::lock_guard lock(mutex_);
        reader_ = reader;
    }

    // Forgets queued samples and errors; samples arriving from now on are kept and the next one is requested
    // after each (streaming), or dropped and not requested again (stopped).
    void set_running(bool running)
    {
        {
            const std::lock_guard lock(mutex_);
            arrivals_.clear();
            overflow_ = 0;
            error_.reset();
            flushed_ = false;
        }
        running_.store(running);
    }

    // Waits for a sample. Returns the sample, or nothing on timeout; `error` is set when the stream failed.
    std::optional<Arrival> take(std::chrono::milliseconds timeout, std::optional<HRESULT>& error,
                                std::uint64_t& overflow)
    {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, timeout, [this] { return !arrivals_.empty() || error_.has_value(); });
        error = error_;
        overflow = overflow_;
        overflow_ = 0;
        if (arrivals_.empty()) {
            return std::nullopt;
        }
        Arrival arrival = std::move(arrivals_.front());
        arrivals_.pop_front();
        return arrival;
    }

    bool wait_flushed(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return ready_.wait_for(lock, timeout, [this] { return flushed_; });
    }

private:
    ~SampleSink() = default;

    const IClock& clock_;
    std::atomic<long> references_{1};
    std::atomic<bool> running_{false};
    IMFSourceReader* reader_ = nullptr;  // not owned
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Arrival> arrivals_;
    std::uint64_t overflow_ = 0;
    std::optional<HRESULT> error_;
    bool flushed_ = false;
};

struct NativeMode {
    CameraMode mode;
    DWORD index = 0;  // index among the native media types
};

class MediaFoundationBackend final : public IUvcBackend {
public:
    MediaFoundationBackend(UvcDeviceDescriptor descriptor, const IClock& clock)
        : descriptor_(std::move(descriptor)), clock_(clock)
    {
    }
    ~MediaFoundationBackend() override { MediaFoundationBackend::close(); }

    Expected<void> open() override;
    void close() override;
    Expected<CameraCapabilities> capabilities() override;
    Expected<CameraMode> mode() override;
    Expected<CameraMode> set_mode(const CameraMode& mode) override;
    Expected<ControlState> set_control(CameraControl control, ControlSetting setting) override;
    Expected<ControlSetting> control(CameraControl control) override;
    Expected<void> start() override;
    void stop() override;
    Expected<void> read_frame(Frame& frame, std::chrono::milliseconds timeout) override;

private:
    Expected<void> read_native_modes();
    Expected<void> read_controls();
    [[nodiscard]] Expected<ControlInfo> describe(const ControlBinding& binding) const;
    [[nodiscard]] HRESULT get_raw(const ControlBinding& binding, long& value, long& flags) const;
    [[nodiscard]] HRESULT set_raw(const ControlBinding& binding, long value, long flags) const;
    [[nodiscard]] HRESULT range_raw(const ControlBinding& binding, long& minimum, long& maximum, long& step,
                                    long& fallback, long& flags) const;
    [[nodiscard]] Expected<ControlSetting> read_setting(const ControlBinding& binding) const;

    UvcDeviceDescriptor descriptor_;
    const IClock& clock_;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
    ComPtr<IAMCameraControl> camera_control_;
    ComPtr<IAMVideoProcAmp> proc_amp_;
    SampleSink* sink_ = nullptr;  // owned through its COM reference
    std::vector<NativeMode> native_modes_;
    CameraCapabilities capabilities_;
    std::size_t current_ = 0;  // index into native_modes_
    bool open_ = false;
    bool streaming_ = false;
    std::uint64_t next_sequence_ = 0;
    std::optional<LONGLONG> last_device_time_;
};

Expected<void> MediaFoundationBackend::open()
{
    if (open_) {
        return {};
    }
    if (!ensure_media_foundation()) {
        return fail(ErrorCode::Unavailable, "Media Foundation could not be started");
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "MFCreateAttributes failed", hr);
    }
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, wide(descriptor_.path).c_str());
    hr = MFCreateDeviceSource(attributes.Get(), &source_);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == MF_E_NOT_FOUND || hr == E_INVALIDARG) {
        return com_error(ErrorCode::NotFound, fmt::format("camera '{}' is not connected", descriptor_.name), hr);
    }
    if (FAILED(hr)) {
        return com_error(ErrorCode::Unavailable, fmt::format("camera '{}' could not be opened", descriptor_.name), hr);
    }
    ComPtr<IMFAttributes> reader_attributes;
    hr = MFCreateAttributes(&reader_attributes, 3);
    if (FAILED(hr)) {
        source_.Reset();
        return com_error(ErrorCode::Io, "MFCreateAttributes failed", hr);
    }
    // One reader for the camera's lifetime, asynchronous so that read_frame() can wait with a timeout. A media
    // source serves one reader only; a second reader for streaming is refused.
    sink_ = new SampleSink(clock_);
    reader_attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    reader_attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    reader_attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, sink_);
    hr = MFCreateSourceReaderFromMediaSource(source_.Get(), reader_attributes.Get(), &reader_);
    if (FAILED(hr)) {
        sink_->Release();
        sink_ = nullptr;
        source_.Reset();
        return com_error(hr == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED ? ErrorCode::NotFound : ErrorCode::Unavailable,
                         fmt::format("camera '{}' refused a source reader", descriptor_.name), hr);
    }
    sink_->attach(reader_.Get());
    source_.As(&camera_control_);
    source_.As(&proc_amp_);
    if (auto modes = read_native_modes(); !modes) {
        close();
        return modes;
    }
    if (auto controls = read_controls(); !controls) {
        close();
        return controls;
    }
    open_ = true;
    return {};
}

void MediaFoundationBackend::close()
{
    stop();
    camera_control_.Reset();
    proc_amp_.Reset();
    reader_.Reset();
    if (sink_ != nullptr) {
        sink_->attach(nullptr);
        sink_->Release();
        sink_ = nullptr;
    }
    if (source_) {
        source_->Shutdown();
        source_.Reset();
    }
    native_modes_.clear();
    capabilities_ = {};
    current_ = 0;
    open_ = false;
}

Expected<void> MediaFoundationBackend::read_native_modes()
{
    native_modes_.clear();
    capabilities_.modes.clear();
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader_->GetNativeMediaType(kVideoStream, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            break;
        }
        if (FAILED(hr)) {
            return com_error(ErrorCode::Io, "GetNativeMediaType failed", hr);
        }
        GUID subtype{};
        UINT32 width = 0;
        UINT32 height = 0;
        UINT32 rate_numerator = 0;
        UINT32 rate_denominator = 1;
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
            continue;
        }
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &rate_numerator, &rate_denominator);
        const std::optional<PixelFormat> format = format_of(subtype);
        if (!format || width == 0 || height == 0 || rate_numerator == 0 || rate_denominator == 0) {
            continue;
        }
        const CameraMode mode{.width = static_cast<int>(width),
                              .height = static_cast<int>(height),
                              .format = *format,
                              .fps = static_cast<double>(rate_numerator) / static_cast<double>(rate_denominator)};
        if (std::ranges::find(capabilities_.modes, mode) != capabilities_.modes.end()) {
            continue;  // the same mode listed twice (different stride attributes): keep the first
        }
        native_modes_.push_back({.mode = mode, .index = index});
        capabilities_.modes.push_back(mode);
    }
    if (native_modes_.empty()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("camera '{}' offers no MJPEG, YUY2 or RGB24 mode", descriptor_.name));
    }
    // The camera's own choice after opening: whatever type the reader reports as current, if it is listed.
    ComPtr<IMFMediaType> current;
    if (SUCCEEDED(reader_->GetCurrentMediaType(kVideoStream, &current))) {
        GUID subtype{};
        UINT32 width = 0;
        UINT32 height = 0;
        if (SUCCEEDED(current->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
            SUCCEEDED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
            for (std::size_t i = 0; i < native_modes_.size(); ++i) {
                const CameraMode& mode = native_modes_[i].mode;
                if (format_of(subtype) == mode.format && mode.width == static_cast<int>(width) &&
                    mode.height == static_cast<int>(height)) {
                    current_ = i;
                    break;
                }
            }
        }
    }
    return {};
}

HRESULT MediaFoundationBackend::range_raw(const ControlBinding& binding, long& minimum, long& maximum, long& step,
                                          long& fallback, long& flags) const
{
    if (binding.camera_control) {
        return camera_control_
                   ? camera_control_->GetRange(binding.property, &minimum, &maximum, &step, &fallback, &flags)
                   : E_NOINTERFACE;
    }
    return proc_amp_ ? proc_amp_->GetRange(binding.property, &minimum, &maximum, &step, &fallback, &flags)
                     : E_NOINTERFACE;
}

HRESULT MediaFoundationBackend::get_raw(const ControlBinding& binding, long& value, long& flags) const
{
    if (binding.camera_control) {
        return camera_control_ ? camera_control_->Get(binding.property, &value, &flags) : E_NOINTERFACE;
    }
    return proc_amp_ ? proc_amp_->Get(binding.property, &value, &flags) : E_NOINTERFACE;
}

HRESULT MediaFoundationBackend::set_raw(const ControlBinding& binding, long value, long flags) const
{
    if (binding.camera_control) {
        return camera_control_ ? camera_control_->Set(binding.property, value, flags) : E_NOINTERFACE;
    }
    return proc_amp_ ? proc_amp_->Set(binding.property, value, flags) : E_NOINTERFACE;
}

Expected<ControlInfo> MediaFoundationBackend::describe(const ControlBinding& binding) const
{
    long minimum = 0;
    long maximum = 0;
    long step = 0;
    long fallback = 0;
    long flags = 0;
    const HRESULT hr = range_raw(binding, minimum, maximum, step, fallback, flags);
    if (FAILED(hr) || maximum < minimum) {
        return fail(ErrorCode::Unsupported, "control not offered");
    }
    const long auto_flag = binding.camera_control ? CameraControl_Flags_Auto : VideoProcAmp_Flags_Auto;
    ControlInfo info;
    info.control = binding.control;
    info.supports_auto = (flags & auto_flag) != 0;
    if (binding.log2_exposure) {
        info.minimum = exposure_ms_from_log2(minimum);
        info.maximum = exposure_ms_from_log2(maximum);
        info.default_value = std::clamp(exposure_ms_from_log2(fallback), info.minimum, info.maximum);
        info.step = 0.0;  // powers of two: not a linear step
        info.unit = binding.unit;
        info.calibrated = true;
    } else {
        info.minimum = static_cast<double>(minimum);
        info.maximum = static_cast<double>(maximum);
        info.step = static_cast<double>(std::max(step, 0L));
        info.default_value = std::clamp(static_cast<double>(fallback), info.minimum, info.maximum);
        info.calibrated = binding.unit[0] != '\0';
        info.unit = binding.unit;
    }
    return info;
}

Expected<void> MediaFoundationBackend::read_controls()
{
    capabilities_.controls.clear();
    for (const ControlBinding& binding : kBindings) {
        if (auto info = describe(binding)) {
            capabilities_.controls.push_back(*info);
        }
    }
    return {};
}

Expected<CameraCapabilities> MediaFoundationBackend::capabilities()
{
    return capabilities_;
}

Expected<CameraMode> MediaFoundationBackend::mode()
{
    return native_modes_[current_].mode;
}

Expected<CameraMode> MediaFoundationBackend::set_mode(const CameraMode& mode)
{
    const auto found = std::ranges::find(native_modes_, mode, &NativeMode::mode);
    if (found == native_modes_.end()) {
        return fail(ErrorCode::InvalidArgument, "mode not listed");
    }
    ComPtr<IMFMediaType> type;
    HRESULT hr = reader_->GetNativeMediaType(kVideoStream, found->index, &type);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "GetNativeMediaType failed", hr);
    }
    hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, fmt::format("camera '{}' refused the mode", descriptor_.name), hr);
    }
    current_ = static_cast<std::size_t>(found - native_modes_.begin());
    return found->mode;
}

Expected<ControlSetting> MediaFoundationBackend::read_setting(const ControlBinding& binding) const
{
    long value = 0;
    long flags = 0;
    const HRESULT hr = get_raw(binding, value, flags);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, fmt::format("control '{}' could not be read", to_string(binding.control)), hr);
    }
    const long auto_flag = binding.camera_control ? CameraControl_Flags_Auto : VideoProcAmp_Flags_Auto;
    return ControlSetting{.value = binding.log2_exposure ? exposure_ms_from_log2(value) : static_cast<double>(value),
                          .automatic = (flags & auto_flag) != 0};
}

Expected<ControlState> MediaFoundationBackend::set_control(CameraControl control, ControlSetting setting)
{
    const ControlBinding* binding = binding_of(control);
    const auto info = std::ranges::find(capabilities_.controls, control, &ControlInfo::control);
    if (binding == nullptr || info == capabilities_.controls.end()) {
        return fail(ErrorCode::Unsupported, fmt::format("control '{}' is not offered", to_string(control)));
    }
    const long auto_flag = binding->camera_control ? CameraControl_Flags_Auto : VideoProcAmp_Flags_Auto;
    const long manual_flag = binding->camera_control ? CameraControl_Flags_Manual : VideoProcAmp_Flags_Manual;
    long raw = 0;
    double target = 0.0;
    if (binding->log2_exposure) {
        long minimum = 0;
        long maximum = 0;
        long step = 0;
        long fallback = 0;
        long flags = 0;
        (void)range_raw(*binding, minimum, maximum, step, fallback, flags);  // the range was read at open()
        raw = log2_from_exposure_ms(setting.value, minimum, maximum);
        target = exposure_ms_from_log2(raw);
    } else {
        target = hal::nearest_setting(*info, setting.value);
        raw = static_cast<long>(std::lround(target));
    }
    const bool want_auto = setting.automatic && info->supports_auto;
    HRESULT hr = S_OK;
    if (want_auto) {
        long current = raw;
        long flags = 0;
        (void)get_raw(*binding, current, flags);  // keep the present value; only the automatic flag changes
        hr = set_raw(*binding, current, auto_flag);
    } else {
        hr = set_raw(*binding, raw, manual_flag);
    }
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, fmt::format("control '{}' could not be set", to_string(control)), hr);
    }
    const auto effective = read_setting(*binding);
    if (!effective) {
        return fail(effective.error());
    }
    const bool applied =
        effective->automatic == setting.automatic && (effective->automatic || nearly(effective->value, setting.value));
    return ControlState{.requested = setting, .effective = *effective, .applied = applied};
}

Expected<ControlSetting> MediaFoundationBackend::control(CameraControl control)
{
    const ControlBinding* binding = binding_of(control);
    if (binding == nullptr || std::ranges::count(capabilities_.controls, control, &ControlInfo::control) == 0) {
        return fail(ErrorCode::Unsupported, fmt::format("control '{}' is not offered", to_string(control)));
    }
    return read_setting(*binding);
}

Expected<void> MediaFoundationBackend::start()
{
    if (streaming_) {
        return {};
    }
    if (!reader_ || sink_ == nullptr) {
        return fail(ErrorCode::Unavailable, "not open");
    }
    // Make sure the chosen mode is the one in effect (a previous stop() left the reader flushed, not changed).
    ComPtr<IMFMediaType> type;
    HRESULT hr = reader_->GetNativeMediaType(kVideoStream, native_modes_[current_].index, &type);
    if (SUCCEEDED(hr)) {
        hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
    }
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, fmt::format("camera '{}' refused the mode for streaming", descriptor_.name),
                         hr);
    }
    sink_->set_running(true);
    hr = reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) {
        sink_->set_running(false);
        return com_error(ErrorCode::Io, fmt::format("camera '{}' did not start streaming", descriptor_.name), hr);
    }
    streaming_ = true;
    next_sequence_ = 0;
    last_device_time_.reset();
    return {};
}

void MediaFoundationBackend::stop()
{
    if (!streaming_) {
        return;
    }
    streaming_ = false;
    if (sink_ != nullptr && reader_) {
        sink_->set_running(false);  // the callback stops asking for more
        reader_->Flush(kVideoStream);
        sink_->wait_flushed(std::chrono::milliseconds(2000));
        sink_->set_running(false);  // drop what arrived while flushing
    }
}

Expected<void> MediaFoundationBackend::read_frame(Frame& frame, std::chrono::milliseconds timeout)
{
    if (!streaming_ || sink_ == nullptr) {
        return fail(ErrorCode::Unavailable, "not streaming");
    }
    const CameraMode& mode = native_modes_[current_].mode;
    std::optional<HRESULT> error;
    std::uint64_t overflow = 0;
    const std::optional<Arrival> arrival = sink_->take(timeout, error, overflow);
    if (!arrival) {
        if (error) {
            stop();
            return com_error(ErrorCode::Io, fmt::format("camera '{}' stopped delivering frames", descriptor_.name),
                             *error);
        }
        return fail(ErrorCode::Timeout, fmt::format("camera '{}' delivered no frame in time", descriptor_.name));
    }
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = arrival->sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "ConvertToContiguousBuffer failed", hr);
    }
    BYTE* bytes = nullptr;
    DWORD length = 0;
    hr = buffer->Lock(&bytes, nullptr, &length);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "IMFMediaBuffer::Lock failed", hr);
    }
    const std::size_t stride = static_cast<std::size_t>(mode.width) * bytes_per_pixel(mode.format);
    const std::size_t needed = mode.format == PixelFormat::Mjpeg ? static_cast<std::size_t>(length)
                                                                 : stride * static_cast<std::size_t>(mode.height);
    Expected<void> result;
    if (frame.capacity() < needed || (mode.format != PixelFormat::Mjpeg && length < needed)) {
        result = fail(ErrorCode::InvalidArgument,
                      fmt::format("a frame of {} bytes does not fit ({} bytes of buffer, {} bytes from the camera)",
                                  needed, frame.capacity(), static_cast<unsigned long>(length)));
    } else if (mode.format == PixelFormat::Bgr8) {
        // Media Foundation delivers RGB24 bottom-up; CloudScope frames are top-down.
        const std::size_t rows = static_cast<std::size_t>(mode.height);
        for (std::size_t row = 0; row < rows; ++row) {
            std::memcpy(frame.buffer().data() + row * stride, bytes + (rows - 1 - row) * stride, stride);
        }
    } else {
        std::memcpy(frame.buffer().data(), bytes, needed);
    }
    buffer->Unlock();
    if (!result) {
        return result;
    }
    frame.set_size(needed);

    // Lost frames show as a gap in the device timestamps of one period or more.
    std::uint64_t lost = overflow;
    if (last_device_time_ && mode.fps > 0.0) {
        const double period = 1e7 / mode.fps;
        const double gap = static_cast<double>(arrival->device_time - *last_device_time_) / period;
        if (gap > 1.5) {
            lost += static_cast<std::uint64_t>(std::lround(gap)) - 1;
        }
    }
    last_device_time_ = arrival->device_time;
    next_sequence_ += lost;
    frame.info() = {.sequence = next_sequence_++,
                    .captured = arrival->captured,
                    .width = mode.width,
                    .height = mode.height,
                    .format = mode.format,
                    .stride = mode.format == PixelFormat::Mjpeg ? 0 : stride,
                    .simulated = false};
    return {};
}

}  // namespace

Expected<std::vector<UvcDeviceDescriptor>> enumerate_platform_cameras()
{
    if (!ensure_media_foundation()) {
        return fail(ErrorCode::Unavailable, "Media Foundation could not be started");
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "MFCreateAttributes failed", hr);
    }
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &activates, &count);
    if (FAILED(hr)) {
        return com_error(ErrorCode::Io, "MFEnumDeviceSources failed", hr);
    }
    std::vector<UvcDeviceDescriptor> devices;
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        wchar_t* link = nullptr;
        UINT32 length = 0;
        UvcDeviceDescriptor descriptor;
        if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
            descriptor.name = utf8(name);
            CoTaskMemFree(name);
        }
        if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link,
                                                       &length))) {
            descriptor.path = utf8(link);
            CoTaskMemFree(link);
        }
        activates[i]->Release();
        if (descriptor.path.empty()) {
            continue;
        }
        descriptor.vendor_id = hex_after(descriptor.path, "vid_").value_or(0);
        descriptor.product_id = hex_after(descriptor.path, "pid_").value_or(0);
        devices.push_back(std::move(descriptor));
    }
    CoTaskMemFree(activates);
    std::ranges::sort(devices, {}, &UvcDeviceDescriptor::path);
    return devices;
}

std::unique_ptr<IUvcBackend> make_platform_backend(const UvcDeviceDescriptor& descriptor, const IClock& clock)
{
    return std::make_unique<MediaFoundationBackend>(descriptor, clock);
}

}  // namespace cloudscope::uvc
