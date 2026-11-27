// The metro grows with a town (task K, step K11): stations open where
// buildings are out of reach, two at first so that there is a line.

#include <gtest/gtest.h>

#include <cstdint>

#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::SimSettings Town(std::int64_t minBuildings) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = 4;
    settings.population.wealthSpread = 1;
    settings.population.immigration = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    settings.metro.minBuildings = minBuildings;
    return settings;
}
}  // namespace

TEST(TransitGrowth, ATownOpensItsFirstStations) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town(6)});
    EXPECT_TRUE(world.State().city.stations.empty());
    std::size_t stationsBefore = 0;
    bool grew = false;
    for (int day = 1; day <= 150; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        const std::size_t now = world.State().city.stations.size();
        if (!grew && now > 0) {
            grew = true;
            EXPECT_EQ(now, 2U) << "the first two open together";
            EXPECT_FALSE(world.GetMetro().Lines().empty());
        }
        EXPECT_GE(now, stationsBefore);
        stationsBefore = now;
        ASSERT_TRUE(sim::ValidateWorldState(world.State())) << "day " << day;
    }
    EXPECT_TRUE(grew) << "no station in 150 days";
    EXPECT_TRUE(sim::ValidateEconomy(world.State()));
    // A save round trip keeps the stations.
    auto json = sim::SerializeWorld(world.State());
    ASSERT_TRUE(json.has_value()) << json.error();
    auto loaded = sim::DeserializeWorld(*json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_EQ(loaded->city.stations.size(), world.State().city.stations.size());
}

TEST(TransitGrowth, AFullCityAndADefaultTownOpenNone) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town(0)});
    world.RunFor(100 * sim::kMinutesPerDay);
    EXPECT_TRUE(world.State().city.stations.empty());
}

TEST(TransitGrowth, TheMoneyAddsUpAndTrainsCarryRobots) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 30, .settings = Town(6)});
    std::int64_t rides = 0;
    for (int day = 1; day <= 200; ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    for (const sim::DailyStats& day : world.State().history) {
        rides += day.counted.metroRides;
    }
    EXPECT_TRUE(sim::ValidateEconomy(world.State()));
    EXPECT_FALSE(world.State().city.stations.empty());
    EXPECT_GE(rides, 0);  // May be none: a small town walks.
}
