// Task I, step I5: a site's building carries the style of the finished block
// from the night the site starts, the one StyleOf gives it.

#include <gtest/gtest.h>

#include <cstdint>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

[[nodiscard]] const sim::Building* Find(const sim::WorldState& state, sim::EntityId id) {
    for (const sim::Building& building : state.city.buildings) {
        if (building.id == id) {
            return &building;
        }
    }
    return nullptr;
}

}  // namespace

TEST(SiteStyle, ARaisedBuildingHasTheStyleOfItsFinalHeight) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    sim::EntityId home;
    std::uint32_t finalFloors = 0;
    for (int day = 0; day != 40 && !home.IsValid(); ++day) {
        world.RunFor(sim::kMinutesPerDay);
        for (const sim::Project& project : world.State().projects) {
            const sim::Building* building = Find(world.State(), project.building);
            ASSERT_TRUE(building != nullptr);
            home = project.building;
            finalFloors = building->floors + project.daysLeft;
            EXPECT_EQ(building->style, sim::StyleOf(building->kind, building->id, finalFloors))
                << "the site already knows what it will look like";
            break;
        }
    }
    ASSERT_TRUE(home.IsValid()) << "no site in 40 days";
    // The site ends and the building keeps that style at its final height.
    for (int day = 0; day != 12; ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    const sim::Building* done = Find(world.State(), home);
    ASSERT_TRUE(done != nullptr);
    EXPECT_EQ(done->floors, finalFloors);
    EXPECT_EQ(done->style, sim::StyleOf(done->kind, done->id, done->floors));
}
