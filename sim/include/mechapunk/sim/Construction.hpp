#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

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

// True when a project is raising this building. Its rooms cannot be rented or
// staffed until the project is done: the floors above the ground one are still
// going up, so hiring (`OpeningsFor`, Careers.cpp) and the homes a robot may
// move into (`ReviewHomes`, Careers.cpp) skip them; a newcomer's choice of
// apartment does the same in step D2c2 (LifeCycle.cpp). `WorldState` keeps the
// projects sorted by building id, so the lookup is a binary search; callers ask
// per room and never iterate the list.
[[nodiscard]] inline bool IsUnderConstruction(const std::vector<Project>& projects,
                                              EntityId building) noexcept {
    const auto found =
        std::ranges::lower_bound(projects, building, std::less{}, &Project::building);
    return found != projects.end() && found->building == building;
}

}  // namespace mechapunk::sim
