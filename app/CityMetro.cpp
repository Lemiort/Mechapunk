// The elevated metro as drawn: lines, stations, stairs, platforms, trains,
// doors and the routes riders take to stations.

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

#include "CityMetroInternal.hpp"

namespace mechapunk {
namespace {

// Doors: a leaf each side of a car's middle on each side, 1.3 m wide, on the
// outside of the car's walls; each slides 1.25 m back over the wall to open.
constexpr float kDoorLeafMeters = 1.3F;

constexpr float kDoorPlaneMeters = 1.43F;

constexpr float kDoorSlideMeters = 1.25F;

// A car's end from its middle, where the lamps sit.
constexpr float kCarEndMeters = 6.52F;

// Rider places: up to 4.5 m either way of a car's middle, 0.7 m either side.
constexpr float kAboardAlongMeters = 4.5F;

constexpr float kAboardSideMeters = 0.7F;

}  // namespace

// namespace

CityView::LineFrame CityView::FrameOf(std::uint32_t line) const {
    const sim::MetroLine& metro = metro_.Lines().at(line);
    const sim::CityNode& first = city_.nodes.at(city_.stations.at(metro.stations.front()).node.value);
    return {.origin = NodePosition(first.id) + vec3{0.0F, DeckHeight(metro.axis), 0.0F},
            .along = AlongOf(metro.axis),
            .across = AcrossOf(metro.axis),
            // Meters back from the first station to the grid's west or north edge,
            // where the depot stands beyond: a generated grid starts at 0, a town's
            // rings of streets reach below it.
            .edge = -(static_cast<float>(metro.axis == sim::LineAxis::Row ? first.x : first.z) -
                      ((metro.axis == sim::LineAxis::Row ? minimum_.x : minimum_.z) / kS))};
}

CityView::Stairs CityView::StairsOf(std::uint32_t station, sim::LineAxis axis,
                                    float side) const {
    // Along the pavement past the platform's far end, a step of 1.5 m for
    // each meter they climb.
    const float height = DeckHeight(axis);
    const vec3 base = NodePosition(city_.stations.at(station).node) +
                      (AcrossOf(axis) * (side * kStairsAcross));
    const vec3 top = base + (AlongOf(axis) * (kPlatformHalfLength * kS)) + vec3{0.0F, height, 0.0F};
    return {.top = top,
            .bottom = base + (AlongOf(axis) * ((kPlatformHalfLength * kS) + (1.5F * height))) +
                      vec3{0.0F, kStreetHeight, 0.0F}};
}

vec3 CityView::PlatformSpot(sim::EntityId robot, std::uint32_t station,
                            sim::LineAxis axis, float side) const {
    // Somewhere along the platform under its canopy, clear of the edge.
    const std::uint64_t hash = robot.value * 2654435761ULL;
    const float along = ((static_cast<float>(hash & 0xFFU) / 255.0F) * 2.0F - 1.0F) * 11.0F;
    const float across = kPlatformInner + 0.6F +
                         ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) *
                          (kPlatformOuter - kPlatformInner - 1.0F));
    return NodePosition(city_.stations.at(station).node) +
           vec3{0.0F, DeckHeight(axis), 0.0F} + (AlongOf(axis) * (along * kS)) +
           (AcrossOf(axis) * (side * across * kS));
}

