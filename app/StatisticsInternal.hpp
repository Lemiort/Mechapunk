#pragma once

// Helpers shared by the Statistics files. The statistics window keeps its
// big tabs in separate files, like the inspector keeps its pages; never
// include this header from outside app/.

#include <cfloat>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "CityView.hpp"
#include "Inspector.hpp"
#include "Statistics.hpp"
#include "SimulationHost.hpp"

#include "mechapunk/sim/Economy.hpp"

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

// A labelled line chart of one statistic over the days kept, with the latest
// value written over it. Step D5e2b moved these plot helpers out of
// `Statistics.cpp`'s anonymous namespace, the way step D5e moved Credits,
// BeginFacts and Row: the Construction section's by-day plots read them too.
using Stat = std::function<double(const sim::DailyStats&)>;

inline constexpr float kPlotHeight = 46.0F;

inline void Plot(const char* label, const std::vector<sim::DailyStats>& history,
                 const Stat& stat, const std::function<std::string(double)>& format) {
    std::vector<float> values;
    values.reserve(history.size());
    for (const sim::DailyStats& day : history) {
        values.push_back(static_cast<float>(stat(day)));
    }
    const std::string overlay =
        values.empty() ? std::string{"no days yet"}
                       : std::format("{}: {}", label, format(stat(history.back())));
    ImGui::PlotLines(std::format("##{}", label).c_str(), values.data(),
                     static_cast<int>(values.size()), 0, overlay.c_str(), FLT_MAX, FLT_MAX,
                     ImVec2{-1.0F, kPlotHeight});
}

inline void PlotCount(const char* label, const std::vector<sim::DailyStats>& history,
                      const Stat& stat) {
    Plot(label, history, stat, [](double value) { return std::format("{:.0f}", value); });
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
// Statistics.cpp: the by-day plots of the construction counters (step D5e2a's
// save change, version 26 - the sites the review started and ended that day
// and the builders at the site offices that night), the sites going up now
// (`WorldFrame::projects`, step D5a), their facts - the last kept day's
// counters among them - and each one as a clickable link to its project page;
// `request` reports what the player clicked this frame, as the Crime tab does.
void DrawConstruction(const std::vector<sim::DailyStats>& history,
                      const WorldFrame& frame, const CityView& city,
                      std::optional<InspectorRequest>* request);

}  // namespace mechapunk
