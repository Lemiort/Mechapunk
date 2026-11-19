// Where a robot is drawn: its spot in a room, the ways out of rooms, and its
// position along a walk or a metro ride. Never teleports: every position
// follows a walk.

#include "CityView.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Venues.hpp"

#include "CityViewInternal.hpp"

namespace mechapunk {
namespace {

constexpr float kRobotSpread = 3.0F * kS;

}  // namespace

const CityView::RoomWays* CityView::WaysOf(sim::EntityId room) {
    if (!room.IsValid()) {
        return nullptr;
    }
    if (const auto cached = roomWays_.find(room.value); cached != roomWays_.end()) {
        return &cached->second;
    }
    const auto ref = rooms_.Find(room);
    if (!ref) {
        return nullptr;
    }
    const auto shape = shapes_.find(ref->building->id.value);
    if (shape == shapes_.end()) {
        return nullptr;
    }
    // Plan the floor without keeping it, unless it is kept already.
    const std::uint32_t floor = ref->room->floor;
    FloorPlan fresh;
    const FloorPlan* plan = nullptr;
    if (const auto kept = plans_.find({ref->building->id.value, floor});
        kept != plans_.end()) {
        plan = &kept->second;
    } else {
        fresh = PlanFloor(*ref->building, floor);
        plan = &fresh;
    }
    const RoomPlan none;
    const RoomPlan* roomPlan = &none;
    for (const RoomPlan& candidate : plan->rooms) {
        if (candidate.room == room) {
            roomPlan = &candidate;
        }
    }
    const auto toWorld = [&](const std::vector<NavPoint>& points) {
        Walk walk;
        walk.reserve(points.size());
        for (const NavPoint& point : points) {
            walk.push_back(
                {.position = ToWorld(shape->second, point.floor, point.position),
                 .spread = 0.0F,
                 .building = point.outside ? sim::EntityId{} : ref->building->id,
                 .floor = point.floor});
        }
        return walk;
    };
    RoomWays ways{.home = roomPlan->home, .staffSpots = roomPlan->staffSpots};
    for (std::size_t spot = 0; spot < roomPlan->spots.size(); ++spot) {
        ways.fromSpot.push_back(toWorld(WayOut(*roomPlan, spot, floor)));
    }
    ways.fromDoor = toWorld(WayOut(*roomPlan, roomPlan->spots.size(), floor));
    return &roomWays_.emplace(room.value, std::move(ways)).first->second;
}

std::size_t CityView::SpotFor(sim::EntityId robot, sim::EntityId room, bool home,
                              std::size_t count, double tick, std::size_t staffSpots,
                              bool staff) const {
    if (count == 0) {
        return 0;
    }
    // Guests use the first spots, staff the last staffSpots.
    const auto [first, last] = SpotRange(count, staffSpots, staff);
    if (home) {
        // Each resident has its own bed and seat, in the order of their ids.
        const double minute = std::fmod(tick, static_cast<double>(sim::kMinutesPerDay));
        const bool night = minute >= kBedtime || minute < kWakeUp;
        const auto found = residentIndex_.find(robot.value);
        const std::size_t resident = found != residentIndex_.end() ? found->second : 0;
        return std::min<std::size_t>((2 * resident) + (night ? 0 : 1), count - 1);
    }
    if (const auto found = assignedSpots_.find(robot.value);
        found != assignedSpots_.end() && found->second.room == room &&
        found->second.spot >= first && found->second.spot < last) {
        return found->second.spot;
    }
    return first + (static_cast<std::size_t>((robot.value * 2654435761ULL) >> 11U) % (last - first));
}

std::pair<std::size_t, std::size_t> CityView::SpotRange(std::size_t count,
                                                        std::size_t staffSpots, bool staff) {
    if (staffSpots == 0 || staffSpots >= count) {
        return {0, count};
    }
    return staff ? std::pair{count - staffSpots, count}
                 : std::pair{std::size_t{0}, count - staffSpots};
}

void CityView::AssignSpots(const WorldFrame& frame, const FloorPlan& plan) {
    // Robots in a room or heading to it, earliest arrival first.
    std::unordered_map<std::uint64_t, std::vector<std::pair<sim::Tick, std::uint64_t>>>
        visitors;
    std::unordered_map<std::uint64_t, std::size_t> spotCounts;
    std::unordered_map<std::uint64_t, std::size_t> staffCounts;
    for (const RoomPlan& room : plan.rooms) {
        if (!room.home && !room.spots.empty()) {
            visitors[room.room.value];
            spotCounts[room.room.value] = room.spots.size();
            staffCounts[room.room.value] = room.staffSpots;
        }
    }
    // Robots at, or on their way to, the venue they work at.
    std::unordered_set<std::uint64_t> staff;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.workspace.IsValid() &&
            (robot.workspace == robot.location || robot.workspace == robot.destination)) {
            staff.insert(robot.id.value);
        }
    }
    std::unordered_map<std::uint64_t, AssignedSpot> next;
    for (const RobotFrame& robot : frame.robots) {
        const bool walking = robot.activity == sim::RobotActivity::Walking;
        const sim::EntityId room = walking ? robot.destination : robot.location;
        if (const auto found = visitors.find(room.value); found != visitors.end()) {
            found->second.emplace_back(robot.arrivesAt, robot.id.value);
        }
        // A robot on its way out keeps the spot it left from.
        if (walking) {
            if (const auto kept = assignedSpots_.find(robot.id.value);
                kept != assignedSpots_.end() && kept->second.room == robot.location) {
                next[robot.id.value] = kept->second;
            }
        }
    }
    for (auto& [roomValue, robots] : visitors) {
        std::ranges::sort(robots);
        const std::size_t count = spotCounts.at(roomValue);
        const sim::EntityId room{roomValue};
        std::vector<bool> taken(count, false);
        std::vector<std::uint64_t> newcomers;
        const std::size_t staffSpots = staffCounts.at(roomValue);
        // Robots keep the spots they already had.
        for (const auto& [arrives, id] : robots) {
            const auto had = assignedSpots_.find(id);
            const auto [first, last] = SpotRange(count, staffSpots, staff.contains(id));
            if (had != assignedSpots_.end() && had->second.room == room &&
                had->second.spot >= first && had->second.spot < last &&
                !taken[had->second.spot]) {
                taken[had->second.spot] = true;
                next[id] = had->second;
            } else {
                newcomers.push_back(id);
            }
        }
        // Newcomers take their favourite free spot, or the next free one.
        for (const std::uint64_t id : newcomers) {
            const auto [first, last] = SpotRange(count, staffSpots, staff.contains(id));
            const std::size_t range = last - first;
            const std::size_t favourite =
                static_cast<std::size_t>((id * 2654435761ULL) >> 11U) % range;
            std::size_t spot = first + favourite;
            for (std::size_t step = 0; step < range; ++step) {
                const std::size_t candidate = first + ((favourite + step) % range);
                if (!taken[candidate]) {
                    spot = candidate;
                    break;
                }
            }
            if (spot < count) {
                taken[spot] = true;
            }
            next[id] = {.room = room, .spot = spot};
        }
    }
    assignedSpots_ = std::move(next);
}

