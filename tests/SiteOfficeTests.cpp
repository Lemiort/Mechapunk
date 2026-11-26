// Tests for steps D3b and D3d of docs/tasks/D-construction.md: the site office
// of a building going up (`startSite` in sim/src/Construction.cpp puts it on
// the ground floor, owned by the project's owner), and the finished site whose
// office `ReviewConstruction` closes into an ordinary empty unit, laying off
// its builders as `CloseUnit` (Units.cpp) does. Split from
// `tests/ConstructionTests.cpp` by topic, as D2c3 split the venue tests from
// the housing ones: the file with the office tests of both steps fits the
// budget, the file with the rest does too, and the `construction` module holds
// both.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <unordered_set>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

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
[[nodiscard]] const sim::Building* ProjectBuilding(const sim::WorldState& state,
                                                   const sim::Project& project) {
    return BuildingWithId(state, project.building);
}

// Runs the city a night at a time until a site of `kind` is going up, and says
// whether one ever started: the shortage is read off seven saved days, so a
// site starts only after the seventh night of it. Returns the project, or
// null; the pointer is only good until the city changes again. A copy of
// `ConstructionTests.cpp`'s helper: the module split forbids reaching into
// the other test file.
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

// The site office of a building going up: the one room of kind `SiteOffice`
// `startSite` (Construction.cpp) puts on its ground floor.
[[nodiscard]] const sim::Room* SiteOffice(const sim::Building& building) {
    for (const sim::Room& room : building.rooms) {
        if (room.kind == sim::RoomKind::SiteOffice) {
            return &room;
        }
    }
    return nullptr;
}

// A workplace building that has workspaces, and the employer that owns them:
// the ids a test needs, since a state moved into `FromState` invalidates
// pointers into it. A copy of `CareersTests.cpp`'s helper.
[[nodiscard]] std::pair<sim::EntityId, sim::EntityId> WorkplaceAndOwner(
    const sim::WorldState& state) {
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
// saved days, so a test that wants builders at an office writes the site
// instead of waiting for one; the list stays sorted by building id, as
// validation asks. A copy of `CareersTests.cpp`'s helper.
void Raise(sim::WorldState& state, sim::EntityId building, sim::EntityId owner,
           std::uint32_t nights) {
    state.projects.push_back({
        .building = building,
        .owner = owner,
        .daysLeft = nights,
        .floorsDone = 1,
    });
}

// The site office of a building put on a site by hand, as `startSite`
// (Construction.cpp) makes it: the builders' room on the ground floor, owned
// by the site's owner, with room for the crew of six a site asks for. A
// copy of `CareersTests.cpp`'s helper.
[[nodiscard]] sim::EntityId AddOffice(sim::WorldState& state, sim::EntityId building,
                                       sim::EntityId owner) {
    for (sim::Building& each : state.city.buildings) {
        if (each.id != building) {
            continue;
        }
        // The new id is the highest there is, so the building's rooms stay
        // sorted by id, as validation asks.
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

// Lays a robot off as `CloseUnit` (Units.cpp) does it: no workplace, junior,
// no performance, no commute planned, and at home when it stood at work. A
// copy of `CareersTests.cpp`'s helper.
void LayOff(sim::Robot& robot) {
    robot.workspace = sim::EntityId{};
    robot.jobLevel = 0;
    robot.performance = 0;
    robot.commuteAt = sim::kNoCommute;
    if (robot.activity == sim::RobotActivity::Working) {
        robot.activity = sim::RobotActivity::Resting;
        robot.location = robot.apartment;
    }
}

}  // namespace

TEST(SiteOffice, AStartedSiteHasASiteOffice) {
    // The ground floor of a building going up is its site office: the builders'
    // room, on floor 0, owned by the site's owner, asking no rent (step D3b).
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    const sim::EntityId home = site->building;
    const sim::EntityId owner = site->owner;
    const sim::WorldState& state = world.State();
    const sim::Building* building = BuildingWithId(state, home);
    ASSERT_TRUE(building != nullptr);
    const sim::Room* office = SiteOffice(*building);
    ASSERT_TRUE(office != nullptr) << "the site has no office";
    EXPECT_EQ(office->floor, 0U) << "the office is on the ground floor";
    EXPECT_EQ(office->name, "Site Office");
    EXPECT_EQ(office->owner, owner) << "the site's owner owns its office";
    EXPECT_GT(office->capacity, 0U) << "a room of builders has places";
    EXPECT_EQ(office->rent, 0U) << "nobody is charged for an office";
    EXPECT_EQ(std::ranges::count_if(building->rooms,
                                    [](const sim::Room& room) {
                                        return room.kind == sim::RoomKind::SiteOffice;
                                    }),
              1)
        << "one office per site";
    EXPECT_TRUE(sim::ValidateWorldState(state).has_value())
        << sim::ValidateWorldState(state).error();

    // One night more: the floors above go up, and the office stays an office.
    // `ReviewUnits` (Units.cpp) branches its ground-floor loop by kind, so it
    // neither counts the site's quiet nights nor reopens its rooms.
    world.RunFor(sim::kMinutesPerDay);
    building = BuildingWithId(world.State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 2U);
    office = SiteOffice(*building);
    ASSERT_TRUE(office != nullptr) << "the office stands while the site goes up";
    EXPECT_EQ(office->kind, sim::RoomKind::SiteOffice);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()).has_value())
        << sim::ValidateWorldState(world.State()).error();
}

