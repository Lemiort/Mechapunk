// ValidateWorldState: checks a loaded or running world for broken invariants.

#include "mechapunk/sim/World.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Names.hpp"
#include "mechapunk/sim/Venues.hpp"

#include "WorldInternal.hpp"

namespace mechapunk::sim {

std::expected<void, std::string> ValidateWorldState(const WorldState& state) {
    const auto fail = [](std::string message) {
        return std::unexpected(std::move(message));
    };
    const City& city = state.city;
    if (city.nodes.empty()) {
        return fail("The city has no street nodes");
    }
    for (std::size_t index = 0; index < city.nodes.size(); ++index) {
        if (city.nodes[index].id.value != index) {
            return fail(std::format("Street node {} has id {}", index,
                                    city.nodes[index].id.value));
        }
    }
    const auto nodeCount = city.nodes.size();
    for (const CityEdge& edge : city.edges) {
        if (edge.from.value >= nodeCount || edge.to.value >= nodeCount ||
            edge.lengthMeters == 0) {
            return fail(std::format("Invalid street {} -> {}",
                                    edge.from.value, edge.to.value));
        }
    }

    // Every id is unique across buildings, rooms and robots.
    std::unordered_set<std::uint64_t> ids;
    const auto claim = [&](EntityId id) {
        return id.IsValid() && id.value < state.ids.next &&
               ids.insert(id.value).second;
    };
    EntityId previous;
    for (const Building& building : city.buildings) {
        if (!claim(building.id) || building.id <= previous) {
            return fail(std::format("Building '{}' has an invalid, repeated "
                                    "or out-of-order id {}",
                                    building.name, building.id.value));
        }
        previous = building.id;
        if (building.entrance.value >= nodeCount || building.floors == 0) {
            return fail(std::format("Building '{}' has no valid entrance or "
                                    "floors",
                                    building.name));
        }
        // Metro depots (workplaces whose rooms are all crew rooms) carry no
        // building-level shift by design; their staff follow room-kind rules,
        // so the workplace shift checks don't apply to them.
        if (building.kind == BuildingKind::Workplace &&
            std::any_of(building.rooms.begin(), building.rooms.end(),
                        [](const Room& room) {
                            return room.kind != RoomKind::CrewRoom;
                        }) &&
            (building.shift.startMinute >= kMinutesPerDay ||
             building.shift.lengthMinutes == 0 ||
             building.shift.lengthMinutes >= kMinutesPerDay)) {
            return fail(std::format("Workplace '{}' has an invalid shift",
                                    building.name));
        }
        EntityId previousRoom;
        for (const Room& room : building.rooms) {
            if (!claim(room.id) || room.id <= previousRoom) {
                return fail(std::format("Room '{}' in '{}' has an invalid, "
                                        "repeated or out-of-order id {}",
                                        room.name, building.name,
                                        room.id.value));
            }
            previousRoom = room.id;
            if (room.floor >= building.floors || room.capacity == 0) {
                return fail(std::format("Room '{}' in '{}' has an invalid "
                                        "floor or capacity",
                                        room.name, building.name));
            }
        }
    }

    // Free plots: out of `kCornersPerNode` corners per node, those no
    // building of that node stands on. Construction raises a building on a
    // plot, so a plot a building already occupies would put two buildings
    // on one corner; the list names each free corner once, sorted by node
    // and then corner.
    std::vector<std::uint32_t> taken(nodeCount, 0);
    for (const Building& building : city.buildings) {
        ++taken.at(building.entrance.value);
    }
    std::uint64_t previousPlotKey = 0;
    bool firstPlot = true;
    for (const CityLot& lot : city.lots) {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(lot.node.value) * kCornersPerNode) +
            lot.corner;
        if (lot.node.value >= nodeCount || lot.corner >= kCornersPerNode ||
            lot.corner < taken.at(static_cast<std::size_t>(lot.node.value)) ||
            (!firstPlot && key <= previousPlotKey)) {
            return fail(std::format("Free plot corner {} of node {} is not a "
                                    "free corner of its node, or is out of order",
                                    lot.corner, lot.node.value));
        }
        previousPlotKey = key;
        firstPlot = false;
    }

