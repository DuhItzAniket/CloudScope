#include <cloudscope/common/error.hpp>
#include <cloudscope/common/scope_exit.hpp>
#include <cloudscope/common/units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>
#include <stdexcept>
#include <string>

using namespace cloudscope;
using namespace cloudscope::literals;
using Catch::Matchers::WithinAbs;

namespace {

Expected<int> parse_port(const std::string& text)
{
    if (text.empty()) {
        return fail(ErrorCode::InvalidArgument, "the port is empty");
    }
    return std::stoi(text);
}

}  // namespace

TEST_CASE("Expected carries either a value or an error", "[common][error]")
{
    const Expected<int> ok = parse_port("8080");
    REQUIRE(ok.has_value());
    CHECK(*ok == 8080);

    const Expected<int> bad = parse_port("");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK(bad.error().message == "the port is empty");
    CHECK(bad.value_or(-1) == -1);
}

TEST_CASE("Expected<void> reports success without a value", "[common][error]")
{
    const Expected<void> ok{};
    CHECK(ok.has_value());
    const Expected<void> bad = fail(ErrorCode::Timeout, "no answer");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == ErrorCode::Timeout);
}

TEST_CASE("errors propagate through chained operations", "[common][error]")
{
    const auto doubled = [](const std::string& text) {
        return parse_port(text).transform([](int port) { return port * 2; });
    };
    CHECK(doubled("21").value() == 42);
    CHECK(doubled("").error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("context is added in front of the message and keeps the code", "[common][error]")
{
    const Error inner{.code = ErrorCode::Parse, .message = "line 3: unexpected '='"};
    const Error outer = inner.with_context("config.toml");
    CHECK(outer.code == ErrorCode::Parse);
    CHECK(outer.message == "config.toml: line 3: unexpected '='");
    CHECK(outer.to_string() == "Parse: config.toml: line 3: unexpected '='");
    CHECK(inner.message == "line 3: unexpected '='");  // the original is unchanged
}

TEST_CASE("every error code has its own name", "[common][error]")
{
    std::set<std::string> names;
    for (const ErrorCode code :
         {ErrorCode::InvalidArgument, ErrorCode::NotFound, ErrorCode::AlreadyExists, ErrorCode::PermissionDenied,
          ErrorCode::Unavailable, ErrorCode::Timeout, ErrorCode::Io, ErrorCode::Parse, ErrorCode::Validation,
          ErrorCode::Unsupported, ErrorCode::Cancelled, ErrorCode::Internal}) {
        names.insert(std::string(to_string(code)));
    }
    CHECK(names.size() == 12);
    CHECK_FALSE(names.contains("Unknown"));
}

TEST_CASE("degrees and radians convert explicitly", "[common][units]")
{
    CHECK_THAT(to_radians(180.0_deg).value(), WithinAbs(3.141592653589793, 1e-15));
    CHECK_THAT(to_degrees(Radians(3.141592653589793 / 2)).value(), WithinAbs(90.0, 1e-12));
    CHECK_THAT(to_degrees(to_radians(Degrees(123.456))).value(), WithinAbs(123.456, 1e-12));

    // Different units are different types: mixing them does not compile.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Degrees, Radians>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<double, Degrees>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Degrees, double>);
}

TEST_CASE("quantities support arithmetic within one unit", "[common][units]")
{
    constexpr Degrees kA = 30_deg;
    constexpr Degrees kB = 12.5_deg;
    STATIC_REQUIRE((kA + kB).value() == 42.5);
    STATIC_REQUIRE((kA - kB).value() == 17.5);
    STATIC_REQUIRE((-kA).value() == -30.0);
    STATIC_REQUIRE((kA * 2.0).value() == 60.0);
    STATIC_REQUIRE((2.0 * kA).value() == 60.0);
    STATIC_REQUIRE((kA / 4.0).value() == 7.5);
    STATIC_REQUIRE(kA / kB == 2.4);
    STATIC_REQUIRE(kB < kA);
    STATIC_REQUIRE(kA == Degrees(30.0));

    Degrees sum = kA;
    sum += kB;
    sum -= 2.5_deg;
    CHECK(sum == 40_deg);
    CHECK(Degrees().value() == 0.0);
}

TEST_CASE("wrap_360 maps every angle into the range from 0 up to 360", "[common][units]")
{
    const auto [input, expected] = GENERATE(table<double, double>({
        {0.0, 0.0},
        {359.5, 359.5},
        {360.0, 0.0},
        {725.0, 5.0},
        {-1.0, 359.0},
        {-360.0, 0.0},
        {-725.0, 355.0},
    }));
    CAPTURE(input);
    CHECK_THAT(wrap_360(Degrees(input)).value(), WithinAbs(expected, 1e-9));
}

TEST_CASE("wrap_360 never returns 360 for tiny negative angles", "[common][units]")
{
    const double wrapped = wrap_360(Degrees(-1e-17)).value();
    CHECK(wrapped >= 0.0);
    CHECK(wrapped < 360.0);
}

TEST_CASE("wrap_180 maps every angle into the range from -180 up to 180", "[common][units]")
{
    const auto [input, expected] = GENERATE(table<double, double>({
        {0.0, 0.0},
        {179.0, 179.0},
        {180.0, -180.0},
        {181.0, -179.0},
        {-180.0, -180.0},
        {-181.0, 179.0},
        {540.0, -180.0},
        {-350.0, 10.0},
    }));
    CAPTURE(input);
    CHECK_THAT(wrap_180(Degrees(input)).value(), WithinAbs(expected, 1e-9));
}

TEST_CASE("angular_difference takes the short way round", "[common][units]")
{
    CHECK_THAT(angular_difference(350_deg, 10_deg).value(), WithinAbs(20.0, 1e-9));   // across north, clockwise
    CHECK_THAT(angular_difference(10_deg, 350_deg).value(), WithinAbs(-20.0, 1e-9));  // and back
    CHECK_THAT(angular_difference(90_deg, 90_deg).value(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(angular_difference(0_deg, 180_deg).value(), WithinAbs(-180.0, 1e-9));  // exactly opposite: -180
    CHECK_THAT(angular_difference(720_deg, 45_deg).value(), WithinAbs(45.0, 1e-9));
}

TEST_CASE("ScopeExit runs its function when the scope ends, also when an exception passes", "[common][scope_exit]")
{
    int calls = 0;
    {
        const ScopeExit count([&calls] { ++calls; });
        CHECK(calls == 0);
    }
    CHECK(calls == 1);

    try {
        const ScopeExit count([&calls] { ++calls; });
        throw std::runtime_error("leaving early");
    } catch (const std::runtime_error&) {
        CHECK(calls == 2);
    }
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<ScopeExit<void (*)()>>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<ScopeExit<void (*)()>>);
}

TEST_CASE("fractional durations convert with std::chrono", "[common][units]")
{
    const Milliseconds exposure(12.5);
    CHECK_THAT(Seconds(exposure).count(), WithinAbs(0.0125, 1e-15));
    CHECK(std::chrono::duration_cast<std::chrono::microseconds>(exposure).count() == 12500);
    CHECK_THAT(Milliseconds(std::chrono::seconds(2)).count(), WithinAbs(2000.0, 1e-12));
}
