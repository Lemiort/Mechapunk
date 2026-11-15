#include "mechapunk/sim/Random.hpp"

#include <bit>
#include <cstdint>

namespace mechapunk::sim {

namespace {
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
constexpr unsigned kXorShift = 18U;
constexpr unsigned kOutputShift = 27U;
constexpr unsigned kRotationShift = 59U;
}  // namespace

Pcg32 Pcg32::FromSeed(std::uint64_t seed, std::uint64_t stream) noexcept {
    Pcg32 generator{.state = 0, .increment = (stream << 1U) | 1U};
    generator.NextU32();
    generator.state += seed;
    generator.NextU32();
    return generator;
}

std::uint32_t Pcg32::NextU32() noexcept {
    const std::uint64_t old = state;
    state = old * kMultiplier + increment;
    const auto xorShifted =
        static_cast<std::uint32_t>(((old >> kXorShift) ^ old) >> kOutputShift);
    const auto rotation = static_cast<int>(old >> kRotationShift);
    return std::rotr(xorShifted, rotation);
}

std::uint32_t Pcg32::UniformInt(std::uint32_t low,
                                std::uint32_t high) noexcept {
    const std::uint32_t span = high - low;
    if (span == UINT32_MAX) {
        return NextU32();
    }
    const std::uint32_t range = span + 1U;
    // Reject the few values that would bias the modulo (Lemire / PCG
    // "bounded rand" approach); the threshold is 2^32 mod range.
    const std::uint32_t threshold = (0U - range) % range;
    while (true) {
        const std::uint32_t value = NextU32();
        if (value >= threshold) {
            return low + (value % range);
        }
    }
}

}  // namespace mechapunk::sim
