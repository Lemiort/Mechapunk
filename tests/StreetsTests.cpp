// Streets that grow (task K, step K9): a ring of streets round the grid when
// a town runs out of free plots.

#include <gtest/gtest.h>

#include <cstdint>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
// A seed town with every plot taken, and streets that grow.
sim::WorldState FullSeedTown(std::int64_t freePlotsMin) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = freePlotsMin;
    sim::WorldState state =
        sim::World::Generate({.seed = 1, .robots = 20, .settings = settings}).State();
    state.city.lots.clear();
    return state;
}
}  // namespace

TEST(Streets, ARingGrowsWhenPlotsRunOut) {
    auto made = sim::World::FromState(FullSeedTown(4));
    ASSERT_TRUE(made.has_value()) << made.error();
    ASSERT_EQ(made->State().city.nodes.size(), 9U);
    made->RunFor(sim::kMinutesPerDay);
    const sim::City& city = made->State().city;
    // 3 by 3 becomes 5 by 5; every new node has four free corners.
    EXPECT_EQ(city.nodes.size(), 25U);
    EXPECT_EQ(city.lots.size(), 16U * sim::kCornersPerNode);
    EXPECT_TRUE(sim::ValidateWorldState(made->State()));
    // Every node can be reached from every other.
    sim::RoutePlanner routes(city);
    for (const sim::CityNode& node : city.nodes) {
        EXPECT_TRUE(routes.DistanceMeters(sim::NodeId{0}, node.id).has_value())
            << "node " << node.id.value;
    }
    // Nothing more is added while plots remain.
    made->RunFor(3 * sim::kMinutesPerDay);
    EXPECT_EQ(made->State().city.nodes.size(), 25U);
}

TEST(Streets, ADefaultCityNeverGrowsStreets) {
    auto made = sim::World::FromState(FullSeedTown(0));
    ASSERT_TRUE(made.has_value()) << made.error();
    made->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_EQ(made->State().city.nodes.size(), 9U);
}

TEST(Streets, StopsAtTheLargestGrid) {
    sim::WorldState state = FullSeedTown(4);
    state.settings.city.maxGridSize = 5;
    auto made = sim::World::FromState(std::move(state));
    ASSERT_TRUE(made.has_value()) << made.error();
    made->RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(made->State().city.nodes.size(), 25U);
    // Those plots are used up again; the grid is at its limit.
    sim::WorldState again = made->State();
    again.city.lots.clear();
    auto next = sim::World::FromState(std::move(again));
    ASSERT_TRUE(next.has_value()) << next.error();
    next->RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(next->State().city.nodes.size(), 25U);
}

TEST(Streets, GrownStreetsSurviveASave) {
    auto made = sim::World::FromState(FullSeedTown(4));
    ASSERT_TRUE(made.has_value()) << made.error();
    made->RunFor(sim::kMinutesPerDay);
    auto json = sim::SerializeWorld(made->State());
    ASSERT_TRUE(json.has_value()) << json.error();
    auto loaded = sim::DeserializeWorld(*json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_EQ(loaded->city.nodes.size(), made->State().city.nodes.size());
    EXPECT_EQ(loaded->city.edges.size(), made->State().city.edges.size());
    EXPECT_EQ(loaded->city.lots, made->State().city.lots);
    EXPECT_TRUE(sim::ValidateWorldState(*loaded));
}

TEST(Streets, GrowAfterGrowKeepsTheTownConnected) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = 4;
    settings.population.immigration = 1;
    settings.population.wealthSpread = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    sim::World world =
        sim::World::Generate({.seed = 2, .robots = 30, .settings = settings});
    world.RunFor(150 * sim::kMinutesPerDay);
    EXPECT_GT(world.State().city.nodes.size(), 9U) << "the streets never grew";
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
