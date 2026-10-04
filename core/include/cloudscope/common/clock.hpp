// Time for CloudScope (ADR-012, NFR-DATA-01).
//
//   - Stored times are UTC with millisecond resolution, written as ISO 8601 with an explicit offset.
//   - Scheduling and intervals use the monotonic clock: a wall-clock jump never shifts a schedule.
//   - Every timestamp states where the wall-clock time comes from (TimeSource).
//
// Code takes an IClock& instead of calling the system clock, so that tests can control time (ManualClock).
#pragma once

#include "cloudscope/common/error.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace cloudscope {

using UtcTime = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
using MonotonicTime = std::chrono::steady_clock::time_point;

// What disciplines the wall clock.
enum class TimeSource {
    Host,    // the computer's clock; whether it is synchronised is not known
    Ntp,     // the operating system reports the clock as synchronised (NTP or similar)
    GpsPps,  // GPS time with pulse-per-second (set by the GPS driver)
};

// "host", "ntp", "gps-pps": the spelling used in sidecars and the API.
[[nodiscard]] std::string_view to_string(TimeSource source);
[[nodiscard]] Expected<TimeSource> time_source_from_string(std::string_view text);

struct Timestamp {
    UtcTime utc;
    MonotonicTime monotonic;
    TimeSource source = TimeSource::Host;
};

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual UtcTime now_utc() const = 0;
    [[nodiscard]] virtual MonotonicTime now_monotonic() const = 0;
    [[nodiscard]] virtual TimeSource time_source() const = 0;

    // Wall-clock and monotonic time read together.
    [[nodiscard]] Timestamp now() const { return {now_utc(), now_monotonic(), time_source()}; }
};

// The operating system's clocks. Thread-safe.
// time_source() is Ntp on Linux when the kernel reports a synchronised clock; on Windows it is always Host,
// because Windows offers no simple query for it.
class SystemClock final : public IClock {
public:
    [[nodiscard]] UtcTime now_utc() const override;
    [[nodiscard]] MonotonicTime now_monotonic() const override;
    [[nodiscard]] TimeSource time_source() const override;

private:
    mutable std::mutex mutex_;
    mutable MonotonicTime source_checked_at_{};
    mutable TimeSource source_ = TimeSource::Host;
    mutable bool source_known_ = false;
};

// A clock that only moves when told to; for tests and simulations. Thread-safe.
class ManualClock final : public IClock {
public:
    explicit ManualClock(UtcTime start, TimeSource source = TimeSource::Host);

    [[nodiscard]] UtcTime now_utc() const override;
    [[nodiscard]] MonotonicTime now_monotonic() const override;
    [[nodiscard]] TimeSource time_source() const override;

    // Time passes: wall-clock and monotonic time both move forward.
    void advance(std::chrono::milliseconds duration);
    // The wall clock is stepped (as by an NTP correction or a user); monotonic time does not change.
    void set_utc(UtcTime utc);
    void set_time_source(TimeSource source);

private:
    mutable std::mutex mutex_;
    UtcTime utc_;
    MonotonicTime monotonic_;
    TimeSource source_;
};

// "2026-10-04T12:34:56.789+00:00": the form used in sidecars, catalogues and the API.
[[nodiscard]] std::string format_iso8601(UtcTime time);

// "20261004T123456_789Z": the form used in file names (same as the interim sky logger).
[[nodiscard]] std::string format_file_stamp(UtcTime time);

// Parses "YYYY-MM-DDThh:mm:ss[.fraction](Z|+hh:mm|-hh:mm)" and converts to UTC. The offset is mandatory:
// a time without one is ambiguous. Fractions finer than a millisecond are truncated.
[[nodiscard]] Expected<UtcTime> parse_iso8601(std::string_view text);

[[nodiscard]] std::int64_t to_unix_ms(UtcTime time);
[[nodiscard]] UtcTime from_unix_ms(std::int64_t milliseconds);

}  // namespace cloudscope
