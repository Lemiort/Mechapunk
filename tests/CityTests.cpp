#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

#include "mechapunk/sim/World.hpp"

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

TEST(City, EveryBuildingIsReachable) {
    const sim::City city = Generate(3, 300);
    sim::RoutePlanner routes(city);
    const sim::NodeId origin = city.buildings.front().entrance;
    for (const sim::Building& building : city.buildings) {
        EXPECT_TRUE(routes.DistanceMeters(origin, building.entrance))
            << building.name;
    }
    // A few streets were removed, but never so many that nodes are cut off.
    for (const sim::CityNode& node : city.nodes) {
        EXPECT_TRUE(routes.DistanceMeters(sim::NodeId{0}, node.id));
    }
}

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

TEST(RoutePlanner, FindsShortestGridDistance) {
    sim::City city;
    for (std::uint32_t index = 0; index < 4; ++index) {
        city.nodes.push_back({.id = sim::NodeId{index}, .x = 0, .z = 0});
    }
    // 0 - 1 - 2 is 200 m; the direct 0 - 2 street is 500 m; 3 is isolated.
    city.edges = {{.from = sim::NodeId{0}, .to = sim::NodeId{1},
                   .lengthMeters = 100},
                  {.from = sim::NodeId{1}, .to = sim::NodeId{2},
                   .lengthMeters = 100},
                  {.from = sim::NodeId{0}, .to = sim::NodeId{2},
                   .lengthMeters = 500}};
    sim::RoutePlanner routes(city);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{2}), 200U);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{2}, sim::NodeId{0}), 200U);
    EXPECT_EQ(routes.DistanceMeters(sim::NodeId{1}, sim::NodeId{1}), 0U);
    EXPECT_FALSE(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{3}));
    EXPECT_FALSE(routes.DistanceMeters(sim::NodeId{0}, sim::NodeId{9}));

    const std::vector<sim::NodeId> expected{sim::NodeId{0}, sim::NodeId{1},
                                            sim::NodeId{2}};
    EXPECT_EQ(routes.Path(sim::NodeId{0}, sim::NodeId{2}), expected);
    EXPECT_EQ(routes.Path(sim::NodeId{1}, sim::NodeId{1}),
              std::vector<sim::NodeId>{sim::NodeId{1}});
    EXPECT_TRUE(routes.Path(sim::NodeId{0}, sim::NodeId{3}).empty());
}

// The view places a building on one of the four corners of its entrance
// node, and pushes a fifth building out to a far ring where it overlaps a
// neighbour. Generation must therefore never put more than four buildings
// at one intersection, depots included.
TEST(City, NoIntersectionHasMoreThanFourBuildings) {
    for (const std::uint64_t seed : {1U, 7U, 33U}) {
        for (const std::uint32_t residents : {100U, 1000U, 4000U, 10000U}) {
            const sim::World world = sim::World::Generate(
                {.seed = seed, .robots = residents});
            std::unordered_map<std::uint64_t, std::uint32_t> at;
            for (const sim::Building& building : world.State().city.buildings) {
                ++at[building.entrance.value];
            }
            for (const auto& [node, count] : at) {
                EXPECT_LE(count, 4U) << "seed=" << seed
                                     << " residents=" << residents
                                     << " node=" << node;
            }
        }
    }
}

// A free plot is a corner of an intersection no building stands on: the
// view draws a park there, and construction raises a building on it, so
// the saved list must hold exactly the corners that are free - a node's
// slots after its buildings - sorted by node and then corner.
TEST(City, LotsAreTheFreeCornersOfTheGrid) {
    for (const std::uint64_t seed : {1U, 7U, 33U}) {
        for (const std::uint32_t residents : {100U, 1000U, 10000U}) {
            const sim::City city = Generate(seed, residents);
            std::vector<std::uint32_t> taken(city.nodes.size(), 0);
            for (const sim::Building& building : city.buildings) {
                ++taken.at(building.entrance.value);
            }
            std::vector<sim::CityLot> expected;
            for (std::size_t node = 0; node < taken.size(); ++node) {
                for (std::uint32_t corner = taken.at(node);
                     corner < sim::kCornersPerNode; ++corner) {
                    expected.push_back({.node = sim::NodeId{static_cast<std::uint32_t>(node)},
                                        .corner = corner});
                }
            }
            EXPECT_EQ(city.lots, expected)
                << "seed=" << seed << " residents=" << residents;
            EXPECT_FALSE(city.lots.empty());
        }
    }
}

TEST(RoutePlanner, PathLengthMatchesDistance) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(4);
    sim::EntityIdAllocator ids;
    const sim::City city = sim::GenerateCity({.residents = 80}, random, ids);
    sim::RoutePlanner routes(city);
    const auto edgeLength = [&](sim::NodeId a, sim::NodeId b) {
        for (const sim::CityEdge& edge : city.edges) {
            if ((edge.from == a && edge.to == b) ||
                (edge.from == b && edge.to == a)) {
                return edge.lengthMeters;
            }
        }
        ADD_FAILURE() << "no street between " << a.value << " and " << b.value;
        return 0U;
    };
    const sim::NodeId origin = city.buildings.front().entrance;
    for (const sim::Building& building : city.buildings) {
        const auto path = routes.Path(origin, building.entrance);
        ASSERT_FALSE(path.empty());
        std::uint32_t length = 0;
        for (std::size_t index = 1; index < path.size(); ++index) {
            length += edgeLength(path[index - 1], path[index]);
        }
        EXPECT_EQ(length, routes.DistanceMeters(origin, building.entrance));
    }
}
