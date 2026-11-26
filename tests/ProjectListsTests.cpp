// Tests for step D5b of docs/tasks/D-construction.md: the plain, tested lists
// behind a construction project's pages - the builders of a site, the projects
// of an organization, the project on a building, and the lot a project stands
// on. The pattern is `tests/OrganizationListsTests.cpp`: a `WorldFrame` built
// by hand from a generated city, and no test that can pass by finding no data.

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ProjectLists.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

using mechapunk::EconomyFrame;
using mechapunk::LotOf;
using mechapunk::ProjectLot;
using mechapunk::ProjectOn;
using mechapunk::ProjectsOf;
using mechapunk::RobotDirectory;
using mechapunk::RobotFrame;
using mechapunk::RobotInfo;
using mechapunk::WorldFrame;

namespace {

// An `EconomyFrame` for a state: organizations, loans and the owner of every
// room, as `SimulationHost::Publish` fills them. A local copy of
// `OrganizationListsTests.cpp`'s helper: the module split forbids reaching
// into the other test file.
std::shared_ptr<EconomyFrame> MakeEconomy(const sim::WorldState& state) {
    auto economy = std::make_shared<EconomyFrame>();
    economy->organizations = state.economy.organizations;
    economy->loans = state.economy.loans;
    auto owners = std::make_shared<std::unordered_map<std::uint64_t, sim::EntityId>>();
    for (const auto& building : state.city.buildings) {
        for (const auto& room : building.rooms) {
            owners->emplace(room.id.value, room.owner);
        }
    }
    economy->owners = owners;
    return economy;
}

// A `WorldFrame` for a state, the projects included: the shared list step D5a
// publishes, copied here as `Publish` does at the night's first minute.
std::shared_ptr<WorldFrame> MakeWorldFrame(const sim::WorldState& state,
                                           const std::shared_ptr<EconomyFrame>& economy) {
    auto frame = std::make_shared<WorldFrame>();
    auto directory = std::make_shared<RobotDirectory>();
    for (const auto& robot : state.robots) {
        directory->emplace(robot.id.value, RobotInfo{
            .name = robot.name, .daysOff = robot.daysOff, .interests = robot.interests});
    }
    frame->directory = directory;
    frame->economy = economy;
    frame->city = std::make_shared<const sim::City>(state.city);
    frame->projects = std::make_shared<std::vector<sim::Project>>(state.projects);
    for (const auto& robot : state.robots) {
        const RobotFrame rf{
            .id = robot.id,
            .activity = robot.activity,
            .location = robot.location,
            .credits = robot.credits,
            .workspace = robot.workspace,
            .jobLevel = robot.jobLevel};
        frame->robots.push_back(rf);
    }
    return frame;
}

// A habitat building with apartments, and the housing trust that owns them:
// the ids a project page needs. A city of 300 robots has both.
std::pair<sim::EntityId, sim::EntityId> HabitatAndTrust(const sim::WorldState& state) {
    for (const sim::Building& building : state.city.buildings) {
        if (building.kind != sim::BuildingKind::Habitat) {
            continue;
        }
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                return {building.id, room.owner};
            }
        }
    }
    return {};
}

// The employer of a workplace building's rooms, and that building: a second
// owner, so the projects of an organization are told apart from the trust's.
std::pair<sim::EntityId, sim::EntityId> WorkplaceAndEmployer(const sim::WorldState& state) {
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Workspace) {
                return {building.id, room.owner};
            }
        }
    }
    return {};
}

// Puts a building on a site: a project raises it for `nights` more nights,
// owned by `owner`. The shortage that starts a project is read off seven
// saved days, so a lists test writes the site instead of waiting for one;
// the list stays sorted by building id, as validation asks. A copy of
// `SiteOfficeTests.cpp`'s helper.
void Raise(sim::WorldState& state, sim::EntityId building, sim::EntityId owner,
           std::uint32_t nights) {
    state.projects.push_back({
        .building = building, .owner = owner, .daysLeft = nights, .floorsDone = 1});
    std::ranges::sort(state.projects, std::less{}, &sim::Project::building);
}

