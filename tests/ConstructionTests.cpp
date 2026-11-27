// Tests for steps D2b and D2b2 of docs/tasks/D-construction.md: `World::ReviewConstruction`
// (sim/src/Construction.cpp) pays a site its supplies, raises one floor a night,
// ends the project when its nights are up, and starts a habitat when the
// market's homes have stood under a quarter empty for seven nights, or a
// workplace when over 5 per cent of the robots have been out of work for
// seven nights. A site started this way has its site office on the ground
// floor, owned by the project's owner - the tests that read the office itself
// live in `tests/SiteOfficeTests.cpp`, and the tests of what a site pays and
// records - its supplies, its cost so far, its reason - in
// `tests/SiteSuppliesTests.cpp`, both split by topic as D2c3 split the venue
// tests from the housing ones.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

// The shares `ReviewConstruction` measures (Construction.cpp), restated here
// so a test says which number it is about: a quarter of the market's homes
// standing empty is room enough, and 5 per cent of the robots out of work is
// the line `Units.cpp` already reopens an empty unit at.
constexpr std::int64_t kMarketVacancyPercent = 25;
constexpr std::int64_t kUnemploymentPercent = 5;

// The economy's organization with this id.
[[nodiscard]] const sim::Organization* OrganizationWithId(
    const sim::WorldState& state, sim::EntityId id) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.id == id) {
            return &organization;
        }
    }
    return nullptr;
}

// The city's building with this id.
[[nodiscard]] const sim::Building* BuildingWithId(const sim::WorldState& state,
                                                  sim::EntityId id) {
    for (const sim::Building& building : state.city.buildings) {
        if (building.id == id) {
            return &building;
        }
    }
    return nullptr;
}

// The building a project is raising.
[[nodiscard]] const sim::Building* ProjectBuilding(
    const sim::WorldState& state, const sim::Project& project) {
    return BuildingWithId(state, project.building);
}

// The market's homes of a city: the apartments of the classes `SetPrices`
// counts, `ApartmentClass::Studio` up; a government dorm bed is off the market.
[[nodiscard]] std::int64_t MarketHomes(const sim::WorldState& state) {
    std::int64_t homes = 0;
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment &&
                static_cast<std::size_t>(room.apartmentClass) >= 2) {
                ++homes;
            }
        }
    }
    return homes;
}

// The robots living in the market's homes on a saved day: `residentsByClass`
// starts at the dorm beds, so the market is the entries after it.
[[nodiscard]] std::int64_t MarketResidents(const sim::DailyStats& stats) {
    std::int64_t lived = 0;
    for (std::size_t index = 1; index != stats.residentsByClass.size(); ++index) {
        lived += stats.residentsByClass.at(index);
    }
    return lived;
}

// Per cent of the market's homes standing empty on a saved day, measured as
// `ReviewConstruction` measures it: the homes the city has now, the robots
// that day lived in them.
[[nodiscard]] std::int64_t MarketVacancyPercent(const sim::WorldState& state,
                                                const sim::DailyStats& stats) {
    const std::int64_t homes = MarketHomes(state);
    const std::int64_t lived = MarketResidents(stats);
    const std::int64_t vacant = homes > lived ? homes - lived : 0;
    return homes > 0 ? vacant * 100 / homes : 0;
}

// Per cent of the robots out of work on a saved day.
[[nodiscard]] std::int64_t UnemploymentPercent(const sim::DailyStats& stats) {
    return stats.population > 0 ? stats.unemployed * 100 / stats.population : 0;
}

