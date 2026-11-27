#pragma once

// Helpers shared by the Statistics files. The statistics window keeps its
// big tabs in separate files, like the inspector keeps its pages; never
// include this header from outside app/.

#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include <imgui.h>

#include "CityView.hpp"
#include "Inspector.hpp"
#include "Statistics.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// 1234567 -> "1 234 567 cr", the way every Statistics file writes credits.
inline std::string Credits(std::int64_t credits) {
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

// A two-column facts table; the caller ends it.
inline bool BeginFacts(const char* id) {
    return ImGui::BeginTable(id, 2,
                             ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg);
}

// Two columns of label and value.
inline void Row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

// The Chronicle tab of the statistics window, drawn in StatisticsChronicle.cpp
// while the other tabs sit in Statistics.cpp: the city's saved story
// (`WorldFrame::chronicle`), newest entry first, every robot, room, building
// and organization an entry names drawn as its clickable link. `view` holds
// the tab's filter controls (its group button, day range and search box), so
// they stay put between frames; `request` reports what the player clicked
// this frame, as the Crime tab does.
void DrawChronicle(StatisticsView& view, const WorldFrame& frame,
                   const CityView& city,
                   std::optional<InspectorRequest>* request);

// The Construction section of the Property tab, drawn in
// StatisticsConstruction.cpp while the tab's own facts and plots sit in
// Statistics.cpp: the sites going up now (`WorldFrame::projects`, step D5a),
// their facts and each one as a clickable link to its project page; `request`
// reports what the player clicked this frame, as the Crime tab does. The
// "by day" counters - projects started, finished and builders employed per
// day - come with step D5e2's save change, so until that lands the section
// draws only what the current projects allow.
void DrawConstruction(const WorldFrame& frame, const CityView& city,
                      std::optional<InspectorRequest>* request);

}  // namespace mechapunk
