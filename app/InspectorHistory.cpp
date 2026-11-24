// The "History" section of the robot page in the inspector: the chronicle
// entries that name this robot, newest first, so the player can read what
// shaped it long after the event log has scrolled away.

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
// screen are drawn (ImGuiListClipper), as on the gang page.
constexpr int kVisibleRows = 12;

// Height of a scrolling list of `rows` rows plus its header.
ImVec2 ListSize(std::size_t rows) {
    const auto shown =
        static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

// Credits as "1 234 567 cr", the money format the Chronicle tab uses, so one
// entry reads alike wherever it is shown. A local copy, as the organization's
// and the gang's pages already have.
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

// The other robot an entry names: the one this robot acted on, or the one that
// acted on it. No second robot leaves an invalid id, which `AccountLink` draws
// as "nobody".
sim::EntityId Counterpart(const sim::ChronicleEntry& entry, sim::EntityId robot) {
    return entry.robot == robot ? entry.otherRobot : entry.robot;
}

}  // namespace

// Draws the robot page's "History" section: every chronicle entry naming this
// robot, newest first (the saved list is oldest first, so it is read backwards
// into `about`). The robot or room an entry names may be gone by now - the
// chronicle keeps a scrapped robot or a closed unit on purpose - so those cells
// fall back to plain text. Links go into `request` as on the rest of the page.
void DrawRobotHistory(const RobotFrame& robot, const WorldFrame& frame,
                      const CityView& city,
                      std::optional<InspectorRequest>* request) {
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    // Entries about this robot, newest first. The list is at most kChronicleCap
    // long, so one scan per frame costs little.
    std::vector<const sim::ChronicleEntry*> about;
    if (frame.chronicle != nullptr) {
        for (auto it = frame.chronicle->rbegin(); it != frame.chronicle->rend();
             ++it) {
            if (it->robot == robot.id || it->otherRobot == robot.id) {
                about.push_back(&*it);
            }
        }
    }
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
