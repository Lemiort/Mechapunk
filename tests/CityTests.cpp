#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

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

TEST(City, HasRoomForEveryResident) {
    const sim::City city = Generate(11, 250);
    std::uint32_t housing = 0;
    std::uint32_t jobs = 0;
    for (const sim::Building& building : city.buildings) {
        (building.kind == sim::BuildingKind::Habitat ? housing : jobs) +=
            building.capacity;
    }
    EXPECT_GE(housing, 250U);
    EXPECT_GE(jobs, 250U);
}

TEST(City, BuildingsAreSortedAndFindable) {
    const sim::City city = Generate(5, 100);
    for (std::size_t index = 1; index < city.buildings.size(); ++index) {
        EXPECT_LT(city.buildings[index - 1].id, city.buildings[index].id);
    }
    const sim::Building& last = city.buildings.back();
    EXPECT_EQ(city.FindBuilding(last.id), &last);
    EXPECT_EQ(city.FindBuilding(sim::EntityId{999999}), nullptr);
}

TEST(City, SameSeedGivesSameCity) {
    const sim::City first = Generate(9, 120);
    const sim::City second = Generate(9, 120);
    ASSERT_EQ(first.edges.size(), second.edges.size());
    ASSERT_EQ(first.buildings.size(), second.buildings.size());
    for (std::size_t index = 0; index < first.buildings.size(); ++index) {
        EXPECT_EQ(first.buildings[index].name, second.buildings[index].name);
        EXPECT_EQ(first.buildings[index].entrance,
                  second.buildings[index].entrance);
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
