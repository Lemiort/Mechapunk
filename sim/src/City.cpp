#include "mechapunk/sim/City.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "mechapunk/sim/Venues.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::uint32_t kUnreachable = std::numeric_limits<std::uint32_t>::max();
// One candidate street segment in this many is considered for removal.
constexpr std::uint32_t kStreetRemovalOdds = 10;
constexpr std::uint32_t kMinApartments = 30;
constexpr std::uint32_t kMaxApartments = 60;
constexpr std::uint32_t kApartmentsPerFloor = 6;
// Daily rent of a habitat's lowest apartments; each floor up costs a credit
// more.
constexpr std::uint32_t kMinBaseRent = 25;
constexpr std::uint32_t kMaxBaseRent = 40;
constexpr std::uint32_t kMinStores = 1;
constexpr std::uint32_t kMaxStores = 3;
constexpr std::uint32_t kMinWorkplaceJobs = 15;
constexpr std::uint32_t kMaxWorkplaceJobs = 40;
constexpr std::uint32_t kMinWorkspaceSize = 5;
constexpr std::uint32_t kMaxWorkspaceSize = 10;
constexpr std::uint32_t kWorkspacesPerFloor = 2;

struct WorkplaceType {
    std::string_view name;
    std::string_view workspace;
    Shift shift;
};

// Shift times in minutes of the day; wages in credits per shift.
constexpr std::array kWorkplaceTypes{
    WorkplaceType{.name = "Foundry",
                  .workspace = "Smelting Bay",
                  .shift = {.startMinute = 6 * 60, .lengthMinutes = 600,
                            .wage = 130}},
    WorkplaceType{.name = "Assembly Line",
                  .workspace = "Assembly Cell",
                  .shift = {.startMinute = 7 * 60, .lengthMinutes = 480,
                            .wage = 100}},
    WorkplaceType{.name = "Repair Shop",
                  .workspace = "Service Bay",
                  .shift = {.startMinute = 8 * 60, .lengthMinutes = 600,
                            .wage = 110}},
    WorkplaceType{.name = "Corp Tower",
                  .workspace = "Office Floor",
                  .shift = {.startMinute = 9 * 60, .lengthMinutes = 540,
                            .wage = 150}},
    WorkplaceType{.name = "Noodle Bar",
                  .workspace = "Kitchen",
                  .shift = {.startMinute = 14 * 60, .lengthMinutes = 480,
                            .wage = 90}},
    WorkplaceType{.name = "Data Farm",
                  .workspace = "Server Hall",
                  .shift = {.startMinute = 22 * 60, .lengthMinutes = 480,
                            .wage = 120}},
};

bool IsConnected(std::uint32_t nodeCount, const std::vector<CityEdge>& edges,
                 const std::vector<bool>& removed) {
    std::vector<std::vector<std::uint32_t>> adjacency(nodeCount);
    for (std::size_t index = 0; index < edges.size(); ++index) {
        if (!removed[index]) {
            adjacency[edges[index].from.value].push_back(edges[index].to.value);
            adjacency[edges[index].to.value].push_back(edges[index].from.value);
        }
    }
    std::vector<bool> seen(nodeCount, false);
    std::vector<std::uint32_t> pending{0};
    seen[0] = true;
    std::uint32_t reached = 1;
    while (!pending.empty()) {
        const std::uint32_t node = pending.back();
        pending.pop_back();
        for (const std::uint32_t next : adjacency[node]) {
            if (!seen[next]) {
                seen[next] = true;
                ++reached;
                pending.push_back(next);
            }
        }
    }
    return reached == nodeCount;
}

NodeId RandomNodeInColumns(const CityParams& params, Pcg32& random,
                           std::uint32_t firstColumn,
                           std::uint32_t lastColumn) {
    const std::uint32_t column = random.UniformInt(firstColumn, lastColumn);
    const std::uint32_t row = random.UniformInt(0, params.gridSize - 1);
    return NodeId{(row * params.gridSize) + column};
}

template <typename T>
const T& Pick(std::span<const T> items, Pcg32& random) {
    return items[random.UniformInt(0, static_cast<std::uint32_t>(items.size() - 1))];
}

// Store kinds in the order habitats get them, the ones robots cannot do
// without first, so even a small city can repair and charge its robots.
constexpr std::array kStoreOrder{
    RoomKind::RepairShop, RoomKind::ChargingBay, RoomKind::Diner,
    RoomKind::Clinic,     RoomKind::Bar,         RoomKind::Arcade,
    RoomKind::Shop,       RoomKind::BlackMarket};

