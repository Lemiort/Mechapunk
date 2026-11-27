// A town's first metro line gets its Metro Authority and a crew room, and
// the drivers are hired (task K, step K12).

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
    settings.metro.minBuildings = 6;
    return settings;
}

std::int64_t Authorities(const sim::WorldState& state) {
    return std::ranges::count_if(state.economy.organizations, [](const sim::Organization& o) {
        return o.sector == sim::Sector::Transit;
    });
}
}  // namespace

TEST(MetroAuthority, ComesWithTheFirstLineAndGetsACrewRoom) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town()});
    EXPECT_EQ(Authorities(world.State()), 0);
    bool line = false;
    for (int day = 1; day <= 300; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        if (!line && !world.GetMetro().Lines().empty()) {
            line = true;
            // Founded the night the line opens, once and only once.
            EXPECT_EQ(Authorities(world.State()), 1);
            // It borrowed its capital from the central bank.
            const auto authority = std::ranges::find_if(
                world.State().economy.organizations,
                [](const sim::Organization& o) { return o.sector == sim::Sector::Transit; });
            ASSERT_NE(authority, world.State().economy.organizations.end());
            EXPECT_TRUE(std::ranges::any_of(world.State().economy.loans,
                                            [&](const sim::Loan& loan) {
                                                return loan.borrower == authority->id;
                                            }));
        }
        ASSERT_TRUE(sim::ValidateWorldState(world.State())) << "day " << day;
    }
    ASSERT_TRUE(line) << "no line in 300 days";
    EXPECT_EQ(Authorities(world.State()), 1);
    EXPECT_TRUE(sim::ValidateEconomy(world.State()));
    std::int64_t crewRooms = 0;
    for (const sim::Building& building : world.State().city.buildings) {
        for (const sim::Room& room : building.rooms) {
            crewRooms += room.kind == sim::RoomKind::CrewRoom ? 1 : 0;
        }
    }
    EXPECT_GE(crewRooms, static_cast<std::int64_t>(world.GetMetro().Lines().size()));
    // Drivers work in the crew rooms.
    std::int64_t drivers = 0;
    for (const sim::Robot& robot : world.State().robots) {
        for (const sim::Building& building : world.State().city.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.kind == sim::RoomKind::CrewRoom && room.id == robot.workspace) {
                    ++drivers;
                }
            }
        }
    }
    EXPECT_GT(drivers, 0) << "nobody was hired to drive";
}
