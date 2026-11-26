#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// An entrance to the underground metro at a street corner. Trains run
// between every pair of stations.
struct Station {
    NodeId node;
    std::string name;
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
    Clinic,       // Major repairs; also undoes wear.
    BlackMarket,  // Cheap repairs at night, sometimes with a dodgy part.
    // A ground-floor unit standing empty: a venue that closed, or a
    // workspace given up, until someone opens it again.
    VacantStore,
    PoliceStation,
    CrewRoom,
    // The office of a building going up: the builders' job while the
    // project is up, a vacant unit once it is done (task D, step D3).
    SiteOffice,
};

// Classes of apartment, from a government dorm room to a whole floor. Each
// is bigger and dearer than the one before; a habitat floor holds
// apartments of one class.
enum class ApartmentClass : std::uint8_t {
    None,  // Not an apartment.
    Dorm,
    Studio,
    OneBedroom,
    TwoBedroom,
    Penthouse,
};

// "dorm room", "studio", "one-bedroom flat", ...
[[nodiscard]] std::string_view ApartmentClassName(ApartmentClass kind) noexcept;
// Apartments of a class on one floor: 12 dorm rooms down to 1 penthouse.
[[nodiscard]] std::uint32_t ApartmentsPerFloor(ApartmentClass kind) noexcept;
// Robots an apartment sleeps: one a bedroom; dorm rooms and studios one.
[[nodiscard]] std::uint32_t Sleeps(ApartmentClass kind) noexcept;
// Floor area in square meters.
[[nodiscard]] std::uint32_t AreaOf(ApartmentClass kind) noexcept;

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
    // Credits charged every midnight; apartments only.
    std::uint32_t rent{};
    // The robot or organization it belongs to.
    EntityId owner;
    // Apartments only.
    ApartmentClass apartmentClass{ApartmentClass::None};
    // Venues only: the price asked, in percent of the kind's base price,
    // and guests since midnight, by which the price is set each night.
    std::uint32_t pricePercent{100};
    std::uint32_t visits{};
    // Nights in a row a venue has stood quiet with its prices cut, or a
    // workspace under half staffed; it closes after enough of them. For an
    // empty unit, nights it has stood empty.
    std::uint32_t quietNights{};
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

// An intersection has room for four buildings, one on each corner; the
// view numbers those corners 0 to 3, and generation never puts a fifth
// building at a node.
inline constexpr std::uint32_t kCornersPerNode = 4;

// A free building plot: a corner of a street intersection with no building
// on it. `corner` is the slot the view numbers an intersection's four
// corners, and the buildings at a node take the first slots in id order,
// so a plot is a slot no building of that node has. The view draws a park
// there; construction raises a building on it.
struct CityLot {
    NodeId node;
    std::uint32_t corner{};

    bool operator==(const CityLot&) const = default;
};

// Saved city layout. Buildings are kept sorted by id.
struct City {
    std::vector<CityNode> nodes;
    std::vector<CityEdge> edges;
    std::vector<Building> buildings;
    std::vector<Station> stations;
    // Every free plot, sorted by node and then corner: the corners of
    // each node that no building stands on, out of `kCornersPerNode`
    // corners per node.
    std::vector<CityLot> lots;

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
    // Intersections per side of the square street grid; 0 sizes the grid to
    // the population, so buildings fill about half of the street corners.
    std::uint32_t gridSize{0};
    std::uint32_t blockMeters{80};
    // Apartments and workspaces are added until both hold this many.
    std::uint32_t residents{100};
};

// Allocates entity ids; the counter is saved with the world.
struct EntityIdAllocator {
    std::uint64_t next{1};

    EntityId Allocate() noexcept { return EntityId{next++}; }
};

// Builds a connected street grid with a few missing segments, and a metro
// station at every second intersection each way. Every intersection has
// room for four buildings, one on each corner. Buildings rise toward the
// middle of a big city, up to about thirty floors; towns stay low. Habitats,
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
