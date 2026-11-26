#pragma once

#include <cstdint>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// The robots building a site: those whose job room is the site office of the
// building `project` raises, by robot id. A site office is the only room of a
// building going up anyone works in: its other rooms take no workers until
// the project is done (steps D2c and D3d), and its office closes into an
// empty unit when the project ends, so a robot whose job room is a
// `SiteOffice` room is a builder of the project on that building.
std::vector<const RobotFrame*> BuildersOf(const WorldFrame& frame,
                                          const sim::RoomIndex& rooms,
                                          sim::EntityId building);

// Every project `owner` is raising, in the saved order: by building id, as
// `WorldState` keeps the list (see sim::Project). The organization page's
// "Building now" list draws these.
std::vector<const sim::Project*> ProjectsOf(const WorldFrame& frame,
                                            sim::EntityId owner);

// The project raising `building`, or null when it is finished land. The
// building page's "Being built" line and a builder's project link ask this;
// the saved list is sorted by building id, so the lookup is the binary search
// `IsUnderConstruction` does.
[[nodiscard]] const sim::Project* ProjectOn(const WorldFrame& frame,
                                            sim::EntityId building);

// Where a project stands: the building it raises, its entrance node and the
// corner of that node it occupies. The lot its project consumed is gone from
// `City::lots`; the corner is derived the way the view places buildings, which
// is how the project picked that lot (step D1a: a node's buildings take its
// first slots in id order).
struct ProjectLot {
    const sim::Building* building{};
    sim::NodeId node{};
    std::uint32_t corner{};
};
[[nodiscard]] ProjectLot LotOf(const WorldFrame& frame, const sim::Project& project);

}  // namespace mechapunk
