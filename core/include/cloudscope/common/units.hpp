// Strong types for physical quantities, so that degrees are never passed where radians are expected.
//
//   Degrees azimuth{123.4};
//   double s = std::sin(to_radians(azimuth).value());
//   Degrees error = angular_difference(target, actual);     // shortest way round, in [-180, 180)
//
// Conventions (ADR-012): files and APIs use degrees; radians appear only inside computations.
// Time spans use std::chrono; Milliseconds and Seconds below are the fractional forms used for measurements.
#pragma once

#include <chrono>
#include <compare>
#include <numbers>

namespace cloudscope {

// A double with a unit. Quantities of different units do not convert into each other implicitly.
template <class Tag>
class Quantity {
public:
    constexpr Quantity() = default;
    constexpr explicit Quantity(double value) : value_(value) {}

    [[nodiscard]] constexpr double value() const { return value_; }

    constexpr auto operator<=>(const Quantity&) const = default;

    constexpr Quantity operator-() const { return Quantity(-value_); }
    constexpr Quantity& operator+=(Quantity other)
    {
        value_ += other.value_;
        return *this;
    }
    constexpr Quantity& operator-=(Quantity other)
    {
        value_ -= other.value_;
        return *this;
    }

    friend constexpr Quantity operator+(Quantity a, Quantity b) { return Quantity(a.value_ + b.value_); }
    friend constexpr Quantity operator-(Quantity a, Quantity b) { return Quantity(a.value_ - b.value_); }
    friend constexpr Quantity operator*(Quantity a, double factor) { return Quantity(a.value_ * factor); }
    friend constexpr Quantity operator*(double factor, Quantity a) { return Quantity(a.value_ * factor); }
    friend constexpr Quantity operator/(Quantity a, double divisor) { return Quantity(a.value_ / divisor); }
    friend constexpr double operator/(Quantity a, Quantity b) { return a.value_ / b.value_; }

private:
    double value_ = 0.0;
};

using Degrees = Quantity<struct DegreesTag>;
using Radians = Quantity<struct RadiansTag>;

[[nodiscard]] constexpr Radians to_radians(Degrees angle)
{
    return Radians(angle.value() * std::numbers::pi / 180.0);
}

[[nodiscard]] constexpr Degrees to_degrees(Radians angle)
{
    return Degrees(angle.value() * 180.0 / std::numbers::pi);
}

// The same direction expressed in [0, 360): azimuths.
[[nodiscard]] Degrees wrap_360(Degrees angle);

// The same direction expressed in [-180, 180): signed offsets.
[[nodiscard]] Degrees wrap_180(Degrees angle);

// Shortest rotation that takes `from` to `to`, in [-180, 180). Positive is clockwise for azimuths.
[[nodiscard]] Degrees angular_difference(Degrees from, Degrees to);

// Fractional time spans for measured values (exposure time, latency). Schedules use integer std::chrono types.
using Milliseconds = std::chrono::duration<double, std::milli>;
using Seconds = std::chrono::duration<double>;

namespace literals {

constexpr Degrees operator""_deg(long double value)
{
    return Degrees(static_cast<double>(value));
}
constexpr Degrees operator""_deg(unsigned long long value)
{
    return Degrees(static_cast<double>(value));
}
constexpr Radians operator""_rad(long double value)
{
    return Radians(static_cast<double>(value));
}
constexpr Radians operator""_rad(unsigned long long value)
{
    return Radians(static_cast<double>(value));
}

}  // namespace literals

}  // namespace cloudscope
