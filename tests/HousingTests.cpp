#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <set>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

// A new city has a dorm bed for every robot and homes on the market for
// most of them; each habitat floor holds one class of apartment, every
// class bigger and dearer than the one before, and the middle of a big city
// has penthouses.
TEST(Housing, CitiesHaveEveryClassOfHome) {
    const sim::World world = sim::World::Generate({.seed = 2, .robots = 3000});
    std::map<sim::ApartmentClass, std::uint64_t> beds;
    std::map<sim::ApartmentClass, std::uint64_t> rent;
    std::map<sim::ApartmentClass, std::uint64_t> homes;
    for (const sim::Building& building : world.State().city.buildings) {
        std::map<std::uint32_t, std::set<sim::ApartmentClass>> floors;
        for (const sim::Room& room : building.rooms) {
            if (room.kind != sim::RoomKind::Apartment) {
                EXPECT_EQ(room.apartmentClass, sim::ApartmentClass::None);
                continue;
            }
            floors[room.floor].insert(room.apartmentClass);
            beds[room.apartmentClass] += room.capacity;
            rent[room.apartmentClass] += room.rent;
            ++homes[room.apartmentClass];
            EXPECT_EQ(room.capacity, sim::Sleeps(room.apartmentClass));
        }
        for (const auto& [floor, classes] : floors) {
            EXPECT_EQ(classes.size(), 1U) << building.name << " floor " << floor;
            EXPECT_LE(std::ranges::count_if(building.rooms, [&](const sim::Room& room) {
                          return room.floor == floor && room.kind == sim::RoomKind::Apartment;
                      }),
                      sim::ApartmentsPerFloor(*classes.begin()));
        }
    }
    EXPECT_GE(beds[sim::ApartmentClass::Dorm], 3000U);
    EXPECT_GE(beds[sim::ApartmentClass::Studio] + beds[sim::ApartmentClass::OneBedroom] +
                  beds[sim::ApartmentClass::TwoBedroom] + beds[sim::ApartmentClass::Penthouse],
              1800U);
    EXPECT_GT(homes[sim::ApartmentClass::Penthouse], 0U);
    // Average rent and area grow with the class.
    std::uint64_t lastRent = 0;
    std::uint32_t lastArea = 0;
    for (const sim::ApartmentClass kind :
         {sim::ApartmentClass::Dorm, sim::ApartmentClass::Studio, sim::ApartmentClass::OneBedroom,
          sim::ApartmentClass::TwoBedroom, sim::ApartmentClass::Penthouse}) {
        ASSERT_GT(homes[kind], 0U);
        const std::uint64_t average = rent[kind] / homes[kind];
        EXPECT_GT(average, lastRent) << sim::ApartmentClassName(kind);
        EXPECT_GT(sim::AreaOf(kind), lastArea);
        lastRent = average;
        lastArea = sim::AreaOf(kind);
    }
}

// Friends who rent move in together, into a flat with a bedroom each,
// remember doing so, and split its rent.
TEST(Housing, FriendsShareFlats) {
    sim::World world = sim::World::Generate({.seed = 4, .robots = 1000});
    std::map<std::uint64_t, std::int64_t> rentPaid;
    world.RunFor(20 * sim::kMinutesPerDay);
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::RentPaid) {
            rentPaid[event.robot.value] = event.credits;
        }
    });
    world.RunFor(sim::kMinutesPerDay);
    std::int64_t movedIn = 0;
    for (const sim::DailyStats& day : world.State().history) {
        movedIn += day.counted.movedInTogether;
    }
    EXPECT_GT(movedIn, 0);
    std::map<std::uint64_t, std::vector<const sim::Robot*>> homes;
    for (const sim::Robot& robot : world.State().robots) {
        homes[robot.apartment.value].push_back(&robot);
    }
    std::size_t shared = 0;
    for (const auto& [apartment, residents] : homes) {
        if (residents.size() < 2) {
            continue;
        }
        ++shared;
        const sim::Room& flat = *world.Rooms().Find(sim::EntityId{apartment})->room;
        EXPECT_GE(sim::Sleeps(flat.apartmentClass), residents.size()) << flat.name;
        for (const sim::Robot* robot : residents) {
            if (flat.owner == robot->id || rentPaid[robot->id.value] == 0 || robot->rentOwed > 0) {
                continue;  // Owners pay upkeep; and those short tonight paid less.
            }
            // Its share of tonight's market rent.
            const auto kind = static_cast<std::size_t>(flat.apartmentClass);
            const std::int64_t rent =
                static_cast<std::int64_t>(flat.rent) * world.State().economy.rentPercent.at(kind - 2) / 100;
            const auto sharers = static_cast<std::int64_t>(residents.size());
            // The market rent moved a point after the rent was paid.
            EXPECT_NEAR(static_cast<double>(rentPaid[robot->id.value]),
                        static_cast<double>((rent + sharers - 1) / sharers),
                        static_cast<double>(flat.rent) / 100.0 + 1.0)
                << robot->name;
        }
    }
    EXPECT_GT(shared, 0U);
}

// A new city's market homes mostly stand empty, so their rents fall night
// by night while dorm rents stay where the housing office set them.
TEST(Housing, RentsFallWhileHomesStandEmpty) {
    sim::World world = sim::World::Generate({.seed = 6, .robots = 400});
    world.RunFor(5 * sim::kMinutesPerDay);
    const auto& history = world.State().history;
    ASSERT_EQ(history.size(), 5U);
    // Studios and one-bedroom flats, which every town has.
    EXPECT_LT(world.State().economy.rentPercent[0], 100);
    EXPECT_LT(world.State().economy.rentPercent[1], 100);
    EXPECT_LT(history.back().averageRent[1], history.front().averageRent[1]);
    EXPECT_EQ(history.back().averageRent[0], history.front().averageRent[0]);
}

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

// An empty unit opens again, as the kind of venue the city is short of.
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
    EXPECT_TRUE(sim::IsVenue(world->Rooms().Find(id)->room->kind));
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}
