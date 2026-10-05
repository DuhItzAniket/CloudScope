#include "cloudscope/geometry/rotation.hpp"

#include <algorithm>
#include <cmath>

namespace cloudscope {

namespace {

// The rotation whose matrix has the columns x, y, z (the images of the three coordinate axes).
Quaternion from_axes(const Vector3& x, const Vector3& y, const Vector3& z)
{
    // Entry m_rc of the matrix: row r, column c.
    const double m00 = x.x;
    const double m01 = y.x;
    const double m02 = z.x;
    const double m10 = x.y;
    const double m11 = y.y;
    const double m12 = z.y;
    const double m20 = x.z;
    const double m21 = y.z;
    const double m22 = z.z;

    // Standard conversion, branching on the largest diagonal term so that the divisor is never small.
    Quaternion q;
    const double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(trace + 1.0);
        q = {.w = 0.25 * s, .x = (m21 - m12) / s, .y = (m02 - m20) / s, .z = (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
        q = {.w = (m21 - m12) / s, .x = 0.25 * s, .y = (m01 + m10) / s, .z = (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
        q = {.w = (m02 - m20) / s, .x = (m01 + m10) / s, .y = 0.25 * s, .z = (m12 + m21) / s};
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
        q = {.w = (m10 - m01) / s, .x = (m02 + m20) / s, .y = (m12 + m21) / s, .z = 0.25 * s};
    }
    return normalised(q);
}

}  // namespace

Quaternion normalised(const Quaternion& q)
{
    const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(norm > 0.0)) {
        return {};
    }
    return {.w = q.w / norm, .x = q.x / norm, .y = q.y / norm, .z = q.z / norm};
}

Quaternion multiply(const Quaternion& a, const Quaternion& b)
{
    return {.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quaternion conjugate(const Quaternion& q)
{
    return {.w = q.w, .x = -q.x, .y = -q.y, .z = -q.z};
}

Vector3 rotate(const Quaternion& q, const Vector3& v)
{
    // v + 2 w (u x v) + 2 u x (u x v), with u the vector part of q.
    const Vector3 t{
        .x = 2.0 * (q.y * v.z - q.z * v.y), .y = 2.0 * (q.z * v.x - q.x * v.z), .z = 2.0 * (q.x * v.y - q.y * v.x)};
    return {.x = v.x + q.w * t.x + (q.y * t.z - q.z * t.y),
            .y = v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
            .z = v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
}

Quaternion from_axis_angle(const Vector3& axis, Radians angle)
{
    const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (!(length > 0.0)) {
        return {};
    }
    const double half = angle.value() / 2.0;
    const double scale = std::sin(half) / length;
    return {.w = std::cos(half), .x = axis.x * scale, .y = axis.y * scale, .z = axis.z * scale};
}

Degrees angle_between(const Quaternion& a, const Quaternion& b)
{
    // q and -q are the same orientation, hence the absolute value.
    const double dot = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    return to_degrees(Radians(2.0 * std::acos(std::min(dot, 1.0))));
}

Quaternion camera_orientation(SkyDirection direction)
{
    const double azimuth = to_radians(direction.azimuth).value();
    const double elevation = to_radians(direction.elevation).value();
    const double sin_az = std::sin(azimuth);
    const double cos_az = std::cos(azimuth);
    const double sin_el = std::sin(elevation);
    const double cos_el = std::cos(elevation);
    // The camera's axes expressed in East-North-Up.
    const Vector3 forward{.x = sin_az * cos_el, .y = cos_az * cos_el, .z = sin_el};  // +z: optical axis
    const Vector3 right{.x = cos_az, .y = -sin_az, .z = 0.0};                        // +x: level
    const Vector3 down{.x = sin_el * sin_az, .y = sin_el * cos_az, .z = -cos_el};    // +y = z cross x
    return from_axes(right, down, forward);
}

SkyDirection optical_axis(const Quaternion& camera_to_enu)
{
    const Vector3 forward = rotate(camera_to_enu, {.x = 0.0, .y = 0.0, .z = 1.0});
    const double horizontal = std::hypot(forward.x, forward.y);
    const Degrees elevation = to_degrees(Radians(std::atan2(forward.z, horizontal)));
    if (horizontal < 1e-12) {
        return {.azimuth = Degrees(0.0), .elevation = elevation};
    }
    return {.azimuth = wrap_360(to_degrees(Radians(std::atan2(forward.x, forward.y)))), .elevation = elevation};
}

}  // namespace cloudscope
