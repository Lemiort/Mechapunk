#pragma once

#include <optional>

#include "CityView.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// What the player asked for in the inspector.
struct InspectorRequest {
    Selection selection;
    // A robot's name was double-clicked, or Look at pressed: move the camera
    // to the selected robot.
    bool jumpToRobot{false};
};

// Draws the inspector window for the selected building, room or robot with
// Dear ImGui. Names, rooms and floors in it are clickable: the result is the
// selection the player asked for, or an empty selection when the window was
// closed. Nothing when the selection should stay as it is. `following` is the
// Follow toggle of the selected robot.
[[nodiscard]] std::optional<InspectorRequest> DrawInspector(
    const Selection& selection, const WorldFrame& frame, const CityView& city,
    const SimulationHost& host, bool& following);

}  // namespace mechapunk
