#include "overlay.hpp"

#include <cloudscope/common/units.hpp>

#include <QtCore/QRectF>
#include <QtGui/QFont>
#include <QtGui/QPainter>
#include <QtGui/QPen>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace cloudscope::desktop {

namespace {

constexpr double kMinForwardZ = 1e-6;  // rays this close to the camera's horizon are not drawn

Vector3 cross(const Vector3& a, const Vector3& b)
{
    return {.x = a.y * b.z - a.z * b.y, .y = a.z * b.x - a.x * b.z, .z = a.x * b.y - a.y * b.x};
}

double length(const Vector3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 scaled(const Vector3& v, double factor)
{
    return {.x = v.x * factor, .y = v.y * factor, .z = v.z * factor};
}

Vector3 add(const Vector3& a, const Vector3& b)
{
    return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

// Polylines from a sequence of optional pixels: a gap or a jump across the image (a wrap-around) starts a new one.
std::vector<QPolygonF> polylines(const std::vector<std::optional<QPointF>>& points, double max_jump_px)
{
    std::vector<QPolygonF> lines;
    QPolygonF current;
    for (const std::optional<QPointF>& point : points) {
        const bool jump = point.has_value() && !current.isEmpty() &&
                          std::hypot(point->x() - current.last().x(), point->y() - current.last().y()) > max_jump_px;
        if (!point.has_value() || jump) {
            if (current.size() >= 2) {
                lines.push_back(current);
            }
            current.clear();
        }
        if (point.has_value()) {
            current.append(*point);
        }
    }
    if (current.size() >= 2) {
        lines.push_back(current);
    }
    return lines;
}

void draw_polylines(QPainter& painter, const Viewport& viewport, const std::vector<QPolygonF>& lines)
{
    for (const QPolygonF& line : lines) {
        QPolygonF mapped;
        mapped.reserve(line.size());
        for (const QPointF& point : line) {
            mapped.append(viewport.image_to_widget(point));
        }
        painter.drawPolyline(mapped);
    }
}

}  // namespace

Vector3 direction_vector(SkyDirection direction)
{
    const double az = to_radians(direction.azimuth).value();
    const double el = to_radians(direction.elevation).value();
    return {.x = std::cos(el) * std::sin(az), .y = std::cos(el) * std::cos(az), .z = std::sin(el)};
}

SkyDirection direction_of(const Vector3& v)
{
    const double horizontal = std::hypot(v.x, v.y);
    const Degrees elevation = to_degrees(Radians(std::atan2(v.z, horizontal)));
    const Degrees azimuth = horizontal < 1e-12 ? Degrees(0.0) : wrap_360(to_degrees(Radians(std::atan2(v.x, v.y))));
    return {.azimuth = azimuth, .elevation = elevation};
}

SkyProjection::SkyProjection(CameraModel model, SkyPose pose) : model_(std::move(model)), pose_(pose)
{
    const Quaternion level =
        camera_orientation({.azimuth = Degrees(pose.azimuth_deg), .elevation = Degrees(pose.elevation_deg)});
    const Quaternion roll = from_axis_angle({.x = 0.0, .y = 0.0, .z = 1.0}, to_radians(Degrees(pose.roll_deg)));
    camera_to_enu_ = multiply(level, roll);  // first the roll in the camera frame, then the orientation
}

std::optional<QPointF> SkyProjection::to_pixel(SkyDirection direction) const
{
    const Vector3 enu = direction_vector(direction);
    const Vector3 cam = rotate(conjugate(camera_to_enu_), enu);
    if (!(cam.z > kMinForwardZ)) {
        return std::nullopt;
    }
    const cv::Point2d pixel = ray_to_pixel(model_, cv::Point3d(cam.x, cam.y, cam.z));
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y)) {
        return std::nullopt;
    }
    return QPointF(pixel.x, pixel.y);
}