void CityView::AddStations() {
    const float deck = kDeckThickness * kS;
    for (std::uint32_t station = 0; station < city_.stations.size(); ++station) {
        const vec3 node = NodePosition(city_.stations[station].node);
        for (const sim::LineAxis axis : {sim::LineAxis::Row, sim::LineAxis::Column}) {
            if (!metro_.LineOf(station, axis)) {
                continue;
            }
            const vec3 along = AlongOf(axis);
            const vec3 across = AcrossOf(axis);
            const vec3 up{0.0F, DeckHeight(axis), 0.0F};
            const auto box = [&](float alongFrom, float alongTo, float acrossFrom,
                                 float acrossTo, float bottom, float top, vec3 color,
                                 bool glows = false) {
                const vec3 a = node + (along * (alongFrom * kS)) + (across * (acrossFrom * kS));
                const vec3 b = node + (along * (alongTo * kS)) + (across * (acrossTo * kS));
                AddStaticBox(vec3{std::min(a.x, b.x), bottom, std::min(a.z, b.z)},
                             vec3{std::max(a.x, b.x), top, std::max(a.z, b.z)}, color, glows);
            };
            for (const float side : {-1.0F, 1.0F}) {
                const float middle = (kPlatformInner + kPlatformOuter) * 0.5F;
                // The platform, its glowing edge, and a canopy on posts.
                box(-kPlatformHalfLength, kPlatformHalfLength, side * kPlatformInner,
                    side * kPlatformOuter, up.y - deck, up.y, kTrackSteel);
                box(-kPlatformHalfLength, kPlatformHalfLength, side * kPlatformInner,
                    side * (kPlatformInner + 0.25F), up.y, up.y + (0.03F * kS), kStationSign,
                    true);
                // The canopy is an open steel frame, so the riders waiting
                // under it show from above.
                for (const float edge : {kPlatformInner + 0.2F, kPlatformOuter}) {
                    box(-12.0F, 12.0F, side * edge, side * (edge + 0.2F), up.y + (3.3F * kS),
                        up.y + (3.5F * kS), kCanopy);
                }
                for (float rib = -12.0F; rib <= 12.01F; rib += 3.0F) {
                    box(rib - 0.08F, rib + 0.08F, side * (kPlatformInner + 0.2F),
                        side * (kPlatformOuter + 0.2F), up.y + (3.35F * kS), up.y + (3.45F * kS),
                        kCanopy);
                }
                for (const float post : {-11.0F, 0.0F, 11.0F}) {
                    box(post - 0.1F, post + 0.1F, side * (kPlatformOuter - 0.3F),
                        side * (kPlatformOuter - 0.1F), up.y, up.y + (3.3F * kS), kTrackSteel);
                }
                for (const float column : {-11.0F, 11.0F}) {
                    box(column - 0.45F, column + 0.45F, side * (middle - 0.45F),
                        side * (middle + 0.45F), 0.0F, up.y - deck, kTrackSteel);
                }
                // Stairs down to the pavement, and the M sign at their foot.
                const Stairs stairs = StairsOf(station, axis, side);
                const vec3 run = vec3{stairs.top.x - stairs.bottom.x, 0.0F,
                                      stairs.top.z - stairs.bottom.z};
                const vec3 width = across * (0.8F * kS);
                constexpr int kSteps = 16;
                for (int step = 0; step < kSteps; ++step) {
                    const float t = static_cast<float>(step + 1) / static_cast<float>(kSteps);
                    const vec3 at = glm::mix(stairs.bottom, stairs.top, t);
                    const vec3 a = at - width;
                    const vec3 b = at + (run / static_cast<float>(kSteps)) + width;
                    AddStaticBox(vec3{std::min(a.x, b.x), stairs.bottom.y, std::min(a.z, b.z)},
                                 vec3{std::max(a.x, b.x), at.y, std::max(a.z, b.z)},
                                 kTrackSteel);
                }
                const vec3 sign = stairs.bottom + (along * (1.1F * kS));
                AddStaticBox(sign - vec3{0.08F * kS, 0.0F, 0.08F * kS},
                             sign + vec3{0.08F * kS, 3.0F * kS, 0.08F * kS}, kTrackSteel);
                AddStaticBox(sign + vec3{-0.28F * kS, 3.0F * kS, -0.28F * kS},
                             sign + vec3{0.28F * kS, 3.6F * kS, 0.28F * kS}, kStationSign, true);
            }
        }
    }
}

