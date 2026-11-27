// Buildings and their rooms: what a generated city puts inside its
// buildings - floors, rooms, venues, sorting and findability - and that
// the same seed gives the same city. The style each building is dressed in
// is in CityStyleTests.cpp; the streets, the route planner and the free
// plots of the grid are in CityStreetTests.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::City Generate(std::uint64_t seed, std::uint32_t residents) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed);
    sim::EntityIdAllocator ids;
    return sim::GenerateCity({.residents = residents}, random, ids);
}
}  // namespace

TEST(City, HasRoomForEveryResident) {
    const sim::City city = Generate(11, 250);
    std::uint32_t apartments = 0;
    std::uint32_t jobs = 0;
    for (const sim::Building& building : city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                apartments += room.capacity;
            } else if (room.kind == sim::RoomKind::Workspace) {
                jobs += room.capacity;
            }
        }
    }
    EXPECT_GE(apartments, 250U);
    EXPECT_GE(jobs, 250U);
}

TEST(City, BuildingsHaveFloorsAndRooms) {
    const sim::City city = Generate(12, 300);
    std::set<sim::RoomKind> venueKinds;
    std::set<std::uint64_t> roomIds;
    for (const sim::Building& building : city.buildings) {
        ASSERT_FALSE(building.rooms.empty()) << building.name;
        // Metro depots are workplaces whose rooms are all crew rooms, by
        // design: they carry no building-level shift and their staff follow
        // room-kind rules, so the workplace checks below don't apply.
        const bool depot = building.kind == sim::BuildingKind::Workplace &&
            std::all_of(building.rooms.begin(), building.rooms.end(),
                        [](const sim::Room& room) {
                            return room.kind == sim::RoomKind::CrewRoom;
                        });
        for (const sim::Room& room : building.rooms) {
            EXPECT_TRUE(roomIds.insert(room.id.value).second);
            EXPECT_GT(room.id, building.id);
            EXPECT_LT(room.floor, building.floors) << room.name;
            EXPECT_GT(room.capacity, 0U);
            if (building.kind == sim::BuildingKind::Habitat) {
                // Stores on the ground floor, homes above.
                EXPECT_EQ(room.kind == sim::RoomKind::Apartment,
                          room.floor > 0)
                    << room.name;
                if (sim::IsVenue(room.kind)) {
                    venueKinds.insert(room.kind);
                }
            } else if (depot) {
                EXPECT_EQ(room.kind, sim::RoomKind::CrewRoom);
            } else {
                EXPECT_EQ(room.kind, sim::RoomKind::Workspace);
            }
        }
        if (!depot && building.kind == sim::BuildingKind::Workplace) {
            EXPECT_GT(building.shift.wage, 0U) << building.name;
        }
    }
    // A city this size has every kind of venue.
    EXPECT_EQ(venueKinds.size(), sim::VenueKinds().size());
}

TEST(RoomIndex, FindsRoomsAndTheirBuildings) {
    const sim::City city = Generate(13, 80);
    const sim::RoomIndex index(city);
    const sim::Building& building = city.buildings.back();
    const sim::Room& room = building.rooms.back();
    const auto found = index.Find(room.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->room, &room);
    EXPECT_EQ(found->building, &building);
    EXPECT_FALSE(index.Find(building.id).has_value());
    EXPECT_FALSE(index.RoomsOfKind(sim::RoomKind::Apartment).empty());
}

TEST(Venues, OpeningHoursWrapPastMidnight) {
    const sim::VenueRules& bar = *sim::FindVenueRules(sim::RoomKind::Bar);
    EXPECT_TRUE(sim::IsOpen(bar, 23 * 60));
    EXPECT_TRUE(sim::IsOpen(bar, 60));
    EXPECT_FALSE(sim::IsOpen(bar, 12 * 60));
    EXPECT_EQ(sim::MinutesUntilClose(bar, 23 * 60), 4U * 60U);
    const sim::VenueRules& charging =
        *sim::FindVenueRules(sim::RoomKind::ChargingBay);
    EXPECT_TRUE(sim::IsOpen(charging, 4 * 60));
    EXPECT_EQ(sim::FindVenueRules(sim::RoomKind::Apartment), nullptr);
}

TEST(City, BuildingsAreSortedAndFindable) {
    const sim::City city = Generate(5, 100);
    for (std::size_t index = 1; index < city.buildings.size(); ++index) {
        EXPECT_LT(city.buildings[index - 1].id, city.buildings[index].id);
    }
    const sim::Building& last = city.buildings.back();
    EXPECT_EQ(city.FindBuilding(last.id), &last);
    EXPECT_EQ(city.FindBuilding(sim::EntityId{999999}), nullptr);
}

TEST(City, SameSeedGivesSameCity) {
    const sim::City first = Generate(9, 120);
    const sim::City second = Generate(9, 120);
    ASSERT_EQ(first.edges.size(), second.edges.size());
    ASSERT_EQ(first.buildings.size(), second.buildings.size());
    for (std::size_t index = 0; index < first.buildings.size(); ++index) {
        EXPECT_EQ(first.buildings[index].name, second.buildings[index].name);
        EXPECT_EQ(first.buildings[index].entrance,
                  second.buildings[index].entrance);
    }
}
