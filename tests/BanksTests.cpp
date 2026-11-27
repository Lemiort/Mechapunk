// A growing town founds banks as its robots outnumber them (task K, step K14).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>

#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::SimSettings Town(std::int64_t robotsPerBank) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = 4;
    settings.population.wealthSpread = 1;
    settings.population.immigration = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    settings.economy.robotsPerBank = robotsPerBank;
    return settings;
}

std::int64_t Banks(const sim::WorldState& state) {
    return std::ranges::count_if(state.economy.organizations, [](const sim::Organization& o) {
        return o.kind == sim::OrganizationKind::Bank;
    });
}
}  // namespace

TEST(Banks, ATownFoundsABankWhenRobotsOutnumberThem) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town(60)});
    EXPECT_EQ(Banks(world.State()), 1);
    for (int day = 1; day <= 150; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        ASSERT_TRUE(sim::ValidateEconomy(world.State())) << "day " << day;
    }
    const sim::WorldState& state = world.State();
    EXPECT_GE(Banks(state), 2) << state.robots.size() << " robots";
    // Robots hold accounts at more than one bank.
    std::set<std::uint64_t> used;
    for (const sim::Robot& robot : state.robots) {
        used.insert(robot.bank.value);
    }
    EXPECT_GE(used.size(), 2U);
    // Every bank is owned by robots that paid for the shares.
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.kind == sim::OrganizationKind::Bank) {
            EXPECT_FALSE(organization.owners.empty()) << organization.name;
        }
    }
    EXPECT_TRUE(sim::ValidateWorldState(state));
}

// A fourth workplace goes to a new corporation when each owns three (K15).
TEST(Banks, ANewCorporationOwnsTheWorkplaceThatWouldBeOneTooMany) {
    sim::SimSettings settings = Town(0);
    settings.economy.workplacesPerCorporation = 2;
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = settings});
    const auto industry = [&] {
        return std::ranges::count_if(world.State().economy.organizations,
                                     [](const sim::Organization& o) {
                                         return o.sector == sim::Sector::Industry;
                                     });
    };
    EXPECT_EQ(industry(), 1);
    for (int day = 1; day <= 250; ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    const std::int64_t workplaces = std::ranges::count_if(
        world.State().city.buildings,
        [](const sim::Building& b) { return b.kind == sim::BuildingKind::Workplace; });
    ASSERT_GE(workplaces, 3) << "the town raised no workplace to speak of";
    EXPECT_GE(industry(), 2);
    for (const sim::Organization& organization : world.State().economy.organizations) {
        if (organization.sector == sim::Sector::Industry) {
            EXPECT_FALSE(organization.owners.empty()) << organization.name;
        }
    }
    EXPECT_TRUE(sim::ValidateEconomy(world.State()));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

TEST(Banks, TheDefaultFoundsNone) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town(0)});
    world.RunFor(150 * sim::kMinutesPerDay);
    EXPECT_EQ(Banks(world.State()), 1);
}
