#include "mechapunk/sim/GameTime.hpp"

#include <cstdint>
#include <format>
#include <string>

namespace mechapunk::sim {

std::string FormatClock(std::uint32_t minuteOfDay) {
    const std::uint32_t minutes = minuteOfDay % kMinutesPerDay;
    return std::format("{:02}:{:02}", minutes / kMinutesPerHour,
                       minutes % kMinutesPerHour);
}

std::string FormatTime(Tick tick) {
    return std::format("Day {} {}", DayNumber(tick),
                       FormatClock(MinuteOfDay(tick)));
}

}  // namespace mechapunk::sim
