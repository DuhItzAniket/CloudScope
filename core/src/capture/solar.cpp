#include "cloudscope/capture/solar.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cloudscope {

namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr double kRadians = std::numbers::pi / 180.0;

double wrap_degrees(double value)
{
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

}  // namespace

double atmospheric_refraction_deg(double elevation_deg)
{
    if (elevation_deg > 85.0) {
        return 0.0;
    }
    const double tangent = std::tan(elevation_deg * kRadians);
    double arcseconds = 0.0;
    if (elevation_deg > 5.0) {
        arcseconds = 58.1 / tangent - 0.07 / (tangent * tangent * tangent) + 0.000086 / std::pow(tangent, 5.0);
    } else if (elevation_deg > -0.575) {
        arcseconds = 1735.0 + elevation_deg *
                                  (-518.2 + elevation_deg * (103.4 + elevation_deg * (-12.79 + elevation_deg * 0.711)));
    } else {
        arcseconds = -20.772 / tangent;
    }
    return arcseconds / 3600.0;
}

SunPosition sun_position(UtcTime time, double latitude_deg, double longitude_deg)
{
    const double unix_days = static_cast<double>(to_unix_ms(time)) / 86'400'000.0;
    const double julian_day = unix_days + 2440587.5;
    const double t = (julian_day - 2451545.0) / 36525.0;  // Julian centuries from J2000.0

    const double mean_longitude = wrap_degrees(280.46646 + t * (36000.76983 + t * 0.0003032));
    const double mean_anomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double m = mean_anomaly * kRadians;
    const double centre = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                          std::sin(2.0 * m) * (0.019993 - 0.000101 * t) + std::sin(3.0 * m) * 0.000289;
    const double true_longitude = mean_longitude + centre;
    const double omega = (125.04 - 1934.136 * t) * kRadians;
    const double apparent_longitude = true_longitude - 0.00569 - 0.00478 * std::sin(omega);
    const double mean_obliquity = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double obliquity = (mean_obliquity + 0.00256 * std::cos(omega)) * kRadians;

    const double declination = std::asin(std::sin(obliquity) * std::sin(apparent_longitude * kRadians));
    const double y = std::tan(obliquity / 2.0) * std::tan(obliquity / 2.0);
    const double l0 = mean_longitude * kRadians;
    const double equation_of_time =
        4.0 * kDegrees *
        (y * std::sin(2.0 * l0) - 2.0 * eccentricity * std::sin(m) +
         4.0 * eccentricity * y * std::sin(m) * std::cos(2.0 * l0) - 0.5 * y * y * std::sin(4.0 * l0) -
         1.25 * eccentricity * eccentricity * std::sin(2.0 * m));

    const double minutes_of_day = (unix_days - std::floor(unix_days)) * 1440.0;
    double true_solar_minutes = std::fmod(minutes_of_day + equation_of_time + 4.0 * longitude_deg, 1440.0);
    if (true_solar_minutes < 0.0) {
        true_solar_minutes += 1440.0;
    }
    const double hour_angle_deg =
        true_solar_minutes / 4.0 < 0.0 ? true_solar_minutes / 4.0 + 180.0 : true_solar_minutes / 4.0 - 180.0;

    const double latitude = latitude_deg * kRadians;
    const double hour_angle = hour_angle_deg * kRadians;
    double cos_zenith =
        std::sin(latitude) * std::sin(declination) + std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    cos_zenith = std::clamp(cos_zenith, -1.0, 1.0);
    const double zenith = std::acos(cos_zenith);
    const double elevation_deg = 90.0 - zenith * kDegrees;

    double azimuth_deg = 0.0;
    const double denominator = std::cos(latitude) * std::sin(zenith);
    if (std::abs(denominator) > 1e-12) {
        double cos_azimuth = (std::sin(latitude) * std::cos(zenith) - std::sin(declination)) / denominator;
        cos_azimuth = std::clamp(cos_azimuth, -1.0, 1.0);
        const double angle = std::acos(cos_azimuth) * kDegrees;
        azimuth_deg = hour_angle_deg > 0.0 ? wrap_degrees(angle + 180.0) : wrap_degrees(540.0 - angle);
    } else {
        azimuth_deg = latitude_deg >= 0.0 ? 180.0 : 0.0;  // Sun at the zenith or nadir: azimuth is undefined
    }

    return SunPosition{.elevation_deg = elevation_deg,
                       .apparent_elevation_deg = elevation_deg + atmospheric_refraction_deg(elevation_deg),
                       .azimuth_deg = azimuth_deg,
                       .declination_deg = declination * kDegrees,
                       .equation_of_time_min = equation_of_time,
                       .hour_angle_deg = hour_angle_deg};
}

SkyPeriod sky_period(double sun_elevation_deg)
{
    if (sun_elevation_deg > 0.0) {
        return SkyPeriod::Day;
    }
    if (sun_elevation_deg > -6.0) {
        return SkyPeriod::CivilTwilight;
    }
    if (sun_elevation_deg > -12.0) {
        return SkyPeriod::NauticalTwilight;
    }
    if (sun_elevation_deg > -18.0) {
        return SkyPeriod::AstronomicalTwilight;
    }
    return SkyPeriod::Night;
}

std::string_view to_string(SkyPeriod period)
{
    switch (period) {
    case SkyPeriod::Day:
        return "day";
    case SkyPeriod::CivilTwilight:
        return "civil-twilight";
    case SkyPeriod::NauticalTwilight:
        return "nautical-twilight";
    case SkyPeriod::AstronomicalTwilight:
        return "astronomical-twilight";
    case SkyPeriod::Night:
        return "night";
    }
    return "unknown";
}

}  // namespace cloudscope
