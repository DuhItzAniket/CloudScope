// Rotations in three dimensions: the small set of operations CloudScope needs on orientation quaternions.
//
// Frames (ADR-012, architecture 5.2):
//   local horizontal  East-North-Up (ENU): x east, y north, z up. Azimuth is measured from north, clockwise
//                     seen from above (east = 90 degrees); elevation from the horizon, up positive.
//   camera            +z along the optical axis, +x to the right of the image, +y down the image.
//
// An orientation is the rotation that takes camera (or sensor) coordinates to ENU coordinates.
#pragma once

#include "cloudscope/common/units.hpp"
#include "cloudscope/hal/sensors.hpp"

namespace cloudscope {

using hal::Quaternion;

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// The same rotation with length 1; the identity if `q` is all zero.
[[nodiscard]] Quaternion normalised(const Quaternion& q);

// The rotation "first b, then a".
[[nodiscard]] Quaternion multiply(const Quaternion& a, const Quaternion& b);

[[nodiscard]] Quaternion conjugate(const Quaternion& q);

// `v` turned by the unit quaternion `q`.
[[nodiscard]] Vector3 rotate(const Quaternion& q, const Vector3& v);

// A turn by `angle` about `axis` (any length but zero), following the right-hand rule.
[[nodiscard]] Quaternion from_axis_angle(const Vector3& axis, Radians angle);

// The size of the turn that takes orientation `a` to orientation `b`: 0 to 180 degrees.
[[nodiscard]] Degrees angle_between(const Quaternion& a, const Quaternion& b);

struct SkyDirection {
    Degrees azimuth;    // [0, 360)
    Degrees elevation;  // [-90, 90]
};

// The orientation of a camera that looks towards `direction` with a level horizon (image +x horizontal).
// Looking straight up, the image's "down" points towards the given azimuth.
[[nodiscard]] Quaternion camera_orientation(SkyDirection direction);

// Where the optical axis of a camera with this orientation points. Straight up or down, the azimuth is 0.
[[nodiscard]] SkyDirection optical_axis(const Quaternion& camera_to_enu);

}  // namespace cloudscope
