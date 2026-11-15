#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Random.hpp"

namespace mechapunk::sim {

// Walkable point of the city: a street intersection that buildings open
// onto. Coordinates are whole meters on the ground plane (x east, z south).
struct CityNode {
    NodeId id;
    std::int32_t x{};
    std::int32_t z{};
};

// Undirected street segment between two nodes.
struct CityEdge {
    NodeId from;
    NodeId to;
    std::uint32_t lengthMeters{};
};

enum class BuildingKind : std::uint8_t { Habitat, Workplace };

enum class RoomKind : std::uint8_t {
    Apartment,
    Workspace,
    // Venues robots visit after work; see Venues.hpp.
    Bar,
    Diner,
    ChargingBay,
    RepairShop,
    Arcade,
    Shop,
};

// One room on one floor. Ids come from the world's entity counter, so a room
// can be referenced directly, and robots always know which room they are in.
struct Room {
    EntityId id;
    std::string name;
    RoomKind kind{RoomKind::Apartment};
    // 0 is the ground floor.
    std::uint32_t floor{};
    // Residents of an apartment, workers of a workspace, guests of a venue.
    std::uint32_t capacity{};
};

// Daily working hours of a workplace.
struct Shift {
    std::uint32_t startMinute{};  // Minute of the day the shift starts.
    std::uint32_t lengthMinutes{};
    std::uint32_t wage{};  // Credits paid at the end of each shift.
};

struct Building {
    EntityId id;
    std::string name;
    BuildingKind kind{BuildingKind::Habitat};
    NodeId entrance;
    std::uint32_t floors{1};
    // Used by workplaces only.
    Shift shift;
    // Sorted by id.
    std::vector<Room> rooms;
};

// Saved city layout. Buildings are kept sorted by id.
struct City {
    std::vector<CityNode> nodes;
    std::vector<CityEdge> edges;
    std::vector<Building> buildings;

    [[nodiscard]] const Building* FindBuilding(EntityId id) const noexcept;
};

// A room together with the building that contains it.
struct RoomRef {
    const Building* building{};
    const Room* room{};
};

// Lookup from room ids to rooms. Derived from a City and never saved; it
// points into the City, which must outlive it and not change.
class RoomIndex {
public:
    explicit RoomIndex(const City& city);
    [[nodiscard]] std::optional<RoomRef> Find(EntityId room) const;
    // Every room of `kind`, in id order.
    [[nodiscard]] const std::vector<RoomRef>& RoomsOfKind(RoomKind kind) const;

private:
    std::unordered_map<std::uint64_t, RoomRef> rooms_;
    std::unordered_map<std::uint8_t, std::vector<RoomRef>> byKind_;
};

struct CityParams {
    // Intersections per side of the square street grid.
    std::uint32_t gridSize{8};
    std::uint32_t blockMeters{120};
    // Apartments and workspaces are added until both hold this many.
    std::uint32_t residents{100};
};

// Allocates entity ids; the counter is saved with the world.
struct EntityIdAllocator {
    std::uint64_t next{1};

    EntityId Allocate() noexcept { return EntityId{next++}; }
};

// Builds a connected street grid with a few missing segments. Habitats,
// concentrated in the west, have stores on the ground floor and apartments
// above; workplaces in the east are split into workspaces over their floors.
[[nodiscard]] City GenerateCity(const CityParams& params, Pcg32& random,
                                EntityIdAllocator& ids);

// Shortest walking distances over the street graph. Derived from a City and
// never saved; results are cached per origin node.
class RoutePlanner {
public:
    explicit RoutePlanner(const City& city);

    // Nothing when `to` cannot be reached from `from`.
    [[nodiscard]] std::optional<std::uint32_t> DistanceMeters(NodeId from,
                                                              NodeId to);
    // Nodes of a shortest route, from `from` to `to` inclusive. Empty when
    // unreachable. Ties between equally short routes are broken the same way
    // on every run.
    [[nodiscard]] std::vector<NodeId> Path(NodeId from, NodeId to);

private:
    struct Neighbor {
        std::uint32_t node{};
        std::uint32_t lengthMeters{};
    };
    struct Tree {
        std::vector<std::uint32_t> distances;
        std::vector<std::uint32_t> previous;
    };
    const Tree& TreeFrom(std::uint32_t origin);

    std::vector<std::vector<Neighbor>> adjacency_;
    std::unordered_map<std::uint32_t, Tree> cache_;
};

}  // namespace mechapunk::sim