// The site office written onto a building put on a site by hand, as `startSite`
// (Construction.cpp) makes it: floor 0, "Site Office", the project's owner,
// room for the crew of six. The new id is the highest there is, so the
// building's rooms stay sorted by id. Returns the office's id.
sim::EntityId AddOffice(sim::WorldState& state, sim::EntityId building, sim::EntityId owner) {
    for (sim::Building& each : state.city.buildings) {
        if (each.id != building) {
            continue;
        }
        each.rooms.push_back({
            .id = state.ids.Allocate(),
            .name = "Site Office",
            .kind = sim::RoomKind::SiteOffice,
            .floor = 0,
            .capacity = 6,
            .owner = owner,
        });
        return each.rooms.back().id;
    }
    return {};
}

// A building the city has that no project raises: finished land, where no
// builder works and no project is.
sim::EntityId FinishedBuilding(const sim::WorldState& state, sim::EntityId site) {
    for (const sim::Building& building : state.city.buildings) {
        if (building.id != site) {
            return building.id;
        }
    }
    return {};
}

// The corner of `building`'s entrance node the view places it on: the node's
// buildings take its first slots in id order (step D1a), so the corner is the
// number of the node's buildings with a smaller id. The test's own count, to
// check `LotOf` against.
std::uint32_t CornerOf(const sim::WorldState& state, const sim::Building& building) {
    std::uint32_t corner{};
    for (const sim::Building& other : state.city.buildings) {
        if (other.entrance == building.entrance && other.id < building.id) {
            ++corner;
        }
    }
    return corner;
}

// BuildersOf: the robots whose job is a site's office, by robot id; a finished
// building and a nonexistent one have none.
TEST(ProjectLists, BuildersAreListedUnderTheirSite) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    sim::WorldState state = world.State();
    const auto [site, trust] = HabitatAndTrust(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(trust.IsValid());
    Raise(state, site, trust, 20);
    const sim::EntityId office = AddOffice(state, site, trust);
    ASSERT_TRUE(office.IsValid());

    // Three robots whose job is the office: the builders the page lists.
    std::vector<sim::EntityId> hired;
    for (sim::Robot& robot : state.robots) {
        robot.workspace = office;
        hired.push_back(robot.id);
        if (hired.size() == 3) {
            break;
        }
    }
    ASSERT_EQ(hired.size(), 3U);

    const auto economy = MakeEconomy(state);
    const auto frame = MakeWorldFrame(state, economy);
    const sim::RoomIndex rooms(state.city);

    const auto builders = BuildersOf(*frame, rooms, site);
    ASSERT_EQ(builders.size(), 3U);
    for (std::size_t i = 0; i != hired.size(); ++i) {
        EXPECT_EQ(builders[i]->id, hired[i]);
    }
    // Robots working in the site's other rooms would not be builders, and
    // none of the city's finished buildings has any.
    const sim::EntityId finished = FinishedBuilding(state, site);
    ASSERT_TRUE(finished.IsValid());
    EXPECT_TRUE(BuildersOf(*frame, rooms, finished).empty());
    EXPECT_TRUE(BuildersOf(*frame, rooms, sim::EntityId{}).empty());
}

