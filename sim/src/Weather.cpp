#include "mechapunk/sim/Weather.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace mechapunk::sim {

namespace {
constexpr Tick kMinDryMinutes = 8 * kMinutesPerHour;
constexpr Tick kMaxDryMinutes = 48 * kMinutesPerHour;
constexpr Tick kMinRainMinutes = 1 * kMinutesPerHour;
constexpr Tick kMaxRainMinutes = 8 * kMinutesPerHour;
constexpr std::uint32_t kMinRain = 10;

// SplitMix64: a well-mixed 64-bit value for every input.
std::uint64_t Mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// A value in [0, 1) for one part (`salt`) of spell `spell`.
double Unit(std::uint64_t seed, std::uint32_t spell, std::uint64_t salt) noexcept {
    const std::uint64_t mixed = Mix(Mix(seed ^ 0x5745415448455221ULL) ^
                                    (static_cast<std::uint64_t>(spell) << 8U) ^ salt);
    return static_cast<double>(mixed >> 11U) * 0x1.0p-53;
}

Tick Between(Tick low, Tick high, double unit) noexcept {
    return low + static_cast<Tick>(unit * static_cast<double>(high - low + 1));
}
}  // namespace

Weather StartingWeather(std::uint64_t seed, Tick now) {
    return {.rain = 0,
            .changesAt = now + Between(kMinDryMinutes, kMaxDryMinutes,
                                       Unit(seed, 0, 1)),
            .spells = 0};
}

Weather NextWeather(const Weather& weather, std::uint64_t seed, Tick now) {
    const std::uint32_t spell = weather.spells + 1;
    if (weather.rain > 0) {
        return {.rain = 0,
                .changesAt = now + Between(kMinDryMinutes, kMaxDryMinutes,
                                           Unit(seed, spell, 1)),
                .spells = spell};
    }
    // Squaring makes light rain more common than heavy rain.
    const double heaviness = Unit(seed, spell, 2);
    const auto rain = static_cast<std::uint32_t>(std::lround(
        kMinRain + (static_cast<double>(kMaxRain - kMinRain) * heaviness * heaviness)));
    return {.rain = rain,
            .changesAt = now + Between(kMinRainMinutes, kMaxRainMinutes,
                                       Unit(seed, spell, 1)),
            .spells = spell};
}

std::string_view RainName(std::uint32_t rain) noexcept {
    if (rain == 0) {
        return "dry";
    }
    if (rain < 25) {
        return "drizzle";
    }
    if (rain < 55) {
        return "rain";
    }
    if (rain < 85) {
        return "heavy rain";
    }
    return "downpour";
}

}  // namespace mechapunk::sim