// A city whose jobs are gone: every workspace nobody is in or on the way to is
// removed, and its workers are laid off as `CloseUnit` (Units.cpp) does it -
// no workplace, junior, no performance, no commute planned - so they stay out
// of work night after night instead of being rehired the same midnight.
// Validation accepts it: no robot lives in, works in or is on its way to a
// room that was removed.
[[nodiscard]] sim::WorldState JobsTakenState(std::uint64_t seed, std::uint32_t robots) {
    sim::WorldState state = sim::World::Generate({.seed = seed, .robots = robots}).State();
    std::unordered_set<std::uint64_t> busy;
    for (const sim::Robot& robot : state.robots) {
        busy.insert(robot.location.value);
        busy.insert(robot.destination.value);
    }
    std::unordered_set<std::uint64_t> gone;
    for (sim::Building& building : state.city.buildings) {
        std::erase_if(building.rooms,
                      [&busy, &gone](const sim::Room& room) {
                          if (room.kind != sim::RoomKind::Workspace ||
                              busy.contains(room.id.value)) {
                              return false;
                          }
                          gone.insert(room.id.value);
                          return true;
                      });
    }
    for (sim::Robot& robot : state.robots) {
        if (!gone.contains(robot.workspace.value)) {
            continue;
        }
        robot.workspace = sim::EntityId{};
        robot.jobLevel = 0;
        robot.performance = 0;
        robot.commuteAt = sim::kNoCommute;
        if (robot.activity == sim::RobotActivity::Working) {
            // The room it stood in is gone: it is home instead.
            robot.activity = sim::RobotActivity::Resting;
            robot.location = robot.apartment;
        }
    }
    return state;
}

// Runs the city a night at a time until a site of `kind` is going up, and says
// whether one ever started: the shortage is read off seven saved days, so a
// site starts only after the seventh night of it. Returns the project, or
// null; the pointer is only good until the city changes again.
[[nodiscard]] const sim::Project* RunUntilSiteOf(sim::World& world, sim::Tick days,
                                                 sim::BuildingKind kind) {
    for (sim::Tick day = 0; day != days; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        const sim::WorldState& state = world.State();
        for (const sim::Project& project : state.projects) {
            const sim::Building* building = ProjectBuilding(state, project);
            if (building != nullptr && building->kind == kind) {
                return &project;
            }
        }
    }
    return nullptr;
}

// The project raising a habitat, if the city has one: the workplace sites
// start on the same night, so the first project is not always a home.
[[nodiscard]] const sim::Project* HabitatSite(const sim::WorldState& state) {
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        if (building != nullptr && building->kind == sim::BuildingKind::Habitat) {
            return &project;
        }
    }
    return nullptr;
}

// The project raising a workplace.
[[nodiscard]] const sim::Project* WorkplaceSite(const sim::WorldState& state) {
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        if (building != nullptr && building->kind == sim::BuildingKind::Workplace) {
            return &project;
        }
    }
    return nullptr;
}

// The plot nearest the middle of the grid, the way Construction.cpp picks
// one: the mean of the node coordinates, and the saved order of the plots
// breaks a tie.
[[nodiscard]] sim::NodeId MiddlePlot(const sim::WorldState& state) {
    std::int64_t centreX = 0;
    std::int64_t centreZ = 0;
    for (const sim::CityNode& node : state.city.nodes) {
        centreX += node.x;
        centreZ += node.z;
    }
    const std::int64_t count = static_cast<std::int64_t>(state.city.nodes.size());
    centreX /= count;
    centreZ /= count;
    std::optional<sim::NodeId> best;
    std::int64_t bestDistance = 0;
    for (const sim::CityLot& lot : state.city.lots) {
        const sim::CityNode& node = state.city.nodes[lot.node.value];
        const std::int64_t dx = node.x - centreX;
        const std::int64_t dz = node.z - centreZ;
        const std::int64_t distance = (dx * dx) + (dz * dz);
        if (!best.has_value() || distance < bestDistance) {
            best = lot.node;
            bestDistance = distance;
        }
    }
    return best.value_or(sim::NodeId{0});
}

}  // namespace

TEST(Construction, HomesShortRaisesAHabitat) {
    // A young city fills its market: robots move up out of the dorm beds, and
    // by the 27th night fewer than a quarter of the market's homes stand empty
    // seven nights running, so the review raises a habitat.
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    const sim::WorldState& state = world.State();
    const sim::Building* building = ProjectBuilding(state, *site);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->kind, sim::BuildingKind::Habitat);
    EXPECT_EQ(site->daysLeft, 6U) << "six floors above the ground floor";
    EXPECT_EQ(site->floorsDone, 1U) << "the ground floor stands from the first night";
    EXPECT_EQ(site->reason, sim::ProjectReason::HomesShort)
        << "the market's homes ran short for seven nights";
    EXPECT_EQ(building->floors, 1U);
    // The ground floor is the site office, and nothing else (step D3b).
    ASSERT_EQ(building->rooms.size(), 1U) << "the site office is the ground floor";
    EXPECT_EQ(building->rooms.front().kind, sim::RoomKind::SiteOffice);

    // The home is owned by the housing trust, the homes nobody else owns.
    const sim::Organization* owner = OrganizationWithId(state, site->owner);
    ASSERT_TRUE(owner != nullptr);
    EXPECT_EQ(owner->sector, sim::Sector::Housing);

    // The seven nights the rule looked at are all under the share.
    ASSERT_GE(state.history.size(), 7U);
    for (std::size_t day = state.history.size() - 7; day != state.history.size(); ++day) {
        EXPECT_LT(MarketVacancyPercent(state, state.history[day]), kMarketVacancyPercent)
            << "day " << state.history[day].day;
    }
    EXPECT_TRUE(WorkplaceSite(state) == nullptr) << "jobs are not short in this city";
}

