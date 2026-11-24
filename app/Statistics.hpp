#pragma once

#include <array>
#include <optional>

#include "CityView.hpp"
#include "Inspector.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/Ids.hpp"

namespace mechapunk {

// What the statistics window keeps between frames.
struct StatisticsView {
    bool open{false};
    // The Chronicle tab's filters, held here so the player's choices outlive
    // the frame they were made in: `chronicleGroup` is 0 for every group and
    // otherwise one past the button's index in the six `ChronicleGroup`s; the
    // two day bounds are day numbers as `sim::DayNumber` gives them, 0 meaning
    // "no bound"; `chronicleName` is what the search box holds, empty meaning
    // no robot's name was typed. `app/StatisticsChronicle.cpp` reads all four
    // into a `ChronicleFilter`.
    int chronicleGroup{0};
    int chronicleFromDay{};
    int chronicleToDay{};
    std::array<char, 64> chronicleName{};
};

// Draws the city's statistics with Dear ImGui: daily charts of the economy,
// society and property; every organization and its owners; and the central
// bank's log of created and destroyed money, with a check that the money in
// all accounts adds up. Owners' names are clickable: the result selects
// that robot.
[[nodiscard]] std::optional<InspectorRequest> DrawStatistics(StatisticsView& view,
                                                              const WorldFrame& frame,
                                                              const CityView& city);

}  // namespace mechapunk
