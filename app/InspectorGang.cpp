#include "Inspector.hpp"
#include "InspectorInternal.hpp"
#include "Links.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "GangLists.hpp"

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Gangs.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Social.hpp"

namespace mechapunk {

namespace {

// Format an RGB colour as a hex string.
std::string RgbColor(std::uint32_t color) {
    return std::format("#{:06X}", color);
}

// Gang colour as an ImVec4.
inline ImVec4 GangColour(std::uint32_t colour) {
    std::uint8_t r = (colour >> 16U) & 0xFF;
    std::uint8_t g = (colour >> 8U) & 0xFF;
    std::uint8_t b = colour & 0xFF;
    return ImVec4(static_cast<float>(r) / 255.0F,
                  static_cast<float>(g) / 255.0F,
                  static_cast<float>(b) / 255.0F,
                  1.0F);
}

}  // namespace

// Draw the gang page in the inspector: colour swatch, turf, relations, members.
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

    // Title with colour swatch and hex.
    ImVec4 col = GangColour(gang->color);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    const std::string title = std::format("{}  {}  {}", RgbColor(gang->color),
                                          RgbColor(gang->color), gang->name);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();

    // Turf.
    if (gang->turf.value != 0) {
        BuildingLink(city, gang->turf, "Turf: ");
    } else {
        ImGui::TextDisabled("No turf");
    }

    // Relations table.
    if (!gang->relations.empty()) {
        ImGui::SeparatorText("Relations");
        if (ImGui::BeginTable("relations", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
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

    // Members list (clipped).
    std::vector<const RobotFrame*> members = MembersOf(frame, selection.gang);

    if (!members.empty()) {
        ImGui::SeparatorText("Members");
        const float memberHeight =
            std::min(static_cast<float>(members.size()), 8.0F) *
            (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
        if (ImGui::BeginChild("members", ImVec2{0.0F, memberHeight},
                              ImGuiChildFlags_Borders)) {
            for (const auto* robot : members) {
                const std::string name = Name(frame, robot->id);
                Selection next = selection;
                next.robot = robot->id;
                next.room = {};
                if (auto link = RobotLink(name, next)) {
                    result = link;
                }
            }
        }
        ImGui::EndChild();
    }

    // Recent crimes list (clipped).
    std::vector<GangCrime> crimes = GangCrimes(frame, selection.gang);

    if (!crimes.empty()) {
        ImGui::SeparatorText("Recent crimes");
        const float crimeHeight =
            std::min(static_cast<float>(crimes.size()), 8.0F) *
            (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
        if (ImGui::BeginChild("crimes", ImVec2{0.0F, crimeHeight},
                              ImGuiChildFlags_Borders)) {
            for (const auto& crime : crimes) {
                ImGui::TextUnformatted(crime.text.c_str());
                // Show perpetrator and victim as links.
                if (crime.perpetrator.value != 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(");
                    ImGui::SameLine();
                    Selection perp_sel = selection;
                    perp_sel.robot = crime.perpetrator;
                    perp_sel.room = {};
                    if (auto link = RobotLink(
                            Name(frame, crime.perpetrator), perp_sel)) {
                        result = link;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(")");
                }
            }
        }
        ImGui::EndChild();
    }

    // Rivals' members in turf now.
    if (gang->turf.value != 0 && !gang->relations.empty()) {
        // Collect rival gang ids.
        std::vector<sim::EntityId> rival_ids;
        for (const auto& rel : gang->relations) {
            if (rel.stance == sim::Stance::Rival) {
                rival_ids.push_back(rel.gang);
            }
        }
        if (!rival_ids.empty()) {
            std::vector<const RobotFrame*> intruders;
            for (const auto& robot : frame.robots) {
                // Check if this robot is a member of a rival gang.
                for (sim::EntityId rival_id : rival_ids) {
                    if (robot.gang == rival_id) {
                        intruders.push_back(&robot);
                        break;
                    }
                }
            }
            std::sort(intruders.begin(), intruders.end(),
                      [](const RobotFrame* a, const RobotFrame* b) {
                          return a->id < b->id;
                      });

            if (!intruders.empty()) {
                ImGui::SeparatorText("Rivals in turf");
                const float intruderHeight =
                    std::min(static_cast<float>(intruders.size()), 8.0F) *
                    (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
                if (ImGui::BeginChild("intruders",
                                      ImVec2{0.0F, intruderHeight},
                                      ImGuiChildFlags_Borders)) {
                    for (const auto* robot : intruders) {
                        const std::string name = Name(frame, robot->id);
                        Selection next = selection;
                        next.robot = robot->id;
                        next.room = {};
                        if (auto link = RobotLink(name, next)) {
                            result = link;
                        }
                    }
                }
                ImGui::EndChild();
            }
        }
    }

    return result;
}

}  // namespace mechapunk