TEST(Construction, GrowsOneFloorPerNightAndEndsWhenDone) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    const sim::EntityId home = site->building;

    // One night: a floor more, one night less, and the homes of that floor.
    world.RunFor(sim::kMinutesPerDay);
    const sim::Building* building = BuildingWithId(world.State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 2U);
    EXPECT_EQ(building->rooms.size(), 7U) << "the office and six studios";
    int homes = 0;
    for (const sim::Room& room : building->rooms) {
        if (room.kind == sim::RoomKind::SiteOffice) {
            EXPECT_EQ(room.floor, 0U) << "the office is on the ground floor";
            continue;
        }
        EXPECT_EQ(room.floor, 1U) << "the new floor";
        EXPECT_TRUE(room.owner.IsValid());
        EXPECT_NE(room.apartmentClass, sim::ApartmentClass::None);
        EXPECT_GT(room.rent, 0U);
        ++homes;
    }
    EXPECT_EQ(homes, 6) << "six studios on a low floor";
    const auto going = std::ranges::find_if(
        world.State().projects, [home](const sim::Project& each) {
            return each.building == home;
        });
    ASSERT_TRUE(going != world.State().projects.end());
    EXPECT_EQ(going->daysLeft, 5U);
    EXPECT_EQ(going->floorsDone, 2U);

    // The rest of its nights: the site ends, and the building is ordinary.
    const auto stillUp = [home](const sim::WorldState& state) {
        return std::ranges::find_if(state.projects, [home](const sim::Project& each) {
                   return each.building == home;
               }) != state.projects.end();
    };
    for (int guard = 0; guard != 10; ++guard) {
        world.RunFor(sim::kMinutesPerDay);
        if (!stillUp(world.State())) {
            break;
        }
    }
    EXPECT_FALSE(stillUp(world.State()));
    building = BuildingWithId(world.State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 7U) << "the ground floor and six of homes";
    std::vector<std::uint32_t> homesPerFloor(building->floors, 0);
    for (const sim::Room& room : building->rooms) {
        ASSERT_LT(room.floor, building->floors);
        if (room.kind == sim::RoomKind::Apartment) {
            ++homesPerFloor[room.floor];
        }
    }
    EXPECT_EQ(homesPerFloor[0], 0U) << "the ground floor holds the office, not homes";
    for (std::size_t floor = 1; floor != homesPerFloor.size(); ++floor) {
        EXPECT_GT(homesPerFloor[floor], 0U) << "floor " << floor;
    }
}

TEST(Construction, HomesNotShortLeavesNoHabitat) {
    // The same city 15 nights in: 44 per cent of the market's homes still stand
    // empty, well over the quarter, so nothing is raised.
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const std::size_t buildingsAtStart = world.State().city.buildings.size();
    world.RunFor(15 * sim::kMinutesPerDay);
    const sim::WorldState& state = world.State();

    EXPECT_TRUE(state.projects.empty()) << "homes are not short yet";
    EXPECT_EQ(state.city.buildings.size(), buildingsAtStart)
        << "nothing was raised in these nights";
    ASSERT_GE(state.history.size(), 7U);
    for (std::size_t day = state.history.size() - 7; day != state.history.size(); ++day) {
        EXPECT_GE(MarketVacancyPercent(state, state.history[day]), kMarketVacancyPercent + 10)
            << "day " << state.history[day].day;
    }
}