    std::unordered_set<std::uint32_t> stationNodes;
    for (const Station& station : city.stations) {
        if (station.node.value >= nodeCount ||
            !stationNodes.insert(station.node.value).second) {
            return fail(std::format("Station '{}' is not at a street node of its own",
                                    station.name));
        }
    }

    RoutePlanner routes(city);
    if (!city.buildings.empty()) {
        const NodeId origin = city.buildings.front().entrance;
        for (const Building& building : city.buildings) {
            if (!routes.DistanceMeters(origin, building.entrance)) {
                return fail(std::format("Building '{}' cannot be reached",
                                        building.name));
            }
        }
    }

    const RoomIndex rooms(city);
    std::unordered_map<std::uint64_t, std::uint32_t> occupants;
    const auto roomOfKind = [&](EntityId id,
                                RoomKind kind) -> const Room* {
        const auto found = rooms.Find(id);
        return found && found->room->kind == kind ? found->room : nullptr;
    };
    previous = EntityId{};
    for (const Robot& robot : state.robots) {
        if (!claim(robot.id) || robot.id <= previous) {
            return fail(std::format("Robot '{}' has an invalid, repeated or "
                                    "out-of-order id {}",
                                    robot.name, robot.id.value));
        }
        previous = robot.id;
        // At least one working day a week.
        constexpr std::uint32_t kWholeWeek = (1U << kDaysPerWeek) - 1;
        if ((robot.daysOff & kWholeWeek) == kWholeWeek ||
            robot.daysOff > kWholeWeek) {
            return fail(std::format("Robot '{}' has invalid days off",
                                    robot.name));
        }
        const Room* apartment = roomOfKind(robot.apartment, RoomKind::Apartment);
        // A job at a workspace, or on a venue's staff.
        const auto job = rooms.Find(robot.workspace);
        const Room* workspace = job && JobsAt(*job->room) > 0 ? job->room : nullptr;
        if (apartment == nullptr ||
            (robot.workspace.IsValid() && workspace == nullptr) ||
            robot.jobLevel > kMaxJobLevel) {
            return fail(std::format("Robot '{}' has no valid apartment or "
                                    "workspace",
                                    robot.name));
        }
        if (++occupants[apartment->id.value] > apartment->capacity ||
            (workspace != nullptr &&
             ++occupants[workspace->id.value] > JobsAt(*workspace))) {
            return fail(std::format("Robot '{}' exceeds the capacity of its "
                                    "apartment or workspace",
                                    robot.name));
        }
        const auto location = rooms.Find(robot.location);
        if (!location) {
            return fail(std::format("Robot '{}' is in a missing room",
                                    robot.name));
        }
        const bool consistent = [&] {
            switch (robot.activity) {
                case RobotActivity::Resting:
                    return robot.location == robot.apartment;
                case RobotActivity::Working:
                    // At work, or an officer looking round a venue.
                    return robot.workspace.IsValid() &&
                           (robot.location == robot.workspace ||
                            (robot.patrol.IsValid() && robot.location == robot.patrol &&
                             IsVenue(location->room->kind)));
                case RobotActivity::Visiting:
                    return IsVenue(location->room->kind);
                case RobotActivity::Walking:
                    return rooms.Find(robot.destination).has_value() &&
                           robot.departedAt <= robot.nextActionAt &&
                           (robot.alightsAt == 0 ||
                            (robot.departedAt <= robot.boardsAt &&
                             robot.boardsAt <= robot.alightsAt &&
                             robot.alightsAt <= robot.nextActionAt &&
                             robot.rideFrom < city.stations.size() &&
                             robot.rideVia < city.stations.size() &&
                             robot.rideTo < city.stations.size()));
            }
            return false;
        }();
        if (robot.patrol.IsValid()) {
            const auto station = rooms.Find(robot.workspace);
            const bool walking = robot.activity == RobotActivity::Walking &&
                                 robot.destination == robot.patrol;
            const bool there = robot.activity == RobotActivity::Working &&
                               robot.location == robot.patrol;
            if (!station || station->room->kind != RoomKind::PoliceStation ||
                (!walking && !there)) {
                return fail(std::format("Robot '{}' patrols without being an officer on it",
                                        robot.name));
            }
        }
        if (robot.activity != RobotActivity::Walking && robot.alightsAt != 0) {
            return fail(std::format("Robot '{}' rides the metro without walking",
                                    robot.name));
        }
        if (!consistent) {
            return fail(std::format("Robot '{}' is in a room that does not "
                                    "match what it is doing",
                                    robot.name));
        }
        const auto inRange = [](std::int32_t need) {
            return need >= 0 && need <= Needs::kMax;
        };
        if (!inRange(robot.needs.charge) || !inRange(robot.needs.condition) ||
            !inRange(robot.needs.social) || !inRange(robot.needs.fun) ||
            robot.credits < 0 || robot.rentOwed < 0 || robot.wear < 0 ||
            robot.wear > kMaxWear) {
            return fail(std::format("Robot '{}' has needs or credits out of "
                                    "range",
                                    robot.name));
        }
    }
    if (state.weather.rain > kMaxRain) {
        return fail("The rain is out of range");
    }
    if (auto economy = ValidateEconomy(state); !economy) {
        return economy;
    }
    // Gangs sorted by unique id, relations sorted and valid.
    auto& gangs = state.gangs;
    for (std::size_t i = 0; i < gangs.size(); ++i) {
        if (!gangs[i].id.IsValid() ||
            (i > 0 && gangs[i].id <= gangs[i - 1].id)) {
            return fail(std::format("Gang '{}' has an invalid or repeated id",
                                    gangs[i].name));
        }
    }
    // Gang relations: sorted, no self-reference, point to existing gangs.
    for (const Gang& gang : gangs) {
        if (!gang.relations.empty()) {
            for (std::size_t j = 1; j < gang.relations.size(); ++j) {
                if (gang.relations[j].gang <= gang.relations[j - 1].gang) {
                    return fail(std::format(
                        "Gang '{}' has unordered relations", gang.name));
                }
            }
        }
        for (const GangRelation& rel : gang.relations) {
            if (rel.gang == gang.id || !std::any_of(gangs.begin(), gangs.end(),
                    [&](const Gang& each) { return each.id == rel.gang; })) {
                return fail(std::format(
                    "Gang '{}' has an invalid relation to {}",
                    gang.name, rel.gang.value));
            }
        }
    }
    // Robots' gangs exist; gang turfs are habitat buildings or none.
    for (const Robot& robot : state.robots) {
        if (robot.gang.IsValid() && !std::any_of(gangs.begin(), gangs.end(),
                [&](const Gang& each) { return each.id == robot.gang; })) {
            return fail(std::format("Robot '{}' references a missing gang {}",
                                    robot.name, robot.gang.value));
        }
    }
    for (const Gang& gang : gangs) {
        if (gang.turf.IsValid()) {
            bool found = false;
            for (const Building& building : state.city.buildings) {
                if (building.id == gang.turf && building.kind == BuildingKind::Habitat) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return fail(std::format("Gang '{}' turf is not a habitat",
                                        gang.name));
            }
        }
    }
    // The chronicle: oldest first, never longer than the game keeps. Entries
    // are not checked against the robots and rooms they name: an entry about
    // a scrapped robot outlives that robot on purpose.
    if (state.chronicle.size() > kChronicleCap) {
        return fail(std::format("The chronicle holds {} entries, more than {}",
                                state.chronicle.size(), kChronicleCap));
    }
    for (std::size_t i = 1; i < state.chronicle.size(); ++i) {
        if (state.chronicle[i].tick < state.chronicle[i - 1].tick) {
            return fail(std::format("The chronicle's entry {} is older than {}",
                                    i, i - 1));
        }
    }
    return ValidateSocial(state);
}

}  // namespace mechapunk::sim
