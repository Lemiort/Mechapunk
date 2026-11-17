#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
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

// Two friends who rent and can afford it move into a flat with a bedroom
// each, and split its rent.
TEST(Housing, FriendsMoveInTogether) {
    sim::World start = sim::World::Generate({.seed = 4, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));
    sim::WorldState state = start.State();
    std::vector<std::size_t> resting;
    for (std::size_t index = 0; index < state.robots.size() && resting.size() < 2; ++index) {
        if (state.robots[index].activity == sim::RobotActivity::Resting) {
            resting.push_back(index);
        }
    }
    ASSERT_EQ(resting.size(), 2U);
    sim::Robot& first = state.robots[resting[0]];
    sim::Robot& second = state.robots[resting[1]];
    const auto befriend = [&](sim::Robot& robot, const sim::Robot& other) {
        robot.bonds.clear();  // Each other's only friend.
        robot.bonds.push_back({.robot = other.id,
                               .affinity = sim::Bond::kMaxAffinity,
                               .level = sim::BondLevel::CloseFriend,
                               .lastTalk = state.tick});
        std::ranges::sort(robot.bonds, {}, [](const sim::Bond& bond) { return bond.robot.value; });
    };
    befriend(first, second);
    befriend(second, first);
    // Out of work, so no bank gives them a mortgage, and with enough for a
    // share of a flat's rent for a while but not to buy one.
    for (sim::Robot* robot : {&first, &second}) {
        sim::SetCredits(state, *robot, 1500);
        robot->savings = 0;
        robot->workspace = {};
        robot->commuteAt = sim::kNoCommute;
    }
    const sim::EntityId a = first.id;
    const sim::EntityId b = second.id;
    auto loaded = sim::World::FromState(std::move(state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    for (int day = 0; day < 30 && world.FindRobot(a)->apartment != world.FindRobot(b)->apartment;
         ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    const sim::Robot& one = *world.FindRobot(a);
    const sim::Robot& two = *world.FindRobot(b);
    ASSERT_EQ(one.apartment, two.apartment);
    const sim::Room& flat = *world.Rooms().Find(one.apartment)->room;
    EXPECT_GE(sim::Sleeps(flat.apartmentClass), 2U);
    bool remembers = false;
    for (const sim::Memory& memory : one.memories) {
        remembers = remembers || (memory.kind == sim::MemoryKind::MovedInTogether && memory.robot == b);
    }
    EXPECT_TRUE(remembers);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
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
