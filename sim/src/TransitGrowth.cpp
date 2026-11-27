// World::ReviewTransit: a growing town opens metro stations where buildings
// stand out of reach of every station. A line needs two stations, so a town
// with none opens two at once; later ones are opened one at a time, always on
// the row or column of a station that already stands, so that they join a line.
// City Hall pays for each station; it opens none it cannot pay for.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "CityInternal.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

void World::ReviewTransit() {
    const MetroSettings& settings = state_.settings.metro;
    if (settings.minBuildings == 0) {
        return;
    }
    City& city = state_.city;
    const std::int64_t reach = settings.stationReachMeters;
    const auto near = [&](NodeId first, NodeId second) {
        const CityNode& a = city.nodes[first.value];
        const CityNode& b = city.nodes[second.value];
        const std::int64_t dx = a.x - b.x;
        const std::int64_t dz = a.z - b.z;
        return (dx * dx) + (dz * dz) <= reach * reach;
    };
    // The buildings that stand (not the ones going up) out of reach of every station.
    std::vector<NodeId> stations;
    for (const Station& station : city.stations) {
        stations.push_back(station.node);
    }
    std::vector<NodeId> uncovered;
    for (const Building& building : city.buildings) {
        if (IsUnderConstruction(state_.projects, building.id)) {
            continue;
        }
        if (std::ranges::none_of(stations, [&](NodeId station) {
                return near(building.entrance, station);
            })) {
            uncovered.push_back(building.entrance);
        }
    }
    if (uncovered.size() < static_cast<std::size_t>(settings.minBuildings)) {
        return;
    }

    // Stations to open: two for a town with fewer than two, else one.
    const std::size_t wanted = stations.size() < 2 ? 2 - stations.size() : 1;
    std::vector<NodeId> chosen;
    for (std::size_t round = 0; round != wanted; ++round) {
        const bool completesLine = stations.size() + chosen.size() == 1;
        std::vector<NodeId> all = stations;
        all.insert(all.end(), chosen.begin(), chosen.end());
        const auto taken = [&](NodeId node) { return std::ranges::contains(all, node); };
        const auto sharesLine = [&](const CityNode& node) {
            return all.empty() || std::ranges::any_of(all, [&](NodeId other) {
                       const CityNode& there = city.nodes[other.value];
                       return there.x == node.x || there.z == node.z;
                   });
        };
        bool found = false;
        NodeId best;
        std::size_t bestScore = 0;
        for (const CityNode& node : city.nodes) {
            if (taken(node.id) || !sharesLine(node)) {
                continue;
            }
            const auto score = static_cast<std::size_t>(std::ranges::count_if(
                uncovered, [&](NodeId entrance) { return near(entrance, node.id); }));
            if (!found || score > bestScore) {
                found = true;
                best = node.id;
                bestScore = score;
            }
        }
        // The first station of a line must serve someone; the one that completes
        // a line is opened wherever it serves most, even nobody new.
        if (!found || (bestScore == 0 && !completesLine)) {
            break;
        }
        chosen.push_back(best);
        std::erase_if(uncovered, [&](NodeId entrance) { return near(entrance, best); });
    }
    if (chosen.empty() || (stations.size() + chosen.size() < 2)) {
        return;
    }
    // City Hall pays the supply market, as it does for public works.
    Organization* hall = MutableOrganization(cityHall_);
    const std::int64_t cost = settings.stationCost * static_cast<std::int64_t>(chosen.size());
    if (hall == nullptr || hall->credits < cost) {
        return;
    }
    hall->credits -= cost;
    hall->today.supplies += cost;
    state_.economy.supplyMarket += cost;
    state_.today.supplies += cost;
    state_.today.publicSpending += cost;
    for (const NodeId node : chosen) {
        city.stations.push_back(
            {.node = node,
             .name = city_build::StationName(static_cast<std::uint32_t>(city.stations.size()))});
    }
    metro_ = Metro(city);
    nearestStation_.clear();
    metersToStation_.clear();
    IndexStations();
    ++cityVersion_;
}

}  // namespace mechapunk::sim
