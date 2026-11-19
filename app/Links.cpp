#include "Links.hpp"

#include <format>

#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

std::string RoomLabel(const CityView& city, sim::EntityId room) {
    const auto found = city.Rooms().Find(room);
    if (!found) {
        return "somewhere";
    }
    if (const sim::VenueRules* rules = sim::FindVenueRules(found->room->kind)) {
        return std::format("{} ({}, {})", found->room->name, rules->label,
                           found->building->name);
    }
    return std::format("{}, {}", found->room->name, found->building->name);
}

// The open building and floor that show a room, with the room selected.
Selection ShowRoom(const CityView& city, sim::EntityId room) {
    const auto found = city.Rooms().Find(room);
    if (!found) {
        return {};
    }
    return {.building = found->building->id,
            .floor = found->room->floor,
            .room = room,
            .robot = {}};
}

// A clickable room name; returns where clicking it leads.
std::optional<Selection> RoomLink(const CityView& city, sim::EntityId room,
                                  std::string_view prefix) {
    const std::string label = std::format("{}{}##room{}", prefix,
                                          RoomLabel(city, room), room.value);
    std::optional<Selection> result;
    if (ImGui::Selectable(label.c_str())) {
        result = ShowRoom(city, room);
    }
    ImGui::SetItemTooltip("Show this room");
    return result;
}

// Selects a robot and shows the room it is in, if it is in one.
Selection ShowRobot(const WorldFrame& frame, const CityView& city,
                    sim::EntityId robot) {
    const RobotFrame* found = SimulationHost::FindRobot(frame, robot);
    if (found == nullptr || found->activity == sim::RobotActivity::Walking) {
        return {.robot = robot};
    }
    Selection next = ShowRoom(city, found->location);
    next.room = {};
    next.robot = robot;
    return next;
}

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(const std::string& name,
                                          const Selection& target,
                                          ImGuiSelectableFlags flags) {
    if (!ImGui::Selectable(name.c_str(), false,
                           flags | ImGuiSelectableFlags_AllowDoubleClick)) {
        return std::nullopt;
    }
    return InspectorRequest{
        .selection = target,
        .jumpToRobot = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
}

}  // namespace mechapunk
