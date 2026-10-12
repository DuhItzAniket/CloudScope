// P033, P044, P045: the main window: layout persistence, themes, perspectives, accessibility names and the
// screenshot regression of the live view.

#include "desktop_fixture.hpp"
#include "live_view.hpp"
#include "main_window.hpp"

#include <cloudscope/sim/synthetic_sky.hpp>

#include <DockWidget.h>

#include <QtCore/QDir>
#include <QtGui/QAction>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAbstractSlider>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;

namespace {

MainWindowOptions test_options(bool restore_layout = true)
{
    return MainWindowOptions{
        .renderer = Renderer::Raster, .theme = std::nullopt, .restore_layout = restore_layout, .gamepad = false};
}

bool is_internal(const QWidget* widget)
{
    for (const QObject* object = widget; object != nullptr; object = object->parent()) {
        const QString class_name = QString::fromLatin1(object->metaObject()->className());
        if (class_name.startsWith(QStringLiteral("ads::")) || object->objectName().startsWith(QStringLiteral("qt_"))) {
            return true;
        }
        if (object != widget && (qobject_cast<const QComboBox*>(object) != nullptr ||
                                 qobject_cast<const QAbstractSpinBox*>(object) != nullptr ||
                                 qobject_cast<const QAbstractItemView*>(object) != nullptr)) {
            return true;  // a child of a control: Qt's own internals
        }
    }
    return false;
}

// Interactive widgets without an accessible name, text or placeholder (NFR-USE-01, P044 checklist).
QStringList unnamed_controls(QWidget& root)
{
    QStringList problems;
    for (QWidget* widget : root.findChildren<QWidget*>()) {
        if (is_internal(widget)) {
            continue;
        }
        bool interactive = false;
        bool named = !widget->accessibleName().isEmpty();
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            interactive = true;
            named = named || !button->text().isEmpty();
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            interactive = true;
            named = named || !edit->placeholderText().isEmpty();
        } else if (qobject_cast<QComboBox*>(widget) != nullptr || qobject_cast<QAbstractSpinBox*>(widget) != nullptr ||
                   qobject_cast<QAbstractSlider*>(widget) != nullptr ||
                   qobject_cast<QAbstractItemView*>(widget) != nullptr) {
            interactive = true;
        }
        if (interactive && !named) {
            problems << QString::fromLatin1(widget->metaObject()->className()) + QStringLiteral("/") +
                            widget->objectName();
        }
    }
    return problems;
}

struct ImageDifference {
    double mean_abs = 0.0;
    double fraction_over_40 = 0.0;
    bool same_size = false;
};

ImageDifference compare(const QImage& a, const QImage& b)
{
    ImageDifference d;
    if (a.size() != b.size()) {
        return d;
    }
    d.same_size = true;
    double sum = 0.0;
    int over = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const int delta = std::abs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
            sum += delta;
            over += delta > 40 ? 1 : 0;
        }
    }
    const double pixels = static_cast<double>(a.width()) * a.height();
    d.mean_abs = sum / pixels;
    d.fraction_over_40 = over / pixels;
    return d;
}

}  // namespace

TEST_CASE("the panel layout and the saved perspectives persist across restarts", "[desktop][main_window][layout]")
{
    DesktopFixture fixture;
    std::filesystem::path state_file;
    {
        MainWindow first(*fixture.context, test_options());
        first.show();
        REQUIRE(first.docks().count(QStringLiteral("image_tools")) == 1);
        REQUIRE_FALSE(first.docks().at(QStringLiteral("image_tools"))->isClosed());
        first.docks().at(QStringLiteral("image_tools"))->toggleView(false);
        first.docks().at(QStringLiteral("console"))->toggleView(false);
        first.add_perspective(QStringLiteral("night ops"));
        first.save_layout();
        state_file = first.state_file();
        REQUIRE(std::filesystem::exists(state_file));
        CHECK(first.docks().at(QStringLiteral("image_tools"))->isClosed());
    }
    {
        MainWindow second(*fixture.context, test_options());
        second.show();
        CHECK(second.docks().at(QStringLiteral("image_tools"))->isClosed());
        CHECK(second.docks().at(QStringLiteral("console"))->isClosed());
        CHECK_FALSE(second.docks().at(QStringLiteral("camera"))->isClosed());
        CHECK(second.perspective_names().contains(QStringLiteral("night ops")));
        second.open_perspective(QStringLiteral("night ops"));
    }
    {
        MainWindow fresh(*fixture.context, test_options(false));
        fresh.show();
        CHECK_FALSE(fresh.docks().at(QStringLiteral("image_tools"))->isClosed());
        CHECK_FALSE(fresh.docks().at(QStringLiteral("console"))->isClosed());
    }
}

