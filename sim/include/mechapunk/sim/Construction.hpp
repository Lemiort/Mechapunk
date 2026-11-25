#pragma once

#include <cstdint>

#include "mechapunk/sim/Ids.hpp"

namespace mechapunk::sim {

// A building going up, from the night its project starts to the night it
// finishes. `building` names the building itself: it stands in the city from
// the first night, raised on a lot of `City::lots` the project consumes,
// and its floors grow as `floorsDone` counts the ones finished; its rooms
// cannot be rented or staffed until the project is done. `owner` is the
// organization that pays the supplies the site costs every night, and
// `daysLeft` counts the nights still to go. `WorldState` keeps the projects
// sorted by building id, so the same world reviews them the same way
// everywhere (rule 1).
struct Project {
    EntityId building;
    EntityId owner;
    std::uint32_t daysLeft{};
    std::uint32_t floorsDone{};
};

}  // namespace mechapunk::sim
