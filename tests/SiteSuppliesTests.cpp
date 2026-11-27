// Tests for what a site pays: `World::ReviewConstruction` (sim/src/Construction.cpp)
// charges a going-up site the supplies it costs every night through
// `PaySupplies` (Money.cpp), counts into the project the credits its owner
// was charged night by night, and names the shortage that started it (task D,
// step D5b2). The review's other business - the two shortages that start a
// site, its floors, its plot and its validity - stays in
// `tests/ConstructionTests.cpp`, split by topic as D3d split the site office
// tests out of that file.

#include <gtest/gtest.h>

#include <cstdint>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

// The economy's organization of a sector, in the order `Economy` keeps them.
[[nodiscard]] const sim::Organization* SectorOrganization(
    const sim::WorldState& state, sim::Sector sector) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sector) {
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

}  // namespace

TEST(SiteSupplies, TheOwnerPaysTheSuppliesTheSiteCosts) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    ASSERT_TRUE(RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat) != nullptr);
    // One night more, so the site has paid its supplies and raised a floor.
    world.RunFor(sim::kMinutesPerDay);

    const sim::WorldState& state = world.State();
    // Only a site that was already going up pays tonight: the review advances
    // the sites first and starts new ones after, so a site started this very
    // midnight has not paid yet. `floorsDone` above the ground floor says it
    // has raised a floor, so it was there to pay.
    int sitesPaying = 0;
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        if (building != nullptr && building->kind == sim::BuildingKind::Habitat &&
            project.floorsDone > 1) {
            ++sitesPaying;
        }
    }
    ASSERT_GT(sitesPaying, 0);

    // The books rolled over before the review, so tonight's supplies are
    // only what the sites paid: the housing trust's sector share of the
    // site's cost, and nothing for an industry corporation, whose supplies
    // are what the other sectors buy from it (PaySupplies, Money.cpp).
    const sim::Organization* trust = SectorOrganization(state, sim::Sector::Housing);
    const sim::Organization* industry = SectorOrganization(state, sim::Sector::Industry);
    ASSERT_TRUE(trust != nullptr);
    ASSERT_TRUE(industry != nullptr);
    EXPECT_EQ(trust->today.supplies, 100 * sitesPaying);
    EXPECT_EQ(industry->today.supplies, 0) << "no workplace is going up here";

    // Money still adds up, with the supply market taking the site's share.
    EXPECT_TRUE(sim::ValidateEconomy(state).has_value())
        << sim::ValidateEconomy(state).error();
}

// A site that has run a few nights counts what it paid and says why it was
// raised: the cost is the credits its owner was charged night by night - the
// housing trust's half of the 200 a site asks for (PaySupplies, Money.cpp) -
// and the reason is the shortage the review read off the saved history.
TEST(SiteSupplies, SiteCountsItsCostAndNamesItsReason) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::Project* site = RunUntilSiteOf(world, 40, sim::BuildingKind::Habitat);
    ASSERT_TRUE(site != nullptr) << "no habitat site in 40 days";
    // The review advances the sites going up and starts new ones after, so
    // the site raised this very midnight has paid nothing yet.
    EXPECT_EQ(site->costSoFar, 0);
    EXPECT_EQ(site->reason, sim::ProjectReason::HomesShort);

    world.RunFor(sim::kMinutesPerDay);
    const sim::Project* going = HabitatSite(world.State());
    ASSERT_TRUE(going != nullptr) << "the site is still going up";
    EXPECT_EQ(going->costSoFar, 100) << "the trust's half of the site's 200";
    EXPECT_EQ(going->reason, sim::ProjectReason::HomesShort);

    world.RunFor(sim::kMinutesPerDay);
    going = HabitatSite(world.State());
    ASSERT_TRUE(going != nullptr) << "six floors to raise";
    EXPECT_EQ(going->costSoFar, 200) << "two nights of supplies";
}
