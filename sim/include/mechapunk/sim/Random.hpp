#pragma once

#include <cstdint>

namespace mechapunk::sim {

// PCG32 (XSH RR) random number generator.
//
// The simulation uses this instead of <random> because the standard
// distributions are not specified bit-for-bit and differ between standard
// libraries. Every value drawn here is the same on every platform, which keeps
// runs reproducible and saves portable. The whole state is two integers and
// is saved with the world.
struct Pcg32 {
    std::uint64_t state{};
    std::uint64_t increment{1};

    // Independent sequences for the same seed are selected with `stream`.
    [[nodiscard]] static Pcg32 FromSeed(std::uint64_t seed,
                                        std::uint64_t stream = 0) noexcept;

    std::uint32_t NextU32() noexcept;
    // Uniform integer in [low, high], inclusive; requires low <= high.
    std::uint32_t UniformInt(std::uint32_t low, std::uint32_t high) noexcept;
};

}  // namespace mechapunk::sim
