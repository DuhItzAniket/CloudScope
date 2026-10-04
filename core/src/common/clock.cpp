#include "cloudscope/common/clock.hpp"

#include <fmt/format.h>

#include <optional>

#ifdef __linux__
#include <sys/timex.h>
#endif

namespace cloudscope {

namespace {

using std::chrono::milliseconds;

constexpr std::chrono::seconds kSourceCheckInterval{10};

TimeSource detect_time_source()
{
#ifdef __linux__
    struct timex info{};
    const int state = adjtimex(&info);  // modes = 0: read only, needs no privilege
    if (state != -1 && state != TIME_ERROR && (info.status & STA_UNSYNC) == 0) {
        return TimeSource::Ntp;
    }
#endif
    return TimeSource::Host;
}

// Reads exactly `count` decimal digits at `position` and advances it.
std::optional<int> read_digits(std::string_view text, std::size_t& position, std::size_t count)
{
    if (position + count > text.size()) {
        return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[position + i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    position += count;
    return value;
}

bool read_char(std::string_view text, std::size_t& position, char expected)
{
    if (position < text.size() && text[position] == expected) {
        ++position;
        return true;
    }
    return false;
}

Unexpected bad_time(std::string_view text, std::string_view reason)
{
    return fail(ErrorCode::Parse, fmt::format("'{}' is not a valid timestamp: {}", text, reason));
}

}  // namespace

std::string_view to_string(TimeSource source)
{
    switch (source) {
    case TimeSource::Host:
        return "host";
    case TimeSource::Ntp:
        return "ntp";
    case TimeSource::GpsPps:
        return "gps-pps";
    }
    return "host";
}

Expected<TimeSource> time_source_from_string(std::string_view text)
{
    for (const TimeSource source : {TimeSource::Host, TimeSource::Ntp, TimeSource::GpsPps}) {
        if (text == to_string(source)) {
            return source;
        }
    }
    return fail(ErrorCode::Parse, fmt::format("'{}' is not a time source (host, ntp, gps-pps)", text));
}

UtcTime SystemClock::now_utc() const
{
    return std::chrono::time_point_cast<milliseconds>(std::chrono::system_clock::now());
}

MonotonicTime SystemClock::now_monotonic() const
{
    return std::chrono::steady_clock::now();
}

TimeSource SystemClock::time_source() const
{
    const MonotonicTime now = std::chrono::steady_clock::now();
    const std::lock_guard lock(mutex_);
    if (!source_known_ || now - source_checked_at_ >= kSourceCheckInterval) {
        source_ = detect_time_source();
        source_checked_at_ = now;
        source_known_ = true;
    }
    return source_;
}

ManualClock::ManualClock(UtcTime start, TimeSource source)
    : utc_(start), monotonic_(MonotonicTime{} + std::chrono::hours(1)), source_(source)
{
}

UtcTime ManualClock::now_utc() const
{
    const std::lock_guard lock(mutex_);
    return utc_;
}

MonotonicTime ManualClock::now_monotonic() const
{
    const std::lock_guard lock(mutex_);
    return monotonic_;
}

TimeSource ManualClock::time_source() const
{
    const std::lock_guard lock(mutex_);
    return source_;
}

void ManualClock::advance(milliseconds duration)
{
    const std::lock_guard lock(mutex_);
    utc_ += duration;
    monotonic_ += duration;
}

void ManualClock::set_utc(UtcTime utc)
{
    const std::lock_guard lock(mutex_);
    utc_ = utc;
}

void ManualClock::set_time_source(TimeSource source)
{
    const std::lock_guard lock(mutex_);
    source_ = source;
}

namespace {

struct CivilTime {
    int year;
    unsigned month;
    unsigned day;
    long long hour;
    long long minute;
    long long second;
    long long millisecond;
};

CivilTime to_civil(UtcTime time)
{
    const auto day_point = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{day_point};
    const std::chrono::hh_mm_ss<milliseconds> clock_time{time - day_point};
    return {.year = static_cast<int>(date.year()),
            .month = static_cast<unsigned>(date.month()),
            .day = static_cast<unsigned>(date.day()),
            .hour = clock_time.hours().count(),
            .minute = clock_time.minutes().count(),
            .second = clock_time.seconds().count(),
            .millisecond = clock_time.subseconds().count()};
}

}  // namespace

std::string format_iso8601(UtcTime time)
{
    const CivilTime t = to_civil(time);
    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}+00:00", t.year, t.month, t.day, t.hour, t.minute,
                       t.second, t.millisecond);
}

std::string format_file_stamp(UtcTime time)
{
    const CivilTime t = to_civil(time);
    return fmt::format("{:04}{:02}{:02}T{:02}{:02}{:02}_{:03}Z", t.year, t.month, t.day, t.hour, t.minute, t.second,
                       t.millisecond);
}

Expected<UtcTime> parse_iso8601(std::string_view text)
{
    std::size_t position = 0;
    const auto year = read_digits(text, position, 4);
    const bool dash1 = read_char(text, position, '-');
    const auto month = read_digits(text, position, 2);
    const bool dash2 = read_char(text, position, '-');
    const auto day = read_digits(text, position, 2);
    const bool separator = read_char(text, position, 'T');
    const auto hour = read_digits(text, position, 2);
    const bool colon1 = read_char(text, position, ':');
    const auto minute = read_digits(text, position, 2);
    const bool colon2 = read_char(text, position, ':');
    const auto second = read_digits(text, position, 2);
    if (!year || !dash1 || !month || !dash2 || !day || !separator || !hour || !colon1 || !minute || !colon2 ||
        !second) {
        return bad_time(text, "expected YYYY-MM-DDThh:mm:ss");
    }

    int millisecond = 0;
    if (read_char(text, position, '.')) {
        std::size_t digits = 0;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            if (digits < 3) {
                millisecond = millisecond * 10 + (text[position] - '0');
            }
            ++digits;
            ++position;
        }
        if (digits == 0 || digits > 9) {
            return bad_time(text, "the fraction of a second needs 1 to 9 digits");
        }
        for (; digits < 3; ++digits) {
            millisecond *= 10;
        }
    }

