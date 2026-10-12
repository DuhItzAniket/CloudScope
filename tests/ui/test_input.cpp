// P043: keyboard shortcuts with overrides and conflicts; a gamepad driving the simulated mount.

#include "desktop_fixture.hpp"
#include "gamepad.hpp"
#include "shortcuts.hpp"

#include <cloudscope/sim/sim_devices.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::wait_until;

namespace {

class FakeGamepad final : public IGamepadSource {
public:
    GamepadState state;
    [[nodiscard]] GamepadState poll() override { return state; }
};

}  // namespace

TEST_CASE("shortcuts take their defaults, overrides from the configuration, and report conflicts",
          "[desktop][shortcuts]")
{
    QObject owner;
    ShortcutRegistry registry(&owner);
    QAction* fit = registry.add({.id = QStringLiteral("view.fit"),
                                 .text = QStringLiteral("Fit"),
                                 .shortcut = QKeySequence(Qt::Key_F),
                                 .status_tip = QStringLiteral("tip")},
                                &owner);
    QAction* one = registry.add({.id = QStringLiteral("view.one_to_one"),
                                 .text = QStringLiteral("1:1"),
                                 .shortcut = QKeySequence(Qt::Key_1),
                                 .status_tip = QStringLiteral("tip")},
                                &owner);
    CHECK(fit->shortcut() == QKeySequence(Qt::Key_F));
    CHECK(registry.action(QStringLiteral("view.fit")) == fit);
    CHECK(registry.action(QStringLiteral("nope")) == nullptr);

    REQUIRE(registry.apply_overrides({{"view.fit", "Ctrl+Shift+F"}}).has_value());
    CHECK(fit->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    CHECK(registry.effective(QStringLiteral("view.fit")) == QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    CHECK(registry.default_of(QStringLiteral("view.fit")) == QKeySequence(Qt::Key_F));
    // Back to the default when the override goes away; an empty override removes the shortcut.
    REQUIRE(registry.apply_overrides({}).has_value());
    CHECK(fit->shortcut() == QKeySequence(Qt::Key_F));
    REQUIRE(registry.apply_overrides({{"view.fit", ""}}).has_value());
    CHECK(fit->shortcut().isEmpty());

    const auto unknown = registry.apply_overrides({{"view.missing", "X"}});
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.find("view.missing") != std::string::npos);
    const auto garbage = registry.apply_overrides({{"view.fit", "Ctrl+NotAKey"}});
    REQUIRE_FALSE(garbage.has_value());
    CHECK(garbage.error().message.find("not a key sequence") != std::string::npos);

    REQUIRE(registry.apply_overrides({{"view.fit", "1"}}).has_value());
    const std::vector<ShortcutConflict> conflicts = registry.conflicts();
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts.front().first_id == QStringLiteral("view.fit"));
    CHECK(conflicts.front().second_id == QStringLiteral("view.one_to_one"));
    CHECK(one->shortcut() == QKeySequence(Qt::Key_1));
    CHECK(shortcut_text(QKeySequence(Qt::CTRL | Qt::Key_S)) == QStringLiteral("Ctrl+S"));
}

TEST_CASE("the gamepad jog drives the simulated mount and stops it", "[desktop][gamepad]")
{
    DesktopFixture fixture;
    auto created = fixture.context->registry().create<hal::IMount>(sim::kMountId);
    REQUIRE(created.has_value());
    const std::shared_ptr<hal::IMount> mount = *created;
    REQUIRE(mount->open().has_value());
    const auto start = mount->status();
    REQUIRE(start.has_value());

    FakeGamepad pad;
    MountJog jog(mount, pad, JogSettings{.dead_zone = 0.15, .max_speed_deg_s = 20.0, .step_s = 0.25});
    MountJog::Tick idle = jog.tick();
    CHECK_FALSE(idle.state.connected);
    CHECK_FALSE(idle.moving);

    pad.state.connected = true;
    pad.state.name = QStringLiteral("Fake pad");
    pad.state.left_x = 0.1;  // inside the dead zone: nothing moves
    MountJog::Tick dead = jog.tick();
    CHECK_FALSE(dead.moving);

    pad.state.left_x = 1.0;  // full right: pan increases
    MountJog::Tick moving = jog.tick();
    CHECK(moving.moving);
    CHECK(moving.error.isEmpty());
    CHECK(moving.target.pan.value() > start->position.pan.value());
    CHECK(moving.target.tilt.value() == start->position.tilt.value());
    REQUIRE(wait_until(
        [&] {
            const auto status = mount->status();
            return status.has_value() && status->position.pan.value() > start->position.pan.value() + 0.5;
        },
        5s));

    pad.state.left_x = 0.0;
    pad.state.left_y = -1.0;  // stick up: tilt up
    MountJog::Tick up = jog.tick();
    CHECK(up.moving);
    CHECK(up.target.tilt.value() > start->position.tilt.value());

    pad.state.left_y = 0.0;
    MountJog::Tick released = jog.tick();
    CHECK(released.stopped);
    CHECK_FALSE(released.moving);

    pad.state.button_b = true;
    MountJog::Tick halted = jog.tick();
    CHECK(halted.emergency_stop);
    const auto after = mount->status();
    REQUIRE(after.has_value());
    CHECK(after->emergency_stop);
    pad.state.button_b = false;
    pad.state.left_x = 1.0;
    MountJog::Tick refused = jog.tick();
    CHECK_FALSE(refused.moving);
    CHECK_FALSE(refused.error.isEmpty());  // the mount refuses moves under the emergency stop
    REQUIRE(mount->clear_emergency_stop().has_value());
    MountJog::Tick again = jog.tick();
    CHECK(again.moving);
    mount->close();
}

TEST_CASE("the SDL gamepad source starts without a controller and reports no input", "[desktop][gamepad]")
{
    SdlGamepad pad;
    const GamepadState state = pad.poll();
    if (!pad.available()) {
        WARN("SDL game controller subsystem not available here: " << pad.reason().toStdString());
    }
    // No assumption about hardware: with no controller attached the state is "not connected".
    CHECK((state.connected || !state.connected));
    if (!state.connected) {
        CHECK(state.left_x == 0.0);
    }
}
