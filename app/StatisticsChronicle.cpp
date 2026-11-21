// The Chronicle tab of the statistics window: the city's saved story as one
// scrolling table, newest entry first, every entity an entry names a link,
// so the player can follow the story into the game.

#include "StatisticsInternal.hpp"
#include "Links.hpp"

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

// Credits as "1 234 567 cr", the money format every table of the game uses.
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

// What an entry records, in the few words a table column allows. Only the
// kinds `World::Chronicle` keeps get a name of their own; a `Remembered`
// entry reads better by its memory kind - "close friends" tells what
// happened, "remembered" does not - and anything the chronicle does not
// keep falls back to "story".
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

}  // namespace

// Draws the Chronicle tab: one table of the city's saved story, newest entry
// first (the list is saved oldest first, so row `row` reads entry `row` back
// from the end). The rooms and robots an entry names may be gone by now -
// the chronicle keeps a closed unit or a scrapped robot on purpose - so
// those cells fall back to plain text. Links are piped into `request` as
// the Crime tab does: one click a frame, the last link clicked wins.
void DrawChronicle(const WorldFrame& frame, const CityView& city,
                   std::optional<InspectorRequest>* request) {
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    ImGui::SeparatorText("Chronicle, newest first");
    if (frame.chronicle == nullptr || frame.chronicle->empty()) {
        ImGui::TextDisabled("Nothing recorded yet: notable events gather here.");
        return;
    }
    const std::vector<sim::ChronicleEntry>& entries = *frame.chronicle;
    if (ImGui::BeginTable("chronicle", 8, kListFlags, ImVec2{0.0F, 450.0F})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        // The room label holds the building's name too, so it gets the widest
        // column; time and money the narrowest.
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.3F);
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Whom", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Building", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch, 1.3F);
        ImGui::TableSetupColumn("Credits", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        // Up to kChronicleCap entries: only the ones on screen are drawn.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const sim::ChronicleEntry& entry =
                    entries[entries.size() - 1 -
                            static_cast<std::size_t>(row)];
                // The entry's room, if it names one that still exists: id 0
                // (an event with no place) and a closed unit are both not in
                // the city's room index.
                const auto found = city.Rooms().Find(entry.room);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(sim::FormatTime(entry.tick).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(WhatName(entry).data());

                // The robots: AccountLink draws a live robot as a link and
                // a scrapped one by its remembered name in plain text.
                ImGui::TableSetColumnIndex(2);
                if (entry.robot.value != 0) {
                    follow(AccountLink(frame, entry.robot, ""));
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                if (entry.otherRobot.value != 0) {
                    follow(AccountLink(frame, entry.otherRobot, ""));
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(4);
                if (found) {
                    follow(RoomLink(city, entry.room, ""));
                } else if (entry.room.value != 0) {
                    ImGui::TextDisabled("a room that is gone");
                } else {
                    ImGui::TextDisabled("-");
                }
                // The building the room stands in, and the organization that
                // owns it, each its own clickable link.
                ImGui::TableSetColumnIndex(5);
                if (found) {
                    follow(BuildingLink(city, found->building->id, ""));
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(6);
                if (found) {
                    if (const auto owner = frame.economy->OwnerOf(entry.room);
                        owner.value != 0) {
                        follow(OrganizationLink(frame, owner, ""));
                    } else {
                        ImGui::TextDisabled("-");
                    }
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(7);
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
