// The style a building is dressed in: that it is read off the building's own
// id, kind and floor count, so no draw of the city's stream moves it, and
// that a city wears more than one kind of block. What a generated city puts
// inside its buildings is in CityTests.cpp, the streets, the route planner
// and the free plots are in CityStreetTests.cpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Random.hpp"

namespace sim = mechapunk::sim;

namespace {
// How many buildings of each style one kind of building has.
using StyleCounts = std::map<sim::BuildingStyle, std::uint32_t>;

sim::City Generate(std::uint64_t seed, std::uint32_t residents) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed);
    sim::EntityIdAllocator ids;
    return sim::GenerateCity({.residents = residents}, random, ids);
}
}  // namespace

// A building's style is settled the moment the building is: it equals
// `StyleOf` of its own data, so a save round trip and a rerun of the seed
// both give it again. And a city of 1000 robots wears more than one style,
// with no one style filling half of a kind's buildings.
TEST(City, StyleIsSettledAndSpreads) {
    for (const std::uint64_t seed : {1ULL, 7ULL, 33ULL}) {
        const sim::City city = Generate(seed, 1000);
        std::map<sim::BuildingKind, StyleCounts> counts;
        for (const sim::Building& building : city.buildings) {
            EXPECT_EQ(building.style,
                      sim::StyleOf(building.kind, building.id,
                                   building.floors))
                << seed << ' ' << building.name;
            ++counts[building.kind][building.style];
        }
        for (const auto& [kind, byStyle] : counts) {
            std::uint32_t total = 0;
            for (const auto& [style, count] : byStyle) {
                total += count;
            }
            EXPECT_GT(byStyle.size(), 1U)
                << seed << " every building of kind "
                << (kind == sim::BuildingKind::Habitat ? "a habitat"
                                                       : "a workplace")
                << " has the same style";
            for (const auto& [style, count] : byStyle) {
                EXPECT_LE(count * 2U, total)
                    << seed << " style "
                    << static_cast<std::uint8_t>(style) << " of kind "
                    << static_cast<std::uint8_t>(kind) << " in " << total
                    << " buildings";
            }
        }
    }
}
