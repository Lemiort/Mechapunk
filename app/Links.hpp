#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

#include "Inspector.hpp"

namespace mechapunk {

// Clickable names for the inspector and the statistics window.
//
// Every *Link function DRAWS the name itself, every frame, as a light blue
// selectable as wide as its text, and returns a request only on the frame
// it is clicked. So never draw the name again when nothing comes back:
//
//     ImGui::TextUnformatted("Owner:");
//     ImGui::SameLine();
//     if (auto link = AccountLink(frame, city, owner, "")) { result = *link; }
//
// Labels end in "##<kind><id>", so equal names never share an ImGui id, and
// a link drawn twice in one frame (the same room twice on a page) gets a
// separate id for each repeat, so it never conflicts with itself. A colour
// square in front of a name gets the same kind of repeat-safe id: see
// `GangSwatch`, which every window that draws a gang's colour should use
// instead of calling `ImGui::ColorButton` itself.

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

// The same for the statistics window, which has no city: a robot's link
// selects the robot and moves the camera to it.
std::optional<InspectorRequest> AccountLink(const WorldFrame& frame, sim::EntityId account,
                                            std::string_view prefix);

// Any text as a link to `target`; `label` must end in a unique "##id".
std::optional<InspectorRequest> LinkTo(const std::string& label,
                                       const InspectorRequest& target);

// Selects a robot and shows the room it is in, if it is in one.
Selection ShowRobot(const WorldFrame& frame, const CityView& city,
                    sim::EntityId robot);

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(
    const std::string& name, const Selection& target,
    ImGuiSelectableFlags flags = 0);

// A gang's name with its colour square in front of it: a click shows the
// gang page.
std::optional<InspectorRequest> GangLink(const WorldFrame& frame,
                                         sim::EntityId gang,
                                         std::string_view prefix);

// A gang's colour as a small square, for a page that draws the colour next
// to something other than the gang's name (the Crime tab's colour column).
// The square's id counts how many times this window drew that gang this
// frame, so a gang that appears twice (its own row, and another gang's
// relations) never gets one id twice. `what` names the gang the square
// stands for, as in a link label: "gang##<id>".
void GangSwatch(std::string_view what, const ImVec4& colour,
                ImVec2 size = ImVec2{16.0F, 16.0F});

}  // namespace mechapunk