std::optional<vec3> CityView::DrawnRobotPosition(sim::EntityId robot) const {
    for (const Marker& marker : robotMarkers_) {
        if (marker.id == robot) {
            return marker.position;
        }
    }
    return std::nullopt;
}

std::optional<CityView::Placement> CityView::RobotPosition(const RobotFrame& robot,
                                                           double renderTick) {
    walk_.clear();
    // Drivers on shift go their own way: to the platform, the cab and back.
    if (driverRank_.contains(robot.id.value)) {
        return DriverPlacement(robot, renderTick);
    }
    const auto street = [&](const std::vector<vec3>& points, bool reversed) {
        const auto add = [&](vec3 point) {
            walk_.push_back({.position = point + vec3{0.0F, kStreetHeight, 0.0F},
                             .spread = 1.0F,
                             .building = {},
                             .floor = 0});
        };
        if (reversed) {
            std::ranges::for_each(points.rbegin(), points.rend(), add);
        } else {
            std::ranges::for_each(points, add);
        }
    };
    const auto exact = [&](vec3 point) {
        walk_.push_back({.position = point, .spread = 0.0F, .building = {}, .floor = 0});
    };
    // From the robot's spot in the room it left, and on to the spot it will
    // take where it is going.
    const auto leave = [&] {
        if (const RoomWays* ways = WaysOf(robot.location)) {
            const std::size_t spot =
                SpotFor(robot.id, robot.location, ways->home, ways->fromSpot.size(),
                        static_cast<double>(robot.departedAt), ways->staffSpots,
                        robot.workspace == robot.location);
            const Walk& way = ways->fromSpot.empty() ? ways->fromDoor : ways->fromSpot[spot];
            walk_.insert(walk_.end(), way.begin(), way.end());
        } else if (const auto shape = shapes_.find(robot.from.value);
                   shape != shapes_.end()) {
            exact(shape->second.center);
        }
    };
    const auto arrive = [&] {
        if (const RoomWays* ways = WaysOf(robot.destination)) {
            const std::size_t spot =
                SpotFor(robot.id, robot.destination, ways->home, ways->fromSpot.size(),
                        static_cast<double>(robot.arrivesAt), ways->staffSpots,
                        robot.workspace == robot.destination);
            const Walk& way = ways->fromSpot.empty() ? ways->fromDoor : ways->fromSpot[spot];
            walk_.insert(walk_.end(), way.rbegin(), way.rend());
        } else if (const auto shape = shapes_.find(robot.to.value);
                   shape != shapes_.end()) {
            exact(shape->second.center);
        }
    };

    // On foot the whole way, or up to a platform, on the train, and down
    // from the other platform.
    double start = static_cast<double>(robot.departedAt);
    double end = static_cast<double>(robot.arrivesAt);
    bool boarding = false;
    if (robot.alightsAt > robot.boardsAt) {
        const std::vector<sim::Leg>& legs = LegsOf(robot);
        if (legs.empty()) {
            return std::nullopt;  // No train to ride: a ride from an old save.
        }
        const auto axisOf = [&](const sim::Leg& leg) { return metro_.Lines().at(leg.line).axis; };
        const auto spotFor = [&](std::uint32_t station, const sim::Leg& leg) {
            return PlatformSpot(robot.id, station, axisOf(leg), SideOf(leg.direction));
        };
        const auto stairsFor = [&](std::uint32_t station, const sim::Leg& leg) {
            return StairsOf(station, axisOf(leg), SideOf(leg.direction));
        };
        const auto trainOf = [&](const sim::Leg& leg, double time) {
            return TrainPoseOf(metro_.TrainAt(leg.line, leg.train, time));
        };
        // Up the stairs to the platform of a leg's first station.
        const auto upTo = [&](const sim::Leg& leg) {
            const Stairs stairs = stairsFor(leg.fromStation, leg);
            exact(stairs.bottom);
            exact(stairs.top);
            exact(spotFor(leg.fromStation, leg));
        };
        // Out of the train standing at a leg's last station, and down.
        const auto offAndDown = [&](const sim::Leg& leg) {
            const TrainPose train = trainOf(leg, leg.arrives + 0.01);
            const Stairs stairs = stairsFor(leg.toStation, leg);
            const auto [outside, inside] = DoorFor(robot.id, train, stairs.top);
            exact(PlaceAboard(robot.id, train));
            exact(inside);
            exact(outside);
            exact(stairs.top);
            exact(stairs.bottom);
        };
        if (renderTick < static_cast<double>(robot.boardsAt)) {
            leave();
            street(StationRoute(robot.from, legs.front().fromStation), false);
            upTo(legs.front());
            end = static_cast<double>(robot.boardsAt);
        } else {
            for (std::size_t index = 0; index < legs.size(); ++index) {
                const sim::Leg& leg = legs[index];
                const double doors = leg.departs - sim::kDwellMinutes;
                if (renderTick < doors) {
                    // On the platform, facing the track.
                    return Placement{.position = spotFor(leg.fromStation, leg),
                                     .direction = AcrossOf(axisOf(leg)) * -SideOf(leg.direction),
                                     .building = {}};
                }
                if (renderTick < leg.departs) {
                    // Into the standing train: to the nearer door and a place.
                    const TrainPose train = trainOf(leg, renderTick);
                    const vec3 spot = spotFor(leg.fromStation, leg);
                    const auto [outside, inside] = DoorFor(robot.id, train, spot);
                    exact(spot);
                    exact(outside);
                    exact(inside);
                    exact(PlaceAboard(robot.id, train));
                    start = doors;
                    end = leg.departs;
                    boarding = true;
                    break;
                }
                if (renderTick < leg.arrives) {
                    const TrainPose train = trainOf(leg, renderTick);
                    return Placement{.position = PlaceAboard(robot.id, train),
                                     .direction = RightOf(train.forward),
                                     .building = {},
                                     .aboard = true,
                                     .riding = true};
                }
                if (index + 1 < legs.size()) {
                    // Changing lines: down to the street and up to the
                    // other line's platform, then wait there.
                    const sim::Leg& next = legs[index + 1];
                    const double changed = std::min(leg.arrives + sim::kTransferMinutes,
                                                    next.departs - sim::kDwellMinutes);
                    if (renderTick < changed) {
                        offAndDown(leg);
                        exact(NodePosition(city_.stations.at(leg.toStation).node) +
                              vec3{0.0F, kStreetHeight, 0.0F});
                        upTo(next);
                        start = leg.arrives;
                        end = changed;
                        break;
                    }
                    continue;
                }
                offAndDown(leg);
                street(StationRoute(robot.to, leg.toStation), true);
                arrive();
                start = leg.arrives;
                break;
            }
        }
    } else {
        leave();
        street(Route(robot.from, robot.to), false);
        arrive();
    }
    return FollowWalk(robot, start, end, renderTick, boarding);
}

