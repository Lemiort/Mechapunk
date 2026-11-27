// The Construction section of the statistics window's Property tab: the by-day
// plots of the construction counters (step D5e2a's save change, version 26:
// `DayCounters::projectsStarted` and `projectsFinished` over the days kept,
// and `DailyStats::buildersEmployed`), the sites going up now, their facts -
// the last kept day's counters among them - and each one as a clickable link
// to its project page.

#include "StatisticsInternal.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "Links.hpp"
#include "ProjectLists.hpp"

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"

namespace mechapunk {

namespace {

// What a site is raising, from the building's own kind: a habitat stands for
// homes, a workplace for jobs (Construction.cpp's `startSite`), as the project's
// own page reads it (InspectorProject.cpp).
const char* KindOf(const sim::Building& building) {
    return building.kind == sim::BuildingKind::Habitat ? "habitat" : "workplace";
}

}  // namespace

// Draws the Property tab's Construction section. The by-day plots come first:
// they read the counters step D5e2a saved, so they tell the city's building
// story even in a frame where no site is going up. The frame's projects (step
// D5a) are the sites going up: `WorldState` keeps them sorted by building id,
// so a row keeps its place between frames and the player can click the same
// site twice.
void DrawConstruction(const std::vector<sim::DailyStats>& history,
                      const WorldFrame& frame, const CityView& city,
                      std::optional<InspectorRequest>* request) {
    ImGui::SeparatorText("Construction");
    // By day: the sites the review started and ended that day - the review
    // runs after the books roll over, so a site counts into the day it ran
    // into, as Construction.cpp's comment records - and the builders at the
    // site offices that night, the definition `RecordStatistics` and
    // `ProjectLists` share.
    PlotCount("Sites started", history,
              [](const auto& d) { return d.counted.projectsStarted; });
    PlotCount("Sites finished", history,
              [](const auto& d) { return d.counted.projectsFinished; });
    PlotCount("Builders employed", history,
              [](const auto& d) { return d.buildersEmployed; });
    if (frame.projects == nullptr || frame.projects->empty()) {
        ImGui::TextDisabled("No sites going up.");
        return;
    }
    // One click per frame is all a player can ask for; the last link clicked
    // wins, as in the Crime tab.
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    // The builders of each site, once per frame, as the project's page reads
    // them through `BuildersOf` (ProjectLists.hpp): robots whose job room is
    // the site's office.
    std::vector<std::size_t> buildersAt;
    buildersAt.reserve(frame.projects->size());
    std::size_t builders = 0;
    std::int64_t cost = 0;
    for (const sim::Project& project : *frame.projects) {
        const std::size_t atSite =
            BuildersOf(frame, city.Rooms(), project.building).size();
        buildersAt.push_back(atSite);
        builders += atSite;
        cost += project.costSoFar;
    }
    if (BeginFacts("construction")) {
        Row("Sites going up", std::to_string(frame.projects->size()));
        Row("Builders at their site offices", std::to_string(builders));
        Row("Supplies paid so far", Credits(cost));
        // The last kept day's counters (step D5e2a), the plots' last point:
        // the day the Property tab's own facts above read.
        if (!history.empty()) {
            const sim::DailyStats& day = history.back();
            Row("Sites started that day", std::to_string(day.counted.projectsStarted));
            Row("Sites finished that day", std::to_string(day.counted.projectsFinished));
            Row("Builders employed that day", std::to_string(day.buildersEmployed));
        }
        ImGui::EndTable();
    }
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("projects", 7, kFlags, ImVec2{0.0F, 260.0F})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthStretch, 1.6F);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.9F);
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch, 1.6F);
        for (const char* column : {"Floors done", "Nights left", "Cost so far",
                                   "Builders"}) {
            ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, 1.0F);
        }
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < frame.projects->size(); ++index) {
            const sim::Project& project = (*frame.projects)[index];
            ImGui::TableNextRow();
            // The site as a link: opens the project's page (D5c's link).
            ImGui::TableSetColumnIndex(0);
            follow(ProjectLink(city, frame, project.building, ""));
            // What it is raising, from the building's own kind; a building the
            // city no longer has reads as no kind.
            ImGui::TableSetColumnIndex(1);
            const ProjectLot lot = LotOf(frame, project);
            ImGui::TextUnformatted(lot.building == nullptr ? "-" : KindOf(*lot.building));
            // The organization paying the site's supplies.
            ImGui::TableSetColumnIndex(2);
            follow(OrganizationLink(frame, project.owner, ""));
            // A site raises one floor a night, so the floors it will have are
            // `floorsDone` plus `daysLeft`, as on the project's page.
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(
                std::format("{}/{}", project.floorsDone,
                            project.floorsDone + project.daysLeft)
                    .c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(std::to_string(project.daysLeft).c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(Credits(project.costSoFar).c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(std::to_string(buildersAt[index]).c_str());
        }
        ImGui::EndTable();
    }
}

}  // namespace mechapunk