    std::chrono::minutes offset{0};
    if (read_char(text, position, 'Z')) {
        // UTC
    } else if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
        const bool negative = text[position] == '-';
        ++position;
        const auto offset_hours = read_digits(text, position, 2);
        const bool colon = read_char(text, position, ':');
        const auto offset_minutes = read_digits(text, position, 2);
        if (!offset_hours || !colon || !offset_minutes || *offset_hours > 23 || *offset_minutes > 59) {
            return bad_time(text, "the offset must be Z or +hh:mm or -hh:mm");
        }
        offset = std::chrono::minutes(*offset_hours * 60 + *offset_minutes);
        if (negative) {
            offset = -offset;
        }
    } else {
        return bad_time(text, "the UTC offset (Z or +hh:mm) is missing");
    }
    if (position != text.size()) {
        return bad_time(text, "unexpected characters after the offset");
    }

    const std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)),
                                           std::chrono::day(static_cast<unsigned>(*day))};
    if (!date.ok()) {
        return bad_time(text, "no such calendar date");
    }
    if (*hour > 23 || *minute > 59) {
        return bad_time(text, "hour or minute out of range");
    }
    if (*second > 59) {
        return bad_time(text, *second == 60 ? "leap seconds cannot be represented" : "second out of range");
    }

    const auto local = std::chrono::sys_days{date} + std::chrono::hours(*hour) + std::chrono::minutes(*minute) +
                       std::chrono::seconds(*second) + milliseconds(millisecond);
    return UtcTime{std::chrono::time_point_cast<milliseconds>(local - offset)};
}

std::int64_t to_unix_ms(UtcTime time)
{
    return time.time_since_epoch().count();
}

UtcTime from_unix_ms(std::int64_t unix_ms)
{
    return UtcTime{milliseconds(unix_ms)};
}

}  // namespace cloudscope