// Ground-floor stores and apartments above. Store kinds rotate through every
// venue kind so each city has some of each.
Building MakeHabitat(const CityParams& params, Pcg32& random,
                     EntityIdAllocator& ids, std::uint32_t number,
                     std::uint32_t& storeRotation) {
    Building building{.id = ids.Allocate(),
                      .name = std::format("Hab Block {}", number),
                      .kind = BuildingKind::Habitat,
                      .entrance = RandomNodeInColumns(params, random, 0,
                                                      params.gridSize / 2),
                      .floors = 1,
                      .shift = {},
                      .rooms = {}};
    const std::uint32_t stores = random.UniformInt(kMinStores, kMaxStores);
    for (std::uint32_t store = 0; store < stores; ++store) {
        const RoomKind kind = kStoreOrder.at(storeRotation++ % kStoreOrder.size());
        const VenueRules& rules = *FindVenueRules(kind);
        building.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::string{Pick(VenueNames(kind), random)},
             .kind = kind,
             .floor = 0,
             .capacity = random.UniformInt(rules.minCapacity,
                                           rules.maxCapacity)});
    }
    const std::uint32_t apartments =
        random.UniformInt(kMinApartments, kMaxApartments);
    const std::uint32_t baseRent = random.UniformInt(kMinBaseRent, kMaxBaseRent);
    for (std::uint32_t index = 0; index < apartments; ++index) {
        const std::uint32_t floor = 1 + (index / kApartmentsPerFloor);
        building.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::format("Apartment {}-{:02}", floor,
                                 (index % kApartmentsPerFloor) + 1),
             .kind = RoomKind::Apartment,
             .floor = floor,
             .capacity = 1,
             .rent = baseRent + floor});
        building.floors = floor + 1;
    }
    return building;
}

Building MakeWorkplace(const CityParams& params, Pcg32& random,
                       EntityIdAllocator& ids,
                       std::array<std::uint32_t, kWorkplaceTypes.size()>& counts) {
    const std::uint32_t typeIndex = random.UniformInt(
        0, static_cast<std::uint32_t>(kWorkplaceTypes.size() - 1));
    const WorkplaceType& type = kWorkplaceTypes.at(typeIndex);
    Building building{
        .id = ids.Allocate(),
        .name = std::format("{} {}", type.name, ++counts.at(typeIndex)),
        .kind = BuildingKind::Workplace,
        .entrance = RandomNodeInColumns(params, random, params.gridSize / 2,
                                        params.gridSize - 1),
        .floors = 1,
        .shift = type.shift,
        .rooms = {}};
    const std::uint32_t jobs =
        random.UniformInt(kMinWorkplaceJobs, kMaxWorkplaceJobs);
    std::uint32_t staffed = 0;
    for (std::uint32_t index = 0; staffed < jobs; ++index) {
        const std::uint32_t size = std::min(
            jobs - staffed,
            random.UniformInt(kMinWorkspaceSize, kMaxWorkspaceSize));
        const std::uint32_t floor = index / kWorkspacesPerFloor;
        building.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::format("{} {}", type.workspace, index + 1),
             .kind = RoomKind::Workspace,
             .floor = floor,
             .capacity = size});
        building.floors = floor + 1;
        staffed += size;
    }
    return building;
}
}  // namespace

const Building* City::FindBuilding(EntityId id) const noexcept {
    const auto found = std::ranges::lower_bound(buildings, id, std::less{},
                                                &Building::id);
    return found != buildings.end() && found->id == id ? &*found : nullptr;
}

RoomIndex::RoomIndex(const City& city) {
    for (const Building& building : city.buildings) {
        for (const Room& room : building.rooms) {
            const RoomRef ref{.building = &building, .room = &room};
            rooms_.emplace(room.id.value, ref);
            byKind_[static_cast<std::uint8_t>(room.kind)].push_back(ref);
        }
    }
}

std::optional<RoomRef> RoomIndex::Find(EntityId room) const {
    const auto found = rooms_.find(room.value);
    if (found == rooms_.end()) {
        return std::nullopt;
    }
    return found->second;
}

const std::vector<RoomRef>& RoomIndex::RoomsOfKind(RoomKind kind) const {
    static const std::vector<RoomRef> kNone;
    const auto found = byKind_.find(static_cast<std::uint8_t>(kind));
    return found != byKind_.end() ? found->second : kNone;
}

