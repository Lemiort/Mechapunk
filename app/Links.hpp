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
std::optional<InspectorRequest> RoomLink(
    const CityView& city, sim::EntityId room, std::string_view prefix);

// A building's name: a click opens the building at its ground floor.
std::optional<InspectorRequest> BuildingLink(
    const CityView& city, sim::EntityId building, std::string_view prefix);

// An organization's name: a click shows it in the inspector.
std::optional<InspectorRequest> OrganizationLink(const WorldFrame& frame,
                                                 sim::EntityId organization,
                                                 std::string_view prefix);

// The name of any account: a robot or an organization is clickable, a
// remembered scrapped robot and no one at all are plain text.
std::optional<InspectorRequest> AccountLink(
    const WorldFrame& frame, const CityView& city, sim::EntityId account,
    std::string_view prefix);

// Selects a robot and shows the room it is in, if it is in one.
Selection ShowRobot(const WorldFrame& frame, const CityView& city,
                    sim::EntityId robot);

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(
    const std::string& name, const Selection& target,
    ImGuiSelectableFlags flags = 0);

}  // namespace mechapunk