std::optional<CityView::Placement> CityView::FollowWalk(const RobotFrame& robot, double start,
                                                        double end, double renderTick,
                                                        bool boarding) const {
    if (walk_.empty()) {
        return std::nullopt;
    }

    // Spread robots sharing a street so they do not overlap exactly.
    const std::uint64_t hash = robot.id.value * 2654435761ULL;
    const vec3 spread{
        ((static_cast<float>(hash & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread, 0.0F,
        ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread};
    const auto at = [&](const WalkPoint& a, const WalkPoint& b, float t) {
        const bool indoors = a.building.IsValid() && a.building == b.building;
        const vec3 segment = b.position - a.position;
        const float length = glm::length(segment);
        return Placement{
            .position = glm::mix(a.position, b.position, t) +
                        (spread * glm::mix(a.spread, b.spread, t)),
            .direction = length > 0.0F ? segment / length : vec3{0.0F, 0.0F, 1.0F},
            .building = indoors ? a.building : sim::EntityId{},
            .floor = std::min(a.floor, b.floor),
            .aboard = boarding};
    };
    if (walk_.size() < 2) {
        return at(walk_.front(), walk_.front(), 0.0F);
    }
    const double duration = end - start;
    const double progress =
        duration > 0.0 ? std::clamp((renderTick - start) / duration, 0.0, 1.0) : 1.0;
    float total = 0.0F;
    for (std::size_t index = 1; index < walk_.size(); ++index) {
        total += glm::distance(walk_[index - 1].position, walk_[index].position);
    }
    float remaining = static_cast<float>(progress) * total;
    for (std::size_t index = 1; index < walk_.size(); ++index) {
        const float length =
            glm::distance(walk_[index - 1].position, walk_[index].position);
        if (remaining <= length && length > 0.0F) {
            return at(walk_[index - 1], walk_[index], remaining / length);
        }
        remaining -= length;
    }
    return at(walk_[walk_.size() - 2], walk_.back(), 1.0F);
}

}  // namespace mechapunk