TEST(SiteOffice, FinishedSiteOfficeBecomesAVacantUnit) {
    // While its project is up, a site's office holds builders (step D3c);
    // when the project's nights are used up, the builders are laid off as
    // `CloseUnit` (Units.cpp) does it and the office becomes an ordinary
    // empty unit (step D3d), which `ReviewUnits` may then reopen.
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    sim::WorldState state = start.State();
    const auto [site, employer] = WorkplaceAndOwner(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(employer.IsValid());
    Raise(state, site, employer, 20);  // Going up for twenty nights.
    const sim::EntityId office = AddOffice(state, site, employer);
    ASSERT_TRUE(office.IsValid());
    // Everyone whose job is a workspace is laid off, so the city looks hard
    // for work: many robots ask for a job each night, and the office is one
    // of the few openings they choose among (as in D3c's builders test).
    std::unordered_set<std::uint64_t> workspaces;
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Workspace) {
                workspaces.insert(room.id.value);
            }
        }
    }
    std::uint32_t jobless = 0;
    for (sim::Robot& robot : state.robots) {
        if (!workspaces.contains(robot.workspace.value)) {
            continue;
        }
        LayOff(robot);
        ++jobless;
    }
    ASSERT_GT(jobless, 0U);

    auto made = sim::World::FromState(std::move(state));
    ASSERT_TRUE(made.has_value()) << made.error();
    sim::World& world = *made;

    // A fortnight: the office fills its crew (D3c measured six there, and
    // the bound leaves room for the random stream to move in a later step).
    for (int day = 0; day != 14; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()).has_value())
            << sim::ValidateWorldState(world.State()).error();
    }
    std::uint32_t builders = 0;
    for (const sim::Robot& robot : world.State().robots) {
        builders += robot.workspace == office ? 1U : 0U;
    }
    EXPECT_GE(builders, 3U) << "a site's office takes builders";

    // The site's last nights: the project ends at the twentieth midnight.
    const auto going = [site](const sim::WorldState& each) {
        return std::ranges::find_if(each.projects, [site](const sim::Project& project) {
                   return project.building == site;
               }) != each.projects.end();
    };
    for (int guard = 0; guard != 10 && going(world.State()); ++guard) {
        world.RunFor(sim::kMinutesPerDay);
    }
    EXPECT_FALSE(going(world.State())) << "the project ended";

    // Where the office stood there is now an ordinary empty unit, and no
    // robot works there: the builders were laid off with the project.
    const sim::WorldState& after = world.State();
    const sim::Building* building = BuildingWithId(after, site);
    ASSERT_TRUE(building != nullptr);
    const sim::Room* converted = nullptr;
    for (const sim::Room& room : building->rooms) {
        if (room.id == office) {
            converted = &room;
        }
    }
    ASSERT_TRUE(converted != nullptr) << "the building still has its ground-floor room";
    EXPECT_EQ(converted->kind, sim::RoomKind::VacantStore) << "the office became a vacant unit";
    EXPECT_EQ(converted->name, "Empty Unit") << "named as `CloseUnit` renames it";
    EXPECT_TRUE(SiteOffice(*building) == nullptr) << "no office is left on a finished site";
    for (const sim::Robot& robot : after.robots) {
        EXPECT_NE(robot.workspace, office) << "a builder still works at the closed office";
    }
    EXPECT_TRUE(sim::ValidateWorldState(after).has_value())
        << sim::ValidateWorldState(after).error();
    EXPECT_TRUE(sim::ValidateEconomy(after).has_value())
        << sim::ValidateEconomy(after).error();
}
