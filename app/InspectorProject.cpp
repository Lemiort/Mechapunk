// The construction project's page in the inspector: what the site is raising
// and where, its owner, its progress as a bar, what it has cost so far and
// asks a night, the shortage that started it, and the builders working at its
// site office now.

#include "InspectorInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <imgui.h>

#include "Links.hpp"
#include "ProjectLists.hpp"

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/GameTime.hpp"

namespace mechapunk {

namespace {

// Long lists scroll inside a table this many rows high, and only the rows on
// screen are drawn (ImGuiListClipper), as on the organization's and the gang's
// pages.
constexpr int kVisibleRows = 12;

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

// Height of a scrolling list of `rows` rows plus its header.
ImVec2 ListSize(std::size_t rows) {
    const auto shown =
        static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

std::string Credits(std::int64_t value) {
    return std::format("{} cr", value);
}

// What a builder is doing now, in a few words.
const char* Doing(const RobotFrame& robot) {
    switch (robot.activity) {
        case sim::RobotActivity::Resting: return "at home";
        case sim::RobotActivity::Working: return "working";
        case sim::RobotActivity::Visiting: return "out";
        case sim::RobotActivity::Walking: return "walking";
    }
    return "";
}

// What the site is raising, from the building's own kind: a habitat stands
// for homes, a workplace for jobs (Construction.cpp's `startSite`).
const char* KindOf(const sim::Building& building) {
    return building.kind == sim::BuildingKind::Habitat ? "a habitat"
                                                       : "a workplace";
}

// The shortage that started the site, as `ProjectReason` (Construction.hpp).
// `Unknown` is what a project of a version 24 save reads as: that game chose
// a shortage without naming it, and the migration must not invent one.
// The shares and the streak it names are the world's own settings
// (`construction.*`): the number of the night that fired is not saved, so the
// page names the rule with its share.
std::string ReasonOf(const sim::Project& project, const sim::ConstructionSettings& rules) {
    switch (project.reason) {
        case sim::ProjectReason::HomesShort:
            return std::format(
                "homes short: under {}% of the market's homes stood empty "
                "for {} nights running",
                rules.marketVacancyPercent, rules.streakNights);
        case sim::ProjectReason::JobsShort:
            return std::format(
                "jobs short: over {}% of the robots out of work for {} nights "
                "running",
                rules.unemploymentPercent, rules.streakNights);
        default:
            return "not recorded: an older game started this site";
    }
}

}  // namespace

// Draw the project's page in the inspector. `selection.project` is the id of
// the building the project raises, as `WorldState` and `WorldFrame::projects`
// key the sites (see sim::Project).
std::optional<InspectorRequest> DrawProject(
    const Selection& selection, const WorldFrame& frame, const CityView& city) {
    std::optional<InspectorRequest> result;
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            result = *link;
        }
    };

    // The site: a project of the published frame on this building. A project
    // that ended between the click and this frame leaves no page, the way an
    // unknown gang leaves none.
    const sim::Project* project = ProjectOn(frame, selection.project);
    if (project == nullptr) {
        ImGui::TextDisabled("No site is going up there");
        return result;
    }
    const ProjectLot lot = LotOf(frame, *project);
    if (lot.building == nullptr) {
        ImGui::TextDisabled("Unknown building");
        return result;
    }

    // Title: the building going up and what it is being built as. The
    // "Where" line below links the building itself, so the player can open
    // the scaffold the page describes.
    ImGui::TextUnformatted(lot.building->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("building %s", KindOf(*lot.building));

    ImGui::TextUnformatted("Where:");
    ImGui::SameLine();
    follow(BuildingLink(city, project->building, ""));

    ImGui::TextUnformatted("Owner:");
    ImGui::SameLine();
    follow(OrganizationLink(frame, project->owner, ""));

    // Progress: a site raises one floor a night, so the floors still to come
    // are exactly `daysLeft` and the whole building will be `floorsDone`
    // plus `daysLeft` floors. The bar fills as the floors go up.
    ImGui::SeparatorText("Progress");
    const std::uint32_t floorsTotal = project->floorsDone + project->daysLeft;
    const std::string overlay =
        std::format("{}/{}", project->floorsDone, floorsTotal);
    ImGui::ProgressBar(static_cast<float>(project->floorsDone) /
                           static_cast<float>(floorsTotal),
                       ImVec2{-1.0F, 0.0F}, overlay.c_str());
    ImGui::Text("%u floors done of %u, %u nights left",
                project->floorsDone, floorsTotal, project->daysLeft);
    // What the scaffold will become: the style was set the night the site
    // started (task I, step I5) and the view shows it once the site ends.
    ImGui::TextDisabled("It will be a %s of %u floors",
                        std::string{sim::StyleName(lot.building->style)}.c_str(), floorsTotal);

    // Cost: what the site has paid for its supplies since the night it
    // started (step D5b2's `costSoFar`) and the ask every night pays.
    ImGui::SeparatorText("Cost");
    ImGui::Text("%s so far, %s asked a night", Credits(project->costSoFar).c_str(),
                Credits(frame.settings.construction.siteSuppliesNight).c_str());

    // The shortage that started it, with the share the review reads.
    ImGui::SeparatorText("Why it started");
    ImGui::TextUnformatted(ReasonOf(*project, frame.settings.construction).c_str());

    // The crew: every robot whose job is this site's office (ProjectLists'
    // `BuildersOf`), by id. A site office holds six builders (Construction.cpp
    // and the office's staff rules), so the table is short; it is clipped like
    // every other list all the same.
    const std::vector<const RobotFrame*> builders =
        BuildersOf(frame, city.Rooms(), project->building);
    ImGui::SeparatorText(
        std::format("Builders: {}", builders.size()).c_str());
    if (builders.empty()) {
        ImGui::TextDisabled("Nobody works at the office now.");
    } else if (ImGui::BeginTable("builders", 4, kListFlags,
                                ListSize(builders.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Shift", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(builders.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RobotFrame& robot =
                    *builders[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                follow(RobotLink(Name(frame, robot.id),
                                 ShowRobot(frame, city, robot.id)));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(
                    std::string{sim::JobTitle(robot.jobLevel)}.c_str());
                ImGui::TableNextColumn();
                // The site's shift, read through the office as the
                // organization page reads a worker's: the staff rules of a
                // site office are the site's day shift (step D3c).
                if (const auto job = city.Rooms().Find(robot.workspace);
                    job) {
                    const sim::Shift shift = sim::JobShift(*job, robot.id);
                    ImGui::Text(
                        "%s-%s",
                        sim::FormatClock(shift.startMinute).c_str(),
                        sim::FormatClock((shift.startMinute +
                                          shift.lengthMinutes) %
                                         sim::kMinutesPerDay)
                            .c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Doing(robot));
            }
        }
        ImGui::EndTable();
    }

    return result;
}

}  // namespace mechapunk
