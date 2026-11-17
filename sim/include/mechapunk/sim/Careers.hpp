#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

#include "mechapunk/sim/GameTime.hpp"

namespace mechapunk::sim {

// Nights of rent owed that get a tenant evicted, or an owner of upkeep
// owed that make it sell its home.
inline constexpr std::int64_t kEvictionNights = 5;

// Job levels from 0 (junior) to kMaxJobLevel (lead). Each level pays a
// quarter more than the one below.
inline constexpr std::uint8_t kMaxJobLevel = 3;

// Performance below this gets a robot fired.
inline constexpr std::int32_t kFiringPerformance = -12;

// commuteAt of a robot out of work: it never leaves for a shift.
inline constexpr Tick kNoCommute = std::numeric_limits<Tick>::max();

// "junior", "regular", "senior", "lead"
[[nodiscard]] std::string_view JobTitle(std::uint8_t level) noexcept;
// The wage a workplace's base wage comes to at a job level.
[[nodiscard]] std::uint32_t WageAtLevel(std::uint32_t baseWage,
                                        std::uint8_t level) noexcept;
// Performance at which a robot at `level` is promoted.
[[nodiscard]] std::int32_t PromotionTarget(std::uint8_t level) noexcept;

}  // namespace mechapunk::sim
