// Tests for what the nightly review counts: `World::ReviewConstruction`
// (sim/src/Construction.cpp) counts the sites it started and ended into the
// day it ran into - the review runs after the books roll over, so the counts
// wait in `state_.today` until that day's `RecordStatistics` (EconomyStatistics
// .cpp) snapshots the books into its `DailyStats` - and `RecordStatistics`
// counts the robots whose job is a site office, the builders (task D, step
// D5e2). What a site pays stays in `tests/SiteSuppliesTests.cpp` and the
// office room itself in `tests/SiteOfficeTests.cpp`, split by topic as D5b2
// and D3d did.

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

// Reloads a state edited by hand: `World::FromState` rebuilds the caches and
// validates, so a test that writes a robot's job writes a whole city.
[[nodiscard]] sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
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

// Runs the city a night at a time until a site of `kind` is going up, and
// says whether one ever started: the shortage is read off seven saved days,
// so a site starts only after the seventh night of it. Returns the project,
// or null; the pointer is only good until the city changes again.
[[nodiscard]] const sim::Project* RunUntilSiteOf(sim::World& world, sim::Tick days,
                                                 sim::BuildingKind kind) {
    for (sim::Tick day = 0; day != days; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        const sim::WorldState& state = world.State();
        for (const sim::Project& project : state.projects) {
            const sim::Building* building = BuildingWithId(state, project.building);
            if (building != nullptr && building->kind == kind) {
                return &project;
            }
        }
    }
    return nullptr;
}

// Whether a project is still raising this building: the review ends a site
// the midnight its last floor is up, and drops it from the saved list.
[[nodiscard]] bool SiteStillUp(const sim::WorldState& state, sim::EntityId building) {
    for (const sim::Project& project : state.projects) {
        if (project.building == building) {
            return true;
        }
    }
    return false;
}

// The id of the site office of the building with this id, as `startSite`
// (Construction.cpp) writes it onto the ground floor.
[[nodiscard]] std::uint64_t SiteOfficeOf(const sim::WorldState& state,
                                         sim::EntityId building) {
    const sim::Building* found = BuildingWithId(state, building);
    if (found == nullptr) {
        return 0;
    }
    for (const sim::Room& room : found->rooms) {
        if (room.kind == sim::RoomKind::SiteOffice) {
            return room.id.value;
        }
    }
    return 0;
}

}  // namespace

// A site the review starts counts into the day it ran into, not the day it
// ran out of: the review runs after the books roll over, beside the supplies
// that night's sites pay into the same books. The day's statistics carry the
// count once that day is over, and the same for the night the site ends.
TEST(ConstructionCounters, SitesCountIntoTheDayTheReviewRanInto) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    const sim::EntityId siteBuilding = site->building;

    // The midnight that raised the site has just run: its counts sit in the
    // new day's books, and no saved day has them yet.
    const std::int64_t started = world.State().today.projectsStarted;
    EXPECT_GE(started, 1);
    EXPECT_EQ(world.State().history.back().counted.projectsStarted, 0)
        << "the day the review ran into is not recorded yet";

    // One night more: that day is over, and its statistics carry the start.
    world.RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(world.State().history.back().counted.projectsStarted, started);

    // Run to the midnight the site ends: six floors, one a night, and the
    // review drops it from the list the midnight its last floor is up.
    for (sim::Tick day = 0; day != 20 && SiteStillUp(world.State(), siteBuilding); ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    ASSERT_FALSE(SiteStillUp(world.State(), siteBuilding)) << "the site never ended";
    // The night it ended counts it finished into the day that night ran into.
    const std::int64_t finished = world.State().today.projectsFinished;
    EXPECT_GE(finished, 1);

    // One night more, and that day's statistics carry the finish.
    world.RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(world.State().history.back().counted.projectsFinished, finished);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// The robots whose job is a site office are the builders, as `ProjectLists`
// (app) defines them: `RecordStatistics` counts them into the night's
// `DailyStats`. The city is made to have builders by hand - a busy city
// leaves the office one opening among thousands and hires nobody there
// (step D3c's note) - by giving resting robots the office as their job,
// which is what a builder off its shift looks like.
TEST(ConstructionCounters, BuildersAtTheOfficeAreEmployedThatNight) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    const sim::EntityId siteBuilding = site->building;

    sim::WorldState state = world.State();
    const std::uint64_t officeId = SiteOfficeOf(state, siteBuilding);
    ASSERT_NE(officeId, 0U) << "a going-up site has its office";
    // Four robots at home, three the bound asks: the office holds a crew of
    // six (Construction.cpp), so four fit, and a worn robot among them may
    // be scrapped before the night is recorded (the guide's "leave margin").
    // A robot at home simply takes the site's job as its own - what a
    // builder off its shift looks like; the office's staff rules (step D3c)
    // give it the six places `ValidateWorldState` asks the job room to hold.
    std::int64_t setBuilders = 0;
    for (sim::Robot& robot : state.robots) {
        if (setBuilders == 4) {
            break;
        }
        if (!robot.patrol.IsValid() && robot.activity == sim::RobotActivity::Resting) {
            robot.workspace = sim::EntityId{officeId};
            ++setBuilders;
        }
    }
    ASSERT_EQ(setBuilders, 4) << "the city has four robots at home";
    sim::World withBuilders = Load(std::move(state));

    // One night: the builders keep their jobs (a robot with a job is not
    // up for hiring, and the office is not closing yet), and the night's
    // statistics count them.
    withBuilders.RunFor(sim::kMinutesPerDay);
    EXPECT_GE(withBuilders.State().history.back().buildersEmployed, 3);
    EXPECT_TRUE(sim::ValidateWorldState(withBuilders.State()));
}