// The metro as batches of its own (task K, step K13). A grown city redraws it
// whole: a line that gained a station is one longer deck, and a depot moves to
// the city's new edge, with nothing of the old drawing left under them.
void CityView::DrawMetro() {
    // Each line's depot first: the lines are drawn up to it, and the trains
    // leave from it.
    depots_.clear();
    for (std::uint32_t line = 0; line < metro_.Lines().size(); ++line) {
        depots_.push_back(FindDepot(line));
    }
    BatchApart(kMetroBatch, [this] {
        AddElevatedLines();
        AddStations();
    });
}

void CityView::AddTrackSlab(const LineFrame& frame, float from, float to, float acrossFrom,
                            float acrossTo, float bottom, float top, vec3 color, bool glows) {
    const vec3 a = LinePoint(frame, from, acrossFrom);
    const vec3 b = LinePoint(frame, to, acrossTo);
    AddStaticBox(vec3{std::min(a.x, b.x), bottom, std::min(a.z, b.z)},
                 vec3{std::max(a.x, b.x), top, std::max(a.z, b.z)}, color, glows);
}

void CityView::AddRails(const LineFrame& frame, float from, float to, float across) {
    const float height = frame.origin.y;
    for (const float rail : {-kRailGauge, kRailGauge}) {
        AddTrackSlab(frame, from, to, across + rail - 0.08F, across + rail + 0.08F, height,
                     height + (0.2F * kS), kRail);
    }
}

void CityView::AddRailsBetween(vec3 from, vec3 to) {
    const vec3 aside = glm::normalize(glm::cross(to - from, vec3{0.0F, 1.0F, 0.0F}));
    for (const float rail : {-kRailGauge, kRailGauge}) {
        const vec3 offset = aside * (rail * kS);
        AddStaticBeam(from + offset, to + offset, 0.16F * kS, 0.2F * kS, kRail);
    }
}

void CityView::AddSwitchRails(const LineFrame& frame, float fromMeters, float fromAcross,
                              float toMeters, float toAcross) {
    AddRailsBetween(LinePoint(frame, fromMeters, fromAcross), LinePoint(frame, toMeters, toAcross));
}

void CityView::AddTrackBetween(vec3 from, vec3 to, bool rails) {
    // A little longer than the piece, so the pieces of a curve leave no gaps.
    const float deck = kDeckThickness * kS;
    const vec3 down{0.0F, deck, 0.0F};
    const vec3 overlap = glm::normalize(to - from) * (0.4F * kS);
    AddStaticBeam(from - overlap - down, to + overlap - down, 2.0F * kDeckHalf * kS * 0.9F, deck,
                  kTrackSteel);
    if (rails) {
        AddRailsBetween(from, to);
    }
}

void CityView::AddBumper(const LineFrame& frame, float meters, float across, float facing) {
    const float height = frame.origin.y;
    AddTrackSlab(frame, meters, meters + (facing * 0.5F), across - 1.2F, across + 1.2F, height,
                 height + (1.0F * kS), kBumper, true);
}

void CityView::AddElevatedLines() {
    const float deck = kDeckThickness * kS;
    const float tail = static_cast<float>(sim::kTailMeters);
    for (std::uint32_t index = 0; index < metro_.Lines().size(); ++index) {
        const sim::MetroLine& line = metro_.Lines()[index];
        const LineFrame frame = FrameOf(index);
        const float length = static_cast<float>(line.meters.back());
        const float height = frame.origin.y;

        // The deck from tail to tail, and the tracks on it.
        AddTrackSlab(frame, -tail, length + tail, -kDeckHalf, kDeckHalf, height - deck, height,
                     kTrackSteel);
        AddRails(frame, -tail, length + tail, -kTrackOffset);
        AddRails(frame, -tail, length + tail, kTrackOffset);
        // Crossovers in the tails, where trains change ends and tracks.
        AddSwitchRails(frame, length + tail, -kTrackOffset, length + (tail * 0.5F), kTrackOffset);
        AddSwitchRails(frame, -tail, kTrackOffset, -tail * 0.5F, -kTrackOffset);
        // The up track runs on past the far tail to a depot beyond it.
        const std::optional<Depot>& depot = depots_.at(index);
        if (!depot || DepotJunction(*depot) <= length + tail) {
            AddBumper(frame, length + tail, -kTrackOffset, 1.0F);
        }
        AddBumper(frame, length + tail, kTrackOffset, 1.0F);
        AddBumper(frame, -tail, kTrackOffset, -1.0F);
        // Columns under the middle of every block, and the tails.
        for (float meters = -40.0F; meters <= length + 40.0F + 1.0F; meters += 80.0F) {
            AddTrackSlab(frame, meters - 0.45F, meters + 0.45F, -0.45F, 0.45F, 0.0F,
                         height - deck, kTrackSteel);
        }
        AddDepot(index, frame);
    }
}

