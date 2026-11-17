#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}
}  // namespace

// Stations sit at every second intersection, so every street node is a
// short walk from one.
TEST(Transit, StationsCoverTheCity) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 1000});
    const sim::City& city = world.State().city;
    std::uint32_t size = 0;
    while ((size + 1) * (size + 1) <= city.nodes.size()) {
        ++size;
    }
    ASSERT_EQ(city.stations.size(), (size / 2) * (size / 2));
    std::set<std::uint32_t> nodes;
    std::set<std::string> names;
    for (const sim::Station& station : city.stations) {
        EXPECT_TRUE(nodes.insert(station.node.value).second);
        EXPECT_TRUE(names.insert(station.name).second);
    }
    sim::RoutePlanner routes(city);
    for (const sim::CityNode& node : city.nodes) {
        std::uint32_t nearest = UINT32_MAX;
        for (const sim::Station& station : city.stations) {
            nearest = std::min(nearest,
                               routes.DistanceMeters(node.id, station.node).value_or(UINT32_MAX));
        }
        // A block each way, plus a detour round a missing street.
        EXPECT_LE(nearest, 4U * 80U) << "node " << node.id.value;
    }
}

// The metro is taken only when it is faster than walking, never inside one
// building, and its parts add up to the whole trip.
TEST(Transit, TripsTakeTheMetroOnlyWhenFaster) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 1000});
    const auto& apartments = world.Rooms().RoomsOfKind(sim::RoomKind::Apartment);
    const auto& workspaces = world.Rooms().RoomsOfKind(sim::RoomKind::Workspace);
    std::uint32_t byMetro = 0;
    for (const sim::RoomRef& home : apartments) {
        for (const sim::RoomRef& work : workspaces) {
            const sim::Trip trip = world.PlanTrip(home.room->id, work.room->id);
            const sim::Trip walk = world.PlanTrip(home.room->id, work.room->id, false);
            EXPECT_FALSE(walk.metro);
            EXPECT_LE(trip.minutes, walk.minutes);
            if (trip.metro) {
                ++byMetro;
                EXPECT_LT(trip.minutes, walk.minutes);
                EXPECT_NE(trip.fromStation, trip.toStation);
                // The walk on can take no time when the station is at the door.
                EXPECT_LE(trip.minutesToStation + trip.minutesUnderground, trip.minutes);
            }
        }
        const sim::RoomRef& neighbour = apartments.front();
        if (neighbour.building == home.building) {
            EXPECT_FALSE(world.PlanTrip(home.room->id, neighbour.room->id).metro);
        }
    }
    EXPECT_GT(byMetro, 0U);
}

// Riders pay the fare and are underground between boarding and alighting;
// a robot that cannot spare the fare walks instead.
TEST(Transit, RidersPayAndBrokeRobotsWalk) {
    sim::World start = sim::World::Generate({.seed = 9, .robots = 2000});
    start.RunFor(60);  // Day 1, 01:00: everyone at home.
    sim::WorldState state = start.State();
    // Of the robots whose commute is fastest by metro, half keep their
    // credits and the other half have none.
    std::set<std::uint64_t> broke;
    std::set<std::uint64_t> commuters;
    for (sim::Robot& robot : state.robots) {
        if (!robot.workspace.IsValid() || robot.commuteAt == sim::kNoCommute ||
            !start.PlanTrip(robot.apartment, robot.workspace).metro) {
            continue;
        }
        commuters.insert(robot.id.value);
        if (commuters.size() % 2 == 0) {
            sim::SetCredits(state, robot, 0);
            robot.savings = 0;
            broke.insert(robot.id.value);
        }
    }
    ASSERT_GE(commuters.size(), 4U);
    sim::World world = Load(std::move(state));
    std::uint64_t paidRides = 0;
    std::uint64_t brokeRides = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::Departed || !event.metro) {
            return;
        }
        if (broke.contains(event.robot.value)) {
            ++brokeRides;
        } else {
            ++paidRides;
            EXPECT_EQ(event.credits, sim::kMetroFare);
        }
        const sim::Robot* robot = world.FindRobot(event.robot);
        ASSERT_NE(robot, nullptr);
        EXPECT_LT(robot->boardsAt, robot->alightsAt);
        EXPECT_LE(robot->alightsAt, robot->nextActionAt);
    });
    // The first commute, before anyone has earned a wage.
    world.RunFor(12 * 60);
    EXPECT_GT(paidRides, 0U);
    EXPECT_EQ(brokeRides, 0U);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
