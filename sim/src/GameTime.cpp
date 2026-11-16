#include "mechapunk/sim/GameTime.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace mechapunk::sim {

std::string FormatClock(std::uint32_t minuteOfDay) {
    const std::uint32_t minutes = minuteOfDay % kMinutesPerDay;
    return std::format("{:02}:{:02}", minutes / kMinutesPerHour,
                       minutes % kMinutesPerHour);
}

std::string_view WeekdayName(std::uint32_t dayOfWeek) noexcept {
    constexpr std::array<std::string_view, kDaysPerWeek> kNames{
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return kNames[dayOfWeek % kDaysPerWeek];
}

std::string FormatTime(Tick tick) {
    return std::format("Day {} {} {}", DayNumber(tick),
                       WeekdayName(DayOfWeek(tick)),
                       FormatClock(MinuteOfDay(tick)));
}

}  // namespace mechapunk::sim
