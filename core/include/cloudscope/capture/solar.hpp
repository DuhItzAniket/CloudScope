// Position of the Sun for a time and place (P029, FR-SEQ-06): the capture sequencer switches day and night
// profiles at a configurable Sun elevation, the sidecar records where the Sun was, and the exposure controller
// knows whether to expect it in the picture.
//
// The algorithm is the one of NOAA's solar calculator (Meeus, "Astronomical Algorithms", simplified): geometric
// elevation good to about 0.01° and azimuth to a few hundredths of a degree away from the zenith, for years
// 1900–2100. That is far better than a sky camera's pointing is known. Atmospheric refraction is added separately
// (NOAA's formula), because the switch thresholds (−6°, −12°, −18°) are defined on the geometric elevation.
#pragma once

#include "cloudscope/common/clock.hpp"

#include <cstdint>
#include <string_view>

namespace cloudscope {

struct SunPosition {
    double elevation_deg = 0.0;           // geometric, above the horizon positive
    double apparent_elevation_deg = 0.0;  // with atmospheric refraction (what a camera sees)
    double azimuth_deg = 0.0;             // from north through east, 0..360
    double declination_deg = 0.0;
    double equation_of_time_min = 0.0;  // apparent minus mean solar time
    double hour_angle_deg = 0.0;        // negative before local solar noon
};

[[nodiscard]] SunPosition sun_position(UtcTime time, double latitude_deg, double longitude_deg);

// NOAA's refraction correction in degrees for a geometric elevation.
[[nodiscard]] double atmospheric_refraction_deg(double elevation_deg);

enum class SkyPeriod : std::uint8_t { Day, CivilTwilight, NauticalTwilight, AstronomicalTwilight, Night };

// Day: Sun above the horizon; civil down to −6°, nautical to −12°, astronomical to −18°, night below.
[[nodiscard]] SkyPeriod sky_period(double sun_elevation_deg);
[[nodiscard]] std::string_view to_string(SkyPeriod period);

}  // namespace cloudscope
