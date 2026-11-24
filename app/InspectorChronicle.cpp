// The chronicle's history sections in the inspector: the "What happened here"
// section of the room page and of the building page, and the "History" section
// of the organization page: the chronicle entries that happened in this room,
// in any room of this building, or in a room this organization owns or to one
// of its workers, newest first, so the player can read a place's or a firm's
// story long after the event log has scrolled away.

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

// Draws a history list as a table of `rows`, newest first, in the
// Chronicle tab's column order. Both robots an entry names are links: `AccountLink`
// draws a live robot or an organization as a clickable name and a scrapped one
// as its remembered name in plain text. On the building and the organization
// page the room is a link too, so the player can open the unit an event
// happened in; a unit that closed since reads "a room that is gone", and an
// event with no place ("-") has room id 0, which is in no room index. `showRoom`
// is false on the room page, where every row is about the room already open.
// Links go into `request`, as on the rest of the page, so a click moves the
// inspector.
void DrawEntries(const char* tableId,
                 const std::vector<const sim::ChronicleEntry*>& rows, bool showRoom,
                 const WorldFrame& frame, const CityView& city,
                 std::optional<InspectorRequest>* request) {
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    const int columns = showRoom ? 6 : 5;
    if (ImGui::BeginTable(tableId, columns, kListFlags, ListSize(rows.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Whom", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        if (showRoom) {
            // The room label holds the building's name too, so it gets the
            // widest column.
            ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        }
        ImGui::TableSetupColumn("Credits", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const sim::ChronicleEntry& entry =
                    *rows[static_cast<std::size_t>(row)];
                const auto found = city.Rooms().Find(entry.room);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sim::FormatTime(entry.tick).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(WhatName(entry).data());
                ImGui::TableNextColumn();
                follow(AccountLink(frame, city, entry.robot, ""));
                ImGui::TableNextColumn();
                follow(AccountLink(frame, city, entry.otherRobot, ""));
                if (showRoom) {
                    ImGui::TableNextColumn();
                    if (found) {
                        follow(RoomLink(city, entry.room, ""));
                    } else if (entry.room.value != 0) {
                        ImGui::TextDisabled("a room that is gone");
                    } else {
                        ImGui::TextDisabled("-");
                    }
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
}

}  // namespace

// Draws the room page's "What happened here" section: the chronicle entries
// whose room is this room, from `EntriesAboutRoom`, newest first. A unit that
// closed since keeps its entries, so a room's history outlives its opening hours.
void DrawRoomChronicle(const WorldFrame& frame, const CityView& city,
                       sim::EntityId room,
                       std::optional<InspectorRequest>* request) {
    const auto rows = EntriesAboutRoom(frame, room);
    ImGui::SeparatorText("What happened here");
    if (rows.empty()) {
        ImGui::TextDisabled("Nothing recorded about this room yet.");
        return;
    }
    DrawEntries("room history", rows, false, frame, city, request);
    ImGui::TextDisabled("Click a name to open it in the inspector.");
}

// Draws the building page's "What happened here" section: the chronicle entries
// in any of this building's rooms, from `EntriesAboutBuilding`, newest first,
// each row naming the room it happened in.
void DrawBuildingChronicle(const WorldFrame& frame, const CityView& city,
                           sim::EntityId building,
                           std::optional<InspectorRequest>* request) {
    const auto rows = EntriesAboutBuilding(frame, building);
    ImGui::SeparatorText("What happened here");
    if (rows.empty()) {
        ImGui::TextDisabled("Nothing recorded about this building yet.");
        return;
    }
    DrawEntries("building history", rows, true, frame, city, request);
    ImGui::TextDisabled("Click a name to open it in the inspector.");
}

// Draws the organization page's "History" section: the chronicle entries in a
// room this organization owns or naming a robot that works for it today, from
// `EntriesAboutOrganization`, newest first, each row naming the room the event
// happened in. An entry saves no account, so money an emission gave to this
// organization itself is not a row of its own (task C, step C4e): the section
// reaches money through the organization's rooms and its workers.
void DrawOrganizationHistory(const WorldFrame& frame, const CityView& city,
                             sim::EntityId organization,
                             std::optional<InspectorRequest>* request) {
    const auto rows = EntriesAboutOrganization(frame, organization);
    ImGui::SeparatorText("History");
    ImGui::TextDisabled(
        "What happened in a room it owns, or to a robot that works for it.");
    if (rows.empty()) {
        ImGui::TextDisabled("Nothing recorded about this organization yet.");
        return;
    }
    DrawEntries("organization history", rows, true, frame, city, request);
    ImGui::TextDisabled("Click a name to open it in the inspector.");
}

}  // namespace mechapunk
