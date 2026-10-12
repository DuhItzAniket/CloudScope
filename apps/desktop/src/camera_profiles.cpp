#include "camera_profiles.hpp"

#include <cloudscope/common/config.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace cloudscope::desktop {

namespace {

constexpr std::string_view kSchema = "cloudscope.camera_profiles/1";

constexpr std::array<PixelFormat, 6> kFormats = {PixelFormat::Gray8, PixelFormat::Gray16, PixelFormat::Bgr8,
                                                 PixelFormat::Rgb8,  PixelFormat::Yuyv,   PixelFormat::Mjpeg};
constexpr std::array<hal::CameraControl, 9> kControls = {
    hal::CameraControl::Exposure,   hal::CameraControl::Gain,      hal::CameraControl::WhiteBalance,
    hal::CameraControl::Brightness, hal::CameraControl::Contrast,  hal::CameraControl::Saturation,
    hal::CameraControl::Gamma,      hal::CameraControl::Sharpness, hal::CameraControl::Focus};

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

template <class T>
bool parse_number(std::string_view text, T& out)
{
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc() && end == text.data() + text.size();
}

std::string display(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

std::string mode_text(const hal::CameraMode& mode)
{
    return fmt::format("{}x{}@{:g}/{}", mode.width, mode.height, mode.fps, to_string(mode.format));
}

Expected<PixelFormat> pixel_format_from_string(std::string_view text)
{
    const std::string wanted = lower(text);
    for (const PixelFormat format : kFormats) {
        if (lower(to_string(format)) == wanted) {
            return format;
        }
    }
    return fail(ErrorCode::InvalidArgument,
                "unknown pixel format \"" + std::string(text) + "\" (GRAY8, GRAY16, BGR8, RGB8, YUYV, MJPEG)");
}

Expected<hal::CameraControl> camera_control_from_string(std::string_view text)
{
    for (const hal::CameraControl control : kControls) {
        if (hal::to_string(control) == text) {
            return control;
        }
    }
    return fail(ErrorCode::InvalidArgument, "unknown camera control \"" + std::string(text) + "\"");
}

Expected<hal::CameraMode> parse_mode(std::string_view text)
{
    // WxH@FPS/FORMAT; the rate and the format are optional ("1920x1080" alone is accepted for a size).
    hal::CameraMode mode;
    std::string_view rest = text;
    std::string_view format_text;
    if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
        format_text = rest.substr(slash + 1);
        rest = rest.substr(0, slash);
    }
    std::string_view fps_text;
    if (const std::size_t at = rest.find('@'); at != std::string_view::npos) {
        fps_text = rest.substr(at + 1);
        rest = rest.substr(0, at);
    }
    const std::size_t x = rest.find('x');
    if (x == std::string_view::npos || !parse_number(rest.substr(0, x), mode.width) ||
        !parse_number(rest.substr(x + 1), mode.height) || mode.width <= 0 || mode.height <= 0) {
        return fail(ErrorCode::InvalidArgument,
                    "mode \"" + std::string(text) +
                        "\" must look like 1920x1080@30/MJPEG (width x height @ fps / format)");
    }
    if (!fps_text.empty()) {
        std::istringstream in{std::string(fps_text)};
        in >> mode.fps;
        if (in.fail() || !in.eof() || mode.fps < 0.0) {
            return fail(ErrorCode::InvalidArgument,
                        "mode \"" + std::string(text) + "\": the rate after @ must be a number");
        }
    }
    if (!format_text.empty()) {
        auto format = pixel_format_from_string(format_text);
        if (!format) {
            return fail(format.error());
        }
        mode.format = *format;
    }
    return mode;
}

nlohmann::json to_json(const CameraProfile& profile)
{
    nlohmann::json controls = nlohmann::json::object();
    for (const auto& [name, setting] : profile.controls) {
        controls[name] = {{"value", setting.value}, {"automatic", setting.automatic}};
    }
    nlohmann::json out{{"name", profile.name}, {"camera_id", profile.camera_id}, {"controls", controls}};
    if (profile.mode.has_value()) {
        out["mode"] = {{"width", profile.mode->width},
                       {"height", profile.mode->height},
                       {"fps", profile.mode->fps},
                       {"format", std::string(to_string(profile.mode->format))}};
    } else {
        out["mode"] = nullptr;
    }
    return out;
}

Expected<CameraProfile> profile_from_json(const nlohmann::json& document)
{
    if (!document.is_object() || !document.contains("name") || !document.at("name").is_string()) {
        return fail(ErrorCode::Parse, "a camera profile needs a \"name\"");
    }
    CameraProfile profile;
    profile.name = document.at("name").get<std::string>();
    if (profile.name.empty()) {
        return fail(ErrorCode::Validation, "a camera profile's name must not be empty");
    }
    if (document.contains("camera_id") && document.at("camera_id").is_string()) {
        profile.camera_id = document.at("camera_id").get<std::string>();
    }
    if (document.contains("mode") && document.at("mode").is_object()) {
        const nlohmann::json& m = document.at("mode");
        try {
            hal::CameraMode mode{.width = m.at("width").get<int>(), .height = m.at("height").get<int>()};
            mode.fps = m.value("fps", 0.0);
            auto format = pixel_format_from_string(m.value("format", std::string("MJPEG")));
            if (!format) {
                return fail(format.error().with_context("profile \"" + profile.name + "\""));
            }
            mode.format = *format;
            profile.mode = mode;
        } catch (const nlohmann::json::exception& error) {
            return fail(ErrorCode::Parse, "profile \"" + profile.name + "\": mode: " + error.what());
        }
    }
    if (document.contains("controls")) {
        const nlohmann::json& controls = document.at("controls");
        if (!controls.is_object()) {
            return fail(ErrorCode::Parse, "profile \"" + profile.name + "\": controls must be an object");
        }
        for (const auto& [name, value] : controls.items()) {
            if (!camera_control_from_string(name)) {
                return fail(ErrorCode::Validation,
                            "profile \"" + profile.name + "\": unknown control \"" + name + "\"");
            }
            if (!value.is_object() || !value.contains("value") || !value.at("value").is_number()) {
                return fail(ErrorCode::Parse,
                            "profile \"" + profile.name + "\": control \"" + name + "\" needs a numeric value");
            }
            profile.controls[name] = {.value = value.at("value").get<double>(),
                                      .automatic = value.value("automatic", false)};
        }
    }
    return profile;
}

ProfileStore::ProfileStore(std::filesystem::path file) : file_(std::move(file)) {}

Expected<std::vector<CameraProfile>> ProfileStore::load() const
{
    std::error_code error;
    if (!std::filesystem::exists(file_, error)) {
        return std::vector<CameraProfile>{};
    }
    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::Io, "cannot read " + display(file_));
    }
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& parse_error) {
        return fail(ErrorCode::Parse, display(file_) + ": " + parse_error.what());
    }
    if (!document.is_object() || document.value("schema", std::string()) != kSchema) {
        return fail(ErrorCode::Validation, display(file_) + ": not a \"" + std::string(kSchema) + "\" document");
    }
    std::vector<CameraProfile> profiles;
    if (document.contains("profiles") && document.at("profiles").is_array()) {
        for (const nlohmann::json& item : document.at("profiles")) {
            auto profile = profile_from_json(item);
            if (!profile) {
                return fail(profile.error().with_context(display(file_)));
            }
            profiles.emplace_back(std::move(*profile));
        }
    }
    return profiles;
}

