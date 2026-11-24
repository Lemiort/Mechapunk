// The "What happened here" sections of the room page and the building page in
// the inspector: the chronicle entries that happened in this room, or in any
// room of this building, newest first, so the player can read a place's story
// long after the event log has scrolled away.

#include "InspectorInternal.hpp"
#include "Links.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

// Long lists scroll inside a table this many rows high, and only the rows on
// screen are drawn (ImGuiListClipper), as on the robot page.
constexpr int kVisibleRows = 12;

// Height of a scrolling list of `rows` rows plus its header.
ImVec2 ListSize(std::size_t rows) {
    const auto shown =
        static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

// Credits as "1 234 567 cr", the money format the Chronicle tab uses, so one
// entry reads alike wherever it is shown. A local copy, as the robot page's,
// the organization's and the gang's pages already have.
std::string Credits(std::int64_t credits) {
    // 1234567 -> "1 234 567 cr"
    std::string digits = std::to_string(credits < 0 ? -credits : credits);
    std::string grouped;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index > 0 && (digits.size() - index) % 3 == 0) {
            grouped += ' ';
        }
        grouped += digits[index];
    }
    return std::format("{}{} cr", credits < 0 ? "-" : "", grouped);
}

// What an entry records, in the few words a table column allows: the Chronicle
// tab's labels, so one story reads the same wherever it is shown. Only the
// kinds `World::Chronicle` keeps get a name of their own; a `Remembered` entry
// reads better by its memory kind.
std::string_view WhatName(const sim::ChronicleEntry& entry) {
    switch (entry.kind) {
        case sim::EventKind::Scrapped:        return "scrapped";
        case sim::EventKind::Arrived:         return "arrived in the city";
        case sim::EventKind::BoughtProperty:  return "bought a home";
        case sim::EventKind::UnitOpened:      return "a unit opened";
        case sim::EventKind::UnitClosed:      return "a unit closed";
        case sim::EventKind::Robbed:          return "robbery";
        case sim::EventKind::Arrested:        return "arrest";
        case sim::EventKind::Fought:          return "gang fight";
        case sim::EventKind::JoinedGang:      return "joined a gang";
        case sim::EventKind::LeftGang:        return "left a gang";
        case sim::EventKind::MoneySupplyChanged:
            return entry.credits > 0 ? "money created" : "money destroyed";
        case sim::EventKind::Remembered:
            switch (entry.memory) {
                case sim::MemoryKind::BecameCloseFriends: return "close friends";
                case sim::MemoryKind::BecameRivals:       return "rivals";
                case sim::MemoryKind::LostJob:            return "lost a job";
                case sim::MemoryKind::Promoted:           return "promoted";
                case sim::MemoryKind::Evicted:             return "evicted";
                default:                                   return "remembered";
            }
        default: return "story";
    }
}

// Draws one place's history as a table of `rows`, newest first, in the
// Chronicle tab's column order. Both robots an entry names are links: `AccountLink`
// draws a live robot or an organization as a clickable name and a scrapped one
// as its remembered name in plain text. On the building page the room is a link
// too, so the player can open the unit an event happened in; a unit that closed
// since reads "a room that is gone", and an event with no place ("-") has room id
// 0, which is in no room index. `showRoom` is false on the room page, where
// every row is about the room already open. Links go into `request`, as on the
// rest of the page, so a click moves the inspector.
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

}  // namespace mechapunk