TEST(Construction, JobsShortRaisesAWorkplace) {
    // A city whose workspaces are gone: 76 per cent of its robots are out of
    // work, far over the 5 per cent the rule asks for, so it raises a workplace
    // owned by an industry corporation - and no habitat, its homes are roomy.
    auto made = sim::World::FromState(JobsTakenState(5, 1000));
    ASSERT_TRUE(made.has_value()) << made.error();
    const sim::Project* site = RunUntilSiteOf(*made, 20, sim::BuildingKind::Workplace);
    ASSERT_TRUE(site != nullptr) << "no workplace site in 20 days";
    const sim::WorldState& state = made->State();
    const sim::EntityId workplace = site->building;
    const sim::EntityId owner = site->owner;

    const sim::Building* building = BuildingWithId(state, workplace);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->kind, sim::BuildingKind::Workplace);
    EXPECT_EQ(site->daysLeft, 4U) << "four floors above the ground floor";
    EXPECT_EQ(site->reason, sim::ProjectReason::JobsShort)
        << "jobs ran short for seven nights";
    EXPECT_EQ(building->floors, 1U);
    ASSERT_EQ(building->rooms.size(), 1U) << "the ground floor is the site office";
    EXPECT_EQ(building->rooms.front().kind, sim::RoomKind::SiteOffice);

    // The jobs belong to a corporation of industry.
    const sim::Organization* employer = OrganizationWithId(state, owner);
    ASSERT_TRUE(employer != nullptr);
    EXPECT_EQ(employer->sector, sim::Sector::Industry);
    EXPECT_EQ(employer->kind, sim::OrganizationKind::Corporation);
    EXPECT_TRUE(HabitatSite(state) == nullptr) << "homes are not short here";

    // The seven nights the rule looked at are all over the share.
    ASSERT_GE(state.history.size(), 7U);
    for (std::size_t day = state.history.size() - 7; day != state.history.size(); ++day) {
        EXPECT_GE(UnemploymentPercent(state.history[day]), kUnemploymentPercent + 10)
            << "day " << state.history[day].day;
    }

    // One night more: the floor above the ground floor is workspaces of the
    // site's owner.
    made->RunFor(sim::kMinutesPerDay);
    building = BuildingWithId(made->State(), workplace);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 2U);
    EXPECT_EQ(building->rooms.size(), 3U) << "the office and two workspaces";
    int workspaces = 0;
    for (const sim::Room& room : building->rooms) {
        EXPECT_EQ(room.owner, owner) << "the site's owner owns its rooms";
        if (room.kind == sim::RoomKind::SiteOffice) {
            EXPECT_EQ(room.floor, 0U) << "the office stays on the ground floor";
            continue;
        }
        EXPECT_EQ(room.kind, sim::RoomKind::Workspace);
        EXPECT_GT(room.capacity, 0U);
        ++workspaces;
    }
    EXPECT_EQ(workspaces, 2) << "two workspaces on the floor above";
}

TEST(Construction, JobsNotShortLeavesNoWorkplace) {
    // A young city has work for nearly all its robots: at most 3 per cent are
    // out of work, under the 5 per cent the rule asks for, so over 30 nights
    // it raises no workplace - only homes, once its market fills up.
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const std::uint64_t highestAtStart = world.State().city.buildings.back().id.value;
    world.RunFor(30 * sim::kMinutesPerDay);
    const sim::WorldState& state = world.State();

    int raisedWorkplaces = 0;
    for (const sim::Building& building : state.city.buildings) {
        if (building.id.value > highestAtStart && building.kind == sim::BuildingKind::Workplace) {
            ++raisedWorkplaces;
        }
    }
    EXPECT_EQ(raisedWorkplaces, 0) << "the jobs rule bites only when jobs go";

    ASSERT_GE(state.history.size(), 7U);
    for (std::size_t day = state.history.size() - 7; day != state.history.size(); ++day) {
        EXPECT_LE(UnemploymentPercent(state.history[day]), kUnemploymentPercent - 1)
            << "day " << state.history[day].day;
    }
}