SkyDirection SkyProjection::to_sky(QPointF pixel) const
{
    const cv::Point3d ray = pixel_to_ray(model_, cv::Point2d(pixel.x(), pixel.y()));
    const Vector3 enu = rotate(camera_to_enu_, {.x = ray.x, .y = ray.y, .z = ray.z});
    return direction_of(enu);
}

std::vector<QPolygonF> altitude_circle(const SkyProjection& projection, double elevation_deg, int samples)
{
    samples = std::max(samples, 8);
    std::vector<std::optional<QPointF>> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int i = 0; i <= samples; ++i) {
        const double azimuth = 360.0 * static_cast<double>(i % samples) / samples;
        points.push_back(projection.to_pixel({.azimuth = Degrees(azimuth), .elevation = Degrees(elevation_deg)}));
    }
    const double max_jump = std::hypot(projection.model().width, projection.model().height) / 2.0;
    return polylines(points, max_jump);
}

std::vector<CompassMark> compass_marks(const SkyProjection& projection, double elevation_deg)
{
    static constexpr std::array<std::pair<const char*, double>, 4> kPoints = {
        {{"N", 0.0}, {"E", 90.0}, {"S", 180.0}, {"W", 270.0}}};
    std::vector<CompassMark> marks;
    for (const auto& [label, azimuth] : kPoints) {
        const std::optional<QPointF> pixel =
            projection.to_pixel({.azimuth = Degrees(azimuth), .elevation = Degrees(elevation_deg)});
        if (pixel.has_value()) {
            marks.push_back({.label = QString::fromLatin1(label), .azimuth_deg = azimuth, .pixel = *pixel});
        }
    }
    return marks;
}

std::optional<QPointF> sun_pixel(const SkyProjection& projection, const SunPosition& sun)
{
    return projection.to_pixel({.azimuth = Degrees(sun.azimuth_deg), .elevation = Degrees(sun.apparent_elevation_deg)});
}

std::vector<QPolygonF> keep_out_ring(const SkyProjection& projection, const SunPosition& sun, double radius_deg,
                                     int samples)
{
    samples = std::max(samples, 8);
    const Vector3 centre =
        direction_vector({.azimuth = Degrees(sun.azimuth_deg), .elevation = Degrees(sun.apparent_elevation_deg)});
    // Two unit vectors perpendicular to the Sun's direction.
    Vector3 u = cross(centre, {.x = 0.0, .y = 0.0, .z = 1.0});
    if (length(u) < 1e-9) {
        u = cross(centre, {.x = 0.0, .y = 1.0, .z = 0.0});
    }
    u = scaled(u, 1.0 / length(u));
    const Vector3 v = cross(centre, u);
    const double r = to_radians(Degrees(radius_deg)).value();
    std::vector<std::optional<QPointF>> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int i = 0; i <= samples; ++i) {
        const double phi = 2.0 * std::numbers::pi * static_cast<double>(i % samples) / samples;
        const Vector3 dir = add(scaled(centre, std::cos(r)),
                                scaled(add(scaled(u, std::cos(phi)), scaled(v, std::sin(phi))), std::sin(r)));
        points.push_back(projection.to_pixel(direction_of(dir)));
    }
    const double max_jump = std::hypot(projection.model().width, projection.model().height) / 2.0;
    return polylines(points, max_jump);
}