City GenerateCity(const CityParams& params, Pcg32& random,
                  EntityIdAllocator& ids) {
    if (params.gridSize < 2 || params.blockMeters == 0) {
        throw std::invalid_argument(
            "A city needs at least a 2x2 grid and non-zero block length");
    }
    const std::uint32_t size = params.gridSize;
    City city;

    for (std::uint32_t row = 0; row < size; ++row) {
        for (std::uint32_t column = 0; column < size; ++column) {
            city.nodes.push_back(
                {.id = NodeId{(row * size) + column},
                 .x = static_cast<std::int32_t>(column * params.blockMeters),
                 .z = static_cast<std::int32_t>(row * params.blockMeters)});
        }
    }

    std::vector<CityEdge> candidates;
    for (std::uint32_t row = 0; row < size; ++row) {
        for (std::uint32_t column = 0; column < size; ++column) {
            const std::uint32_t node = (row * size) + column;
            if (column + 1 < size) {
                candidates.push_back({.from = NodeId{node},
                                      .to = NodeId{node + 1},
                                      .lengthMeters = params.blockMeters});
            }
            if (row + 1 < size) {
                candidates.push_back({.from = NodeId{node},
                                      .to = NodeId{node + size},
                                      .lengthMeters = params.blockMeters});
            }
        }
    }
    // Drop a few segments for less regular routes, keeping the city connected.
    std::vector<bool> removed(candidates.size(), false);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (random.UniformInt(1, kStreetRemovalOdds) == 1) {
            removed[index] = true;
            if (!IsConnected(size * size, candidates, removed)) {
                removed[index] = false;
            }
        }
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!removed[index]) {
            city.edges.push_back(candidates[index]);
        }
    }

    std::uint32_t apartments = 0;
    std::uint32_t habitats = 0;
    std::uint32_t storeRotation = 0;
    while (apartments < params.residents) {
        Building habitat =
            MakeHabitat(params, random, ids, ++habitats, storeRotation);
        apartments += static_cast<std::uint32_t>(std::ranges::count(
            habitat.rooms, RoomKind::Apartment, &Room::kind));
        city.buildings.push_back(std::move(habitat));
    }

    std::array<std::uint32_t, kWorkplaceTypes.size()> typeCounts{};
    std::uint32_t jobs = 0;
    while (jobs < params.residents) {
        Building workplace = MakeWorkplace(params, random, ids, typeCounts);
        for (const Room& room : workplace.rooms) {
            jobs += room.capacity;
        }
        city.buildings.push_back(std::move(workplace));
    }
    return city;
}

RoutePlanner::RoutePlanner(const City& city) : adjacency_(city.nodes.size()) {
    for (const CityEdge& edge : city.edges) {
        adjacency_.at(edge.from.value)
            .push_back({.node = edge.to.value,
                        .lengthMeters = edge.lengthMeters});
        adjacency_.at(edge.to.value)
            .push_back({.node = edge.from.value,
                        .lengthMeters = edge.lengthMeters});
    }
}

std::optional<std::uint32_t> RoutePlanner::DistanceMeters(NodeId from,
                                                          NodeId to) {
    if (from.value >= adjacency_.size() || to.value >= adjacency_.size()) {
        return std::nullopt;
    }
    const std::uint32_t distance = TreeFrom(from.value).distances[to.value];
    if (distance == kUnreachable) {
        return std::nullopt;
    }
    return distance;
}

std::vector<NodeId> RoutePlanner::Path(NodeId from, NodeId to) {
    if (from.value >= adjacency_.size() || to.value >= adjacency_.size()) {
        return {};
    }
    const Tree& tree = TreeFrom(from.value);
    if (tree.distances[to.value] == kUnreachable) {
        return {};
    }
    std::vector<NodeId> path;
    for (std::uint32_t node = to.value; node != from.value;
         node = tree.previous[node]) {
        path.push_back(NodeId{node});
    }
    path.push_back(from);
    std::ranges::reverse(path);
    return path;
}

const RoutePlanner::Tree& RoutePlanner::TreeFrom(std::uint32_t origin) {
    if (const auto cached = cache_.find(origin); cached != cache_.end()) {
        return cached->second;
    }
    // Dijkstra over whole meters; integer arithmetic keeps results identical
    // on every platform. The queue orders equal distances by node index, so
    // the chosen route among equally short ones is always the same.
    std::vector<std::uint32_t> distances(adjacency_.size(), kUnreachable);
    std::vector<std::uint32_t> previous(adjacency_.size(), kUnreachable);
    using Item = std::pair<std::uint32_t, std::uint32_t>;  // distance, node
    std::priority_queue<Item, std::vector<Item>, std::greater<>> frontier;
    distances[origin] = 0;
    frontier.emplace(0, origin);
    while (!frontier.empty()) {
        const auto [distance, node] = frontier.top();
        frontier.pop();
        if (distance > distances[node]) {
            continue;
        }
        for (const Neighbor& neighbor : adjacency_[node]) {
            const std::uint32_t candidate = distance + neighbor.lengthMeters;
            if (candidate < distances[neighbor.node]) {
                distances[neighbor.node] = candidate;
                previous[neighbor.node] = node;
                frontier.emplace(candidate, neighbor.node);
            }
        }
    }
    return cache_
        .emplace(origin, Tree{.distances = std::move(distances),
                              .previous = std::move(previous)})
        .first->second;
}

}  // namespace mechapunk::sim
