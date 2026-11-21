#pragma once

// Helpers shared by the Statistics files. The statistics window keeps its
// big tabs in separate files, like the inspector keeps its pages; never
// include this header from outside app/.

#include <optional>

#include "CityView.hpp"
#include "Inspector.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// The Chronicle tab of the statistics window, drawn in StatisticsChronicle.cpp
// while the other tabs sit in Statistics.cpp: the city's saved story
// (`WorldFrame::chronicle`), newest entry first, every robot, room, building
// and organization an entry names drawn as its clickable link. `request`
// reports what the player clicked this frame, as the Crime tab does.
void DrawChronicle(const WorldFrame& frame, const CityView& city,
                   std::optional<InspectorRequest>* request);

}  // namespace mechapunk
