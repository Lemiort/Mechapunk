#include "ProjectLists.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>
#include <vector>

namespace mechapunk {

std::vector<const RobotFrame*> BuildersOf(const WorldFrame& frame,
                                          const sim::RoomIndex& rooms,
                                          sim::EntityId building) {
    std::vector<const RobotFrame*> builders;
    if (!building.IsValid()) {
        return builders;
    }
    // A robot's job is a room; the site it belongs to is that room's building,
    // and only a `SiteOffice` room says the building is going up.
    for (auto& robot : frame.robots) {
        if (!robot.workspace.IsValid()) {
            continue;
        }
        const auto ref = rooms.Find(robot.workspace);
        if (!ref || ref->room->kind != sim::RoomKind::SiteOffice ||
            ref->building->id != building) {
            continue;
        }
        builders.push_back(&robot);
    }
    // Sort by id so the order is stable across frames.
    std::sort(builders.begin(), builders.end(), [](const RobotFrame* a, const RobotFrame* b) {
        return a->id < b->id;
    });
    return builders;
}

std::vector<const sim::Project*> ProjectsOf(const WorldFrame& frame,
                                            sim::EntityId owner) {
    std::vector<const sim::Project*> projects;
    if (frame.projects == nullptr || !owner.IsValid()) {
        return projects;
    }
    for (const auto& project : *frame.projects) {
        if (project.owner == owner) {
            projects.push_back(&project);
        }
    }
    // The saved list is sorted by building id and this keeps that order, so
    // the rows are already the order the page shows them in.
    return projects;
}

const sim::Project* ProjectOn(const WorldFrame& frame, sim::EntityId building) {
    if (frame.projects == nullptr || !building.IsValid()) {
        return nullptr;
    }
    // The same binary search `IsUnderConstruction` (Construction.hpp) does:
    // `WorldState` keeps the projects sorted by building id.
    const auto found = std::ranges::lower_bound(*frame.projects, building, std::less{},
                                                &sim::Project::building);
    if (found == frame.projects->end() || found->building != building) {
        return nullptr;
    }
    return &*found;
}

ProjectLot LotOf(const WorldFrame& frame, const sim::Project& project) {
    if (frame.city == nullptr) {
        return {};
    }
    const auto* building = frame.city->FindBuilding(project.building);
    if (building == nullptr) {
        return {};
    }
    // The corner the view numbers: a node's buildings take its first slots in
    // id order (step D1a), so this building's slot is the number of the node's
    // buildings with a smaller id - the lot its project consumed.
    std::uint32_t corner{};
    for (const auto& other : frame.city->buildings) {
        if (other.entrance == building->entrance && other.id < building->id) {
            ++corner;
        }
    }
    return ProjectLot{.building = building, .node = building->entrance, .corner = corner};
}

}  // namespace mechapunk