Expected<void> ProfileStore::save(const std::vector<CameraProfile>& profiles) const
{
    nlohmann::json list = nlohmann::json::array();
    for (const CameraProfile& profile : profiles) {
        list.push_back(to_json(profile));
    }
    const nlohmann::json document{{"schema", std::string(kSchema)}, {"profiles", list}};
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    return write_file_atomically(file_, document.dump(2) + "\n");
}

Expected<ProfileApplication> apply_profile(hal::ICamera& camera, const CameraProfile& profile)
{
    ProfileApplication result;
    if (profile.mode.has_value()) {
        if (camera.is_streaming()) {
            result.not_applied.emplace_back("mode (the camera is streaming)");
        } else {
            auto mode = camera.set_mode(*profile.mode);
            if (!mode) {
                return fail(mode.error().with_context("profile \"" + profile.name + "\""));
            }
            result.mode = *mode;
            if (*mode != *profile.mode) {
                result.not_applied.push_back("mode (the camera chose " + mode_text(*mode) + ")");
            }
        }
    }
    auto capabilities = camera.capabilities();
    if (!capabilities) {
        return fail(capabilities.error());
    }
    for (const auto& [name, setting] : profile.controls) {
        const auto control = camera_control_from_string(name);
        if (!control) {
            result.not_applied.push_back(name + " (unknown control)");
            continue;
        }
        const bool offered = std::ranges::any_of(
            capabilities->controls, [&](const hal::ControlInfo& info) { return info.control == *control; });
        if (!offered) {
            result.not_applied.push_back(name + " (this camera has no such control)");
            continue;
        }
        auto state = camera.set_control(*control, setting);
        if (!state) {
            return fail(state.error().with_context("profile \"" + profile.name + "\", control " + name));
        }
        result.controls[name] = *state;
        if (!state->applied) {
            result.not_applied.push_back(
                name + fmt::format(" (requested {:g}, the device set {:g})", setting.value, state->effective.value));
        }
    }
    return result;
}

}  // namespace cloudscope::desktop
