// The gang's page in the inspector: its colour and facts, its relations, and
// as scrolling tables its members, the crimes they remember, and the rivals
// standing on its turf now.

#include "Inspector.hpp"
#include "InspectorInternal.hpp"
#include "Links.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <imgui.h>

#include "GangLists.hpp"

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Gangs.hpp"
#include "mechapunk/sim/Ids.hpp"

namespace mechapunk {

namespace {

// Long lists scroll inside a table this many rows high, and only the rows on
// screen are drawn (ImGuiListClipper), as on the organization's page.
constexpr int kVisibleRows = 12;

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

// Height of a scrolling list of `rows` rows plus its header.
ImVec2 ListSize(std::size_t rows) {
    const auto shown =
        static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

// Format an RGB colour as a hex string.
std::string RgbColor(std::uint32_t color) {
    return std::format("#{:06X}", color);
}

// Gang colour as an ImVec4.
ImVec4 GangColour(std::uint32_t colour) {
    const std::uint8_t r = (colour >> 16U) & 0xFF;
    const std::uint8_t g = (colour >> 8U) & 0xFF;
    const std::uint8_t b = colour & 0xFF;
    return ImVec4(static_cast<float>(r) / 255.0F,
                  static_cast<float>(g) / 255.0F,
                  static_cast<float>(b) / 255.0F,
                  1.0F);
}

std::string Credits(std::int64_t value) {
    return std::format("{} cr", value);
}

// What a member is doing now, in a few words.
const char* Doing(const RobotFrame& robot) {
    switch (robot.activity) {
        case sim::RobotActivity::Resting: return "at home";
        case sim::RobotActivity::Working: return "working";
        case sim::RobotActivity::Visiting: return "out";
        case sim::RobotActivity::Walking: return "walking";
    }
    return "";
}

// The other robot in a crime row, if any: who the member did it to, or who
// did it to the member. Since B15 an arrest names the officer that made it,
// so the row links that officer; a memory of a version 19 save recorded no
// officer (B15e's migration clears the field) and that row still reads
// "nobody" instead of linking the member to itself.
sim::EntityId Counterpart(const GangCrime& crime) {
    switch (crime.kind) {
        case CrimeKind::Robbery:   return crime.victim;
        case CrimeKind::WasRobbed: return crime.perpetrator;
        case CrimeKind::Arrest:    return crime.perpetrator;
        default:                   return sim::EntityId{};
    }
}

}  // namespace

// Draw the gang page in the inspector.
std::optional<InspectorRequest> DrawGangPage(
    const Selection& selection, const WorldFrame& frame, const CityView& city) {
    std::optional<InspectorRequest> result;
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            result = *link;
        }
    };
    if (frame.gangs == nullptr) {
        ImGui::TextDisabled("Gang data not loaded");
        return result;
    }

    // Find the gang.
    const sim::Gang* gang = nullptr;
    for (const auto& g : *frame.gangs) {
        if (g.id == selection.gang) {
            gang = &g;
            break;
        }
    }
    if (!gang) {
        ImGui::TextDisabled("Unknown gang");
        return result;
    }

