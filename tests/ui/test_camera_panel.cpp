// P035: the camera panel against the simulated camera: every control round-trips to the device.

#include "camera_panel.hpp"
#include "desktop_fixture.hpp"

#include <cloudscope/sim/sim_devices.hpp>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>

using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::wait_until;

namespace {

bool settled(const CameraService& service)
{
    return !service.state().busy;
}

}  // namespace

TEST_CASE("every control and the mode round-trip to the simulated camera", "[desktop][camera_panel]")
{
    DesktopFixture fixture;
    CameraService service(fixture.context->registry(), fixture.context->clock());
    CameraPanel panel(service, ProfileStore(fixture.context->profiles_file()));
    service.refresh_devices();
    REQUIRE(wait_until([&] { return !service.devices().empty(); }, 5s));
    CHECK(panel.device_box()->count() == static_cast<int>(service.devices().size()));

    service.open(std::string(sim::kSkyCameraId));
    REQUIRE(wait_until([&] { return service.is_open() && settled(service); }, 5s));
    REQUIRE_FALSE(panel.rows().empty());
    const std::shared_ptr<hal::ICamera> camera = service.camera();
    REQUIRE(camera != nullptr);

    for (const auto& [control, row] : panel.rows()) {
        const double target = row.info.minimum + (row.info.maximum - row.info.minimum) * 0.37;
        row.spin->setValue(target);
        Q_EMIT row.spin->editingFinished();
        REQUIRE(wait_until([&] { return settled(service); }, 5s));
        const auto device = camera->control(control);
        REQUIRE(device.has_value());
        const hal::ControlState& shown = service.state().controls.at(control);
        INFO(hal::to_string(control));
        CHECK(shown.effective == *device);  // the panel shows the device's own value (FR-CAM-04)
        CHECK_THAT(row.spin->value(), WithinAbs(device->value, 1e-9));
        CHECK(row.effective->text().contains(QString::number(device->value, 'f', row.info.step >= 1.0 ? 0 : 3)));
        if (row.automatic != nullptr) {
            row.automatic->setChecked(true);
            REQUIRE(wait_until([&] { return settled(service); }, 5s));
            const auto automatic = camera->control(control);
            REQUIRE(automatic.has_value());
            CHECK(automatic->automatic);
            CHECK(service.state().controls.at(control).effective.automatic);
            CHECK(row.effective->text().contains(QStringLiteral("auto")));
            row.automatic->setChecked(false);
            REQUIRE(wait_until([&] { return settled(service); }, 5s));
        }
    }

    const std::vector<hal::CameraMode> modes = service.state().capabilities.modes;
    REQUIRE(modes.size() >= 2);
    const int chosen = static_cast<int>(modes.size()) - 1;
    Q_EMIT panel.mode_box()->activated(chosen);
    REQUIRE(wait_until(
        [&] { return settled(service) && service.state().mode == modes[static_cast<std::size_t>(chosen)]; }, 5s));
    const auto device_mode = camera->mode();
    REQUIRE(device_mode.has_value());
    CHECK(*device_mode == modes[static_cast<std::size_t>(chosen)]);
    CHECK(panel.mode_box()->currentIndex() == chosen);

    service.close();
    REQUIRE(wait_until([&] { return !service.is_open() && settled(service); }, 5s));
    CHECK(panel.rows().empty());
}

TEST_CASE("a request the device does not take is marked in the read-back column", "[desktop][camera_panel]")
{
    DesktopFixture fixture;
    CameraService service(fixture.context->registry(), fixture.context->clock());
    CameraPanel panel(service, ProfileStore(fixture.context->profiles_file()));
    service.open(std::string(sim::kSkyCameraId));
    REQUIRE(wait_until([&] { return service.is_open() && settled(service); }, 5s));
    REQUIRE_FALSE(panel.rows().empty());
    const auto& [control, row] = *panel.rows().begin();
    service.set_control(control, hal::ControlSetting{.value = row.info.maximum * 10.0 + 1.0, .automatic = false});
    REQUIRE(wait_until([&] { return settled(service); }, 5s));
    const hal::ControlState& state = service.state().controls.at(control);
    CHECK_FALSE(state.applied);
    CHECK_THAT(state.effective.value, WithinAbs(row.info.maximum, 1e-9));
    CHECK(row.effective->text().contains(QStringLiteral("device set")));
}

TEST_CASE("profiles are saved, listed, applied and deleted", "[desktop][camera_panel][profiles]")
{
    DesktopFixture fixture;
    CameraService service(fixture.context->registry(), fixture.context->clock());
    CameraPanel panel(service, ProfileStore(fixture.context->profiles_file()));
    CHECK_FALSE(panel.save_profile(QStringLiteral("roof")).has_value());  // no camera open yet
    service.open(std::string(sim::kSkyCameraId));
    REQUIRE(wait_until([&] { return service.is_open() && settled(service); }, 5s));
    REQUIRE(panel.save_profile(QStringLiteral("roof")).has_value());
    CHECK(std::filesystem::exists(fixture.context->profiles_file()));
    REQUIRE(panel.profiles().size() == 1);
    CHECK(panel.profiles().front().name == "roof");
    CHECK(panel.profiles().front().mode.has_value());
    CHECK(panel.profile_box()->currentText() == QStringLiteral("roof"));
    panel.apply_profile(QStringLiteral("roof"));
    REQUIRE(wait_until([&] { return settled(service); }, 5s));
    const ProfileStore store(fixture.context->profiles_file());
    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->size() == 1);
    REQUIRE(panel.delete_profile(QStringLiteral("roof")).has_value());
    CHECK(panel.profiles().empty());
    CHECK_FALSE(panel.delete_profile(QStringLiteral("roof")).has_value());
}

TEST_CASE("profile files are validated and modes are parsed", "[desktop][camera_panel][profiles]")
{
    const auto mode = parse_mode("1920x1080@30/MJPEG");
    REQUIRE(mode.has_value());
    CHECK(mode->width == 1920);
    CHECK(mode->height == 1080);
    CHECK(mode->fps == 30.0);
    CHECK(mode->format == PixelFormat::Mjpeg);
    CHECK(mode_text(*mode) == "1920x1080@30/MJPEG");
    CHECK_FALSE(parse_mode("1920by1080").has_value());
    CHECK_FALSE(parse_mode("640x480@fast").has_value());
    CHECK_FALSE(parse_mode("640x480/RAW99").has_value());
    CHECK(parse_mode("640x480").has_value());
    CHECK_FALSE(profile_from_json(nlohmann::json{{"camera_id", "x"}}).has_value());
    CHECK_FALSE(
        profile_from_json(nlohmann::json{{"name", "p"}, {"controls", {{"warp", {{"value", 1.0}}}}}}).has_value());
    const auto profile =
        profile_from_json(nlohmann::json{{"name", "p"},
                                         {"mode", {{"width", 640}, {"height", 480}, {"fps", 30.0}, {"format", "BGR8"}}},
                                         {"controls", {{"exposure", {{"value", 12.5}, {"automatic", false}}}}}});
    REQUIRE(profile.has_value());
    CHECK(profile->controls.at("exposure").value == 12.5);
    const nlohmann::json round_trip = to_json(*profile);
    CHECK(profile_from_json(round_trip)->mode->format == PixelFormat::Bgr8);
}
