#include "mechapunk/sim/City.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace mechapunk::sim {

namespace {
constexpr std::uint32_t kUnreachable = std::numeric_limits<std::uint32_t>::max();
// One candidate street segment in this many is considered for removal.
constexpr std::uint32_t kStreetRemovalOdds = 10;
constexpr std::uint32_t kMinHabitatCapacity = 30;
constexpr std::uint32_t kMaxHabitatCapacity = 60;
constexpr std::uint32_t kMinWorkplaceJobs = 15;
constexpr std::uint32_t kMaxWorkplaceJobs = 40;

struct WorkplaceType {
    std::string_view name;
    Shift shift;
};

// Shift times in minutes of the day.
constexpr std::array kWorkplaceTypes{
    WorkplaceType{"Foundry", {.startMinute = 6 * 60, .lengthMinutes = 600}},
    WorkplaceType{"Assembly Line",
                  {.startMinute = 7 * 60, .lengthMinutes = 480}},
    WorkplaceType{"Repair Shop", {.startMinute = 8 * 60, .lengthMinutes = 600}},
    WorkplaceType{"Corp Tower", {.startMinute = 9 * 60, .lengthMinutes = 540}},
    WorkplaceType{"Noodle Bar", {.startMinute = 14 * 60, .lengthMinutes = 480}},
    WorkplaceType{"Data Farm", {.startMinute = 22 * 60, .lengthMinutes = 480}},
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
}  // namespace

const Building* City::FindBuilding(EntityId id) const noexcept {
    const auto found = std::ranges::lower_bound(buildings, id, std::less{},
                                                &Building::id);
    return found != buildings.end() && found->id == id ? &*found : nullptr;
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

    // Habitats in the western half, workplaces in the eastern half; the
    // middle column is shared.
    const std::uint32_t middle = size / 2;
    std::uint32_t housing = 0;
    std::uint32_t habitatNumber = 0;
    while (housing < params.residents) {
        const std::uint32_t capacity =
            random.UniformInt(kMinHabitatCapacity, kMaxHabitatCapacity);
        city.buildings.push_back(
            {.id = ids.Allocate(),
             .name = std::format("Hab Block {}", ++habitatNumber),
             .kind = BuildingKind::Habitat,
             .entrance = RandomNodeInColumns(params, random, 0, middle),
             .capacity = capacity,
             .shift = {}});
        housing += capacity;
    }

    std::array<std::uint32_t, kWorkplaceTypes.size()> typeCounts{};
    std::uint32_t jobs = 0;
    while (jobs < params.residents) {
        const std::uint32_t typeIndex = random.UniformInt(
            0, static_cast<std::uint32_t>(kWorkplaceTypes.size() - 1));
        const WorkplaceType& type = kWorkplaceTypes.at(typeIndex);
        const std::uint32_t capacity =
            random.UniformInt(kMinWorkplaceJobs, kMaxWorkplaceJobs);
        city.buildings.push_back(
            {.id = ids.Allocate(),
             .name = std::format("{} {}", type.name, ++typeCounts.at(typeIndex)),
             .kind = BuildingKind::Workplace,
             .entrance = RandomNodeInColumns(params, random, middle, size - 1),
             .capacity = capacity,
             .shift = type.shift});
        jobs += capacity;
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
    const std::uint32_t distance = DistancesFrom(from.value)[to.value];
    if (distance == kUnreachable) {
        return std::nullopt;
    }
    return distance;
}

const std::vector<std::uint32_t>& RoutePlanner::DistancesFrom(
    std::uint32_t origin) {
    if (const auto cached = cache_.find(origin); cached != cache_.end()) {
        return cached->second;
    }
    // Dijkstra over whole meters; integer arithmetic keeps results identical
    // on every platform.
    std::vector<std::uint32_t> distances(adjacency_.size(), kUnreachable);
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
                frontier.emplace(candidate, neighbor.node);
            }
        }
    }
    return cache_.emplace(origin, std::move(distances)).first->second;
}

}  // namespace mechapunk::sim
