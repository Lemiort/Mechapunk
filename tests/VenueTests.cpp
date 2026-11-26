// Ground-floor units: venue prices following demand, quiet venues closing,
// empty units opening again. The tests of `Units.cpp`, split out of
// `HousingTests.cpp` (task D, step D2c3) so the homes' tests and the
// property market have room in their module.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

// A venue that filled up three times over asks more the next day; one that
// had fewer guests than places asks less.
TEST(Housing, VenuePricesFollowDemand) {
    sim::World start = sim::World::Generate({.seed = 6, .robots = 400});
    start.RunFor(sim::kMinutesPerDay - 1);
    sim::WorldState state = start.State();
    sim::Room* busy = nullptr;
    sim::Room* quiet = nullptr;
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& room : building.rooms) {
            if (sim::IsVenue(room.kind)) {
                (busy == nullptr ? busy : quiet) = &room;
                if (quiet != nullptr) {
                    break;
                }
            }
        }
        if (quiet != nullptr) {
            break;
        }
    }
    ASSERT_NE(quiet, nullptr);
    busy->visits = busy->capacity * 4;
    quiet->visits = 0;
    const sim::EntityId busyId = busy->id;
    const sim::EntityId quietId = quiet->id;
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(1);
    EXPECT_GT(world->Rooms().Find(busyId)->room->pricePercent, 100U);
    EXPECT_LT(world->Rooms().Find(quietId)->room->pricePercent, 100U);
    EXPECT_EQ(world->Rooms().Find(busyId)->room->visits, 0U);
}

namespace {
// A ground-floor venue nobody is in or on the way to, as an index into
// state.city.buildings and its rooms.
std::optional<std::pair<std::size_t, std::size_t>> EmptyVenue(const sim::WorldState& state) {
    std::set<std::uint64_t> busy;
    for (const sim::Robot& robot : state.robots) {
        busy.insert(robot.location.value);
        busy.insert(robot.destination.value);
    }
    for (std::size_t b = 0; b < state.city.buildings.size(); ++b) {
        const auto& rooms = state.city.buildings[b].rooms;
        for (std::size_t r = 0; r < rooms.size(); ++r) {
            if (sim::IsVenue(rooms[r].kind) && !busy.contains(rooms[r].id.value)) {
                return std::pair{b, r};
            }
        }
    }
    return std::nullopt;
}
}  // namespace

// A venue that has stood quiet for ten nights, its prices cut, closes, and
// its staff are laid off.
TEST(Housing, QuietVenuesClose) {
    sim::World start = sim::World::Generate({.seed = 6, .robots = 400});
    start.RunFor(sim::kMinutesPerDay - 1);
    sim::WorldState state = start.State();
    const auto found = EmptyVenue(state);
    ASSERT_TRUE(found.has_value());
    sim::Room& venue = state.city.buildings[found->first].rooms[found->second];
    venue.pricePercent = 50;
    venue.quietNights = 9;
    venue.visits = 0;
    const sim::EntityId id = venue.id;
    std::vector<sim::EntityId> staff;
    for (const sim::Robot& robot : state.robots) {
        if (robot.workspace == id) {
            staff.push_back(robot.id);
        }
    }
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(1);
    EXPECT_EQ(world->Rooms().Find(id)->room->kind, sim::RoomKind::VacantStore);
    EXPECT_GT(world->CityVersion(), 0U);
    for (const sim::EntityId robot : staff) {
        EXPECT_NE(world->FindRobot(robot)->workspace, id);
    }
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// An empty unit opens again, as the kind of venue the city is short of;
// while no police station exists yet and the population can support one
// (stations * 2000 < population), City Hall takes the unit first.
TEST(Housing, EmptyUnitsOpenAsVenuesInDemand) {
    sim::World start = sim::World::Generate({.seed = 6, .robots = 400});
    start.RunFor(sim::kMinutesPerDay - 1);
    sim::WorldState state = start.State();
    const auto found = EmptyVenue(state);
    ASSERT_TRUE(found.has_value());
    sim::Room& unit = state.city.buildings[found->first].rooms[found->second];
    for (sim::Robot& robot : state.robots) {
        if (robot.workspace == unit.id) {
            robot.workspace = {};
            robot.commuteAt = sim::kNoCommute;
        }
    }
    unit.kind = sim::RoomKind::VacantStore;
    // Bars are packed: their prices are high everywhere.
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Bar) {
                room.pricePercent = 150;
            }
        }
    }
    const sim::EntityId id = unit.id;
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    for (int night = 0; night < 60 && world->Rooms().Find(id)->room->kind == sim::RoomKind::VacantStore;
         ++night) {
        world->RunFor(sim::kMinutesPerDay);
    }
    const sim::RoomKind kind = world->Rooms().Find(id)->room->kind;
    // At this city size (400 robots) the station rule fires before any venue
    // opens, so a police station is an expected outcome as well.
    EXPECT_TRUE(sim::IsVenue(kind) || kind == sim::RoomKind::PoliceStation);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}
