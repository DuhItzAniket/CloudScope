#include <cloudscope/geometry/rotation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace cloudscope;
using Catch::Matchers::WithinAbs;

namespace {

void check_vector(const Vector3& actual, double x, double y, double z)
{
    CHECK_THAT(actual.x, WithinAbs(x, 1e-12));
    CHECK_THAT(actual.y, WithinAbs(y, 1e-12));
    CHECK_THAT(actual.z, WithinAbs(z, 1e-12));
}

constexpr Vector3 kRight{.x = 1.0, .y = 0.0, .z = 0.0};    // camera +x
constexpr Vector3 kDown{.x = 0.0, .y = 1.0, .z = 0.0};     // camera +y
constexpr Vector3 kForward{.x = 0.0, .y = 0.0, .z = 1.0};  // camera +z, the optical axis

}  // namespace

TEST_CASE("a level camera looking north has east to its right and the ground below", "[geometry][rotation]")
{
    const Quaternion q = camera_orientation({.azimuth = Degrees(0.0), .elevation = Degrees(0.0)});
    check_vector(rotate(q, kForward), 0.0, 1.0, 0.0);  // north
    check_vector(rotate(q, kRight), 1.0, 0.0, 0.0);    // east
    check_vector(rotate(q, kDown), 0.0, 0.0, -1.0);    // down
}

TEST_CASE("azimuth turns the camera clockwise seen from above, elevation tips it up", "[geometry][rotation]")
{
    const Quaternion east = camera_orientation({.azimuth = Degrees(90.0), .elevation = Degrees(0.0)});
    check_vector(rotate(east, kForward), 1.0, 0.0, 0.0);  // looks east
    check_vector(rotate(east, kRight), 0.0, -1.0, 0.0);   // south is to its right
    check_vector(rotate(east, kDown), 0.0, 0.0, -1.0);

    const double h = std::numbers::sqrt2 / 2.0;
    const Quaternion up45 = camera_orientation({.azimuth = Degrees(0.0), .elevation = Degrees(45.0)});
    check_vector(rotate(up45, kForward), 0.0, h, h);    // north and up
    check_vector(rotate(up45, kRight), 1.0, 0.0, 0.0);  // the horizon stays level

    // Straight up: the bottom of the image points towards the azimuth the camera was tilted up from.
    const Quaternion zenith = camera_orientation({.azimuth = Degrees(90.0), .elevation = Degrees(90.0)});
    check_vector(rotate(zenith, kForward), 0.0, 0.0, 1.0);
    check_vector(rotate(zenith, kDown), 1.0, 0.0, 0.0);
}

TEST_CASE("the optical axis of an orientation is the direction it was made for", "[geometry][rotation]")
{
    const double azimuth = GENERATE(0.0, 33.0, 90.0, 179.5, 180.0, 270.0, 359.0);
    const double elevation = GENERATE(-30.0, 0.0, 12.5, 45.0, 89.0);
    CAPTURE(azimuth, elevation);
    const Quaternion q = camera_orientation({.azimuth = Degrees(azimuth), .elevation = Degrees(elevation)});
    const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    CHECK_THAT(norm, WithinAbs(1.0, 1e-12));
    const SkyDirection direction = optical_axis(q);
    // Compared as directions: 359.9999999 and 0 are the same azimuth.
    CHECK_THAT(angular_difference(Degrees(azimuth), direction.azimuth).value(), WithinAbs(0.0, 1e-9));
    CHECK(direction.azimuth.value() >= 0.0);
    CHECK(direction.azimuth.value() < 360.0);
    CHECK_THAT(direction.elevation.value(), WithinAbs(elevation, 1e-9));
}

TEST_CASE("straight up has no azimuth, and tilting past the zenith looks the other way", "[geometry][rotation]")
{
    const SkyDirection up = optical_axis(camera_orientation({.azimuth = Degrees(123.0), .elevation = Degrees(90.0)}));
    CHECK_THAT(up.elevation.value(), WithinAbs(90.0, 1e-9));
    CHECK(up.azimuth.value() == 0.0);

    // 120 degrees of tilt from azimuth 10: the camera has gone over the top and looks back at azimuth 190.
    const SkyDirection over = optical_axis(camera_orientation({.azimuth = Degrees(10.0), .elevation = Degrees(120.0)}));
    CHECK_THAT(over.azimuth.value(), WithinAbs(190.0, 1e-9));
    CHECK_THAT(over.elevation.value(), WithinAbs(60.0, 1e-9));
}

TEST_CASE("quaternions combine, invert and measure rotations", "[geometry][rotation]")
{
    const Vector3 up{.x = 0.0, .y = 0.0, .z = 1.0};
    const Quaternion quarter = from_axis_angle(up, to_radians(Degrees(90.0)));
    // A positive turn about "up" is anticlockwise seen from above: east goes to north.
    check_vector(rotate(quarter, {.x = 1.0, .y = 0.0, .z = 0.0}), 0.0, 1.0, 0.0);
    // The axis may have any length.
    const Quaternion same = from_axis_angle({.x = 0.0, .y = 0.0, .z = 25.0}, to_radians(Degrees(90.0)));
    CHECK_THAT(angle_between(quarter, same).value(), WithinAbs(0.0, 1e-6));

    // Two quarter turns are a half turn.
    const Quaternion half = multiply(quarter, quarter);
    check_vector(rotate(half, {.x = 1.0, .y = 0.0, .z = 0.0}), -1.0, 0.0, 0.0);
    CHECK_THAT(angle_between(Quaternion{}, half).value(), WithinAbs(180.0, 1e-6));
    CHECK_THAT(angle_between(Quaternion{}, quarter).value(), WithinAbs(90.0, 1e-9));

    // A rotation followed by its conjugate is no rotation.
    const Quaternion q = camera_orientation({.azimuth = Degrees(200.0), .elevation = Degrees(35.0)});
    const Quaternion identity = multiply(q, conjugate(q));
    CHECK_THAT(identity.w, WithinAbs(1.0, 1e-12));
    CHECK_THAT(angle_between(identity, Quaternion{}).value(), WithinAbs(0.0, 1e-6));

    // q and -q are the same orientation.
    const Quaternion negated{.w = -q.w, .x = -q.x, .y = -q.y, .z = -q.z};
    CHECK_THAT(angle_between(q, negated).value(), WithinAbs(0.0, 1e-6));

    // "first b, then a": turning to azimuth 90 and then a quarter turn anticlockwise looks north again.
    const Quaternion east = camera_orientation({.azimuth = Degrees(90.0), .elevation = Degrees(0.0)});
    const SkyDirection turned = optical_axis(multiply(quarter, east));
    CHECK_THAT(wrap_180(turned.azimuth).value(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("degenerate input gives the identity instead of not-a-number", "[geometry][rotation]")
{
    const Quaternion zero = normalised({.w = 0.0, .x = 0.0, .y = 0.0, .z = 0.0});
    CHECK(zero.w == 1.0);
    CHECK(zero.x == 0.0);
    const Quaternion no_axis = from_axis_angle({.x = 0.0, .y = 0.0, .z = 0.0}, to_radians(Degrees(45.0)));
    CHECK(no_axis.w == 1.0);
    const Quaternion scaled = normalised({.w = 2.0, .x = 0.0, .y = 0.0, .z = 0.0});
    CHECK(scaled.w == 1.0);
}
