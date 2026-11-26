#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
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

namespace {
// The housing trust: the organization of the housing sector. It owns every
// home on the market, and it owns a habitat site (Construction.cpp), which is
// why the property market can reach a half-built tower at all.
sim::EntityId HousingTrust(const sim::WorldState& state) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sim::Sector::Housing) {
            return organization.id;
        }
    }
    return {};
}

// A state written by hand loads as a world; the assertion is what keeps a
// setup that does not validate from reading as a passing test.
sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}

// True when `room` is one of the rooms of `building`; an invalid room id
// answers false.
bool InBuilding(const sim::WorldState& state, sim::EntityId building, sim::EntityId room) {
    for (const sim::Building& each : state.city.buildings) {
        if (each.id != building) {
            continue;
        }
        return std::ranges::any_of(
            each.rooms, [room](const sim::Room& held) { return held.id == room; });
    }
    return false;
}

// A habitat building with a home nobody lives in, and that home.
std::pair<sim::EntityId, sim::EntityId> HabitatAndVacantHome(const sim::WorldState& state) {
    std::set<std::uint64_t> homes;
    for (const sim::Robot& robot : state.robots) {
        homes.insert(robot.apartment.value);
    }
    for (const sim::Building& building : state.city.buildings) {
        if (building.kind != sim::BuildingKind::Habitat) {
            continue;
        }
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment && !homes.contains(room.id.value)) {
                return {building.id, room.id};
            }
        }
    }
    return {};
}

// Puts a building on a site: a project raises it for `nights` more nights,
// owned by `owner`. The cities these tests write have no project yet - the
// shortage that starts one is read off seven saved days - so the list stays
// sorted by building id, as validation asks.
void Raise(sim::WorldState& state, sim::EntityId building, sim::EntityId owner,
           std::uint32_t nights) {
    state.projects.push_back({
        .building = building,
        .owner = owner,
        .daysLeft = nights,
        .floorsDone = 1,
    });
}

// The rent `RentFor` (Units.cpp) reads a home at: its daily rent under the
// saved market percent of its class.
std::int64_t RentPrice(const sim::WorldState& state, const sim::Room& room) {
    const std::size_t kind = static_cast<std::size_t>(room.apartmentClass);
    if (kind < 2) {
        return static_cast<std::int64_t>(room.rent);
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(room.rent) *
                                        state.economy.rentPercent.at(kind - 2) / 100);
}
}  // namespace

// A home a robot buys is a finished one: the mortgage market of `TradeProperty`
// (Property.cpp) skips the apartments of a building a project is raising, the
// way `ReviewHomes` (step D2c) and a newcomer's choice of home (D2c2) do.
TEST(Housing, BuyersSkipSiteHomes) {
    sim::World start = sim::World::Generate({.seed = 5, .robots = 1000});
    start.RunFor(sim::kMinutesPerDay);
    sim::WorldState state = start.State();
    const sim::EntityId trust = HousingTrust(state);
    ASSERT_TRUE(trust.IsValid());
    const auto [site, free] = HabitatAndVacantHome(state);
    ASSERT_TRUE(site.IsValid()) << "the city has a habitat with a free home";
    ASSERT_TRUE(free.IsValid()) << "that habitat has a home nobody lives in";
    Raise(state, site, trust, 30);  // Still going up for every night below.
    // The site's free homes one place there are become the dearest such homes
    // in the city, a hair above the dearest finished one, so a robot that can
    // afford one buys there: with the skip out, the market filling up over
    // these nights means this building is bought into.
    std::int64_t dearest = 0;
    for (const sim::Building& building : state.city.buildings) {
        if (building.id == site) {
            continue;
        }
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment &&
                sim::Sleeps(room.apartmentClass) == 1 && room.owner == trust) {
                dearest = std::max(dearest, RentPrice(state, room));
            }
        }
    }
    for (sim::Building& building : state.city.buildings) {
        if (building.id != site) {
            continue;
        }
        std::set<std::uint64_t> lived;
        for (const sim::Robot& robot : state.robots) {
            lived.insert(robot.apartment.value);
        }
        for (sim::Room& room : building.rooms) {
            if (room.kind != sim::RoomKind::Apartment || sim::Sleeps(room.apartmentClass) != 1 ||
                lived.contains(room.id.value)) {
                continue;
            }
            const std::size_t kind = static_cast<std::size_t>(room.apartmentClass);
            const std::int64_t percent = kind < 2 ? 1 : state.economy.rentPercent.at(kind - 2);
            room.rent = static_cast<std::uint32_t>((dearest + 5) * 100 / percent + 1);
        }
    }
    // Robots with plenty saved, so the market really is bought into: the
    // buy is a three per cent draw a night, and these nights must not run
    // out of buyers. Gangs are disabled so robbery cannot drain the savings.
    for (std::size_t index = 0; index < state.robots.size(); index += 10) {
        sim::SetCredits(state, state.robots[index], 20000);
    }
    state.gangs.clear();
    for (sim::Robot& robot : state.robots) {
        robot.gang = sim::EntityId{0};
    }
    sim::World world = Load(std::move(state));
    std::uint64_t sales = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::BoughtProperty) {
            ++sales;
        }
    });
    for (int night = 0; night < 20; ++night) {
        world.RunFor(sim::kMinutesPerDay);
    }
    // The market was bought into, and none of it onto the site.
    EXPECT_GT(sales, 0U) << "no robot bought a home in twenty nights";
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_FALSE(InBuilding(world.State(), site, robot.apartment))
            << "a robot bought into a building going up";
    }
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
