#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace cloudscope;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace {

// 2026-10-04T12:34:56.789Z, checked against Python's datetime.
constexpr std::int64_t kReferenceMs = 1791117296789LL;

}  // namespace

TEST_CASE("timestamps are written as ISO 8601 UTC with milliseconds and an explicit offset", "[common][clock]")
{
    CHECK(format_iso8601(from_unix_ms(kReferenceMs)) == "2026-10-04T12:34:56.789+00:00");
    CHECK(format_iso8601(from_unix_ms(0)) == "1970-01-01T00:00:00.000+00:00");
    CHECK(format_iso8601(from_unix_ms(-1)) == "1969-12-31T23:59:59.999+00:00");
    CHECK(format_iso8601(from_unix_ms(951782400000LL)) == "2000-02-29T00:00:00.000+00:00");  // leap day
}

TEST_CASE("file stamps match the interim sky logger's file names", "[common][clock]")
{
    CHECK(format_file_stamp(from_unix_ms(kReferenceMs)) == "20261004T123456_789Z");
    CHECK(format_file_stamp(from_unix_ms(0)) == "19700101T000000_000Z");
}

TEST_CASE("ISO 8601 timestamps parse to UTC", "[common][clock]")
{
    const auto [text, expected_ms] = GENERATE(table<std::string, std::int64_t>({
        {"2026-10-04T12:34:56.789+00:00", kReferenceMs},
        {"2026-10-04T12:34:56.789Z", kReferenceMs},
        {"2026-10-04T18:04:56.789+05:30", kReferenceMs},        // India Standard Time
        {"2026-10-04T05:34:56.789-07:00", kReferenceMs},
        {"2026-10-05T02:19:56.789+13:45", kReferenceMs},        // offset moves the date
        {"2026-10-04T12:34:56Z", kReferenceMs - 789},           // no fraction
        {"2026-10-04T12:34:56.7Z", kReferenceMs - 89},          // tenths
        {"2026-10-04T12:34:56.789123456Z", kReferenceMs},       // finer than a millisecond: truncated
        {"1970-01-01T00:00:00.000+00:00", 0},
        {"2000-02-29T00:00:00Z", 951782400000LL},
    }));
    CAPTURE(text);
    const auto parsed = parse_iso8601(text);
    REQUIRE(parsed.has_value());
    CHECK(to_unix_ms(*parsed) == expected_ms);
}

TEST_CASE("formatting and parsing round-trip", "[common][clock]")
{
    for (const std::int64_t ms :
         std::initializer_list<std::int64_t>{0, 1, 999, kReferenceMs, 4102444799999, -86400000}) {
        CAPTURE(ms);
        const auto parsed = parse_iso8601(format_iso8601(from_unix_ms(ms)));
        REQUIRE(parsed.has_value());
        CHECK(to_unix_ms(*parsed) == ms);
    }
}

TEST_CASE("malformed or ambiguous timestamps are rejected with a reason", "[common][clock]")
{
    const auto [text, reason] = GENERATE(table<std::string, std::string>({
        {"", "expected YYYY-MM-DDThh:mm:ss"},
        {"2026-10-04", "expected YYYY-MM-DDThh:mm:ss"},
        {"2026-10-04 12:34:56Z", "expected YYYY-MM-DDThh:mm:ss"},
        {"2026-10-04T12:34:56", "offset"},                      // no offset: ambiguous
        {"2026-10-04T12:34:56.789", "offset"},
        {"2026-10-04T12:34:56.Z", "fraction"},
        {"2026-10-04T12:34:56.1234567890Z", "fraction"},
        {"2026-10-04T12:34:56+0530", "offset"},
        {"2026-10-04T12:34:56+24:00", "offset"},
        {"2026-10-04T12:34:56Zjunk", "unexpected characters"},
        {"2026-02-30T12:00:00Z", "no such calendar date"},
        {"2025-02-29T12:00:00Z", "no such calendar date"},      // 2025 is not a leap year
        {"2026-13-01T12:00:00Z", "no such calendar date"},
        {"2026-10-04T24:00:00Z", "out of range"},
        {"2026-10-04T12:60:00Z", "out of range"},
        {"2026-10-04T12:34:60Z", "leap seconds"},
        {"2026-10-04T12:34:61Z", "out of range"},
        {"20261004T123456Z", "expected YYYY-MM-DDThh:mm:ss"},
    }));
    CAPTURE(text);
    const auto parsed = parse_iso8601(text);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == ErrorCode::Parse);
    CHECK_THAT(parsed.error().message, ContainsSubstring(reason));
}

TEST_CASE("time sources have stable names", "[common][clock]")
{
    CHECK(to_string(TimeSource::Host) == "host");
    CHECK(to_string(TimeSource::Ntp) == "ntp");
    CHECK(to_string(TimeSource::GpsPps) == "gps-pps");
    for (const TimeSource source : {TimeSource::Host, TimeSource::Ntp, TimeSource::GpsPps}) {
        CHECK(time_source_from_string(to_string(source)).value() == source);
    }
    CHECK(time_source_from_string("GPS").error().code == ErrorCode::Parse);
}

TEST_CASE("the manual clock moves only when told to", "[common][clock]")
{
    ManualClock clock(from_unix_ms(kReferenceMs), TimeSource::Ntp);
    const Timestamp start = clock.now();
    CHECK(to_unix_ms(start.utc) == kReferenceMs);
    CHECK(start.source == TimeSource::Ntp);
    CHECK(clock.now().monotonic == start.monotonic);

    clock.advance(1500ms);
    const Timestamp later = clock.now();
    CHECK(later.utc - start.utc == 1500ms);
    CHECK(later.monotonic - start.monotonic == 1500ms);
}

TEST_CASE("a wall-clock step does not move monotonic time", "[common][clock]")
{
    ManualClock clock(from_unix_ms(kReferenceMs));
    const Timestamp before = clock.now();
    clock.set_utc(from_unix_ms(kReferenceMs - 3600000));  // the clock is set back by an hour
    clock.set_time_source(TimeSource::GpsPps);
    const Timestamp after = clock.now();
    CHECK(after.utc - before.utc == -1h);
    CHECK(after.monotonic == before.monotonic);
    CHECK(after.source == TimeSource::GpsPps);
}

TEST_CASE("the system clock is close to std::chrono and its monotonic time never goes back", "[common][clock]")
{
    const SystemClock clock;
    const auto reference = std::chrono::system_clock::now();
    const Timestamp first = clock.now();
    const Timestamp second = clock.now();

    const auto difference = std::chrono::abs(first.utc - std::chrono::time_point_cast<std::chrono::milliseconds>(reference));
    CHECK(difference < 2s);
    CHECK(second.monotonic >= first.monotonic);
    CHECK(to_unix_ms(first.utc) > 1767225600000LL);  // after 2026-01-01: the machine's clock is set
    const TimeSource source = clock.time_source();
    CHECK((source == TimeSource::Host || source == TimeSource::Ntp));  // never claims GPS on its own
}
