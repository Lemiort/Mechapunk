#include "Inspector.hpp"
#include "Links.hpp"
#include "InspectorInternal.hpp"
#include "GangLists.hpp"
#include "ProjectLists.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <ranges>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
constexpr float kPanelWidth = 430.0F;
constexpr float kMargin = 10.0F;
constexpr ImVec4 kTitle{1.0F, 0.85F, 0.3F, 1.0F};

// Robots in each room right now, in id order.
std::map<std::uint64_t, std::vector<const RobotFrame*>> Occupants(
    const WorldFrame& frame) {
    std::map<std::uint64_t, std::vector<const RobotFrame*>> occupants;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            occupants[robot.location.value].push_back(&robot);
        }
    }
    return occupants;
}

void Title(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTitle);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

std::optional<InspectorRequest> DrawRoom(const Selection& selection,
                                         const WorldFrame& frame,
                                         const CityView& city,
                                         const SimulationHost& host) {
    std::optional<InspectorRequest> result;
    const auto found = city.Rooms().Find(selection.room);
    if (!found) {
        return result;
    }
    const sim::Room& room = *found->room;
    Title(room.name);
    ImGui::Text("%s, %s, in", KindLabel(room.kind).c_str(), FloorName(room.floor).c_str());
    ImGui::SameLine();
    if (auto link = BuildingLink(city, found->building->id, "")) {
        result = *link;
    }
    if (const sim::VenueRules* rules = sim::FindVenueRules(room.kind)) {
        const auto price = static_cast<unsigned>(
            std::max<std::uint64_t>(1, std::uint64_t{rules->price} * room.pricePercent / 100));
        if (rules->opensAt == rules->closesAt) {
            ImGui::TextDisabled("Open around the clock, %u cr a visit", price);
        } else {
            ImGui::TextDisabled("Open %s-%s, %u cr a visit",
                                sim::FormatClock(rules->opensAt).c_str(),
                                sim::FormatClock(rules->closesAt).c_str(), price);
        }
        ImGui::TextDisabled("Prices at %u%% of usual, %u guests today", room.pricePercent,
                            room.visits);
    } else if (room.kind == sim::RoomKind::Apartment) {
        const std::string kind{sim::ApartmentClassName(room.apartmentClass)};
        ImGui::TextDisabled("A %s of %u m2, sleeps %u; %u cr a night, worth %lld cr",
                            kind.c_str(), sim::AreaOf(room.apartmentClass),
                            sim::Sleeps(room.apartmentClass), room.rent,
                            static_cast<long long>(room.rent * sim::kPriceNights));
    }
    if (frame.economy != nullptr) {
        // The owner runs a venue or workspace, and lets an apartment.
        ImGui::TextUnformatted(room.kind == sim::RoomKind::Apartment ? "Owner:" : "Run by:");
        ImGui::SameLine();
        if (auto link = AccountLink(frame, city, frame.economy->OwnerOf(room.id), "")) {
            result = *link;
        }
    }

    const auto occupants = Occupants(frame);
    const auto inside = occupants.find(room.id.value);
    const std::size_t count =
        inside != occupants.end() ? inside->second.size() : 0;
    const std::string occupancy = std::format("{} of {}", count, room.capacity);
    ImGui::ProgressBar(static_cast<float>(count) /
                           static_cast<float>(std::max(room.capacity, 1U)),
                       ImVec2{-1.0F, 0.0F}, occupancy.c_str());

    if (count > 0 &&
        ImGui::BeginTable("occupants", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Robot");
        ImGui::TableSetupColumn("Until", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Gang", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const RobotFrame* robot : inside->second) {
            ImGui::PushID(static_cast<int>(robot->id.value));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string name = Name(frame, robot->id);
            Selection next = selection;
            next.robot = robot->id;
            next.room = {};
            if (auto link = RobotLink(name, next,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                result = link;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Clock(robot->nextActionAt).c_str());
            // A gang member present in this room, named by its gang's link.
            ImGui::TableNextColumn();
            if (robot->gang.IsValid()) {
                if (auto link = GangLink(frame, robot->gang, "")) {
                    result = link;
                }
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // What happened in this room, newest first, drawn in InspectorChronicle.cpp.
    DrawRoomChronicle(frame, city, selection.room, &result);
    return result;
}

std::optional<InspectorRequest> DrawBuilding(const Selection& selection,
                                             const WorldFrame& frame,
                                             const CityView& city,
                                             const SimulationHost& host) {
    std::optional<InspectorRequest> result;
    const sim::Building* building =
        city.City().FindBuilding(selection.building);
    if (building == nullptr) {
        return result;
    }
    Title(building->name);
    const bool habitat = building->kind == sim::BuildingKind::Habitat;
    ImGui::Text("%s, %u floors", habitat ? "Habitat" : "Workplace",
                building->floors);
    ImGui::TextDisabled("A %s of %u floor%s",
                        std::string{sim::StyleName(building->style)}.c_str(), building->floors,
                        building->floors == 1 ? "" : "s");
    if (!habitat) {
        ImGui::TextDisabled(
            "Shift %s-%s, %u cr",
            sim::FormatClock(building->shift.startMinute).c_str(),
            sim::FormatClock(building->shift.startMinute +
                             building->shift.lengthMinutes)
                .c_str(),
            building->shift.wage);
    }

    // A building whose project is raising it: the site's own page, and the
    // crew at its site office. `ProjectOn` and `BuildersOf` are ProjectLists'
    // lookups over the published frame, the same the project's page uses.
    if (ProjectOn(frame, selection.building) != nullptr) {
        ImGui::TextUnformatted("Being built:");
        ImGui::SameLine();
        if (auto link = ProjectLink(city, frame, selection.building, "")) {
            result = link;
        }
        const std::vector<const RobotFrame*> builders =
            BuildersOf(frame, city.Rooms(), selection.building);
        ImGui::TextDisabled("%zu builders at its site office", builders.size());
        for (const RobotFrame* robot : builders) {
            ImGui::Indent();
            if (auto link = RobotLink(Name(frame, robot->id),
                                      ShowRobot(frame, city, robot->id))) {
                result = link;
            }
            ImGui::Unindent();
        }
    }

    auto floor = static_cast<int>(selection.floor);
    const std::string floorName = FloorName(selection.floor);
    if (ImGui::SliderInt("Floor", &floor, 0,
                         static_cast<int>(building->floors) - 1,
                         floorName.c_str())) {
        Selection next = selection;
        next.floor = static_cast<std::uint32_t>(floor);
        next.room = {};
        result = InspectorRequest{.selection = next};
    }
    ImGui::SetItemTooltip("Q / E change floors too");

    // Who owns its rooms: most rooms first.
    if (frame.economy != nullptr) {
        std::map<std::uint64_t, std::uint32_t> owned;
        for (const sim::Room& room : building->rooms) {
            ++owned[frame.economy->OwnerOf(room.id).value];
        }
        std::vector<std::pair<std::uint64_t, std::uint32_t>> owners(owned.begin(), owned.end());
        std::ranges::stable_sort(owners, std::greater{}, &std::pair<std::uint64_t, std::uint32_t>::second);
        ImGui::TextUnformatted("Owned by:");
        ImGui::Indent();
        for (const auto& [owner, rooms] : owners) {
            if (auto link = AccountLink(frame, city, sim::EntityId{owner}, "")) {
                result = *link;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%u room%s", rooms, rooms == 1 ? "" : "s");
        }
        ImGui::Unindent();
    }

    // A building a gang has claimed as its turf.
    if (const auto turfGang = TurfOf(frame, selection.building); turfGang != std::nullopt) {
        ImGui::TextUnformatted("Turf of");
        ImGui::SameLine();
        if (auto link = GangLink(frame, *turfGang, "")) {
            result = *link;
        }
    }

    const auto occupants = Occupants(frame);
    if (ImGui::BeginTable("rooms", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Room");
        ImGui::TableSetupColumn("Who");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const sim::Room& room : building->rooms) {
            if (room.floor != selection.floor) {
                continue;
            }
            const auto inside = occupants.find(room.id.value);
            const std::size_t count =
                inside != occupants.end() ? inside->second.size() : 0;
            ImGui::PushID(static_cast<int>(room.id.value));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (auto link = RoomLink(city, room.id, "")) {
                result = *link;
            }
            ImGui::TableNextColumn();
            if (room.kind == sim::RoomKind::Apartment) {
                if (count == 0) {
                    ImGui::TextDisabled("nobody home");
                } else {
                    const RobotFrame* robot = inside->second.front();
                    const std::string name = Name(frame, robot->id);
                    Selection next = selection;
                    next.robot = robot->id;
                    next.room = {};
                    if (auto link = RobotLink(name, next)) {
                        result = link;
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%u cr", room.rent);
            } else {
                ImGui::Text("%zu of %u", count, room.capacity);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", KindLabel(room.kind).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // What happened in this building's rooms, newest first, drawn in
    // InspectorChronicle.cpp: every room of the building, not only this floor's.
    DrawBuildingChronicle(frame, city, selection.building, &result);
    return result;
}

}  // namespace

std::optional<InspectorRequest> DrawInspector(const Selection& selection,
                                              const WorldFrame& frame,
                                              const CityView& city,
                                              const SimulationHost& host,
                                              bool& following) {
    // Nothing selected at all: no window. A gang alone is a page, even with
    // no building open, which is how a gang clicked in the Statistics window
    // opens one; a project alone is a page the same way.
    if (!selection.organization.IsValid() && !selection.building.IsValid() &&
        !selection.robot.IsValid() && !selection.room.IsValid() &&
        !selection.gang.IsValid() && !selection.project.IsValid()) {
        return std::nullopt;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2{viewport.WorkPos.x + viewport.WorkSize.x - kMargin,
               viewport.WorkPos.y + kMargin},
        ImGuiCond_FirstUseEver, ImVec2{1.0F, 0.0F});
    ImGui::SetNextWindowSize(
        ImVec2{kPanelWidth, viewport.WorkSize.y - (4.0F * kMargin)},
        ImGuiCond_FirstUseEver);

    bool open = true;
    std::optional<InspectorRequest> result;
    if (ImGui::Begin("Inspector", &open)) {
        if ((selection.robot.IsValid() || selection.room.IsValid()) &&
            selection.building.IsValid()) {
            if (ImGui::SmallButton("< Back to floor")) {
                Selection next = selection;
                next.robot = {};
                next.room = {};
                result = InspectorRequest{.selection = next};
            }
        }
        std::optional<InspectorRequest> clicked;
        if (selection.robot.IsValid()) {
            if (const RobotFrame* robot =
                    SimulationHost::FindRobot(frame, selection.robot)) {
                clicked = DrawRobot(*robot, frame, city, host, following);
            }
        } else if (selection.room.IsValid()) {
            clicked = DrawRoom(selection, frame, city, host);
        } else if (selection.organization.IsValid()) {
            clicked = DrawOrganization(selection, frame, city);
        } else if (selection.gang.IsValid()) {
            clicked = DrawGangPage(selection, frame, city);
        } else if (selection.project.IsValid()) {
            clicked = DrawProject(selection, frame, city);
        } else {
            clicked = DrawBuilding(selection, frame, city, host);
        }
        if (clicked) {
            result = clicked;
        }
    }
    ImGui::End();
    if (!open) {
        return InspectorRequest{};
    }
    return result;
}

}  // namespace mechapunk
