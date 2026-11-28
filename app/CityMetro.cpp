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

#include "CityViewInternal.hpp"

namespace mechapunk {
namespace {

// The elevated metro. Row lines run with their deck top 8 m over the street
// and column lines 14 m, so they cross without meeting. Each line has two
// tracks 1.8 m either side of its middle; a station has a side platform
// beside each track, with stairs down to the pavement past its end.
// Lengths in meters.
constexpr float kRowDeck = 8.0F;

constexpr float kColumnDeck = 14.0F;

constexpr float kDeckThickness = 0.7F;

constexpr float kTrackOffset = 1.8F;

constexpr float kDeckHalf = 3.6F;

constexpr float kRailGauge = 0.7F;

// Each rail from its track's middle.
constexpr float kPlatformInner = 3.4F;

constexpr float kPlatformOuter = 6.4F;

constexpr float kPlatformHalfLength = 14.0F;

constexpr float kStairsAcross = (kStreetWidth * 0.5F) + (0.8F * kS);

// Units.
// Depots: beyond the city's edge past each line's first station, a shed
// with a siding for each train, fanning out from the up track.
constexpr float kDepotGap = 4.0F;

constexpr float kDepotLength = 32.0F;

constexpr float kDepotThroat = 12.0F;

// Where the depot's throat starts, in meters along the line: beyond the city's
// edge, and beyond the down track's tail and its crossover too, so the shed
// never stands across a track and no rail runs past the end of the deck.
float DepotStart(float edge) {
    return std::min(edge, -static_cast<float>(sim::kTailMeters) - kDepotThroat) - kDepotGap;
}

constexpr vec3 kTrackSteel{0.22F, 0.23F, 0.27F};

constexpr vec3 kRail{0.55F, 0.56F, 0.6F};

constexpr vec3 kCanopy{0.3F, 0.34F, 0.4F};

constexpr vec3 kStationSign{0.1F, 0.85F, 1.0F};

constexpr vec3 kBumper{1.0F, 0.15F, 0.1F};

float DeckHeight(sim::LineAxis axis) {
    return (axis == sim::LineAxis::Row ? kRowDeck : kColumnDeck) * kS;
}

vec3 AlongOf(sim::LineAxis axis) {
    return axis == sim::LineAxis::Row ? vec3{1.0F, 0.0F, 0.0F} : vec3{0.0F, 0.0F, 1.0F};
}

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
    BatchApart(kMetroBatch, [this] {
        AddElevatedLines();
        AddStations();
    });
}

void CityView::AddElevatedLines() {
    const float deck = kDeckThickness * kS;
    const float tail = static_cast<float>(sim::kTailMeters);
    for (std::uint32_t index = 0; index < metro_.Lines().size(); ++index) {
        const sim::MetroLine& line = metro_.Lines()[index];
        const LineFrame frame = FrameOf(index);
        const float length = static_cast<float>(line.meters.back());
        const float depotStart = DepotStart(frame.edge);
        const float depotEnd = depotStart - kDepotLength;
        const float sidingHalf =
            (static_cast<float>(line.trains - 1) * 0.5F * kSidingSpacing) + 2.5F;
        const float height = frame.origin.y;
        const auto slab = [&](float from, float to, float acrossFrom, float acrossTo,
                              float bottom, float top, vec3 color, bool glows = false) {
            const vec3 a = LinePoint(frame, from, acrossFrom);
            const vec3 b = LinePoint(frame, to, acrossTo);
            AddStaticBox(vec3{std::min(a.x, b.x), bottom, std::min(a.z, b.z)},
                         vec3{std::max(a.x, b.x), top, std::max(a.z, b.z)}, color, glows);
        };
        const auto rails = [&](float from, float to, float across) {
            for (const float rail : {-kRailGauge, kRailGauge}) {
                slab(from, to, across + rail - 0.08F, across + rail + 0.08F, height,
                     height + (0.2F * kS), kRail);
            }
        };
        // Rails that turn from one track to another: a crossover or a
        // siding's switch.
        const auto switchRails = [&](float fromMeters, float fromAcross, float toMeters,
                                     float toAcross) {
            const vec3 a = LinePoint(frame, fromMeters, fromAcross);
            const vec3 b = LinePoint(frame, toMeters, toAcross);
            const vec3 aside = glm::normalize(glm::cross(b - a, vec3{0.0F, 1.0F, 0.0F}));
            for (const float rail : {-kRailGauge, kRailGauge}) {
                const vec3 offset = aside * (rail * kS);
                AddStaticBeam(a + offset, b + offset, 0.16F * kS, 0.2F * kS, kRail);
            }
        };
        const auto bumper = [&](float meters, float across, float facing) {
            slab(meters, meters + (facing * 0.5F), across - 1.2F, across + 1.2F, height,
                 height + (1.0F * kS), kBumper, true);
        };

        // The deck, from the depot to the far tail, and the tracks on it.
        slab(depotStart, length + tail, -kDeckHalf, kDeckHalf, height - deck, height,
             kTrackSteel);
        slab(depotStart, depotStart + kDepotThroat, -sidingHalf, sidingHalf, height - deck,
             height, kTrackSteel);
        rails(depotStart + kDepotThroat, length + tail, -kTrackOffset);
        rails(-tail, length + tail, kTrackOffset);
        // Crossovers in the tails, where trains change ends and tracks.
        switchRails(length + tail, -kTrackOffset, length + (tail * 0.5F), kTrackOffset);
        switchRails(-tail, kTrackOffset, -tail * 0.5F, -kTrackOffset);
        bumper(length + tail, -kTrackOffset, 1.0F);
        bumper(length + tail, kTrackOffset, 1.0F);
        bumper(-tail, kTrackOffset, -1.0F);
        // Columns under the middle of every block, and the tails.
        for (float meters = -40.0F; meters <= length + 40.0F + 1.0F; meters += 80.0F) {
            slab(meters - 0.45F, meters + 0.45F, -0.45F, 0.45F, 0.0F, height - deck,
                 kTrackSteel);
        }

        // The depot: a shed over a siding for each train.
        slab(depotEnd, depotStart, -sidingHalf, sidingHalf, height - deck, height,
             kTrackSteel);
        for (std::uint32_t train = 0; train < line.trains; ++train) {
            const float across = SidingOffset(line, train);
            switchRails(depotStart + kDepotThroat, -kTrackOffset, depotStart, across);
            rails(depotEnd + 0.5F, depotStart, across);
            bumper(depotEnd + 0.5F, across, -1.0F);
        }
        const float roof = height + (5.5F * kS);
        slab(depotEnd, depotStart, -sidingHalf, sidingHalf, roof, roof + (0.3F * kS),
             kCanopy);
        for (const float side : {-1.0F, 1.0F}) {
            slab(depotEnd, depotStart, side * (sidingHalf - 0.3F), side * sidingHalf, height,
                 roof, kConcrete);
        }
        slab(depotEnd, depotEnd + 0.3F, -sidingHalf, sidingHalf, height, roof, kConcrete);
        slab(depotStart - 0.2F, depotStart, -sidingHalf, sidingHalf, roof - (0.4F * kS), roof,
             kStationSign, true);
        for (const float meters : {depotEnd + 1.0F, depotStart - 1.0F}) {
            for (const float side : {-1.0F, 1.0F}) {
                const float across = side * (sidingHalf - 1.0F);
                slab(meters - 0.45F, meters + 0.45F, across - 0.45F, across + 0.45F, 0.0F,
                     height - deck, kTrackSteel);
            }
        }
    }
}

// namespace

CityView::TrainPose CityView::TrainPoseOf(const sim::TrainState& train) const {
    const sim::MetroLine& line = metro_.Lines().at(train.line);
    const LineFrame frame = FrameOf(train.line);
    const float depotStart = DepotStart(frame.edge);
    const float siding = SidingOffset(line, train.train);
    const vec3 parked = LinePoint(frame, depotStart - (kDepotLength * 0.5F), siding);
    switch (train.phase) {
        case sim::TrainState::Phase::InDepot:
            return {.position = parked, .forward = frame.along};
        case sim::TrainState::Phase::Running:
            return {.position = LinePoint(frame, static_cast<float>(train.meters),
                                          static_cast<float>(train.side) * kTrackOffset),
                    .forward = train.towardEnd ? frame.along : -frame.along};
        case sim::TrainState::Phase::LeavingDepot:
        case sim::TrainState::Phase::EnteringDepot:
            break;
    }
    // Out of the siding, through its switch onto the up track, and on to
    // the first platform; or back the same way.
    const std::array<vec3, 4> way{parked, LinePoint(frame, depotStart, siding),
                                  LinePoint(frame, depotStart + kDepotThroat, -kTrackOffset),
                                  LinePoint(frame, 0.0F, -kTrackOffset)};
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
            const vec3 forward = glm::normalize(way[index] - way[index - 1]);
            return {.position = glm::mix(way[index - 1], way[index],
                                         length > 0.0F ? std::min(remaining / length, 1.0F) : 1.0F),
                    .forward = leaving ? forward : -forward};
        }
        remaining -= length;
    }
    return {.position = parked, .forward = frame.along};
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
        for (const float car : kCarOffsets) {
            SubmitModel(queue, Prop(PropKind::TrainCar),
                        train.position + (train.forward * (car * kS)), yaw, 0.0F,
                        vec3(1.0F), kS);
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
