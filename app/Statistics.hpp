#pragma once

#include <optional>

#include "Inspector.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/Ids.hpp"

namespace mechapunk {

// What the statistics window keeps between frames.
struct StatisticsView {
    bool open{false};
    // The organization whose details are shown.
    sim::EntityId organization;
};

// Draws the city's statistics with Dear ImGui: daily charts of the economy,
// society and property; every organization and its owners; and the central
// bank's log of created and destroyed money, with a check that the money in
// all accounts adds up. Owners' names are clickable: the result selects
// that robot.
[[nodiscard]] std::optional<InspectorRequest> DrawStatistics(StatisticsView& view,
                                                             const WorldFrame& frame);

}  // namespace mechapunk
