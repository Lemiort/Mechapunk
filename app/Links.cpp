#include "Links.hpp"

#include <format>

#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {

// The colour of every clickable name, so the player sees what is a link.
constexpr ImVec4 kLink{0.4F, 0.7F, 1.0F, 1.0F};

std::string BuildingName(const CityView& city, sim::EntityId building) {
    if (const sim::Building* found = city.City().FindBuilding(building)) {
        return found->name;
    }
    return "somewhere";
}

// Every link: light blue text with the same tooltip. Returns where a click
// leads, or nothing when the item was not clicked.
std::optional<InspectorRequest> Link(std::string_view label) {
    ImGui::PushStyleColor(ImGuiCol_Text, kLink);
    const bool clicked = ImGui::Selectable(label.data());
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Show in the inspector");
    if (!clicked) {
        return std::nullopt;
    }
    return InspectorRequest{};
}

}  // namespace

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
std::optional<InspectorRequest> RoomLink(const CityView& city, sim::EntityId room,
                                         std::string_view prefix) {
    const std::string label = std::format("{}{}##room{}", prefix,
                                          RoomLabel(city, room), room.value);
    if (auto link = Link(label)) {
        return InspectorRequest{.selection = ShowRoom(city, room)};
    }
    return std::nullopt;
}

// A building's name: a click opens the building at its ground floor.
std::optional<InspectorRequest> BuildingLink(const CityView& city,
                                             sim::EntityId building,
                                             std::string_view prefix) {
    const std::string label =
        std::format("{}{}##building{}", prefix, BuildingName(city, building),
                    building.value);
    if (auto link = Link(label)) {
        return InspectorRequest{.selection = {.building = building, .floor = 0}};
    }
    return std::nullopt;
}

// An organization's name: a click shows it in the inspector.
std::optional<InspectorRequest> OrganizationLink(const WorldFrame& frame,
                                                 sim::EntityId organization,
                                                 std::string_view prefix) {
    const std::string label =
        std::format("{}{}##organization{}", prefix,
                    SimulationHost::AccountName(frame, organization),
                    organization.value);
    if (auto link = Link(label)) {
        return InspectorRequest{.selection = {.organization = organization}};
    }
    return std::nullopt;
}

// The name of any account: a robot or an organization is clickable, a
// remembered scrapped robot and no one at all are plain text.
std::optional<InspectorRequest> AccountLink(const WorldFrame& frame,
                                            const CityView& city,
                                            sim::EntityId account,
                                            std::string_view prefix) {
    if (account.IsValid()) {
        if (SimulationHost::FindRobot(frame, account) != nullptr) {
            return RobotLink(
                std::format("{}{}", prefix,
                            SimulationHost::AccountName(frame, account)),
                ShowRobot(frame, city, account));
        }
        if (frame.economy != nullptr &&
            frame.economy->FindOrganization(account) != nullptr) {
            return OrganizationLink(frame, account, prefix);
        }
    }
    // A robot that was scrapped since the player met it, or no one at all:
    // there is nothing to link to.
    const std::string name = account.IsValid()
                                 ? SimulationHost::AccountName(frame, account)
                                 : "nobody";
    ImGui::TextDisabled("%.*s%s", static_cast<int>(prefix.size()), prefix.data(),
                        name.c_str());
    return std::nullopt;
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
    // The suffix keeps same-named robots from sharing an id.
    const std::string label =
        std::format("{}##robot{}", name, target.robot.value);
    ImGui::PushStyleColor(ImGuiCol_Text, kLink);
    const bool clicked =
        ImGui::Selectable(label.data(), false,
                          flags | ImGuiSelectableFlags_AllowDoubleClick);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Show in the inspector");
    if (!clicked) {
        return std::nullopt;
    }
    return InspectorRequest{
        .selection = target,
        .jumpToRobot = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
}

}  // namespace mechapunk
