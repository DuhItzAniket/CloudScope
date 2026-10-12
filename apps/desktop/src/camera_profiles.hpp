// Named camera profiles (P035, FR-CAM-07): a mode and a set of control values saved under a name and applied to a
// camera later, with the read-back rule of FR-CAM-04 (what the device did not take is reported, not hidden).
// Stored as JSON (schema "cloudscope.camera_profiles/1") in the user's configuration folder.
#pragma once

#include <cloudscope/common/error.hpp>
#include <cloudscope/hal/camera.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope::desktop {

struct CameraProfile {
    std::string name;
    std::string camera_id;  // the camera it was saved from; a profile may be applied to another camera
    std::optional<hal::CameraMode> mode = std::nullopt;
    std::map<std::string, hal::ControlSetting> controls;  // by control name ("exposure", "gain", ...)
};

// "1920x1080@30/MJPEG" and back. Parse errors name what is wrong with the text.
[[nodiscard]] std::string mode_text(const hal::CameraMode& mode);
[[nodiscard]] Expected<hal::CameraMode> parse_mode(std::string_view text);
[[nodiscard]] Expected<PixelFormat> pixel_format_from_string(std::string_view text);
[[nodiscard]] Expected<hal::CameraControl> camera_control_from_string(std::string_view text);

[[nodiscard]] nlohmann::json to_json(const CameraProfile& profile);
[[nodiscard]] Expected<CameraProfile> profile_from_json(const nlohmann::json& document);

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path file);

    // An empty list when the file does not exist yet; Parse / Validation for a damaged file.
    [[nodiscard]] Expected<std::vector<CameraProfile>> load() const;
    [[nodiscard]] Expected<void> save(const std::vector<CameraProfile>& profiles) const;
    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path file_;
};

struct ProfileApplication {
    std::optional<hal::CameraMode> mode = std::nullopt;  // the mode in effect afterwards, if the profile set one
    std::map<std::string, hal::ControlState> controls;   // what each control came to
    std::vector<std::string> not_applied;                // controls the device clamped, ignored or does not have
};

// Applies the profile to an open camera that is not streaming (the mode cannot change while it streams; when it
// does, the mode part is skipped and reported in `not_applied`). Fails only when the device itself fails.
[[nodiscard]] Expected<ProfileApplication> apply_profile(hal::ICamera& camera, const CameraProfile& profile);

}  // namespace cloudscope::desktop