OverlaysDrawn paint_overlays(QPainter& painter, const Viewport& viewport, const OverlaySettings& settings,
                             const OverlayScene& scene)
{
    OverlaysDrawn drawn;
    const QSize image = viewport.image_size();
    if (image.isEmpty()) {
        return drawn;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF image_rect = viewport.image_rect_in_widget();
    painter.setClipRect(image_rect.intersected(QRectF(QPointF(0.0, 0.0), viewport.widget_size())));
    QPen pen(scene.colour, 1.5);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (settings.reticle) {
        const QPointF centre = viewport.image_to_widget(QPointF(image.width() / 2.0, image.height() / 2.0));
        constexpr double kArm = 40.0;
        constexpr double kGap = 8.0;
        painter.drawLine(QPointF(centre.x() - kArm, centre.y()), QPointF(centre.x() - kGap, centre.y()));
        painter.drawLine(QPointF(centre.x() + kGap, centre.y()), QPointF(centre.x() + kArm, centre.y()));
        painter.drawLine(QPointF(centre.x(), centre.y() - kArm), QPointF(centre.x(), centre.y() - kGap));
        painter.drawLine(QPointF(centre.x(), centre.y() + kGap), QPointF(centre.x(), centre.y() + kArm));
        painter.drawEllipse(centre, kGap * 2.0, kGap * 2.0);
        drawn.reticle = true;
    }
    if (settings.grid) {
        const int divisions = std::clamp(settings.grid_divisions, 2, 64);
        QPen grid_pen = pen;
        grid_pen.setStyle(Qt::DotLine);
        painter.setPen(grid_pen);
        for (int i = 1; i < divisions; ++i) {
            const double fx = image.width() * static_cast<double>(i) / divisions;
            const double fy = image.height() * static_cast<double>(i) / divisions;
            painter.drawLine(viewport.image_to_widget(QPointF(fx, 0.0)),
                             viewport.image_to_widget(QPointF(fx, image.height())));
            painter.drawLine(viewport.image_to_widget(QPointF(0.0, fy)),
                             viewport.image_to_widget(QPointF(image.width(), fy)));
        }
        painter.setPen(pen);
        drawn.grid = true;
    }
    if (scene.projection != nullptr) {
        if (settings.altitude_circles) {
            for (const double elevation : {0.0, 30.0, 60.0}) {
                const std::vector<QPolygonF> lines = altitude_circle(*scene.projection, elevation);
                draw_polylines(painter, viewport, lines);
                if (!lines.empty() && !lines.front().isEmpty()) {
                    const QPointF at = viewport.image_to_widget(lines.front().first());
                    painter.drawText(at + QPointF(4.0, -4.0), QString::number(elevation) + QStringLiteral("°"));
                }
            }
            drawn.altitude_circles = true;
        }
        if (settings.compass) {
            // The cardinal points are marked low in the sky, where a horizon would be, inside most all-sky views.
            for (const CompassMark& mark : compass_marks(*scene.projection, 10.0)) {
                const QPointF at = viewport.image_to_widget(mark.pixel);
                painter.drawEllipse(at, 3.0, 3.0);
                painter.drawText(at + QPointF(6.0, 5.0), mark.label);
            }
            drawn.compass = true;
        }
        if (scene.sun != nullptr) {
            const std::optional<QPointF> sun = sun_pixel(*scene.projection, *scene.sun);
            if (settings.sun && sun.has_value()) {
                QPen sun_pen(scene.sun_colour, 2.0);
                sun_pen.setCosmetic(true);
                painter.setPen(sun_pen);
                const QPointF at = viewport.image_to_widget(*sun);
                constexpr double kRadius = 12.0;
                painter.drawEllipse(at, kRadius, kRadius);
                for (int i = 0; i < 8; ++i) {
                    const double angle = i * std::numbers::pi / 4.0;
                    const QPointF dir(std::cos(angle), std::sin(angle));
                    painter.drawLine(at + dir * (kRadius + 3.0), at + dir * (kRadius + 9.0));
                }
                painter.setPen(pen);
                drawn.sun = true;
            }
            if (settings.keep_out) {
                QPen ring_pen(scene.sun_colour, 1.5);
                ring_pen.setStyle(Qt::DashLine);
                ring_pen.setCosmetic(true);
                painter.setPen(ring_pen);
                draw_polylines(painter, viewport, keep_out_ring(*scene.projection, *scene.sun, settings.keep_out_deg));
                painter.setPen(pen);
                drawn.keep_out = true;
            }
        }
    }
    painter.restore();
    return drawn;
}

}  // namespace cloudscope::desktop
