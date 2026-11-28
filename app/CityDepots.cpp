// The metro's depots. A line whose first station has a depot building (a
// building there whose rooms are all crew rooms) keeps its trains in a shed on
// that building's roof, at the line's deck height: its sidings run across the
// line, and a curve off the up track turns into it, toward the line's start or
// its end as the building stands. A line without one keeps the old shed beyond
// the city's edge. A town's depot stands at the intersection on the line's
// street nearest its first station that had a free corner: maybe a later
// station, maybe beyond either end, where the up track runs on to it. Its
// trains run along the up track between it and the first platform. Also the
// way a train takes between its siding and that platform.

#include "CityView.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>
#include <glm/geometric.hpp>

#include "CityMetroInternal.hpp"

namespace mechapunk {
namespace {

// Beyond the edge: a shed with a siding for each train, fanning out from the
// up track. Meters.
constexpr float kEdgeGap = 4.0F;

constexpr float kEdgeLength = 32.0F;

constexpr float kEdgeThroat = 12.0F;

// On a roof, in meters across the line from its middle, out on the depot's
// side: the curve off the up track, the lead track on to the throat, the
// throat fanning out to the sidings, and the sidings under the shed.
constexpr float kCurveRadius = 8.5F;

constexpr int kCurveSteps = 8;

constexpr float kThroatFrom = 11.0F;

constexpr float kSidingsFrom = 19.0F;

constexpr float kSidingsTo = 46.5F;

constexpr float kShedFrom = 18.0F;

// Short of the building across the block, which starts 47.4 m out beside a
// column line (51.4 m beside a row line).
constexpr float kShedTo = 47.0F;

constexpr float kShedHeight = 5.5F;

// The sidings keep this far inside the building's ends along the line.
constexpr float kSidingInset = 2.5F;

// Where the edge depot's throat starts, in meters along the line: beyond the
// city's edge, and beyond the down track's tail and its crossover too, so the
// shed never stands across a track.
float EdgeDepotStart(float edge) {
    return std::min(edge, -TrackEndMeters() - kEdgeThroat) - kEdgeGap;
}

}  // namespace

std::optional<CityView::Depot> CityView::FindDepot(std::uint32_t line,
                                                  sim::EntityId depot) const {
    const sim::Building* building = depot.IsValid() ? city_.FindBuilding(depot) : nullptr;
    if (building == nullptr) {
        return std::nullopt;
    }
    const sim::MetroLine& metro = metro_.Lines().at(line);
    const LineFrame frame = FrameOf(line);
    const vec3 ground{frame.origin.x, 0.0F, frame.origin.z};
    // How far along the line its intersection is, on the line's street, and
    // its plot from there.
    const float corner = glm::dot((NodePosition(building->entrance) - ground) / kS, frame.along);
    const vec3 offset = (PlotCenter(*building) - NodePosition(building->entrance)) / kS;
    const float along = corner + glm::dot(offset, frame.along);
    Depot found{.building = building->id.value,
                .alongSign = along < corner ? -1.0F : 1.0F,
                .acrossSign = glm::dot(offset, frame.across) < 0.0F ? -1.0F : 1.0F};
    {
        // The lead track at the building's end away from the station, the
        // sidings from there back toward it, squeezed when there are many.
        const float half = (metro.axis == sim::LineAxis::Row ? kFootprintWidthMeters
                                                             : kFootprintDepthMeters) *
                           0.5F;
        found.lead = along + (found.alongSign * (half - kSidingInset));
        const std::uint32_t trains = std::max(metro.trains, 1U);
        const float spacing =
            trains > 1 ? std::min(kSidingSpacing, 2.0F * (half - kSidingInset) /
                                                      static_cast<float>(trains - 1))
                       : 0.0F;
        for (std::uint32_t train = 0; train < trains; ++train) {
            found.sidings.push_back(found.lead -
                                    (found.alongSign * spacing * static_cast<float>(train)));
        }
    }
    return found;
}

bool CityView::DepotsMoved() const {
    const std::vector<sim::EntityId> depots = sim::LineDepots(city_, metro_);
    if (depots_.size() != depots.size()) {
        return true;
    }
    for (std::size_t line = 0; line < depots.size(); ++line) {
        if (depots[line].value != (depots_[line] ? depots_[line]->building : 0)) {
            return true;
        }
    }
    return false;
}

float CityView::DepotJunction(const Depot& depot) {
    return depot.lead - (depot.alongSign * kCurveRadius);
}

vec3 CityView::DepotCurvePoint(const LineFrame& frame, const Depot& depot, float t,
                               float outward) const {
    // A quarter circle from the up track, heading along the line toward the
    // lead track, to the lead track, heading straight out across it; a rail
    // follows its own circle, `outward` meters wider.
    const float angle = t * std::numbers::pi_v<float> * 0.5F;
    const float radius = kCurveRadius + outward;
    const float centerAlong = depot.lead - (depot.alongSign * kCurveRadius);
    const float centerAcross = -kTrackOffset + (depot.acrossSign * kCurveRadius);
    return LinePoint(frame, centerAlong + (depot.alongSign * radius * std::sin(angle)),
                     centerAcross - (depot.acrossSign * radius * std::cos(angle)));
}

std::vector<vec3> CityView::DepotWay(std::uint32_t line, std::uint32_t train) const {
    const LineFrame frame = FrameOf(line);
    const vec3 platform = LinePoint(frame, 0.0F, -kTrackOffset);
    if (line >= depots_.size() || !depots_[line]) {
        // Out of the siding, through its switch onto the up track.
        const float start = EdgeDepotStart(frame.edge);
        const float siding = SidingOffset(metro_.Lines().at(line), train);
        return {LinePoint(frame, start - (kEdgeLength * 0.5F), siding),
                LinePoint(frame, start, siding),
                LinePoint(frame, start + kEdgeThroat, -kTrackOffset), platform};
    }
    // Out of the shed and the throat, along the lead track, round the curve
    // onto the up track, and along it to the platform: back toward the start
    // when the depot stands past the station.
    const Depot& depot = *depots_[line];
    const float siding = depot.sidings.at(std::min<std::size_t>(train, depot.sidings.size() - 1));
    const float out = depot.acrossSign;
    std::vector<vec3> way{LinePoint(frame, siding, out * (kSidingsFrom + kSidingsTo) * 0.5F),
                          LinePoint(frame, siding, out * kSidingsFrom),
                          LinePoint(frame, depot.lead, out * kThroatFrom)};
    for (int step = kCurveSteps; step >= 0; --step) {
        way.push_back(DepotCurvePoint(frame, depot,
                                      static_cast<float>(step) / static_cast<float>(kCurveSteps)));
    }
    way.push_back(platform);
    return way;
}

void CityView::AddDepot(std::uint32_t index, const LineFrame& frame) {
    const sim::MetroLine& line = metro_.Lines().at(index);
    const float deck = kDeckThickness * kS;
    const float height = frame.origin.y;
    const float tail = TrackEndMeters();
    const auto column = [&](float meters, float across) {
        AddTrackSlab(frame, meters - 0.45F, meters + 0.45F, across - 0.45F, across + 0.45F, 0.0F,
                     height - deck, kTrackSteel);
    };
    if (!depots_.at(index)) {
        // Beyond the edge: the up track runs on past its tail to the throat,
        // and a shed stands over a siding for each train.
        const float depotStart = EdgeDepotStart(frame.edge);
        const float depotEnd = depotStart - kEdgeLength;
        const float sidingHalf =
            (static_cast<float>(line.trains - 1) * 0.5F * kSidingSpacing) + 2.5F;
        AddTrackSlab(frame, depotStart, -tail, -kDeckHalf, kDeckHalf, height - deck, height,
                     kTrackSteel);
        AddTrackSlab(frame, depotStart, depotStart + kEdgeThroat, -sidingHalf, sidingHalf,
                     height - deck, height, kTrackSteel);
        AddRails(frame, depotStart + kEdgeThroat, -tail, -kTrackOffset);
        AddTrackSlab(frame, depotEnd, depotStart, -sidingHalf, sidingHalf, height - deck, height,
                     kTrackSteel);
        for (std::uint32_t train = 0; train < line.trains; ++train) {
            const float across = SidingOffset(line, train);
            AddSwitchRails(frame, depotStart + kEdgeThroat, -kTrackOffset, depotStart, across);
            AddRails(frame, depotEnd + 0.5F, depotStart, across);
            AddBumper(frame, depotEnd + 0.5F, across, -1.0F);
        }
        const float roof = height + (kShedHeight * kS);
        AddTrackSlab(frame, depotEnd, depotStart, -sidingHalf, sidingHalf, roof,
                     roof + (0.3F * kS), kCanopy);
        for (const float side : {-1.0F, 1.0F}) {
            AddTrackSlab(frame, depotEnd, depotStart, side * (sidingHalf - 0.3F),
                         side * sidingHalf, height, roof, kConcrete);
        }
        AddTrackSlab(frame, depotEnd, depotEnd + 0.3F, -sidingHalf, sidingHalf, height, roof,
                     kConcrete);
        AddTrackSlab(frame, depotStart - 0.2F, depotStart, -sidingHalf, sidingHalf,
                     roof - (0.4F * kS), roof, kStationSign, true);
        for (const float meters : {depotEnd + 1.0F, depotStart - 1.0F, depotStart + kEdgeThroat}) {
            for (const float side : {-1.0F, 1.0F}) {
                column(meters, side * (sidingHalf - 1.0F));
            }
        }
        return;
    }

    // On the roof. The up track ends at its tail like the down track, or runs
    // on beyond either tail to the curve.
    const Depot& depot = *depots_[index];
    const float out = depot.acrossSign;
    const float junction = DepotJunction(depot);
    const float length = static_cast<float>(line.meters.back());
    const auto extend = [&](float from, float to, float end, float facing) {
        AddTrackSlab(frame, from, to, -kDeckHalf, kDeckHalf, height - deck, height, kTrackSteel);
        AddRails(frame, from, to, -kTrackOffset);
        AddBumper(frame, end, -kTrackOffset, facing);
        for (float meters = from + 2.0F; meters < to; meters += 20.0F) {
            column(meters, 0.0F);
        }
    };
    if (junction < -tail) {
        extend(junction - 4.0F, -tail, junction - 4.0F, -1.0F);
    } else {
        AddBumper(frame, -tail, -kTrackOffset, -1.0F);
    }
    if (junction > length + tail) {
        extend(length + tail, junction + 4.0F, junction + 4.0F, 1.0F);
    }
    // The curve off the up track, in short straight pieces: the deck along
    // the track's circle, each rail along its own, so the pieces of the outer
    // rail meet as those of the inner one do. Then the lead track on to the
    // throat.
    for (int step = 0; step < kCurveSteps; ++step) {
        const float from = static_cast<float>(step) / static_cast<float>(kCurveSteps);
        const float to = static_cast<float>(step + 1) / static_cast<float>(kCurveSteps);
        AddTrackBetween(DepotCurvePoint(frame, depot, from), DepotCurvePoint(frame, depot, to),
                        false);
        for (const float rail : {-kRailGauge, kRailGauge}) {
            const vec3 a = DepotCurvePoint(frame, depot, from, rail);
            const vec3 b = DepotCurvePoint(frame, depot, to, rail);
            // A hair longer, to close the joint's wedge on the outside.
            const vec3 overlap = glm::normalize(b - a) * (0.05F * kS);
            AddStaticBeam(a - overlap, b + overlap, 0.16F * kS, 0.2F * kS, kRail);
        }
    }
    AddTrackBetween(DepotCurvePoint(frame, depot, 1.0F),
                    LinePoint(frame, depot.lead, out * kThroatFrom));
    // The throat and the sidings on one deck from end to end of the building,
    // a switch out to each siding, and a buffer at its end.
    const auto [lowest, highest] = std::ranges::minmax(depot.sidings);
    const float low = lowest - kSidingInset;
    const float high = highest + kSidingInset;
    AddTrackSlab(frame, low, high, out * kThroatFrom, out * kShedTo, height - deck, height,
                 kTrackSteel);
    for (const float siding : depot.sidings) {
        AddSwitchRails(frame, depot.lead, out * kThroatFrom, siding, out * kSidingsFrom);
        AddRailsBetween(LinePoint(frame, siding, out * kSidingsFrom),
                        LinePoint(frame, siding, out * kSidingsTo));
        AddTrackSlab(frame, siding - 1.2F, siding + 1.2F, out * kSidingsTo,
                     out * (kSidingsTo + 0.5F), height, height + (1.0F * kS), kBumper, true);
    }
    // The shed: side and back walls, a roof, and a glowing strip over its mouth.
    const float roof = height + (kShedHeight * kS);
    AddTrackSlab(frame, low, high, out * kShedFrom, out * kShedTo, roof, roof + (0.3F * kS),
                 kCanopy);
    AddTrackSlab(frame, low, low + 0.3F, out * kShedFrom, out * kShedTo, height, roof, kConcrete);
    AddTrackSlab(frame, high - 0.3F, high, out * kShedFrom, out * kShedTo, height, roof, kConcrete);
    AddTrackSlab(frame, low, high, out * (kShedTo - 0.3F), out * kShedTo, height, roof, kConcrete);
    AddTrackSlab(frame, low, high, out * kShedFrom, out * (kShedFrom + 0.2F), roof - (0.4F * kS),
                 roof, kStationSign, true);
    // Columns from the ground: through the building under the throat, and
    // in the yard behind it under the shed.
    for (const float meters : {low + 1.0F, high - 1.0F}) {
        for (const float across : {kThroatFrom + 1.0F, (kSidingsFrom + kSidingsTo) * 0.5F,
                                   kShedTo - 1.0F}) {
            column(meters, out * across);
        }
    }
}

}  // namespace mechapunk
