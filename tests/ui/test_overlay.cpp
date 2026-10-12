// P037: overlay geometry, checked against the camera model and the Sun position.

#include "overlay.hpp"

#include <cloudscope/common/clock.hpp>

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using Catch::Matchers::WithinAbs;
using namespace cloudscope;
using namespace cloudscope::desktop;

namespace {

// An ideal equidistant fisheye: r = f * theta, 1000 x 1000 pixels, f = 300 px (a 180-degree view is 942 px wide).
CameraModel ideal_fisheye()
{
    CameraModel model;
    model.model = LensModel::Fisheye;
    model.width = 1000;
    model.height = 1000;
    model.fx = 300.0;
    model.fy = 300.0;
    model.cx = 500.0;
    model.cy = 500.0;
    model.distortion = {0.0, 0.0, 0.0, 0.0};
    return model;
}

double radius_from_centre(QPointF pixel)
{
    return std::hypot(pixel.x() - 500.0, pixel.y() - 500.0);
}

double angle_between_deg(SkyDirection a, SkyDirection b)
{
    const Vector3 u = direction_vector(a);
    const Vector3 v = direction_vector(b);
    const double dot = std::clamp(u.x * v.x + u.y * v.y + u.z * v.z, -1.0, 1.0);
    return std::acos(dot) * 180.0 / std::numbers::pi;
}

}  // namespace

TEST_CASE("a zenith camera maps the zenith to the principal point and elevation to radius", "[desktop][overlay]")
{
    const SkyProjection projection(ideal_fisheye(),
                                   SkyPose{.azimuth_deg = 0.0, .elevation_deg = 90.0, .roll_deg = 0.0});
    const auto zenith = projection.to_pixel({.azimuth = Degrees(123.0), .elevation = Degrees(90.0)});
    REQUIRE(zenith.has_value());
    CHECK_THAT(zenith->x(), WithinAbs(500.0, 1e-6));
    CHECK_THAT(zenith->y(), WithinAbs(500.0, 1e-6));
    for (const double elevation : {80.0, 60.0, 30.0, 5.0}) {
        for (const double azimuth : {0.0, 45.0, 170.0, 300.0}) {
            const auto pixel = projection.to_pixel({.azimuth = Degrees(azimuth), .elevation = Degrees(elevation)});
            REQUIRE(pixel.has_value());
            const double theta = (90.0 - elevation) * std::numbers::pi / 180.0;
            CHECK_THAT(radius_from_centre(*pixel), WithinAbs(300.0 * theta, 1e-6));
        }
    }
    // Below the horizon is behind the camera.
    CHECK_FALSE(projection.to_pixel({.azimuth = Degrees(10.0), .elevation = Degrees(-5.0)}).has_value());
}

TEST_CASE("pixels and sky directions invert each other", "[desktop][overlay]")
{
    const SkyProjection projection(ideal_fisheye(),
                                   SkyPose{.azimuth_deg = 35.0, .elevation_deg = 50.0, .roll_deg = 12.0});
    for (const double azimuth : {0.0, 35.0, 90.0, 200.0, 359.0}) {
        for (const double elevation : {10.0, 45.0, 85.0}) {
            const SkyDirection direction{.azimuth = Degrees(azimuth), .elevation = Degrees(elevation)};
            const auto pixel = projection.to_pixel(direction);
            if (!pixel.has_value()) {
                continue;  // outside the camera's hemisphere
            }
            const SkyDirection back = projection.to_sky(*pixel);
            // OpenCV undistorts fisheye pixels iteratively: a few millionths of a degree is its precision.
            CHECK_THAT(angle_between_deg(direction, back), WithinAbs(0.0, 1e-5));
        }
    }
    // The optical axis itself lands on the principal point.
    const auto axis = projection.to_pixel({.azimuth = Degrees(35.0), .elevation = Degrees(50.0)});
    REQUIRE(axis.has_value());
    CHECK_THAT(axis->x(), WithinAbs(500.0, 1e-6));
    CHECK_THAT(axis->y(), WithinAbs(500.0, 1e-6));
}

