#include <cloudscope/capture/solar.hpp>
#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <string_view>

using namespace cloudscope;
using Catch::Matchers::WithinAbs;

namespace {

UtcTime at(std::string_view iso)
{
    const auto parsed = parse_iso8601(iso);
    REQUIRE(parsed);
    return *parsed;
}

struct Reference {
    const char* time;
    double latitude;
    double longitude;
    double elevation;  // geometric, pvlib (NREL SPA)
    double azimuth;
};

// Reference values from pvlib 0.11 `solarposition.get_solarposition` (NREL SPA), three sites and four dates.
constexpr std::array<Reference, 12> kReferences = {{
    {.time = "2026-03-20T12:00:00Z", .latitude = 12.97, .longitude = 77.59, .elevation = 13.885, .azimuth = 266.688},
    {.time = "2026-06-21T06:30:00Z", .latitude = 12.97, .longitude = 77.59, .elevation = 78.366, .azimuth = 25.112},
    {.time = "2026-10-09T10:00:00Z", .latitude = 12.97, .longitude = 77.59, .elevation = 35.995, .azimuth = 252.092},
    {.time = "2026-12-21T23:00:00Z", .latitude = 12.97, .longitude = 77.59, .elevation = -29.668, .azimuth = 109.788},
    {.time = "2026-03-20T12:00:00Z", .latitude = 53.15, .longitude = 8.17, .elevation = 36.543, .azimuth = 187.864},
    {.time = "2026-06-21T06:30:00Z", .latitude = 53.15, .longitude = 8.17, .elevation = 27.567, .azimuth = 87.045},
    {.time = "2026-10-09T10:00:00Z", .latitude = 53.15, .longitude = 8.17, .elevation = 28.442, .azimuth = 158.808},
    {.time = "2026-12-21T23:00:00Z", .latitude = 53.15, .longitude = 8.17, .elevation = -59.894, .azimuth = 348.230},
    {.time = "2026-03-20T12:00:00Z", .latitude = -33.9, .longitude = 151.2, .elevation = -45.526, .azimuth = 226.706},
    {.time = "2026-06-21T06:30:00Z", .latitude = -33.9, .longitude = 151.2, .elevation = 3.451, .azimuth = 301.375},
    {.time = "2026-10-09T10:00:00Z", .latitude = -33.9, .longitude = 151.2, .elevation = -23.840, .azimuth = 243.738},
    {.time = "2026-12-21T23:00:00Z", .latitude = -33.9, .longitude = 151.2, .elevation = 50.840, .azimuth = 86.201},
}};

}  // namespace

TEST_CASE("the Sun position agrees with NREL SPA to a few hundredths of a degree", "[capture][solar]")
{
    for (const Reference& reference : kReferences) {
        INFO(reference.time << " at " << reference.latitude << ", " << reference.longitude);
        const SunPosition sun = sun_position(at(reference.time), reference.latitude, reference.longitude);
        CHECK_THAT(sun.elevation_deg, WithinAbs(reference.elevation, 0.05));
        // Azimuth is ill-conditioned near the zenith (Bengaluru at solstice noon, elevation 78°).
        const double azimuth_tolerance = reference.elevation > 70.0 ? 0.5 : 0.1;
        CHECK_THAT(sun.azimuth_deg, WithinAbs(reference.azimuth, azimuth_tolerance));
    }
}

TEST_CASE("refraction lifts the Sun at the horizon by about half a degree", "[capture][solar]")
{
    CHECK_THAT(atmospheric_refraction_deg(0.0), WithinAbs(0.48, 0.03));
    CHECK_THAT(atmospheric_refraction_deg(45.0), WithinAbs(0.016, 0.003));
    CHECK(atmospheric_refraction_deg(89.0) == 0.0);
    CHECK(atmospheric_refraction_deg(-10.0) > 0.0);
    CHECK(atmospheric_refraction_deg(-10.0) < 0.1);
    const SunPosition sun = sun_position(at("2026-06-21T06:30:00Z"), 53.15, 8.17);
    CHECK(sun.apparent_elevation_deg > sun.elevation_deg);
    CHECK_THAT(sun.apparent_elevation_deg - sun.elevation_deg, WithinAbs(0.032, 0.01));
}

TEST_CASE("the sky period follows the twilight definitions", "[capture][solar]")
{
    CHECK(sky_period(30.0) == SkyPeriod::Day);
    CHECK(sky_period(0.1) == SkyPeriod::Day);
    CHECK(sky_period(-1.0) == SkyPeriod::CivilTwilight);
    CHECK(sky_period(-7.0) == SkyPeriod::NauticalTwilight);
    CHECK(sky_period(-15.0) == SkyPeriod::AstronomicalTwilight);
    CHECK(sky_period(-30.0) == SkyPeriod::Night);
    CHECK(to_string(SkyPeriod::NauticalTwilight) == "nautical-twilight");
    // Bengaluru, 9 Oct 2026, 23:00 UTC (04:30 local): deep night; 10:00 UTC: day.
    CHECK(sky_period(sun_position(at("2026-10-09T23:00:00Z"), 12.97, 77.59).elevation_deg) == SkyPeriod::Night);
    CHECK(sky_period(sun_position(at("2026-10-09T10:00:00Z"), 12.97, 77.59).elevation_deg) == SkyPeriod::Day);
}

TEST_CASE("the Sun crosses the meridian near local solar noon", "[capture][solar]")
{
    // Bengaluru: longitude 77.59° east is 5 h 10 min ahead of UTC; local solar noon on 9 Oct 2026 is about
    // 06:37 UTC (equation of time +12.6 min).
    const SunPosition before = sun_position(at("2026-10-09T06:00:00Z"), 12.97, 77.59);
    const SunPosition after = sun_position(at("2026-10-09T07:15:00Z"), 12.97, 77.59);
    CHECK(before.hour_angle_deg < 0.0);
    CHECK(after.hour_angle_deg > 0.0);
    CHECK(before.azimuth_deg < 180.0);
    CHECK(after.azimuth_deg > 180.0);
    CHECK_THAT(before.equation_of_time_min, WithinAbs(12.6, 0.5));
}
