// The seed town (task K): one dormitory, one workshop and nothing else, on a
// small grid with no metro. A full city stays the default.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

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

namespace {
std::int64_t RichRobots(const sim::World& world, std::int64_t usualMaximum) {
    return std::ranges::count_if(world.State().robots, [&](const sim::Robot& robot) {
        return robot.credits > usualMaximum;
    });
}
}  // namespace

// Poor to rich (task K, step K4): about richPercent of the robots start with
// richMultiplier times the usual credits; with the spread off nobody does.
TEST(SeedTown, WealthSpreadMakesSomeRobotsRich) {
    sim::SimSettings settings;
    const std::int64_t usualMaximum = settings.population.maxStartingCredits;
    const sim::World plain =
        sim::World::Generate({.seed = 6, .robots = 1000, .settings = settings});
    EXPECT_EQ(RichRobots(plain, usualMaximum), 0);

    settings.population.wealthSpread = 1;
    const sim::World spread =
        sim::World::Generate({.seed = 6, .robots = 1000, .settings = settings});
    const std::int64_t rich = RichRobots(spread, usualMaximum);
    EXPECT_GT(rich, 60);   // Ten percent of a thousand, with margin.
    EXPECT_LT(rich, 140);
    // The same seed gives the same city.
    const sim::World again =
        sim::World::Generate({.seed = 6, .robots = 1000, .settings = settings});
    EXPECT_EQ(RichRobots(again, usualMaximum), rich);
}

// Newcomers bring savings the same way: with every robot rich and the
// multiplier at 50, each arrival's savings are 50 times the fixed draw.
TEST(SeedTown, RichNewcomersBringMoreSavings) {
    for (const bool spread : {false, true}) {
        sim::SimSettings settings;
        ASSERT_TRUE(sim::ApplySetting(settings, "population.maxNewcomerCredits", 100));
        ASSERT_TRUE(sim::ApplySetting(settings, "population.minNewcomerCredits", 100));
        ASSERT_TRUE(sim::ApplySetting(settings, "population.wealthSpread", spread ? 1 : 0));
        ASSERT_TRUE(sim::ApplySetting(settings, "population.richPercent", 100));
        ASSERT_TRUE(sim::ApplySetting(settings, "population.richMultiplier", 50));
        sim::WorldState state =
            sim::World::Generate({.seed = 7, .robots = 100, .settings = settings}).State();
        // A hand-written wear caps the head and the torso the way the game caps
        // them wherever wear grows (step L3a): they come to 1, the next midnight
        // lands them on 0, and step L2b6 refuses a state that skips the cap.
        for (std::size_t index = 0; index < 10; ++index) {
            state.robots[index].wear = sim::kMaxWear - 1;
            sim::CapBody(state.robots[index]);
        }
        const std::size_t emissionsBefore = state.economy.emissions.size();
        auto world = sim::World::FromState(std::move(state));
        ASSERT_TRUE(world.has_value()) << world.error();
        world->RunFor(2 * sim::kMinutesPerDay);
        std::vector<std::int64_t> savings;
        const auto& emissions = world->State().economy.emissions;
        for (std::size_t index = emissionsBefore; index < emissions.size(); ++index) {
            if (emissions[index].reason == sim::EmissionReason::NewcomerSavings) {
                savings.push_back(emissions[index].amount);
            }
        }
        ASSERT_FALSE(savings.empty()) << "nobody arrived";
        for (const std::int64_t amount : savings) {
            EXPECT_EQ(amount, spread ? 5000 : 100);
        }
    }
}

// The smallest economy that adds up (task K, step K5): one bank, a trust, City
// Hall and the corporations that own the town's rooms; their capital is bought
// as shares by robots, and the central bank lends what a bank still lacks.
TEST(SeedTown, SmallestEconomyAddsUp) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.population.wealthSpread = 1;
    sim::World world =
        sim::World::Generate({.seed = 1, .robots = 30, .settings = settings});
    const auto check = [&](const char* when) {
        const sim::WorldState& state = world.State();
        EXPECT_TRUE(sim::ValidateEconomy(state)) << when;
        EXPECT_EQ(std::ranges::count_if(state.economy.organizations,
                                        [](const sim::Organization& organization) {
                                            return organization.kind ==
                                                   sim::OrganizationKind::Bank;
                                        }),
                  1)
            << when;
        for (const sim::Building& building : state.city.buildings) {
            for (const sim::Room& room : building.rooms) {
                EXPECT_TRUE(room.owner.IsValid()) << room.name << " " << when;
            }
        }
        for (const sim::Organization& organization : state.economy.organizations) {
            if (organization.kind != sim::OrganizationKind::Corporation &&
                organization.kind != sim::OrganizationKind::Bank) {
                continue;
            }
            EXPECT_FALSE(organization.owners.empty()) << organization.name << " " << when;
            for (const sim::Share& share : organization.owners) {
                if (organization.sector == sim::Sector::Industry ||
                    organization.sector == sim::Sector::Venues ||
                    organization.kind == sim::OrganizationKind::Bank) {
                    EXPECT_TRUE(std::ranges::any_of(
                        state.robots,
                        [&](const sim::Robot& robot) { return robot.id == share.owner; }))
                        << organization.name << " is owned by a robot " << when;
                }
            }
        }
    };
    check("on day 0");
    world.RunFor(20 * sim::kMinutesPerDay);
    check("after 20 days");
    std::int64_t wages = 0;
    for (std::size_t day = 0; day < 7 && day < world.State().history.size(); ++day) {
        wages += world.State().history[day].counted.wages;
    }
    EXPECT_GT(wages, 0) << "the town paid no wages in its first week";
}

