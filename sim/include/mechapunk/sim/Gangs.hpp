#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Ids.hpp"

namespace mechapunk::sim {

// The random stream gangs are founded from, by purpose like the other
// generation streams: a world founded from a seed and a world loaded from a
// save of that seed grow the same gangs.
inline constexpr std::uint64_t kGangStream = 7;

enum class Stance : std::uint8_t { Friendly, Neutral, Rival };

struct GangRelation {
    EntityId gang;
    Stance stance{Stance::Neutral};
};

struct Gang {
    EntityId id;              // From state.ids.
    std::string name;         // "Chrome Jackals", "Rust Saints", ...
    std::uint32_t color{};    // 0xRRGGBB: the members' accent in the view.
    EntityId turf;            // A habitat building it claims, or none.
    std::vector<GangRelation> relations;  // Sorted by gang id; never itself.
};

}  // namespace mechapunk::sim
