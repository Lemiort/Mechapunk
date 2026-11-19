#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

#include "Inspector.hpp"

namespace mechapunk {

// A room's name with its building, so text reads as a place.
std::string RoomLabel(const CityView& city, sim::EntityId room);

// The open building and floor that show a room, with the room selected.
Selection ShowRoom(const CityView& city, sim::EntityId room);

// A clickable room name; returns where clicking it leads.
std::optional<Selection> RoomLink(
    const CityView& city, sim::EntityId room, std::string_view prefix);

// Selects a robot and shows the room it is in, if it is in one.
Selection ShowRobot(const WorldFrame& frame, const CityView& city,
                    sim::EntityId robot);

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(
    const std::string& name, const Selection& target,
    ImGuiSelectableFlags flags = 0);

}  // namespace mechapunk
