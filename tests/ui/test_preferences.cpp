// P041: preferences are validated before anything is written; invalid values are rejected with a clear message.

#include "desktop_fixture.hpp"
#include "preferences_dialog.hpp"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTableWidget>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::desktop::test::read_config_file;

namespace {

ShortcutRegistry* registry_with_actions(QObject* parent)
{
    auto* registry = new ShortcutRegistry(parent);
    registry->add({.id = QStringLiteral("view.fit"),
                   .text = QStringLiteral("Fit"),
                   .shortcut = QKeySequence(Qt::Key_F),
                   .status_tip = QStringLiteral("Fit")},
                  parent);
    registry->add({.id = QStringLiteral("view.one_to_one"),
                   .text = QStringLiteral("1:1"),
                   .shortcut = QKeySequence(Qt::Key_1),
                   .status_tip = QStringLiteral("One to one")},
                  parent);
    return registry;
}

}  // namespace

TEST_CASE("valid preferences are written to the user file and reloaded", "[desktop][preferences]")
{
    DesktopFixture fixture;
    QObject owner;
    PreferencesDialog dialog(*fixture.context, registry_with_actions(&owner));
    dialog.latitude()->setValue(12.97);
    dialog.site_id()->setText(QStringLiteral("blr-roof"));
    dialog.theme_box()->setCurrentIndex(dialog.theme_box()->findData(QStringLiteral("night")));
    dialog.set_shortcut_text(QStringLiteral("view.fit"), QStringLiteral("Ctrl+Shift+F"));
    REQUIRE(dialog.save().has_value());
    CHECK(dialog.last_error().isEmpty());
    const DesktopSettings& settings = fixture.context->settings();
    CHECK(settings.site.id == "blr-roof");
    CHECK(settings.site.latitude_deg == 12.97);
    CHECK(settings.theme == Theme::Night);
    CHECK(settings.shortcuts.at("view.fit") == "Ctrl+Shift+F");
    const nlohmann::json file = read_config_file(fixture.paths.user_config);
    CHECK(file.at("schema_version") == 1);
    CHECK(file.at("desktop").at("site").at("id") == "blr-roof");
    CHECK(file.at("desktop").at("shortcuts").at("view.fit") == "Ctrl+Shift+F");
    CHECK(file.at("desktop").at("theme") == "night");
    // A context started from the files reads the same.
    auto again = AppContext::create(fixture.paths, {}, nlohmann::json::object());
    REQUIRE(again.has_value());
    CHECK((*again)->settings().site.id == "blr-roof");
    CHECK((*again)->settings().theme == Theme::Night);
}

TEST_CASE("an invalid value is rejected with a message that names the key, and nothing is written",
          "[desktop][preferences]")
{
    DesktopFixture fixture;
    QObject owner;
    PreferencesDialog dialog(*fixture.context, registry_with_actions(&owner));
    dialog.site_id()->setText(QStringLiteral("bad id!"));  // pattern: letters, digits, - and _
    const auto saved = dialog.save();
    REQUIRE_FALSE(saved.has_value());
    CHECK(dialog.last_error().contains(QStringLiteral("desktop.site.id")));
    CHECK_FALSE(std::filesystem::exists(fixture.paths.user_config));
    CHECK(fixture.context->settings().site.id == "test-site");

    dialog.site_id()->setText(QStringLiteral("fine"));
    dialog.set_shortcut_text(QStringLiteral("view.fit"), QStringLiteral("NotAKey!!"));
    const auto bad_key = dialog.save();
    REQUIRE_FALSE(bad_key.has_value());
    CHECK(dialog.last_error().contains(QStringLiteral("view.fit")));
    CHECK(dialog.last_error().contains(QStringLiteral("not a key sequence")));
    CHECK_FALSE(std::filesystem::exists(fixture.paths.user_config));
}

TEST_CASE("the user file keeps keys the dialog does not manage", "[desktop][preferences]")
{
    DesktopFixture fixture;
    std::filesystem::create_directories(fixture.paths.user_config.parent_path());
    {
        std::ofstream out(fixture.paths.user_config);
        out << "schema_version = 1\n[simulation]\nseed = 42\n";
    }
    REQUIRE(fixture.context->reload().has_value());
    QObject owner;
    PreferencesDialog dialog(*fixture.context, registry_with_actions(&owner));
    dialog.latitude()->setValue(-33.9);
    REQUIRE(dialog.save().has_value());
    const nlohmann::json file = read_config_file(fixture.paths.user_config);
    CHECK(file.at("simulation").at("seed") == 42);
    CHECK(fixture.context->config().effective.at("simulation").at("seed") == 42);
    CHECK(fixture.context->settings().site.latitude_deg == -33.9);
}