TEST(Construction, ASiteTakesTheFreePlotNearestTheMiddle) {
    auto made = sim::World::FromState(JobsTakenState(5, 1000));
    ASSERT_TRUE(made.has_value()) << made.error();
    const sim::NodeId middle = MiddlePlot(made->State());
    const std::uint64_t highestBuilding = made->State().city.buildings.back().id.value;
    const std::size_t plots = made->State().city.lots.size();
    const std::size_t buildings = made->State().city.buildings.size();

    ASSERT_TRUE(RunUntilSiteOf(*made, 20, sim::BuildingKind::Workplace) != nullptr);
    const sim::WorldState& after = made->State();
    EXPECT_GT(after.city.buildings.size(), buildings);
    EXPECT_EQ(after.city.lots.size(), plots - (after.city.buildings.size() - buildings))
        << "every new building consumes one plot";

    int raised = 0;
    for (const sim::Building& building : after.city.buildings) {
        if (building.id.value <= highestBuilding) {
            continue;
        }
        ++raised;
        EXPECT_EQ(building.floors, 1U) << "the building stands from the first night";
        ASSERT_EQ(building.rooms.size(), 1U) << "its ground floor is the site office";
        EXPECT_EQ(building.rooms.front().kind, sim::RoomKind::SiteOffice);
    }
    EXPECT_GT(raised, 0);

    // The first new building of the middle node's lowest free corner.
    bool onMiddle = false;
    for (const sim::Building& building : after.city.buildings) {
        onMiddle = onMiddle || (building.id.value > highestBuilding && building.entrance == middle);
    }
    EXPECT_TRUE(onMiddle) << "the site nearest the middle of the grid";

    // A plot a building already stands on would be refused on the next load.
    EXPECT_TRUE(sim::ValidateWorldState(after).has_value())
        << sim::ValidateWorldState(after).error();
}

TEST(Construction, KeepsTheCityValidAndSavable) {
    // A city with sites going up: every project names a building of the city,
    // owns its rooms and has no finished floor the building does not have.
    auto made = sim::World::FromState(JobsTakenState(5, 1000));
    ASSERT_TRUE(made.has_value()) << made.error();
    made->RunFor(12 * sim::kMinutesPerDay);
    const sim::WorldState& state = made->State();
    EXPECT_FALSE(state.projects.empty()) << "the sites are going up";

    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        ASSERT_TRUE(building != nullptr) << project.building.value;
        EXPECT_GE(building->floors, project.floorsDone);
        EXPECT_GT(project.daysLeft, 0U) << "a finished site is dropped";
        int offices = 0;
        for (const sim::Room& room : building->rooms) {
            EXPECT_EQ(room.owner, project.owner) << "the site's owner owns its rooms";
            if (room.kind == sim::RoomKind::SiteOffice) {
                EXPECT_EQ(room.floor, 0U) << "the office is on the ground floor";
                ++offices;
                continue;
            }
            EXPECT_GT(room.floor, 0U) << "homes and workspaces are above it";
        }
        EXPECT_EQ(offices, 1) << "every site going up has its office";
    }
    EXPECT_TRUE(sim::ValidateWorldState(state).has_value())
        << sim::ValidateWorldState(state).error();
    EXPECT_TRUE(sim::ValidateEconomy(state).has_value())
        << sim::ValidateEconomy(state).error();

    // The new buildings and rooms survive a save.
    auto json = sim::SerializeWorld(state);
    ASSERT_TRUE(json.has_value());
    auto loaded = sim::DeserializeWorld(*json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    auto resumed = sim::World::FromState(std::move(*loaded));
    ASSERT_TRUE(resumed.has_value()) << resumed.error();
    EXPECT_EQ(resumed->State().projects.size(), state.projects.size());
    EXPECT_EQ(resumed->State().city.buildings.size(), state.city.buildings.size());
}

TEST(Construction, RunsTheSameWayTwice) {
    // Construction takes no random number and walks saved lists in order,
    // so the same city raises the same buildings on every run (rule 1).
    auto run = [](std::uint64_t seed) {
        auto made = sim::World::FromState(JobsTakenState(seed, 1000));
        EXPECT_TRUE(made.has_value());
        made->RunFor(12 * sim::kMinutesPerDay);
        auto json = sim::SerializeWorld(made->State());
        EXPECT_TRUE(json.has_value());
        return *json;
    };
    const std::string first = run(5);
    const std::string second = run(5);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, run(6)) << "a different seed still gives a different city";
}