CityView::TrainPose CityView::TrainPoseOf(const sim::TrainState& train) const {
    const LineFrame frame = FrameOf(train.line);
    if (train.phase == sim::TrainState::Phase::Running) {
        return {.position = LinePoint(frame, static_cast<float>(train.meters),
                                      static_cast<float>(train.side) * kTrackOffset),
                .forward = train.towardEnd ? frame.along : -frame.along};
    }
    // Parked in its siding; or out of it to the first platform, or back the
    // same way.
    const std::vector<vec3> way = DepotWay(train.line, train.train);
    const auto heading = [&](std::size_t index) {
        const vec3 step = way[index] - way[index - 1];
        return glm::length(step) > 0.0F ? glm::normalize(step) : frame.along;
    };
    if (train.phase == sim::TrainState::Phase::InDepot) {
        return {.position = way.front(), .forward = heading(1)};
    }
    const bool leaving = train.phase == sim::TrainState::Phase::LeavingDepot;
    const float t = std::clamp(static_cast<float>(leaving ? train.progress : 1.0 - train.progress),
                               0.0F, 1.0F);
    float total = 0.0F;
    for (std::size_t index = 1; index < way.size(); ++index) {
        total += glm::distance(way[index - 1], way[index]);
    }
    float remaining = t * total;
    for (std::size_t index = 1; index < way.size(); ++index) {
        const float length = glm::distance(way[index - 1], way[index]);
        if (remaining <= length || index + 1 == way.size()) {
            const vec3 forward = heading(index);
            return {.position = glm::mix(way[index - 1], way[index],
                                         length > 0.0F ? std::min(remaining / length, 1.0F) : 1.0F),
                    .forward = leaving ? forward : -forward};
        }
        remaining -= length;
    }
    return {.position = way.front(), .forward = heading(1)};
}

vec3 CityView::PlaceAboard(sim::EntityId robot, const TrainPose& train) const {
    const std::uint64_t hash = robot.value * 0x9E3779B97F4A7C15ULL;
    const float car = kCarOffsets.at(hash & 1U);
    const float along = ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) * 2.0F - 1.0F) *
                        kAboardAlongMeters;
    const float side = ((hash >> 16U) & 1U) != 0 ? kAboardSideMeters : -kAboardSideMeters;
    return train.position + (train.forward * ((car + along) * kS)) +
           (RightOf(train.forward) * (side * kS)) + vec3{0.0F, kCarFloorMeters * kS, 0.0F};
}

std::pair<vec3, vec3> CityView::DoorFor(sim::EntityId robot, const TrainPose& train,
                                        vec3 from) const {
    const std::uint64_t hash = robot.value * 0x9E3779B97F4A7C15ULL;
    const float car = kCarOffsets.at(hash & 1U);
    const vec3 right = RightOf(train.forward);
    const float side = glm::dot(from - train.position, right) >= 0.0F ? 1.0F : -1.0F;
    const vec3 middle = train.position + (train.forward * (car * kS));
    const vec3 floor{0.0F, kCarFloorMeters * kS, 0.0F};
    return {middle + (right * (side * kDoorOutsideMeters * kS)),
            middle + (right * (side * kDoorInsideMeters * kS)) + floor};
}

