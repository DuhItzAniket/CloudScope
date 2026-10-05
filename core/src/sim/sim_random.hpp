// Noise for the simulators that depends only on what is being measured, not on how often it is asked for:
// the same sample of the same sensor always carries the same noise, so simulated runs can be repeated exactly.
#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace cloudscope::sim {

// SplitMix64: turns any 64-bit number into a well-mixed one.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

// A value from the standard normal distribution, fixed by (seed, stream, index). `stream` tells sensors and
// axes apart, `index` numbers the samples of one stream.
[[nodiscard]] inline double normal(std::uint64_t seed, std::uint64_t stream, std::uint64_t index)
{
    const std::uint64_t first = mix(seed ^ mix(stream ^ mix(index)));
    const std::uint64_t second = mix(first);
    constexpr double kTwoTo53 = 9007199254740992.0;
    const double u1 = (static_cast<double>(first >> 11U) + 1.0) / kTwoTo53;         // (0, 1]
    const double u2 = static_cast<double>(second >> 11U) / kTwoTo53;                // [0, 1)
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);  // Box-Muller
}

}  // namespace cloudscope::sim
