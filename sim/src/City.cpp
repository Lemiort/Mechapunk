#include "mechapunk/sim/City.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "CityInternal.hpp"
#include "mechapunk/sim/Metro.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk::sim {

using namespace city_build;

namespace {
constexpr std::uint32_t kUnreachable = std::numeric_limits<std::uint32_t>::max();

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

}  // namespace

namespace city_build {

// An intersection in the given columns with a free corner, if one can be
// found in a few tries; its corner is then taken.
NodeId RandomNodeInColumns(const CityParams& params, Pcg32& random,
                           std::uint32_t firstColumn, std::uint32_t lastColumn,
                           std::vector<std::uint32_t>& buildingsAt) {
    constexpr std::uint32_t kCorners = 4;
    constexpr int kTries = 32;
    NodeId node{};
    bool found = false;
    for (int attempt = 0; attempt < kTries; ++attempt) {
        const std::uint32_t column = random.UniformInt(firstColumn, lastColumn);
        const std::uint32_t row = random.UniformInt(0, params.gridSize - 1);
        node = NodeId{(row * params.gridSize) + column};
        if (buildingsAt.at(node.value) < kCorners) {
            found = true;
            break;
        }
    }
    // Random probing can miss the last few free corners. Never take an
    // already full node: a fifth building there would be drawn outside the
    // block, on top of a neighbouring building's corner.
    if (!found) {
        for (std::uint32_t row = 0; row < params.gridSize && !found; ++row) {
            for (std::uint32_t column = firstColumn; column <= lastColumn; ++column) {
                const NodeId candidate{(row * params.gridSize) + column};
                if (buildingsAt.at(candidate.value) < kCorners) {
                    node = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        throw std::logic_error("Generated city has no free corners");
    }
    ++buildingsAt.at(node.value);
    return node;
}

// How close an intersection is to the middle of the grid: 1 at the middle,
// 0 at the corners.
double Centrality(NodeId node, std::uint32_t gridSize) {
    const double half = (static_cast<double>(gridSize) - 1.0) / 2.0;
    if (half <= 0.0) {
        return 1.0;
    }
    const double dx = static_cast<double>(node.value % gridSize) - half;
    const double dz = static_cast<double>(node.value / gridSize) - half;
    return std::clamp(1.0 - (std::sqrt((dx * dx) + (dz * dz)) / (half * std::numbers::sqrt2)),
                      0.0, 1.0);
}

// How much a city of `residents` rises in the middle: towns of up to
// kTownResidents keep every building low; from kCityResidents up the middle
// reaches full height.
constexpr double kTownResidents = 500.0;
constexpr double kCityResidents = 6000.0;
double Urbanity(std::uint32_t residents) {
    return std::clamp((static_cast<double>(residents) - kTownResidents) /
                          (kCityResidents - kTownResidents),
                      0.0, 1.0);
}

// A whole number between `edge` and `central` by centrality.
std::uint32_t Lerp(std::uint32_t edge, std::uint32_t central, double centrality) {
    return edge + static_cast<std::uint32_t>(
                      std::lround(static_cast<double>(central - edge) * centrality));
}

}  // namespace city_build

namespace {

// The smallest grid of at least kMinGridSize on which the buildings for
// `residents` take about kBuildingsPerNode of every intersection's corners.
std::uint32_t GridSizeFor(std::uint32_t residents, const CitySettings& cfg) {
    constexpr std::uint32_t kMinGridSize = 4;
    // Sizes at the average centrality of a grid's intersections, about 0.45.
    const double averageCentrality = 0.45 * Urbanity(residents);
    const double averageFloors =
        ((static_cast<std::uint32_t>(cfg.minFloors) + static_cast<std::uint32_t>(cfg.maxFloors)) / 2.0) +
        ((((static_cast<std::uint32_t>(cfg.minCentralFloors) + static_cast<std::uint32_t>(cfg.maxCentralFloors)) - (static_cast<std::uint32_t>(cfg.minFloors) + static_cast<std::uint32_t>(cfg.maxFloors))) / 2.0) *
         averageCentrality);
    const double averageJobs =
        ((static_cast<std::uint32_t>(cfg.minWorkplaceJobs) + static_cast<std::uint32_t>(cfg.maxWorkplaceJobs)) / 2.0) +
        ((((static_cast<std::uint32_t>(cfg.minCentralJobs) + static_cast<std::uint32_t>(cfg.maxCentralJobs)) -
           (static_cast<std::uint32_t>(cfg.minWorkplaceJobs) + static_cast<std::uint32_t>(cfg.maxWorkplaceJobs))) / 2.0) * averageCentrality);
    // Dormitories sleep 12 a floor, other habitats about 5.
    constexpr double kBuildingsPerNode = 3.0;
    const double habitats = std::ceil(residents / (12.0 * averageFloors)) +
                            std::ceil(residents * (static_cast<std::uint32_t>(cfg.marketHomesPercent) / 100.0) /
                                      (5.0 * averageFloors));
    const double buildings = habitats + std::ceil(residents / averageJobs);
    const auto size =
        static_cast<std::uint32_t>(std::ceil(std::sqrt(buildings / kBuildingsPerNode)));
    return std::max(kMinGridSize, size);
}

constexpr std::array<std::string_view, 12> kStationNames{
    "Neon Square",  "Foundry Gate", "Old Grid",     "Chrome Market",
    "Circuit Park", "Signal Hill",  "Rust Docks",   "Ion Plaza",
    "Coil Street",  "Relay Yard",   "Static Cross", "Diode Row"};

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

City GenerateCity(const CityParams& requested, Pcg32& random,
                  EntityIdAllocator& ids) {
    CityParams params = requested;
    if (params.gridSize == 0) {
        params.gridSize = GridSizeFor(params.residents, params.city);
    }
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

    // Stations at every second intersection, one in from the edge, so no
    // corner of the city is far from one.
    for (std::uint32_t row = static_cast<std::uint32_t>(params.city.stationOffset); row < size; row += static_cast<std::uint32_t>(params.city.stationSpacing)) {
        for (std::uint32_t column = static_cast<std::uint32_t>(params.city.stationOffset); column < size;
             column += static_cast<std::uint32_t>(params.city.stationSpacing)) {
            const auto number = static_cast<std::uint32_t>(city.stations.size());
            std::string name{kStationNames.at(number % kStationNames.size())};
            if (number >= kStationNames.size()) {
                name += std::format(" {}", (number / kStationNames.size()) + 1);
            }
            city.stations.push_back(
                {.node = NodeId{(row * size) + column}, .name = std::move(name)});
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
        if (random.UniformInt(1, static_cast<std::uint32_t>(params.city.streetRemovalOdds)) == 1) {
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

    // Reserve one corner for every metro depot before any other building is
    // placed. A station node can host two depots (a row line and a column
    // line both start at the inner corner), so count them individually. The
    // builder cap of four corners then applies to habitats, workplaces and
    // depots alike, and no building is ever pushed outside its block.
    Metro metro(city);
    std::vector<std::uint32_t> depotNodes;
    for (const MetroLine& line : metro.Lines()) {
        depotNodes.push_back(city.stations.at(line.stations.front()).node.value);
    }
    std::vector<std::uint32_t> buildingsAt(city.nodes.size(), 0);
    for (const std::uint32_t node : depotNodes) {
        ++buildingsAt.at(node);
    }

    // Dormitories until every robot has a dorm bed, and habitats until
    // there are homes on the market for kMarketHomesPercent of them,
    // whichever is shorter of its goal first.
    std::uint64_t dormBeds = 0;
    std::uint64_t marketBeds = 0;
    const std::uint64_t marketGoal =
        (static_cast<std::uint64_t>(params.residents) * static_cast<std::uint32_t>(params.city.marketHomesPercent)) / 100;
    std::uint32_t habitats = 0;
    std::uint32_t storeRotation = 0;
    while (dormBeds < params.residents || marketBeds < marketGoal) {
        const bool dormitory =
            dormBeds < params.residents &&
            (marketBeds >= marketGoal ||
             dormBeds * marketGoal <= marketBeds * static_cast<std::uint64_t>(params.residents));
        Building habitat = MakeHabitat(params, random, buildingsAt, ids, ++habitats,
                                       storeRotation, dormitory);
        for (const Room& room : habitat.rooms) {
            if (room.kind == RoomKind::Apartment) {
                (room.apartmentClass == ApartmentClass::Dorm ? dormBeds : marketBeds) +=
                    room.capacity;
            }
        }
        city.buildings.push_back(std::move(habitat));
    }

    // One police station per 3000 robots, taking over a ground-floor store of
    // each habitat in turn; apartments and workplaces are never touched.
    const std::uint32_t wanted = params.residents / 3000;
    std::uint32_t placed = 0;
    for (Building& building : city.buildings) {
        if (placed >= wanted || building.kind != BuildingKind::Habitat) continue;
        for (Room& room : building.rooms) {
            if (room.floor == 0 && room.kind != RoomKind::Apartment) {
                room.kind = RoomKind::PoliceStation;
                room.name = std::format("Police Station {}", ++placed);
                room.capacity = 2;
                room.pricePercent = 100;
                room.visits = 0;
                break;
            }
        }
    }

    std::array<std::uint32_t, kWorkplaceTypes.size()> typeCounts{};
    std::uint32_t jobs = 0;
    while (jobs < params.residents) {
        Building workplace =
            MakeWorkplace(params, random, buildingsAt, ids, typeCounts);
        for (const Room& room : workplace.rooms) {
            jobs += room.capacity;
        }
        city.buildings.push_back(std::move(workplace));
    }

    // One metro depot per line, at its first station's corner: a single crew
    // room where drivers change shifts, sized for one driver per train (floor
    // of two) so every run has a staffed cab. Staff rules come from
    // FindStaffRules, so the building carries no shift of its own. Its corner
    // was reserved in buildingsAt before the other buildings were placed.
    std::uint32_t depots = 0;
    for (const MetroLine& line : metro.Lines()) {
        const NodeId depotNode{city.stations.at(line.stations.front()).node};
        Building depot{.id = ids.Allocate(),
                       .name = std::format("Metro Depot {}", ++depots),
                       .kind = BuildingKind::Workplace,
                       .entrance = depotNode,
                       .floors = 1,
                       .shift = {},
                       .rooms = {}};
        depot.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::format("Crew Room {}", depots),
             .kind = RoomKind::CrewRoom,
             .floor = 0,
             .capacity = std::max<std::uint32_t>(2, line.trains)});
        // A depot is one floor high, so it is dressed as a plain box or as
        // a single-storey workshop with roof lights.
        depot.style = StyleOf(depot.kind, depot.id, depot.floors);
        city.buildings.push_back(std::move(depot));
    }

    // Every corner of an intersection that no building stands on is a free
    // plot: the view draws a park there and construction raises a building
    // on it. The view puts a node's buildings on its first corners in id
    // order, which is the order `buildings` are kept in, so the free plots
    // are the slots after the buildings at that node, listed by node and
    // then corner.
    std::vector<std::uint32_t> taken(city.nodes.size(), 0);
    for (const Building& building : city.buildings) {
        ++taken.at(building.entrance.value);
    }
    for (std::size_t node = 0; node < taken.size(); ++node) {
        for (std::uint32_t corner = taken.at(node); corner < kCornersPerNode;
             ++corner) {
            city.lots.push_back({.node = NodeId{static_cast<std::uint32_t>(node)},
                                 .corner = corner});
        }
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


std::string_view ApartmentClassName(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::Dorm: return "dorm room";
        case ApartmentClass::Studio: return "studio";
        case ApartmentClass::OneBedroom: return "one-bedroom flat";
        case ApartmentClass::TwoBedroom: return "two-bedroom flat";
        case ApartmentClass::Penthouse: return "penthouse";
        case ApartmentClass::None: break;
    }
    return "";
}

std::uint32_t ApartmentsPerFloor(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::Dorm: return 12;
        case ApartmentClass::Studio: return 6;
        case ApartmentClass::OneBedroom: return 4;
        case ApartmentClass::TwoBedroom: return 2;
        case ApartmentClass::Penthouse: return 1;
        case ApartmentClass::None: break;
    }
    return 0;
}

std::uint32_t Sleeps(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::TwoBedroom: return 2;
        case ApartmentClass::Penthouse: return 3;
        case ApartmentClass::None: return 0;
        default: return 1;
    }
}

std::uint32_t AreaOf(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::Dorm: return 34;
        case ApartmentClass::Studio: return 69;
        case ApartmentClass::OneBedroom: return 103;
        case ApartmentClass::TwoBedroom: return 206;
        case ApartmentClass::Penthouse: return 460;
        case ApartmentClass::None: break;
    }
    return 0;
}

}  // namespace mechapunk::sim
