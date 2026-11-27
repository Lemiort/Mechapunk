// World::ReviewStreets: when a growing town runs out of free plots, a ring of
// new streets is laid around its grid, so construction always has land. The
// ring is appended to the city (a node's id is its index, so no id changes),
// and its corners become free plots.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

void World::ReviewStreets() {
    City& city = state_.city;
    const auto freePlotsMin = static_cast<std::size_t>(state_.settings.city.freePlotsMin);
    if (freePlotsMin == 0 || city.lots.size() >= freePlotsMin || city.nodes.empty() ||
        city.edges.empty()) {
        return;
    }
    // The grid: its bounds and its block length, read off the saved nodes and
    // edges, so a city of any size is grown the same way.
    std::int32_t minX = city.nodes.front().x;
    std::int32_t maxX = minX;
    std::int32_t minZ = city.nodes.front().z;
    std::int32_t maxZ = minZ;
    for (const CityNode& node : city.nodes) {
        minX = std::min(minX, node.x);
        maxX = std::max(maxX, node.x);
        minZ = std::min(minZ, node.z);
        maxZ = std::max(maxZ, node.z);
    }
    std::uint32_t block = city.edges.front().lengthMeters;
    for (const CityEdge& edge : city.edges) {
        block = std::min(block, edge.lengthMeters);
    }
    if (block == 0) {
        return;
    }
    const auto step = static_cast<std::int32_t>(block);
    const std::int32_t side = ((maxX - minX) / step) + 1;
    if (side >= state_.settings.city.maxGridSize) {
        return;
    }

    std::map<std::pair<std::int32_t, std::int32_t>, NodeId> at;
    for (const CityNode& node : city.nodes) {
        at[{node.x, node.z}] = node.id;
    }
    // The ring, walked once round from the north-west corner: every ring node
    // is appended, joined to the one before it, and, off the corners, to the
    // grid node it stands beside.
    std::vector<NodeId> ring;
    const std::int32_t low = minX - step;
    const std::int32_t high = maxX + step;
    const std::int32_t lowZ = minZ - step;
    const std::int32_t highZ = maxZ + step;
    const auto addNode = [&](std::int32_t x, std::int32_t z) {
        const NodeId id{static_cast<std::uint32_t>(city.nodes.size())};
        city.nodes.push_back({.id = id, .x = x, .z = z});
        at[{x, z}] = id;
        for (std::uint32_t corner = 0; corner < kCornersPerNode; ++corner) {
            city.lots.push_back({.node = id, .corner = corner});
        }
        ring.push_back(id);
        return id;
    };
    const auto join = [&](NodeId from, NodeId to) {
        city.edges.push_back({.from = from, .to = to, .lengthMeters = block});
    };
    for (std::int32_t x = low; x <= high; x += step) {
        addNode(x, lowZ);
    }
    for (std::int32_t z = lowZ + step; z <= highZ; z += step) {
        addNode(high, z);
    }
    for (std::int32_t x = high - step; x >= low; x -= step) {
        addNode(x, highZ);
    }
    for (std::int32_t z = highZ - step; z > lowZ; z -= step) {
        addNode(low, z);
    }
    for (std::size_t index = 0; index != ring.size(); ++index) {
        join(ring[index], ring[(index + 1) % ring.size()]);
    }
    // The spokes: each ring node beside the grid, to the grid node next to it.
    for (const NodeId id : ring) {
        const CityNode node = city.nodes[id.value];
        const bool northOrSouth = node.z == lowZ || node.z == highZ;
        const bool westOrEast = node.x == low || node.x == high;
        if (northOrSouth && westOrEast) {
            continue;  // A corner of the ring touches no grid node.
        }
        const std::int32_t inX = westOrEast ? (node.x == low ? minX : maxX) : node.x;
        const std::int32_t inZ = northOrSouth ? (node.z == lowZ ? minZ : maxZ) : node.z;
        join(id, at.at({inX, inZ}));
    }
    routes_ = RoutePlanner(city);
    ++cityVersion_;
}

}  // namespace mechapunk::sim
