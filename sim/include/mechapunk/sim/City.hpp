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

// Daily working hours of a workplace.
struct Shift {
    std::uint32_t startMinute{};  // Minute of the day the shift starts.
    std::uint32_t lengthMinutes{};
};

struct Building {
    EntityId id;
    std::string name;
    BuildingKind kind{BuildingKind::Habitat};
    NodeId entrance;
    // Residents for a habitat, jobs for a workplace.
    std::uint32_t capacity{};
    // Used by workplaces only.
    Shift shift;
};

// Saved city layout. Buildings are kept sorted by id.
struct City {
    std::vector<CityNode> nodes;
    std::vector<CityEdge> edges;
    std::vector<Building> buildings;

    [[nodiscard]] const Building* FindBuilding(EntityId id) const noexcept;
};

struct CityParams {
    // Intersections per side of the square street grid.
    std::uint32_t gridSize{8};
    std::uint32_t blockMeters{120};
    // Habitats and workplaces are added until both can hold this many.
    std::uint32_t residents{100};
};

// Allocates entity ids; the counter is saved with the world.
struct EntityIdAllocator {
    std::uint64_t next{1};

    EntityId Allocate() noexcept { return EntityId{next++}; }
};

// Builds a connected street grid with a few missing segments, residential
// habitats concentrated in the west and workplaces in the east.
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
