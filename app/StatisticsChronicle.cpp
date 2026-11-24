// The Chronicle tab of the statistics window: the city's saved story as one
// scrolling table, newest entry first, every entity an entry names a link,
// so the player can follow the story into the game.

#include "StatisticsInternal.hpp"
#include "ChronicleLists.hpp"
#include "Links.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
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

// The six group buttons the tab offers, in the order `ChronicleGroup` declares
// its values. The label is what the player reads; the group is what goes into
// `ChronicleFilter::group`, so no cast of an index into the enum is needed.
struct GroupButton {
    const char* label;
    ChronicleGroup group;
};

constexpr std::array<GroupButton, 6> kGroups{{
    {.label = "People", .group = ChronicleGroup::People},
    {.label = "Work", .group = ChronicleGroup::Work},
    {.label = "Homes", .group = ChronicleGroup::Homes},
    {.label = "Venues", .group = ChronicleGroup::Venues},
    {.label = "Crime", .group = ChronicleGroup::Crime},
    {.label = "Money", .group = ChronicleGroup::Money},
}};

}  // namespace

// Draws the Chronicle tab: one table of the city's saved story, newest entry
// first, and the controls that decide which entries the table holds. The
// rooms and robots an entry names may be gone by now - the chronicle keeps a
// closed unit or a scrapped robot on purpose - so those cells fall back to
// plain text. Links are piped into `request` as the Crime tab does: one click
// a frame, the last link clicked wins.
void DrawChronicle(StatisticsView& view, const WorldFrame& frame,
                   const CityView& city,
                   std::optional<InspectorRequest>* request) {
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    ImGui::SeparatorText("Chronicle, newest first");

    // The tab's controls, held in `view` so a filter outlives the frame it
    // was set in. A radio group picks one of the six `ChronicleGroup`s, or
    // "All" for no group at all; the day boxes give an inclusive range of
    // day numbers, 0 meaning no bound; the search box holds a robot's name,
    // matched case-insensitively against the frame's directory, so a search
    // finds the entries of a robot that has since been scrapped.
    ImGui::RadioButton("All", &view.chronicleGroup, 0);
    for (std::size_t index = 0; index < std::size(kGroups); ++index) {
        ImGui::SameLine();
        ImGui::RadioButton(kGroups[index].label, &view.chronicleGroup,
                           static_cast<int>(index) + 1);
    }
    ImGui::SetNextItemWidth(110.0F);
    ImGui::InputInt("From day", &view.chronicleFromDay);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    ImGui::InputInt("To day", &view.chronicleToDay);
    ImGui::SetNextItemWidth(220.0F);
    // The search box writes into the view's buffer: the Dear ImGui the Conan
    // recipe provides (1.92.8) has no `misc/cpp/imgui_stdlib.h`, so there is
    // no std::string overload of InputText to call.
    ImGui::InputText("Robot name", view.chronicleName.data(),
                     static_cast<int>(view.chronicleName.size()));

    // What the controls say, in the one structure the tested list code reads.
    ChronicleFilter filter;
    // The radio group only ever holds one of its own indices, but a value
    // outside the table would read past it, so the guard costs nothing.
    if (view.chronicleGroup > 0 &&
        static_cast<std::size_t>(view.chronicleGroup) <= std::size(kGroups)) {
        filter.group =
            kGroups[static_cast<std::size_t>(view.chronicleGroup) - 1].group;
    }
    // A bound typed as 0, or negative, means "no bound", as ChronicleFilter
    // documents its day numbers.
    if (view.chronicleFromDay > 0) {
        filter.fromDay = static_cast<std::uint64_t>(view.chronicleFromDay);
    }
    if (view.chronicleToDay > 0) {
        filter.toDay = static_cast<std::uint64_t>(view.chronicleToDay);
    }
    filter.text = view.chronicleName.data();

    if (frame.chronicle == nullptr || frame.chronicle->empty()) {
        ImGui::TextDisabled("Nothing recorded yet: notable events gather here.");
        return;
    }
    // The rows come from `FilterChronicle`, newest first, so the tab shows
    // what the tested list code keeps rather than a second copy of its rules.
    const std::vector<const sim::ChronicleEntry*> rows = FilterChronicle(frame, filter);
    ImGui::TextUnformatted(
        std::format("{} of {} entries match this filter.", rows.size(),
                    frame.chronicle->size())
            .c_str());
    if (rows.empty()) {
        ImGui::TextDisabled(
            "Nothing matches: widen the day range, clear the name or pick "
            "another button.");
        return;
    }
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
        // A filtered list of any length, up to kChronicleCap: only the rows
        // on screen are drawn.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const sim::ChronicleEntry& entry =
                    *rows[static_cast<std::size_t>(row)];
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
