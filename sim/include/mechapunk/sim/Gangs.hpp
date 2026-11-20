#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Ids.hpp"

namespace mechapunk::sim {

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
