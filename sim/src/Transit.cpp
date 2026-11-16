// The metro: robots walk to the station nearest them, ride underground to
// the station nearest where they are going, and walk on, whenever that is
// faster than walking all the way.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::uint32_t kWalkingMetersPerMinute = 75;
constexpr std::uint32_t kFloorsPerMinute = 3;
// Trains run at 30 km/h station to station, and a robot waits a few minutes
// for one.
constexpr double kRideMetersPerMinute = 500.0;
constexpr std::uint32_t kWaitMinutes = 3;

std::uint32_t WalkMinutes(std::uint32_t meters) {
    return (meters + kWalkingMetersPerMinute - 1) / kWalkingMetersPerMinute;
}

std::uint32_t StairMinutes(std::uint32_t floors) {
    return (floors + kFloorsPerMinute - 1) / kFloorsPerMinute;
}
}  // namespace

void World::IndexStations() {
    const std::vector<Station>& stations = state_.city.stations;
    if (!nearestStation_.empty() || stations.empty()) {
        return;
    }
    const std::size_t nodes = state_.city.nodes.size();
    nearestStation_.assign(nodes, 0);
    metersToStation_.assign(nodes, std::numeric_limits<std::uint32_t>::max());
    for (std::uint32_t node = 0; node < nodes; ++node) {
        for (std::uint32_t station = 0; station < stations.size(); ++station) {
            const auto meters = routes_.DistanceMeters(NodeId{node}, stations[station].node);
            if (meters && *meters < metersToStation_[node]) {
                metersToStation_[node] = *meters;
                nearestStation_[node] = station;
            }
        }
    }
}

std::uint32_t World::RideMinutes(std::uint32_t fromStation,
                                 std::uint32_t toStation) const {
    const CityNode& from = state_.city.nodes.at(state_.city.stations.at(fromStation).node.value);
    const CityNode& to = state_.city.nodes.at(state_.city.stations.at(toStation).node.value);
    const double dx = static_cast<double>(to.x - from.x);
    const double dz = static_cast<double>(to.z - from.z);
    return kWaitMinutes + static_cast<std::uint32_t>(
                              std::ceil(std::sqrt((dx * dx) + (dz * dz)) /
                                        kRideMetersPerMinute));
}

Trip World::PlanTrip(EntityId fromRoom, EntityId toRoom, bool metroAllowed) {
    const auto from = rooms_.Find(fromRoom);
    const auto to = rooms_.Find(toRoom);
    if (!from || !to) {
        throw std::logic_error("Trip between unknown rooms");
    }
    Trip trip;
    if (from->building == to->building) {
        const std::uint32_t floors = from->room->floor > to->room->floor
                                         ? from->room->floor - to->room->floor
                                         : to->room->floor - from->room->floor;
        // Even next door takes a minute.
        trip.minutes = std::max<std::uint32_t>(1, StairMinutes(floors));
        return trip;
    }
    const NodeId start = from->building->entrance;
    const NodeId end = to->building->entrance;
    const auto meters = routes_.DistanceMeters(start, end);
    if (!meters) {
        throw std::logic_error("No route between buildings");
    }
    trip.minutes = std::max<std::uint32_t>(
        1, WalkMinutes(*meters) +
               StairMinutes(from->room->floor + to->room->floor));

    IndexStations();
    if (!metroAllowed || nearestStation_.empty()) {
        return trip;
    }
    const std::uint32_t board = nearestStation_.at(start.value);
    const std::uint32_t alight = nearestStation_.at(end.value);
    if (board == alight) {
        return trip;
    }
    const std::uint32_t toStation =
        WalkMinutes(metersToStation_.at(start.value)) + StairMinutes(from->room->floor);
    const std::uint32_t underground = RideMinutes(board, alight);
    const std::uint32_t fromStation =
        WalkMinutes(metersToStation_.at(end.value)) + StairMinutes(to->room->floor);
    const std::uint32_t byMetro = toStation + underground + fromStation;
    if (byMetro < trip.minutes) {
        trip = {.minutes = byMetro,
                .metro = true,
                .fromStation = board,
                .toStation = alight,
                .minutesToStation = toStation,
                .minutesUnderground = underground};
    }
    return trip;
}

std::uint32_t World::TravelMinutes(EntityId fromRoom, EntityId toRoom) {
    return PlanTrip(fromRoom, toRoom).minutes;
}

}  // namespace mechapunk::sim
