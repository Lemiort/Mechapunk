#pragma once

#include <optional>

#include "CityView.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// Draws the inspector window for the selected building, room or robot with
// Dear ImGui. Names, rooms and floors in it are clickable: the result is the
// selection the player asked for, or an empty selection when the window was
// closed. Nothing when the selection should stay as it is.
[[nodiscard]] std::optional<Selection> DrawInspector(
    const Selection& selection, const WorldFrame& frame, const CityView& city,
    const SimulationHost& host);

}  // namespace mechapunk
