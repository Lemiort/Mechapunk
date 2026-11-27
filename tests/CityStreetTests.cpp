// Streets, stations and the grid: reaching every building through the
// street network, the route planner's distances and paths, the limit of
// four buildings per intersection the view's placement needs, and the
// free plots of the grid. What stands inside the buildings is in
// CityTests.cpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "mechapunk/sim/World.hpp"

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Random.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::City Generate(std::uint64_t seed, std::uint32_t residents) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed);
    sim::EntityIdAllocator ids;
    return sim::GenerateCity({.residents = residents}, random, ids);
}
}  // namespace

TEST(City, EveryBuildingIsReachable) {
    const sim::City city = Generate(3, 300);
    sim::RoutePlanner routes(city);
    const sim::NodeId origin = city.buildings.front().entrance;
    for (const sim::Building& building : city.buildings) {
        EXPECT_TRUE(routes.DistanceMeters(origin, building.entrance))
            << building.name;
    }
    // A few streets were removed, but never so many that nodes are cut off.
    for (const sim::CityNode& node : city.nodes) {
        EXPECT_TRUE(routes.DistanceMeters(sim::NodeId{0}, node.id));
    }
}

TEST(RoutePlanner, FindsShortestGridDistance) {
    sim::City city;
    for (std::uint32_t index = 0; index < 4; ++index) {
        city.nodes.push_back({.id = sim::NodeId{index}, .x = 0, .z = 0});
    }
    // 0 - 1 - 2 is 200 m; the direct 0 - 2 street is 500 m; 3 is isolated.
    city.edges = {{.from = sim::NodeId{0}, .to = sim::NodeId{1},
                   .lengthMeters = 100},
                  {.from = sim::NodeId{1}, .to = sim::NodeId{2},
                   .lengthMeters = 100},
                  {.from = sim::NodeId{0}, .to = sim::NodeId{2},
                   .lengthMeters = 500}};
    sim::RoutePlanner routes(city);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{2}), 200U);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{2}, sim::NodeId{0}), 200U);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{1}, sim::NodeId{1}), 0U);
    EXPECT_FALSE(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{3}));
    EXPECT_FALSE(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{9}));

    const std::vector<sim::NodeId> expected{sim::NodeId{0}, sim::NodeId{1},
                                            sim::NodeId{2}};
    EXPECT_EQ(routes.Path(sim::NodeId{0}, sim::NodeId{2}), expected);
    EXPECT_EQ(routes.Path(sim::NodeId{1}, sim::NodeId{1}),
              std::vector<sim::NodeId>{sim::NodeId{1}});
    EXPECT_TRUE(routes.Path(sim::NodeId{0}, sim::NodeId{3}).empty());
}

// The view places a building on one of the four corners of its entrance
// node, and pushes a fifth building out to a far ring where it overlaps a
// neighbour. Generation must therefore never put more than four buildings
// at one intersection, depots included.
TEST(City, NoIntersectionHasMoreThanFourBuildings) {
    for (const std::uint64_t seed : {1U, 7U, 33U}) {
        for (const std::uint32_t residents : {100U, 1000U, 4000U, 10000U}) {
            const sim::World world = sim::World::Generate(
                {.seed = seed, .robots = residents});
            std::unordered_map<std::uint64_t, std::uint32_t> at;
            for (const sim::Building& building : world.State().city.buildings) {
                ++at[building.entrance.value];
            }
            for (const auto& [node, count] : at) {
                EXPECT_LE(count, 4U) << "seed=" << seed
                                     << " residents=" << residents
                                     << " node=" << node;
            }
        }
    }
}

// A free plot is a corner of an intersection no building stands on: the
// view draws a park there, and construction raises a building on it, so
// the saved list must hold exactly the corners that are free - a node's
// slots after its buildings - sorted by node and then corner.
TEST(City, LotsAreTheFreeCornersOfTheGrid) {
    for (const std::uint64_t seed : {1U, 7U, 33U}) {
        for (const std::uint32_t residents : {100U, 1000U, 10000U}) {
            const sim::City city = Generate(seed, residents);
            std::vector<std::uint32_t> taken(city.nodes.size(), 0);
            for (const sim::Building& building : city.buildings) {
                ++taken.at(building.entrance.value);
            }
            std::vector<sim::CityLot> expected;
            for (std::size_t node = 0; node < taken.size(); ++node) {
                for (std::uint32_t corner = taken.at(node);
                     corner < sim::kCornersPerNode; ++corner) {
                    expected.push_back({.node = sim::NodeId{static_cast<std::uint32_t>(node)},
                                        .corner = corner});
                }
            }
            EXPECT_EQ(city.lots, expected)
                << "seed=" << seed << " residents=" << residents;
            EXPECT_FALSE(city.lots.empty());
        }
    }
}

TEST(RoutePlanner, PathLengthMatchesDistance) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(4);
    sim::EntityIdAllocator ids;
    const sim::City city = sim::GenerateCity({.residents = 80}, random, ids);
    sim::RoutePlanner routes(city);
    const auto edgeLength = [&](sim::NodeId a, sim::NodeId b) {
        for (const sim::CityEdge& edge : city.edges) {
            if ((edge.from == a && edge.to == b) ||
                (edge.from == b && edge.to == a)) {
                return edge.lengthMeters;
            }
        }
        ADD_FAILURE() << "no street between " << a.value << " and " << b.value;
        return 0U;
    };
    const sim::NodeId origin = city.buildings.front().entrance;
    for (const sim::Building& building : city.buildings) {
        const auto path = routes.Path(origin, building.entrance);
        ASSERT_FALSE(path.empty());
        std::uint32_t length = 0;
        for (std::size_t index = 1; index < path.size(); ++index) {
            length += edgeLength(path[index - 1], path[index]);
        }
        EXPECT_EQ(length, routes.DistanceMeters(origin, building.entrance));
    }
}