void CityView::SubmitTrains(RenderQueue& queue, double renderTick) {
    for (const sim::TrainState& state : metro_.TrainsAt(renderTick)) {
        const TrainPose train = TrainPoseOf(state);
        const float yaw = glm::degrees(std::atan2(train.forward.x, train.forward.z));
        const bool running = state.phase == sim::TrainState::Phase::Running;
        const LineFrame frame = FrameOf(state.line);
        // The doors on the platform's side slide open while it stands there.
        const vec3 right = RightOf(train.forward);
        const float platform =
            glm::dot(frame.across * static_cast<float>(state.side), right) > 0.0F ? 1.0F : -1.0F;
        const float slide = running ? static_cast<float>(state.doors) * kDoorSlideMeters : 0.0F;
        for (const float car : kCarOffsets) {
            SubmitModel(queue, Prop(PropKind::TrainCar),
                        train.position + (train.forward * (car * kS)), yaw, 0.0F,
                        vec3(1.0F), kS);
            for (const float side : {-1.0F, 1.0F}) {
                const float open = side == platform ? slide : 0.0F;
                for (const float leaf : {-1.0F, 1.0F}) {
                    const float along = car + (leaf * ((kDoorLeafMeters * 0.5F) + open));
                    SubmitModel(queue, Prop(PropKind::TrainDoor),
                                train.position + (train.forward * (along * kS)) +
                                    (right * (side * kDoorPlaneMeters * kS)),
                                yaw, 0.0F, vec3(1.0F), kS);
                }
            }
        }
        // Headlights at the driving end and tail lights at the other, out of
        // the depot; a train reversing in a tail track changes them over.
        if (state.phase != sim::TrainState::Phase::InDepot) {
            const vec3 ahead =
                running ? frame.along * (state.cab >= 0.0 ? 1.0F : -1.0F) : train.forward;
            const float aheadYaw = glm::degrees(std::atan2(ahead.x, ahead.z));
            const vec3 end = ahead * ((kCarOffsets.back() + kCarEndMeters) * kS);
            SubmitModel(queue, Prop(PropKind::TrainHeadlights), train.position + end, aheadYaw,
                        0.0F, vec3(1.0F), kS);
            SubmitModel(queue, Prop(PropKind::TrainTailLights), train.position - end, aheadYaw,
                        0.0F, vec3(1.0F), kS);
        }
    }
}

const std::vector<sim::Leg>& CityView::LegsOf(const RobotFrame& robot) {
    RideLegs& ride = rideLegs_[robot.id.value];
    if (!ride.known || ride.boardsAt != robot.boardsAt || ride.from != robot.rideFrom ||
        ride.to != robot.rideTo) {
        // The same trains the simulation planned the ride on.
        ride = {.known = true,
                .boardsAt = robot.boardsAt,
                .from = robot.rideFrom,
                .to = robot.rideTo,
                .legs = robot.rideFrom < city_.stations.size() && robot.rideTo < city_.stations.size()
                            ? metro_.Route(robot.rideFrom, robot.rideTo,
                                           static_cast<double>(robot.boardsAt))
                            : std::vector<sim::Leg>{}};
    }
    return ride.legs;
}

const std::vector<vec3>& CityView::StationRoute(sim::EntityId building,
                                                std::uint32_t station) {
    const auto key = std::make_pair(building.value, station);
    if (const auto cached = stationRoutes_.find(key); cached != stationRoutes_.end()) {
        return cached->second;
    }
    std::vector<vec3> points;
    const sim::Building* found = city_.FindBuilding(building);
    if (found != nullptr && station < city_.stations.size()) {
        for (const sim::NodeId node :
             routes_.Path(found->entrance, city_.stations[station].node)) {
            points.push_back(NodePosition(node));
        }
    }
    return stationRoutes_.emplace(key, std::move(points)).first->second;
}

}  // namespace mechapunk
