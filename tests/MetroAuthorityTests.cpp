// A town's first metro line gets its Metro Authority and a crew room, and
// the drivers are hired (task K, step K12).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

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

// The first line's crew room is in a depot of its own, raised on a free corner
// of an intersection on the line's street, where the view puts the trains'
// shed on its roof; it waits for one if the street has none.
TEST(MetroAuthority, RaisesADepotOnTheLinesStreet) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = Town()});
    const auto depot = [&] {
        return std::ranges::find_if(world.State().city.buildings,
                                    [](const sim::Building& building) {
                                        return building.name == "Metro Depot 1";
                                    });
    };
    // Often the night the town grows a ring of streets, which reaches the line's.
    for (int day = 1; day <= 300 && depot() == world.State().city.buildings.end(); ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    ASSERT_NE(depot(), world.State().city.buildings.end()) << "no depot in 300 days";
    const sim::City& city = world.State().city;
    const sim::MetroLine& line = world.GetMetro().Lines().front();
    const sim::CityNode& first = city.nodes.at(city.stations.at(line.stations.front()).node.value);
    const sim::CityNode& at = city.nodes.at(depot()->entrance.value);
    EXPECT_TRUE(line.axis == sim::LineAxis::Row ? at.z == first.z : at.x == first.x);
    EXPECT_EQ(depot()->kind, sim::BuildingKind::Workplace);
    ASSERT_EQ(depot()->rooms.size(), 1U);
    EXPECT_EQ(depot()->rooms.front().kind, sim::RoomKind::CrewRoom);
    // Its corner is taken: validation checks no free plot lies under a building.
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
