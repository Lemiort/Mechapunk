// The seed town (task K): one dormitory, one workshop and nothing else, on a
// small grid with no metro. A full city stays the default.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Settings.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::City SeedTown(std::uint64_t seed, sim::CitySettings settings = {}) {
    settings.seedTown = 1;
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed);
    sim::EntityIdAllocator ids;
    return sim::GenerateCity({.residents = 30, .city = settings}, random, ids);
}

std::uint32_t Count(const sim::City& city, sim::RoomKind kind) {
    std::uint32_t count = 0;
    for (const sim::Building& building : city.buildings) {
        count += static_cast<std::uint32_t>(std::ranges::count_if(
            building.rooms, [&](const sim::Room& room) { return room.kind == kind; }));
    }
    return count;
}
}  // namespace

TEST(SeedTown, IsOneDormitoryAndOneWorkshop) {
    const sim::City city = SeedTown(1);
    ASSERT_EQ(city.buildings.size(), 2U);
    EXPECT_EQ(std::ranges::count_if(city.buildings, [](const sim::Building& building) {
                  return building.kind == sim::BuildingKind::Habitat;
              }),
              1);
    EXPECT_EQ(std::ranges::count_if(city.buildings, [](const sim::Building& building) {
                  return building.kind == sim::BuildingKind::Workplace;
              }),
              1);
    EXPECT_TRUE(city.stations.empty());
    EXPECT_EQ(city.nodes.size(), 9U);
}

TEST(SeedTown, HasTheRoomsAskedFor) {
    sim::CitySettings settings;
    settings.seedDormFloors = 6;
    settings.seedWorkspaces = 3;
    const sim::City city = SeedTown(2, settings);
    EXPECT_EQ(Count(city, sim::RoomKind::ChargingBay), 1U);
    EXPECT_EQ(Count(city, sim::RoomKind::Diner), 1U);
    EXPECT_EQ(Count(city, sim::RoomKind::Apartment), 6U * 12U);
    EXPECT_EQ(Count(city, sim::RoomKind::Workspace), 3U);
    // Floors of homes plus the ground floor.
    EXPECT_EQ(city.buildings.front().floors, 7U);
    for (const sim::Building& building : city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                EXPECT_EQ(room.apartmentClass, sim::ApartmentClass::Dorm);
            }
        }
    }
}

TEST(SeedTown, EveryOtherCornerIsAFreePlot) {
    const sim::City city = SeedTown(3);
    // Nine intersections of four corners, two of them taken.
    EXPECT_EQ(city.lots.size(), 9U * sim::kCornersPerNode - 2U);
    EXPECT_NE(city.buildings[0].entrance.value, city.buildings[1].entrance.value);
}

TEST(SeedTown, SameSeedSameTown) {
    const sim::City first = SeedTown(4);
    const sim::City second = SeedTown(4);
    ASSERT_EQ(first.buildings.size(), second.buildings.size());
    for (std::size_t index = 0; index < first.buildings.size(); ++index) {
        EXPECT_EQ(first.buildings[index].name, second.buildings[index].name);
        EXPECT_EQ(first.buildings[index].rooms.size(), second.buildings[index].rooms.size());
    }
    EXPECT_EQ(first.edges.size(), second.edges.size());
}

TEST(SeedTown, AFullCityIsStillTheDefault) {
    EXPECT_EQ(sim::CitySettings{}.seedTown, 0);
    sim::Pcg32 random = sim::Pcg32::FromSeed(5);
    sim::EntityIdAllocator ids;
    const sim::City city = sim::GenerateCity({.residents = 100}, random, ids);
    EXPECT_GT(city.buildings.size(), 2U);
    EXPECT_FALSE(city.stations.empty());
}

// A seed town of 30 robots runs for twenty days, valid after every day. The
// workshop has fewer jobs than there are robots, so some start out of work.
TEST(SeedTown, RunsTwentyDaysValid) {
    for (const std::uint64_t seed : {1U, 2U, 3U}) {
        sim::SimSettings settings;
        settings.city.seedTown = 1;
        sim::World world =
            sim::World::Generate({.seed = seed, .robots = 30, .settings = settings});
        ASSERT_EQ(world.State().robots.size(), 30U);
        EXPECT_TRUE(std::ranges::any_of(world.State().robots, [](const sim::Robot& robot) {
            return !robot.workspace.IsValid();
        })) << "seed " << seed;
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
        for (int day = 1; day <= 20; ++day) {
            world.RunFor(sim::kMinutesPerDay);
            ASSERT_TRUE(sim::ValidateWorldState(world.State()))
                << "seed " << seed << " day " << day;
        }
        EXPECT_EQ(world.State().robots.size(), 30U);
    }
}