TEST_CASE("switching the theme changes the palette, the live view and the configuration file",
          "[desktop][main_window][theme]")
{
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    REQUIRE(window.theme() == Theme::Dark);
    window.set_theme(Theme::Night);
    CHECK(window.theme() == Theme::Night);
    CHECK(QApplication::palette().color(QPalette::Window) == theme_palette(Theme::Night).color(QPalette::Window));
    CHECK(window.preview().options().night_vision);
    CHECK(fixture.context->settings().theme == Theme::Night);
    const nlohmann::json user_file = cloudscope::desktop::test::read_config_file(fixture.paths.user_config);
    CHECK(user_file.value("desktop", nlohmann::json::object()).value("theme", std::string()) == "night");
    CHECK(window.shortcuts().action(QStringLiteral("view.theme_night"))->isChecked());
    window.set_theme(Theme::Dark);
    CHECK(QApplication::palette().color(QPalette::Window) == theme_palette(Theme::Dark).color(QPalette::Window));
    CHECK_FALSE(window.preview().options().night_vision);
}

TEST_CASE("the window starts honest: no camera, no mount, no AI, a known disk", "[desktop][main_window][status]")
{
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    window.show();
    CHECK(window.status_bar().camera_label()->text().contains(QStringLiteral("none")));
    CHECK(window.status_bar().mount_label()->text().contains(QStringLiteral("none")));
    CHECK(window.status_bar().ai_label()->text().contains(QStringLiteral("not available")));
    CHECK(window.status_bar().disk().known);
    CHECK(window.status_bar().disk().free_bytes > 0);
    CHECK(window.live_view().scene().message == QStringLiteral("No camera open"));
    CHECK(window.live_view().renderer_in_use() == Renderer::Raster);
}

TEST_CASE("every interactive control has an accessible name and every action a status tip",
          "[desktop][main_window][ux]")
{
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    window.show();
    const QStringList problems = unnamed_controls(window);
    INFO("unnamed controls: " << problems.join(QStringLiteral(", ")).toStdString());
    CHECK(problems.isEmpty());
    for (const ActionSpec& spec : window.shortcuts().specs()) {
        const QAction* action = window.shortcuts().action(spec.id);
        REQUIRE(action != nullptr);
        INFO(spec.id.toStdString());
        CHECK_FALSE(action->statusTip().isEmpty());
        CHECK_FALSE(action->text().isEmpty());
    }
    CHECK(window.shortcuts().conflicts().empty());
}

TEST_CASE("the live view renders the same picture as the committed reference", "[desktop][main_window][screenshot]")
{
    sim::SyntheticSkySpec spec;
    spec.width = 640;
    spec.height = 480;
    spec.seed = 7;
    spec.noise_sigma = 0.0;  // deterministic pixels
    const auto sky = sim::make_synthetic_sky(spec);
    REQUIRE(sky.has_value());
    auto frame = std::make_shared<PreviewFrame>();
    frame->image = to_qimage(sky->image);
    frame->info.width = 640;
    frame->info.height = 480;
    LiveViewScene scene;
    scene.frame = frame;
    scene.viewport.set_widget_size(QSizeF(480.0, 360.0));
    scene.viewport.set_image_size(QSize(640, 480));
    scene.overlays.reticle = true;
    scene.overlays.grid = true;
    scene.show_indicators = false;  // no text: fonts differ between platforms
    QImage rendered(480, 360, QImage::Format_RGB32);
    {
        QPainter painter(&rendered);
        paint_live_view(painter, scene);
    }
    const std::filesystem::path reference = cloudscope::test::data_dir() / "ui" / "live_view_reticle_grid.png";
    const QString reference_name = QString::fromStdU16String(reference.u16string());
    if (qEnvironmentVariable("CLOUDSCOPE_UPDATE_REFERENCES") == QStringLiteral("1")) {
        QDir().mkpath(QString::fromStdU16String((reference.parent_path()).u16string()));
        REQUIRE(rendered.save(reference_name, "PNG"));
    }
    QImage expected(reference_name);
    if (expected.isNull()) {
        const QString candidate = QDir::tempPath() + QStringLiteral("/live_view_reticle_grid.candidate.png");
        rendered.save(candidate, "PNG");
        FAIL("reference picture missing: " << reference.string() << "; candidate written to "
                                           << candidate.toStdString());
    }
    // Compared pixel by pixel in whatever format the PNG loaded as: a format conversion of this size would run
    // on Qt's own thread pool, which the thread sanitizer cannot see into.
    const ImageDifference difference = compare(rendered, expected);
    REQUIRE(difference.same_size);
    INFO("mean |diff| " << difference.mean_abs << ", fraction over 40: " << difference.fraction_over_40);
    CHECK(difference.mean_abs < 1.0);
    CHECK(difference.fraction_over_40 < 0.005);
    // The picture itself is exact: the Sun's pixel is white in the render and in the reference.
    const QPointF sun = scene.viewport.image_to_widget(QPointF(sky->sun_centre.x, sky->sun_centre.y));
    CHECK(qGray(rendered.pixel(sun.toPoint())) > 250);
}
