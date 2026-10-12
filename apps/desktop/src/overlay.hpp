// Overlays on the live view (P037, FR-DSP-06, FR-DSP-07): reticle and grid in image space, and, with an intrinsic
// calibration and a pose, the sky geometry: altitude circles, compass points, the Sun and the keep-out zone.
//
// SkyProjection maps sky directions (azimuth, elevation in the local horizontal frame) to image pixels through the
// camera model of P031 and the orientation of rotation.hpp: the optical axis points to (azimuth, elevation) with a
// level horizon, turned by `roll` about the axis. Everything here is pure and thread-safe; painting takes a QPainter.
#pragma once

#include "viewport.hpp"

#include <cloudscope/calibration/intrinsics.hpp>
#include <cloudscope/capture/solar.hpp>
#include <cloudscope/geometry/rotation.hpp>

#include <QtCore/QPointF>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QPolygonF>

#include <cstdint>
#include <optional>
#include <vector>

class QPainter;

namespace cloudscope::desktop {

struct OverlaySettings {
    bool reticle = true;
    bool grid = false;
    int grid_divisions = 4;
    bool altitude_circles = false;  // every 30 degrees of elevation, plus the horizon
    bool compass = false;
    bool sun = false;
    bool keep_out = false;
    double keep_out_deg = 15.0;  // radius of the cone around the Sun (FR-SAF-01 default)

    friend bool operator==(const OverlaySettings&, const OverlaySettings&) = default;
};

// Where the camera looks when no mount or IMU says so: declared by the operator (sidecar "declared_by_operator").
struct SkyPose {
    double azimuth_deg = 0.0;
    double elevation_deg = 90.0;  // straight up: an all-sky camera
    double roll_deg = 0.0;  // turn about the optical axis; 0 = the image's "down" towards the azimuth (level horizon)

    friend bool operator==(const SkyPose&, const SkyPose&) = default;
};

class SkyProjection {
public:
    SkyProjection(CameraModel model, SkyPose pose);

    // The pixel of a sky direction; empty when the direction is behind the camera (a hemisphere at most).
    [[nodiscard]] std::optional<QPointF> to_pixel(SkyDirection direction) const;
    // The sky direction seen at a pixel.
    [[nodiscard]] SkyDirection to_sky(QPointF pixel) const;
    [[nodiscard]] const CameraModel& model() const { return model_; }
    [[nodiscard]] const SkyPose& pose() const { return pose_; }

private:
    CameraModel model_;
    SkyPose pose_;
    Quaternion camera_to_enu_;
};

// Unit vector in ENU for a direction, and back.
[[nodiscard]] Vector3 direction_vector(SkyDirection direction);
[[nodiscard]] SkyDirection direction_of(const Vector3& v);

// A circle of constant elevation, as polylines in image pixels (broken where the circle leaves the camera's view).
[[nodiscard]] std::vector<QPolygonF> altitude_circle(const SkyProjection& projection, double elevation_deg,
                                                     int samples = 360);

struct CompassMark {
    QString label;  // "N", "E", "S", "W"
    double azimuth_deg = 0.0;
    QPointF pixel;
};
// The four cardinal points at the given elevation (those in view).
[[nodiscard]] std::vector<CompassMark> compass_marks(const SkyProjection& projection, double elevation_deg);

// The Sun's pixel, if it is in the camera's view.
[[nodiscard]] std::optional<QPointF> sun_pixel(const SkyProjection& projection, const SunPosition& sun);

// The ring of directions at `radius_deg` from the Sun (the edge of the keep-out cone), as polylines in pixels.
[[nodiscard]] std::vector<QPolygonF> keep_out_ring(const SkyProjection& projection, const SunPosition& sun,
                                                   double radius_deg, int samples = 180);

struct OverlayScene {
    const SkyProjection* projection = nullptr;  // null: no calibration or pose, sky overlays are not drawn
    const SunPosition* sun = nullptr;           // null: Sun position unknown
    QColor colour{255, 200, 0};
    QColor sun_colour{255, 80, 0};
};

// Draws the enabled overlays for an image shown through `viewport`. Sky overlays need a projection; the Sun and
// the keep-out zone also need a Sun position. Returns which overlays were drawn (for the UI to state honestly).
struct OverlaysDrawn {
    bool reticle = false;
    bool grid = false;
    bool altitude_circles = false;
    bool compass = false;
    bool sun = false;
    bool keep_out = false;
};
OverlaysDrawn paint_overlays(QPainter& painter, const Viewport& viewport, const OverlaySettings& settings,
                             const OverlayScene& scene);

}  // namespace cloudscope::desktop
