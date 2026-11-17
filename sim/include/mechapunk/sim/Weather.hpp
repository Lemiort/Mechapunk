#pragma once

#include <cstdint>
#include <string_view>

#include "mechapunk/sim/GameTime.hpp"

namespace mechapunk::sim {

// Heaviest rain: a downpour.
inline constexpr std::uint32_t kMaxRain = 100;

// Rain over the city. It comes in spells: dry spells of 8 to 48 hours, then
// rain for 1 to 8 hours, light rain more often than heavy. Each spell is
// picked from the world's seed and the spell's number, so weather never
// draws from the random stream robots use, and the same seed always brings
// the same weather.
struct Weather {
    // 0 when dry; up to kMaxRain in a downpour.
    std::uint32_t rain{};
    // Tick the current spell ends and the next one starts.
    Tick changesAt{};
    // Spells since the world began, which picks each next one.
    std::uint32_t spells{};
};

// A new world starts dry.
[[nodiscard]] Weather StartingWeather(std::uint64_t seed, Tick now);
// The spell that follows `weather`, starting at `now`: rain after a dry
// spell, dry after rain.
[[nodiscard]] Weather NextWeather(const Weather& weather, std::uint64_t seed,
                                  Tick now);
// "dry", "drizzle", "rain", "heavy rain" or "downpour".
[[nodiscard]] std::string_view RainName(std::uint32_t rain) noexcept;

}  // namespace mechapunk::sim
