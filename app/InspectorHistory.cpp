// The "History" section of the robot page in the inspector: the chronicle
// entries that name this robot, newest first, so the player can read what
// shaped it long after the event log has scrolled away.

#include "ChroniclePagesInternal.hpp"
#include "InspectorInternal.hpp"
#include "Links.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <imgui.h>

#include "ChronicleLists.hpp"
#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/World.hpp"

#include "SimulationHost.hpp"

namespace mechapunk {

namespace {

// The other robot an entry names: the one this robot acted on, or the one that
// acted on it. No second robot leaves an invalid id, which `AccountLink` draws
// as "nobody".
sim::EntityId Counterpart(const sim::ChronicleEntry& entry, sim::EntityId robot) {
    return entry.robot == robot ? entry.otherRobot : entry.robot;
}

}  // namespace

// Draws the robot page's "History" section: every chronicle entry naming this
// robot, newest first, taken from `EntriesAboutRobot` - the same rows the room,
// building and organization pages read, so one robot's story reads alike
// wherever it is shown. The robot or room an entry names may be gone by now -
// the chronicle keeps a scrapped robot or a closed unit on purpose - so those
// cells fall back to plain text. Links go into `request` as on the rest of the
// page.
void DrawRobotHistory(const RobotFrame& robot, const WorldFrame& frame,
                      const CityView& city,
                      std::optional<InspectorRequest>* request) {
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    const auto about = EntriesAboutRobot(frame, robot.id);
    ImGui::SeparatorText("History");
    if (about.empty()) {
        ImGui::TextDisabled("Nothing recorded about this robot yet.");
        return;
    }
    if (ImGui::BeginTable("history", 5, kListFlags, ListSize(about.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        // The room label holds the building's name too, so it gets the widest
        // column.
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("About whom", ImGuiTableColumnFlags_WidthStretch,
                                1.2F);
        ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Credits", ImGuiTableColumnFlags_WidthStretch,
                                1.0F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(about.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const sim::ChronicleEntry& entry =
                    *about[static_cast<std::size_t>(row)];
                // The room, if it still exists: id 0 (an event with no place)
                // and a closed unit are both not in the room index.
                const auto found = city.Rooms().Find(entry.room);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sim::FormatTime(entry.tick).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(WhatName(entry).data());
                ImGui::TableNextColumn();
                // The other robot: a live one is a link, a scrapped one its
                // remembered name in plain text.
                follow(AccountLink(frame, city, Counterpart(entry, robot.id),
                                   ""));
                ImGui::TableNextColumn();
                if (found) {
                    follow(RoomLink(city, entry.room, ""));
                } else if (entry.room.value != 0) {
                    ImGui::TextDisabled("a room that is gone");
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                if (entry.credits != 0) {
                    ImGui::TextUnformatted(Credits(entry.credits).c_str());
                } else {
                    ImGui::TextDisabled("-");
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Click a name to open it in the inspector.");
}

}  // namespace mechapunk
