// P034: the live view's geometry.

#include "viewport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using cloudscope::desktop::Viewport;
using cloudscope::desktop::ZoomMode;

TEST_CASE("fit shows the whole image centred", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(800.0, 600.0));
    viewport.set_image_size(QSize(4656, 3496));
    REQUIRE(viewport.mode() == ZoomMode::Fit);
    const double expected = std::min(800.0 / 4656.0, 600.0 / 3496.0);
    CHECK_THAT(viewport.scale(), WithinAbs(expected, 1e-12));
    const QRectF rect = viewport.image_rect_in_widget();
    CHECK(rect.width() <= 800.0 + 1e-9);
    CHECK(rect.height() <= 600.0 + 1e-9);
    // Centred: equal margins on the axis that has room.
    CHECK_THAT(rect.left(), WithinAbs(800.0 - rect.right(), 1e-9));
    CHECK_THAT(rect.top(), WithinAbs(600.0 - rect.bottom(), 1e-9));
}

TEST_CASE("one-to-one maps one image pixel to one widget pixel", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(800.0, 600.0));
    viewport.set_image_size(QSize(4000, 3000));
    viewport.one_to_one();
    CHECK(viewport.scale() == 1.0);
    const QPointF a = viewport.image_to_widget(QPointF(100.0, 100.0));
    const QPointF b = viewport.image_to_widget(QPointF(101.0, 100.0));
    CHECK_THAT(b.x() - a.x(), WithinAbs(1.0, 1e-12));
    // A small image at 1:1 is centred, not pinned to a corner.
    viewport.set_image_size(QSize(200, 100));
    viewport.one_to_one();
    const QRectF rect = viewport.image_rect_in_widget();
    CHECK_THAT(rect.left(), WithinAbs(300.0, 1e-9));
    CHECK_THAT(rect.top(), WithinAbs(250.0, 1e-9));
}

TEST_CASE("zooming keeps the image point under the anchor in place", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(800.0, 600.0));
    viewport.set_image_size(QSize(4000, 3000));
    const QPointF anchor(500.0, 200.0);
    const QPointF before = viewport.widget_to_image(anchor);
    viewport.zoom_by(2.0, anchor);
    CHECK(viewport.mode() == ZoomMode::Free);
    const QPointF after = viewport.widget_to_image(anchor);
    CHECK_THAT(after.x(), WithinAbs(before.x(), 1e-9));
    CHECK_THAT(after.y(), WithinAbs(before.y(), 1e-9));
    viewport.zoom_by(3.0, anchor);
    const QPointF again = viewport.widget_to_image(anchor);
    CHECK_THAT(again.x(), WithinAbs(before.x(), 1e-9));
    CHECK_THAT(again.y(), WithinAbs(before.y(), 1e-9));
}

TEST_CASE("panning never shows empty space beyond a large image's edges", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(800.0, 600.0));
    viewport.set_image_size(QSize(4000, 3000));
    viewport.one_to_one();
    viewport.pan_by(QPointF(10'000.0, 10'000.0));
    QRectF rect = viewport.image_rect_in_widget();
    CHECK_THAT(rect.left(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(rect.top(), WithinAbs(0.0, 1e-9));
    viewport.pan_by(QPointF(-10'000.0, -10'000.0));
    rect = viewport.image_rect_in_widget();
    CHECK_THAT(rect.right(), WithinAbs(800.0, 1e-9));
    CHECK_THAT(rect.bottom(), WithinAbs(600.0, 1e-9));
}

TEST_CASE("the scale is clamped and widget and image coordinates invert each other", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(640.0, 480.0));
    viewport.set_image_size(QSize(640, 480));
    viewport.set_scale(1000.0, QPointF(0.0, 0.0));
    CHECK(viewport.scale() == Viewport::kMaxScale);
    viewport.set_scale(0.0, QPointF(0.0, 0.0));
    CHECK(viewport.scale() == Viewport::kMinScale);
    viewport.set_scale(2.5, QPointF(100.0, 100.0));
    const QPointF widget(321.0, 123.0);
    const QPointF back = viewport.image_to_widget(viewport.widget_to_image(widget));
    CHECK_THAT(back.x(), WithinAbs(widget.x(), 1e-9));
    CHECK_THAT(back.y(), WithinAbs(widget.y(), 1e-9));
    CHECK(viewport.transform().map(QPointF(0.0, 0.0)) == viewport.image_to_widget(QPointF(0.0, 0.0)));
    CHECK(viewport.contains_image_point(QPointF(0.0, 0.0)));
    CHECK_FALSE(viewport.contains_image_point(QPointF(640.0, 10.0)));
    CHECK_FALSE(viewport.contains_image_point(QPointF(-0.5, 10.0)));
}

TEST_CASE("resizing the widget keeps the fit and re-centres", "[desktop][viewport]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(800.0, 600.0));
    viewport.set_image_size(QSize(1600, 1200));
    CHECK_THAT(viewport.scale(), WithinAbs(0.5, 1e-12));
    viewport.set_widget_size(QSizeF(400.0, 600.0));
    CHECK_THAT(viewport.scale(), WithinAbs(0.25, 1e-12));
    const QRectF rect = viewport.image_rect_in_widget();
    CHECK_THAT(rect.top(), WithinAbs((600.0 - 300.0) / 2.0, 1e-9));
}