// The beds rule (task K, step K6): with every dorm bed taken for two nights the
// city raises a dormitory, on its own, of dorm rooms on every floor.
TEST(SeedTown, FullDormsRaiseADormitory) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.seedDormFloors = 2;  // 24 beds.
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    sim::World world =
        sim::World::Generate({.seed = 1, .robots = 24, .settings = settings});
    const sim::Building* dormitory = nullptr;
    for (int night = 0; night < 3 && dormitory == nullptr; ++night) {
        world.RunFor(sim::kMinutesPerDay);
        for (const sim::Project& project : world.State().projects) {
            for (const sim::Building& building : world.State().city.buildings) {
                if (building.id == project.building && building.name.starts_with("Dormitory")) {
                    dormitory = &building;
                }
            }
        }
    }
    ASSERT_NE(dormitory, nullptr) << "no dormitory site within three nights";
    // Let it finish: every home on its floors is a dorm room.
    world.RunFor(10 * sim::kMinutesPerDay);
    for (const sim::Building& building : world.State().city.buildings) {
        if (building.id != dormitory->id) {
            continue;
        }
        std::uint32_t beds = 0;
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                EXPECT_EQ(room.apartmentClass, sim::ApartmentClass::Dorm);
                beds += room.capacity;
            }
        }
        EXPECT_GE(beds, 12U);
    }
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Immigration (task K, step K7): each night up to immigrantsPerNight robots
// arrive while a dorm bed and a job are free, and never more robots than beds.
TEST(SeedTown, ImmigrantsArriveWhileThereIsRoom) {
    for (const bool immigration : {false, true}) {
        sim::SimSettings settings;
        settings.city.seedTown = 1;
        settings.city.seedWorkspaces = 4;
        settings.population.immigration = immigration ? 1 : 0;
        settings.population.immigrantsPerNight = 3;
        sim::World world =
            sim::World::Generate({.seed = 1, .robots = 10, .settings = settings});
        std::uint32_t beds = 0;
        for (const sim::Building& building : world.State().city.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.kind == sim::RoomKind::Apartment) {
                    beds += room.capacity;
                }
            }
        }
        std::size_t before = world.State().robots.size();
        std::size_t largest = 0;
        for (int night = 0; night < 15; ++night) {
            world.RunFor(sim::kMinutesPerDay);
            const std::size_t now = world.State().robots.size();
            if (immigration && night == 0) {
                EXPECT_GT(now, before) << "nobody arrived the first night";
                EXPECT_LE(now - before, 3U + 1U);  // Three, and a replacement.
            }
            EXPECT_LE(now, beds) << "more robots than beds";
            largest = std::max(largest, now);
        }
        if (immigration) {
            EXPECT_GT(largest, 10U);
        } else {
            EXPECT_LE(largest, 11U);  // Only replacements.
        }
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
}

// A seed town grows (task K, step K8): 120 days with the town's rules on.
namespace {
sim::SimSettings GrowingTown() {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.population.wealthSpread = 1;
    settings.population.immigration = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    return settings;
}
}  // namespace

TEST(SeedTown, GrowsInAHundredTwentyDays) {
    sim::World world =
        sim::World::Generate({.seed = 1, .robots = 30, .settings = GrowingTown()});
    for (int day = 1; day <= 120; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        if (day % 20 == 0) {
            ASSERT_TRUE(sim::ValidateWorldState(world.State())) << "day " << day;
        }
    }
    const sim::WorldState& state = world.State();
    EXPECT_GT(state.city.buildings.size(), 5U);
    EXPECT_GT(state.robots.size(), 60U);
    for (const sim::Robot& robot : state.robots) {
        EXPECT_TRUE(robot.apartment.IsValid()) << robot.name << " has no bed";
    }
    EXPECT_TRUE(sim::ValidateEconomy(state));
    EXPECT_TRUE(sim::ValidateWorldState(state));
}
