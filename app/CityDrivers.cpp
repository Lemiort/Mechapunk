// Metro drivers on shift: their plan for the shift, the walk to the platform,
// the cab, and the walk back.

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

// A driver's place at the driving end of its train, from the train's middle.
constexpr float kCabMeters = 11.5F;

// Crew room to platform and back, and how early a driver is on the platform.
constexpr double kDriverWalkMinutes = 2.0;

constexpr double kDriverEarlyMinutes = 1.0;

}  // namespace

std::optional<std::uint32_t> CityView::CrewLineOf(sim::EntityId crewRoom) const {
    if (crewLines_.empty()) {
        // Depots were built in line order, one per line at its first
        // station: match them up in that order, by corner.
        std::vector<bool> taken(metro_.Lines().size(), false);
        for (const sim::Building& building : city_.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.kind != sim::RoomKind::CrewRoom) {
                    continue;
                }
                for (std::uint32_t line = 0; line < metro_.Lines().size(); ++line) {
                    const std::uint32_t first = metro_.Lines()[line].stations.front();
                    if (!taken[line] && city_.stations.at(first).node == building.entrance) {
                        taken[line] = true;
                        crewLines_.emplace(room.id.value, line);
                        break;
                    }
                }
            }
        }
        // A town's crew room (task K, step K12) is an empty unit near its line's
        // first station rather than a depot on it: each one left takes the
        // nearest line that has none, the lower line among equals.
        for (const sim::Building& building : city_.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.kind != sim::RoomKind::CrewRoom || crewLines_.contains(room.id.value)) {
                    continue;
                }
                const sim::CityNode& at = city_.nodes.at(building.entrance.value);
                std::optional<std::uint32_t> best;
                std::int64_t bestDistance = 0;
                for (std::uint32_t line = 0; line < metro_.Lines().size(); ++line) {
                    if (taken[line]) {
                        continue;
                    }
                    const sim::CityNode& first = city_.nodes.at(
                        city_.stations.at(metro_.Lines()[line].stations.front()).node.value);
                    const std::int64_t dx = at.x - first.x;
                    const std::int64_t dz = at.z - first.z;
                    const std::int64_t distance = (dx * dx) + (dz * dz);
                    if (!best || distance < bestDistance) {
                        best = line;
                        bestDistance = distance;
                    }
                }
                if (best) {
                    taken[*best] = true;
                    crewLines_.emplace(room.id.value, *best);
                }
            }
        }
        crewLines_.emplace(0, 0);  // Built: never search again.
    }
    const auto found = crewLines_.find(crewRoom.value);
    if (found == crewLines_.end() || !crewRoom.IsValid()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<CityView::DriverPlan> CityView::PlanDriver(const RobotFrame& robot) const {
    const auto rank = driverRank_.find(robot.id.value);
    if (rank == driverRank_.end()) {
        return std::nullopt;
    }
    const auto crew = rooms_.Find(robot.workspace);
    const auto line = CrewLineOf(robot.workspace);
    if (!crew || !line) {
        return std::nullopt;
    }
    // In the crew room from clocking in, or from the shift's start if early.
    const double clockIn = static_cast<double>(std::max(robot.shiftStartsAt, robot.arrivesAt));
    if (const auto cached = driverPlans_.find(robot.id.value);
        cached != driverPlans_.end() && cached->second.clockIn == clockIn &&
        cached->second.rank == rank->second) {
        return cached->second.plan;
    }
    std::optional<DriverPlan> plan;
    const sim::MetroLine& metro = metro_.Lines().at(*line);
    const double shiftEnd =
        static_cast<double>(robot.shiftStartsAt) + sim::JobShift(*crew, robot.id).lengthMinutes;
    // One driver to a train; the rest of the shift stand by in the room.
    if (rank->second < metro.trains) {
        const auto run = metro_.PlanDriverRun(*line, clockIn + kDriverWalkMinutes, rank->second,
                                              shiftEnd - kDriverWalkMinutes - sim::kDwellMinutes);
        if (run) {
            // Out of the room in time to be on the platform a little before
            // the doors open; back in it before the shift ends.
            const double leaves =
                std::max(clockIn, run->boards - kDriverEarlyMinutes - kDriverWalkMinutes);
            plan = DriverPlan{.line = *line,
                              .station = metro.stations.front(),
                              .axis = metro.axis,
                              .room = robot.workspace,
                              .building = crew->building->id,
                              .leaves = leaves,
                              .onPlatform = leaves + kDriverWalkMinutes,
                              .run = *run,
                              .back = run->alights + sim::kDwellMinutes + kDriverWalkMinutes};
        }
    }
    driverPlans_[robot.id.value] = {.clockIn = clockIn, .rank = rank->second, .plan = plan};
    return plan;
}

bool CityView::DriverAway(const RobotFrame& robot, double renderTick) const {
    const auto plan = PlanDriver(robot);
    return plan && renderTick >= plan->leaves && renderTick < plan->back;
}

vec3 CityView::CabOf(std::uint32_t line, const sim::TrainState& state) const {
    // Standing at the driving end, which moves to the other end while the
    // train reverses: the driver walks through it.
    const TrainPose pose = TrainPoseOf(state);
    return pose.position + (FrameOf(line).along * (static_cast<float>(state.cab) * kCabMeters * kS)) +
           vec3{0.0F, kCarFloorMeters * kS, 0.0F};
}

std::optional<CityView::Placement> CityView::DriverPlacement(const RobotFrame& robot,
                                                             double renderTick) {
    // Nothing while the driver is in the crew room: it is drawn there.
    const auto plan = PlanDriver(robot);
    if (!plan || renderTick < plan->leaves || renderTick >= plan->back) {
        return std::nullopt;
    }
    walk_.clear();
    const float side = SideOf(sim::Direction::Up);
    const vec3 platform = PlatformSpot(robot.id, plan->station, plan->axis, side);
    const Stairs stairs = StairsOf(plan->station, plan->axis, side);
    const auto exact = [&](vec3 point) {
        walk_.push_back({.position = point, .spread = 0.0F, .building = {}, .floor = 0});
    };
    // From the driver's place in the crew room, out of the depot and up to
    // the platform; the way back is the same, reversed.
    const auto toPlatform = [&] {
        if (const RoomWays* ways = WaysOf(plan->room)) {
            const std::size_t spot =
                SpotFor(robot.id, plan->room, ways->home, ways->fromSpot.size(), plan->leaves,
                        ways->staffSpots, true);
            const Walk& way = ways->fromSpot.empty() ? ways->fromDoor : ways->fromSpot[spot];
            walk_.insert(walk_.end(), way.begin(), way.end());
        }
        for (const vec3 point : StationRoute(plan->building, plan->station)) {
            walk_.push_back({.position = point + vec3{0.0F, kStreetHeight, 0.0F},
                             .spread = 1.0F,
                             .building = {},
                             .floor = 0});
        }
        exact(stairs.bottom);
        exact(stairs.top);
        exact(platform);
    };
    // Through the front door of the train standing at the platform, to the cab.
    const auto cabWay = [&](double standing) {
        const sim::TrainState state = metro_.TrainAt(plan->line, plan->run.train, standing);
        const TrainPose train = TrainPoseOf(state);
        const vec3 right = RightOf(train.forward);
        const float toward = glm::dot(platform - train.position, right) >= 0.0F ? 1.0F : -1.0F;
        const vec3 door = train.position + (train.forward * (kCarOffsets.back() * kS));
        const vec3 floor{0.0F, kCarFloorMeters * kS, 0.0F};
        exact(door + (right * (toward * kDoorOutsideMeters * kS)));
        exact(door + (right * (toward * kDoorInsideMeters * kS)) + floor);
        exact(CabOf(plan->line, state));
    };
    const sim::DriverRun& run = plan->run;
    if (renderTick < plan->onPlatform) {
        toPlatform();
        return FollowWalk(robot, plan->leaves, plan->onPlatform, renderTick, false);
    }
    if (renderTick < run.boards) {
        // Waiting on the platform, facing the track.
        return Placement{.position = platform,
                         .direction = AcrossOf(plan->axis) * -side,
                         .building = {}};
    }
    if (renderTick < run.departs) {
        exact(platform);
        cabWay(run.boards + 0.01);
        return FollowWalk(robot, run.boards, run.departs, renderTick, true);
    }
    if (renderTick < run.alights) {
        // Driving: in the cab for every round trip until the handover.
        const sim::TrainState state = metro_.TrainAt(plan->line, run.train, renderTick);
        return Placement{.position = CabOf(plan->line, state),
                         .direction = TrainPoseOf(state).forward,
                         .building = {},
                         .aboard = true,
                         .riding = true};
    }
    const double off = run.alights + sim::kDwellMinutes;
    if (renderTick < off) {
        // Out of the cab while the train stands at the platform again.
        cabWay(run.alights + 0.01);
        std::ranges::reverse(walk_);
        exact(platform);
        return FollowWalk(robot, run.alights, off, renderTick, true);
    }
    toPlatform();
    std::ranges::reverse(walk_);
    return FollowWalk(robot, off, plan->back, renderTick, false);
}

}  // namespace mechapunk
