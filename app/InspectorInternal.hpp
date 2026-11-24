#pragma once

// Helpers shared by Inspector*.cpp files, declared inline to keep each file
// small. Never include this header from outside the app/ directory.

#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "CityView.hpp"
#include "Inspector.hpp"
#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// Floor number as text.
inline std::string FloorName(std::uint32_t floor) {
    return floor == 0 ? "ground floor" : std::format("floor {}", floor);
}

// Room kind as display text.
inline std::string KindLabel(sim::RoomKind kind) {
    if (const sim::VenueRules* rules = sim::FindVenueRules(kind)) {
        return std::string{rules->label};
    }
    switch (kind) {
        case sim::RoomKind::Apartment:     return "apartment";
        case sim::RoomKind::VacantStore:   return "empty unit, to let";
        case sim::RoomKind::PoliceStation: return "police station";
        case sim::RoomKind::CrewRoom:      return "metro crew room";
        default:                           return "workspace";
    }
}

// Clock time from a Tick value.
inline std::string Clock(sim::Tick tick) {
    return sim::FormatClock(sim::MinuteOfDay(tick));
}

// Robot name, or "unknown robot" if not found in the frame's directory.
inline std::string Name(const WorldFrame& frame, sim::EntityId robot) {
    const RobotInfo* info = SimulationHost::FindRobotInfo(frame, robot);
    return info != nullptr ? info->name : "unknown robot";
}

// Robots in each room right now, sorted by id.
inline std::map<std::uint64_t, std::vector<const RobotFrame*>> Occupy(
    const WorldFrame& frame) {
    std::map<std::uint64_t, std::vector<const RobotFrame*>> occupants;
    for (const auto& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            occupants[robot.location.value].push_back(&robot);
        }
    }
    return occupants;
}

// Section title styled.
inline void Title(const std::string& text) {
    constexpr ImVec4 kTitle{1.0F, 0.85F, 0.3F, 1.0F};
    ImGui::PushStyleColor(ImGuiCol_Text, kTitle);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

// Two-column table row helper for ImGui tables with 2 columns.
inline void Row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

// Format a sector name as plain text (wraps string_view into std::string).
inline std::string SectorLabel(sim::Sector sector) {
    return std::string{sim::SectorName(sector)};
}

// The organization's page in the inspector, in InspectorOrganization.cpp.
[[nodiscard]] std::optional<InspectorRequest> DrawOrganization(
    const Selection& selection, const WorldFrame& frame, const CityView& city);

// The gang's page in the inspector, in InspectorGang.cpp.
[[nodiscard]] std::optional<InspectorRequest> DrawGangPage(
    const Selection& selection, const WorldFrame& frame, const CityView& city);

// The "History" section of the robot page, in InspectorHistory.cpp: the
// chronicle entries naming this robot, newest first, every entity in one a
// link. Takes the page's request by pointer, as the Chronicle tab does, so a
// click in the section can move the inspector.
void DrawRobotHistory(const RobotFrame& robot, const WorldFrame& frame,
                      const CityView& city,
                      std::optional<InspectorRequest>* request);

}  // namespace mechapunk