// ProjectsOf: only `owner`'s sites, in the saved order (by building id).
TEST(ProjectLists, ProjectsOfOwnerInSavedOrder) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    sim::WorldState state = world.State();
    const auto [site, trust] = HabitatAndTrust(state);
    const auto [work, employer] = WorkplaceAndEmployer(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(trust.IsValid());
    ASSERT_TRUE(work.IsValid());
    ASSERT_TRUE(employer.IsValid());
    ASSERT_NE(trust, employer);
    Raise(state, site, trust, 20);
    Raise(state, work, employer, 4);

    const auto frame = MakeWorldFrame(state, MakeEconomy(state));
    const auto owned = ProjectsOf(*frame, trust);
    ASSERT_EQ(owned.size(), 1U);
    EXPECT_EQ(owned[0]->building, site);
    EXPECT_EQ(owned[0]->owner, trust);
    const auto built = ProjectsOf(*frame, employer);
    ASSERT_EQ(built.size(), 1U);
    EXPECT_EQ(built[0]->building, work);
    // An id that owns no site, and none at all, have none.
    EXPECT_TRUE(ProjectsOf(*frame, sim::EntityId{std::numeric_limits<std::uint64_t>::max()})
                    .empty());
    EXPECT_TRUE(ProjectsOf(*frame, sim::EntityId{}).empty());
}

// ProjectOn: the project raising a building, or null on finished land.
TEST(ProjectLists, ProjectOnFindsTheSite) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    sim::WorldState state = world.State();
    const auto [site, trust] = HabitatAndTrust(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(trust.IsValid());
    Raise(state, site, trust, 20);
    const auto frame = MakeWorldFrame(state, MakeEconomy(state));

    const auto* project = ProjectOn(*frame, site);
    ASSERT_NE(project, nullptr);
    EXPECT_EQ(project->owner, trust);
    EXPECT_EQ(project->daysLeft, 20U);
    // A building the city has that no project raises, and no building at all.
    const sim::EntityId finished = FinishedBuilding(state, site);
    ASSERT_TRUE(finished.IsValid());
    EXPECT_EQ(ProjectOn(*frame, finished), nullptr);
    EXPECT_EQ(ProjectOn(*frame, sim::EntityId{}), nullptr);
}

// LotOf: the building a project raises and the corner of its entrance node it
// occupies - the lot its project consumed.
TEST(ProjectLists, LotOfProjectNamesTheCorner) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    sim::WorldState state = world.State();
    const auto [site, trust] = HabitatAndTrust(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(trust.IsValid());
    Raise(state, site, trust, 20);
    const auto frame = MakeWorldFrame(state, MakeEconomy(state));
    const auto* project = ProjectOn(*frame, site);
    ASSERT_NE(project, nullptr);

    const auto lot = LotOf(*frame, *project);
    ASSERT_NE(lot.building, nullptr);
    EXPECT_EQ(lot.building->id, site);
    EXPECT_EQ(lot.node, lot.building->entrance);
    EXPECT_EQ(lot.corner, CornerOf(state, *lot.building));
    // A project on a building the city does not have reads as nowhere.
    const sim::Project ghost{
        .building = sim::EntityId{std::numeric_limits<std::uint64_t>::max()},
        .owner = trust,
        .daysLeft = 1,
        .floorsDone = 1};
    EXPECT_EQ(LotOf(*frame, ghost).building, nullptr);
}

// A frame before the first project list, or a city with no sites, gives empty
// lists rather than a crash.
TEST(ProjectLists, NoProjectsYetGivesEmptyLists) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    sim::WorldState state = world.State();
    const auto [site, trust] = HabitatAndTrust(state);
    ASSERT_TRUE(site.IsValid());
    const auto frame = MakeWorldFrame(state, MakeEconomy(state));
    frame->projects = nullptr;
    const sim::RoomIndex rooms(state.city);
    EXPECT_TRUE(ProjectsOf(*frame, trust).empty());
    EXPECT_EQ(ProjectOn(*frame, site), nullptr);
    EXPECT_TRUE(BuildersOf(*frame, rooms, site).empty());
    // And no city at all: `LotOf` has nothing to name.
    frame->city = nullptr;
    const sim::Project nowhere{.building = site, .owner = trust, .daysLeft = 1, .floorsDone = 1};
    EXPECT_EQ(LotOf(*frame, nowhere).building, nullptr);
}

}  // namespace
