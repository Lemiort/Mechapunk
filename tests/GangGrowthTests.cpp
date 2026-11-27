// A growing town founds gangs as its robots pass multiples of robotsPerGang
// (task K, step K18).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::SimSettings Town() {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = 4;
    settings.population.wealthSpread = 1;
    settings.population.immigration = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    settings.crime.minGangs = 0;
    settings.crime.robotsPerGang = 40;
    return settings;
}
}  // namespace

TEST(GangGrowth, NoGangAtThirtyRobotsAndOneAfterFortyAndRelationsStaySymmetric) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town()});
    EXPECT_TRUE(world.State().gangs.empty());
    world.RunFor(sim::kMinutesPerDay);
    // One night, at thirty robots (plus the night's newcomers, still under forty).
    if (world.State().robots.size() < 40) {
        EXPECT_TRUE(world.State().gangs.empty());
    }
    bool seenOne = false;
    for (int day = 0; day < 150; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        const sim::WorldState& state = world.State();
        const std::size_t wanted = std::min<std::size_t>(6, state.robots.size() / 40);
        EXPECT_LE(state.gangs.size(), 6U);
        if (state.gangs.size() == 1) {
            seenOne = true;
        }
        // Never ahead of the robots: a gang for each forty of them at most.
        EXPECT_LE(state.gangs.size(), wanted + 1) << "day " << day;
        ASSERT_TRUE(sim::ValidateWorldState(state)) << "day " << day;
    }
    EXPECT_TRUE(seenOne) << "the first gang never formed";
    const sim::WorldState& state = world.State();
    ASSERT_GE(state.gangs.size(), 2U);
    for (const sim::Gang& gang : state.gangs) {
        EXPECT_TRUE(std::ranges::is_sorted(gang.relations, {}, [](const sim::GangRelation& r) {
            return r.gang.value;
        }));
        EXPECT_EQ(gang.relations.size(), state.gangs.size() - 1);
        for (const sim::GangRelation& relation : gang.relations) {
            const auto other = std::ranges::find(state.gangs, relation.gang, &sim::Gang::id);
            ASSERT_NE(other, state.gangs.end());
            const auto back = std::ranges::find(other->relations, gang.id, &sim::GangRelation::gang);
            ASSERT_NE(back, other->relations.end());
            EXPECT_EQ(back->stance, relation.stance);
        }
    }
    // Each gang holds a habitat of its own.
    for (std::size_t first = 0; first < state.gangs.size(); ++first) {
        for (std::size_t second = first + 1; second < state.gangs.size(); ++second) {
            EXPECT_NE(state.gangs[first].turf, state.gangs[second].turf);
        }
    }
}
