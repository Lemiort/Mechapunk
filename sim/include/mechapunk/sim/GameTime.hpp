#pragma once

#include <cstdint>
#include <string>

namespace mechapunk::sim {

// Simulation time in whole game minutes since Day 1, 00:00. One simulation
// step advances one minute.
using Tick = std::uint64_t;

inline constexpr Tick kMinutesPerHour = 60;
inline constexpr Tick kMinutesPerDay = 24 * kMinutesPerHour;

// Day number, starting at 1.
[[nodiscard]] constexpr std::uint64_t DayNumber(Tick tick) noexcept {
    return (tick / kMinutesPerDay) + 1;
}

[[nodiscard]] constexpr std::uint32_t MinuteOfDay(Tick tick) noexcept {
    return static_cast<std::uint32_t>(tick % kMinutesPerDay);
}

// First tick of the day containing `tick`.
[[nodiscard]] constexpr Tick StartOfDay(Tick tick) noexcept {
    return tick - (tick % kMinutesPerDay);
}

// "Day 3 07:42"
[[nodiscard]] std::string FormatTime(Tick tick);
// "07:42" for a minute of the day.
[[nodiscard]] std::string FormatClock(std::uint32_t minuteOfDay);

}  // namespace mechapunk::sim