TEST_CASE("the Sun marker agrees with the solar position to a millionth of a degree", "[desktop][overlay]")
{
    const SkyProjection projection(ideal_fisheye(),
                                   SkyPose{.azimuth_deg = 0.0, .elevation_deg = 90.0, .roll_deg = 0.0});
    const auto time = parse_iso8601("2026-10-09T06:30:00Z");  // Bengaluru, mid-morning
    REQUIRE(time.has_value());
    const SunPosition sun = sun_position(*time, 12.97, 77.59);
    REQUIRE(sun.elevation_deg > 20.0);
    const auto marker = sun_pixel(projection, sun);
    REQUIRE(marker.has_value());
    const SkyDirection seen = projection.to_sky(*marker);
    CHECK_THAT(seen.azimuth.value(), WithinAbs(sun.azimuth_deg, 1e-6));
    CHECK_THAT(seen.elevation.value(), WithinAbs(sun.apparent_elevation_deg, 1e-6));
    // The keep-out ring lies at exactly the chosen angular distance from the Sun.
    const std::vector<QPolygonF> ring = keep_out_ring(projection, sun, 15.0, 72);
    REQUIRE_FALSE(ring.empty());
    const SkyDirection sun_direction{.azimuth = Degrees(sun.azimuth_deg),
                                     .elevation = Degrees(sun.apparent_elevation_deg)};
    int points = 0;
    for (const QPolygonF& line : ring) {
        for (const QPointF& point : line) {
            CHECK_THAT(angle_between_deg(projection.to_sky(point), sun_direction), WithinAbs(15.0, 1e-6));
            ++points;
        }
    }
    CHECK(points >= 72);
}

TEST_CASE("altitude circles and compass marks lie where the model puts them", "[desktop][overlay]")
{
    const SkyProjection projection(ideal_fisheye(),
                                   SkyPose{.azimuth_deg = 0.0, .elevation_deg = 90.0, .roll_deg = 0.0});
    const std::vector<QPolygonF> circle = altitude_circle(projection, 30.0, 120);
    REQUIRE(circle.size() == 1);
    const double expected = 300.0 * 60.0 * std::numbers::pi / 180.0;
    for (const QPointF& point : circle.front()) {
        CHECK_THAT(radius_from_centre(point), WithinAbs(expected, 1e-6));
    }
    const std::vector<CompassMark> marks = compass_marks(projection, 10.0);
    REQUIRE(marks.size() == 4);
    // North and east are a quarter turn apart around the centre, in the same direction as south and west.
    const auto angle_of = [](QPointF p) { return std::atan2(p.y() - 500.0, p.x() - 500.0); };
    const double n = angle_of(marks[0].pixel);
    const double e = angle_of(marks[1].pixel);
    double delta = (e - n) * 180.0 / std::numbers::pi;
    while (delta <= -180.0) {
        delta += 360.0;
    }
    while (delta > 180.0) {
        delta -= 360.0;
    }
    CHECK_THAT(std::abs(delta), WithinAbs(90.0, 1e-6));
    CHECK(marks[0].label == QStringLiteral("N"));
    CHECK(marks[2].label == QStringLiteral("S"));
}

TEST_CASE("painting draws what is enabled and reports it honestly", "[desktop][overlay]")
{
    Viewport viewport;
    viewport.set_widget_size(QSizeF(400.0, 400.0));
    viewport.set_image_size(QSize(1000, 1000));
    QImage canvas(400, 400, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    OverlaySettings settings;
    settings.reticle = true;
    settings.grid = true;
    settings.altitude_circles = true;
    settings.sun = true;
    OverlayScene scene;  // no projection, no Sun
    const OverlaysDrawn drawn = paint_overlays(painter, viewport, settings, scene);
    CHECK(drawn.reticle);
    CHECK(drawn.grid);
    CHECK_FALSE(drawn.altitude_circles);  // no projection: not drawn, not claimed
    CHECK_FALSE(drawn.sun);
    painter.end();
    int lit = 0;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            lit += qGray(canvas.pixel(x, y)) > 40 ? 1 : 0;
        }
    }
    CHECK(lit > 100);  // the reticle and the grid lines
    // With a projection and a Sun, the sky overlays are drawn too.
    const SkyProjection projection(ideal_fisheye(), SkyPose{});
    const SunPosition sun{.elevation_deg = 45.0, .apparent_elevation_deg = 45.0, .azimuth_deg = 120.0};
    scene.projection = &projection;
    scene.sun = &sun;
    settings.keep_out = true;
    settings.compass = true;
    QPainter again(&canvas);
    const OverlaysDrawn all = paint_overlays(again, viewport, settings, scene);
    CHECK(all.altitude_circles);
    CHECK(all.compass);
    CHECK(all.sun);
    CHECK(all.keep_out);
}