    // Title: a swatch in the gang's colour, its hex, its name. The swatch
    // goes through GangSwatch so its id counts repeats, as every other
    // gang's colour square in the game does.
    GangSwatch(std::format("gang##{}", gang->id.value), GangColour(gang->color),
               ImVec2{24.0F, 16.0F});
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, GangColour(gang->color));
    ImGui::TextUnformatted(RgbColor(gang->color).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(gang->name.c_str());

    // Turf.
    if (gang->turf.IsValid()) {
        follow(BuildingLink(city, gang->turf, "Turf: "));
    } else {
        ImGui::TextDisabled("No turf");
    }

    // Relations, each a gang link with its stance in colour. A scrolling list
    // like the three below, whose header row stays pinned while the player
    // scrolls: a city with thirty gangs gives twenty-nine rows here.
    if (!gang->relations.empty()) {
        std::uint32_t rivals = 0;
        std::uint32_t friends = 0;
        for (const sim::GangRelation& relation : gang->relations) {
            if (relation.stance == sim::Stance::Rival) {
                ++rivals;
            } else if (relation.stance == sim::Stance::Friendly) {
                ++friends;
            }
        }
        ImGui::SeparatorText(
            std::format("Relations: {} rivals, {} friendly", rivals, friends)
                .c_str());
        if (ImGui::BeginTable("relations", 2, kListFlags,
                              ListSize(gang->relations.size()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Gang", ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("Stance", ImGuiTableColumnFlags_WidthStretch, 1.0F);
            ImGui::TableHeadersRow();
            for (const sim::GangRelation& rel : gang->relations) {
                ImGui::PushID(static_cast<int>(rel.gang.value));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                follow(GangLink(frame, rel.gang, ""));
                ImGui::TableNextColumn();
                switch (rel.stance) {
                    case sim::Stance::Friendly:
                        ImGui::TextColored(ImVec4{0.2F, 0.75F, 0.45F, 1.0F}, "friendly");
                        break;
                    case sim::Stance::Neutral:
                        ImGui::TextUnformatted("neutral");
                        break;
                    case sim::Stance::Rival:
                        ImGui::TextColored(ImVec4{0.85F, 0.3F, 0.25F, 1.0F}, "rival");
                        break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // Members: name, age, job, arrests, credits, home, what it is doing.
    const std::vector<const RobotFrame*> members = MembersOf(frame, selection.gang);
    std::uint32_t outOfWork = 0;
    std::uint32_t inDebt = 0;
    std::uint32_t arrestedBefore = 0;
    for (const RobotFrame* member : members) {
        if (!member->workspace.IsValid()) ++outOfWork;
        if (frame.economy != nullptr) {
            const sim::Loan* loan = frame.economy->LoanOf(member->id);
            if (loan != nullptr && loan->owed > 0) ++inDebt;
        }
        if (member->arrests > 0) ++arrestedBefore;
    }
    ImGui::SeparatorText(
        std::format("Members: {}, {} out of work, {} in debt, {} arrested before",
                    members.size(), outOfWork, inDebt, arrestedBefore)
            .c_str());
    if (members.empty()) {
        ImGui::TextDisabled("Nobody is in it now.");
    } else if (ImGui::BeginTable("members", 7, kListFlags, ListSize(members.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Arrests", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Credits", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(members.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RobotFrame& robot =
                    *members[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                follow(RobotLink(Name(frame, robot.id),
                                 ShowRobot(frame, city, robot.id)));
                ImGui::TableNextColumn();
                ImGui::Text("%u days", robot.age);
                ImGui::TableNextColumn();
                if (!robot.workspace.IsValid()) {
                    ImGui::TextDisabled("out of work");
                } else {
                    follow(RoomLink(city, robot.workspace, ""));
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", robot.arrests);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Credits(robot.credits).c_str());
                ImGui::TableNextColumn();
                if (!robot.apartment.IsValid()) {
                    ImGui::TextDisabled("no home");
                } else {
                    follow(RoomLink(city, robot.apartment, ""));
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Doing(robot));
            }
        }
        ImGui::EndTable();
    }

    // Recent crimes from the members' memories, newest first.
    const std::vector<GangCrime> crimes = GangCrimes(frame, selection.gang);
    ImGui::SeparatorText(
        std::format("Recent crimes: {}", crimes.size()).c_str());
    if (!crimes.empty() && ImGui::BeginTable("crimes", 4, kListFlags, ListSize(crimes.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 2.5F);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(crimes.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const GangCrime& crime = crimes[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sim::FormatTime(crime.tick).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(crime.text.c_str());
                ImGui::TableNextColumn();
                const sim::EntityId other = Counterpart(crime);
                if (!other.IsValid()) {
                    ImGui::TextDisabled("nobody");
                } else {
                    follow(RobotLink(Name(frame, other),
                                     ShowRobot(frame, city, other)));
                }
                ImGui::TableNextColumn();
                if (!crime.location.IsValid()) {
                    ImGui::TextDisabled("no room");
                } else {
                    follow(RoomLink(city, crime.location, ""));
                }
            }
        }
        ImGui::EndTable();
    }

    // Rival gangs' robots standing in the turf building now.
    const std::vector<const RobotFrame*> intruders =
        RivalsInTurf(frame, selection.gang);
    ImGui::SeparatorText(
        std::format("Rivals in its turf: {}", intruders.size()).c_str());
    if (!intruders.empty() &&
        ImGui::BeginTable("intruders", 2, kListFlags, ListSize(intruders.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Robot", ImGuiTableColumnFlags_WidthStretch, 2.5F);
        ImGui::TableSetupColumn("Gang", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(intruders.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RobotFrame& robot =
                    *intruders[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                follow(RobotLink(Name(frame, robot.id),
                                 ShowRobot(frame, city, robot.id)));
                ImGui::TableNextColumn();
                follow(GangLink(frame, robot.gang, ""));
            }
        }
        ImGui::EndTable();
    }

    return result;
}

}  // namespace mechapunk
