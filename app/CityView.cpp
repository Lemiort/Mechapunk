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
#include <tuple>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
using glm::vec2;
using glm::vec3;

constexpr float kS = CityView::kUnitsPerMeter;
constexpr float kStreetWidth = 10.0F * kS;
constexpr float kStreetHeight = 0.08F * kS;
constexpr float kBuildingWidth = kFootprintWidthMeters * kS;
constexpr float kBuildingDepth = kFootprintDepthMeters * kS;
constexpr float kSlabMeters = 0.2F;
// Distance from an intersection to the center of a building beside it:
// half a street, a pavement and half the building.
constexpr vec2 kQuadrantOffset{(kStreetWidth * 0.5F) + (1.6F * kS) +
                                   (kBuildingWidth * 0.5F),
                               (kStreetWidth * 0.5F) + (1.6F * kS) +
                                   (kBuildingDepth * 0.5F)};
constexpr float kWalkerPickRadius = 0.7F * kS;
constexpr float kRobotSpread = 3.0F * kS;
constexpr float kNeonPower = 2.5F;
constexpr float kGlassOpacity = 0.3F;
constexpr float kHighlightOpacity = 0.3F;
// Separate random stream so decoration never affects the simulation.
constexpr std::uint64_t kDecorationStream = 100;
// Robots in apartments sleep in this part of the day, else sit on the sofa.
constexpr double kBedtime = (21.0 * 60.0) + 30.0;
constexpr double kWakeUp = 7.0 * 60.0;

constexpr vec3 kHabitatNeon{0.1F, 0.9F, 1.0F};
constexpr vec3 kWorkplaceNeon{1.0F, 0.2F, 0.8F};
constexpr vec3 kGrass{0.16F, 0.32F, 0.17F};
// The elevated metro: deck top 8 m over the street, a platform over each
// station's intersection, and stairs up from the pavement.
constexpr float kTrackHeight = 8.0F * kS;
constexpr float kPlatformHalf = 6.5F * kS;
constexpr float kStairsRun = 12.0F * kS;
constexpr float kStairsAcross = (kStreetWidth * 0.5F) + (0.8F * kS);
constexpr vec3 kTrackSteel{0.22F, 0.23F, 0.27F};
constexpr vec3 kRail{0.55F, 0.56F, 0.6F};
constexpr vec3 kCanopy{0.3F, 0.34F, 0.4F};
constexpr vec3 kStationSign{0.1F, 0.85F, 1.0F};
constexpr vec3 kPavement{0.36F, 0.36F, 0.38F};
constexpr vec3 kFacadeGlass{0.13F, 0.17F, 0.24F};
constexpr vec3 kConcrete{0.3F, 0.31F, 0.35F};
constexpr vec3 kGlassColor{0.6F, 0.8F, 0.92F};
constexpr vec3 kSelected{1.0F, 1.0F, 1.0F};
constexpr std::array kRobotAccents{
    vec3{0.85F, 0.3F, 0.2F}, vec3{0.2F, 0.55F, 0.85F}, vec3{0.9F, 0.75F, 0.2F},
    vec3{0.3F, 0.7F, 0.4F},  vec3{0.7F, 0.35F, 0.8F},  vec3{0.25F, 0.75F, 0.75F},
    vec3{0.9F, 0.5F, 0.15F}, vec3{0.55F, 0.55F, 0.6F}};

vec3 StoreNeon(sim::RoomKind kind) {
    switch (kind) {
        case sim::RoomKind::Bar:
            return {1.0F, 0.15F, 0.6F};
        case sim::RoomKind::Diner:
            return {1.0F, 0.5F, 0.1F};
        case sim::RoomKind::ChargingBay:
            return {0.2F, 1.0F, 0.4F};
        case sim::RoomKind::RepairShop:
            return {0.25F, 0.5F, 1.0F};
        case sim::RoomKind::Arcade:
            return {0.7F, 0.2F, 1.0F};
        case sim::RoomKind::Shop:
            return {1.0F, 0.9F, 0.2F};
        case sim::RoomKind::Clinic:
            return {0.6F, 1.0F, 0.9F};
        case sim::RoomKind::BlackMarket:
            return {0.9F, 0.08F, 0.08F};
        default:
            return {1.0F, 1.0F, 1.0F};
    }
}
constexpr vec3 kSunColor{1.0F, 0.86F, 0.7F};
constexpr vec3 kDaySky{0.45F, 0.47F, 0.55F};
constexpr vec3 kNightSky{0.04F, 0.04F, 0.1F};
// Faint light from above while a building is open, so dark rooms still read.
constexpr vec3 kInspectionLight{0.18F, 0.18F, 0.21F};
// Room lamps: warm at home and in venues, cool at work. Lamps beyond the
// pool stay dark.
constexpr std::size_t kLampPool = 32;
constexpr float kLampPower = 0.009F;
constexpr vec3 kWarmLamp{1.0F, 0.82F, 0.6F};
constexpr vec3 kCoolLamp{0.85F, 0.92F, 1.0F};
// Where unused lights wait, far under the city.
constexpr float kHiddenDepth = -1000.0F;
// Street lamps: one every 60 m along each street, alternating sides, a
// 6 m pole and a downward cone.
constexpr float kStreetLampSpacing = 60.0F * kS;
constexpr float kStreetLampCutoff = 62.0F;
constexpr vec3 kStreetLampColor{1.2F, 0.94F, 0.6F};
constexpr vec3 kStreetLampBulb{0.0F, 5.82F * kS, 1.45F * kS};

// Quadrants around an intersection: (+x,+z), (-x,+z), (+x,-z), (-x,-z).
constexpr std::array<vec2, 4> kQuadrants{vec2{1.0F, 1.0F}, vec2{-1.0F, 1.0F},
                                         vec2{1.0F, -1.0F}, vec2{-1.0F, -1.0F}};

float Smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// Sun height in [-1, 1]: rises at 06:00, highest at 13:00, sets at 20:00.
float SunHeight(double minuteOfDay) {
    constexpr double kSunrise = 6.0 * 60.0;
    constexpr double kDaylight = 14.0 * 60.0;
    const double phase = (minuteOfDay - kSunrise) / kDaylight;
    return static_cast<float>(std::sin(phase * std::numbers::pi));
}

vec3 Accent(sim::EntityId robot) {
    return kRobotAccents.at(static_cast<std::size_t>(
        (robot.value * 2654435761ULL >> 9U) % kRobotAccents.size()));
}
}  // namespace

CityView::CityView(const sim::City& city, std::uint64_t seed,
                   RenderAssets& assets)
    : city_(city), rooms_(city_), routes_(city_), assets_(assets) {
    vec3 minimum{0.0F};
    vec3 maximum{0.0F};
    for (const sim::CityNode& node : city_.nodes) {
        const vec3 position = NodePosition(node.id);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    center_ = (minimum + maximum) * 0.5F;
    extent_ = std::max(maximum.x - minimum.x, maximum.z - minimum.z);

    const float groundSize = extent_ + 8.0F;
    statics_.push_back({.mesh = assets_.plane.get(),
                        .transform = {.position = center_,
                                      .rotationDegrees = vec3(0.0F),
                                      .scale = {groundSize, 1.0F, groundSize}},
                        .material = assets_.ground});

    for (const sim::CityEdge& edge : city_.edges) {
        const vec3 from = NodePosition(edge.from);
        const vec3 to = NodePosition(edge.to);
        const vec3 span = glm::abs(to - from);
        statics_.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = (from + to) * 0.5F,
                           .rotationDegrees = vec3(0.0F),
                           .scale = {std::max(span.x, kStreetWidth),
                                     kStreetHeight,
                                     std::max(span.z, kStreetWidth)}},
             .material = assets_.street});
    }

    AddStreetLamps();
    AddElevatedLines();
    AddStations();

    // Simulated buildings take the corners next to their entrance first.
    std::map<std::uint32_t, std::uint32_t> usedSlots;
    std::vector<std::array<bool, 4>> occupied(city_.nodes.size());
    for (const sim::Building& building : city_.buildings) {
        const std::uint32_t slot = usedSlots[building.entrance.value]++;
        const vec2 quadrant = kQuadrants.at(slot % kQuadrants.size());
        // A fifth building at one corner moves to the far side of the block.
        const float ring = slot < kQuadrants.size() ? 1.0F : 3.0F;
        if (slot < kQuadrants.size()) {
            occupied[building.entrance.value].at(slot) = true;
        }
        const vec3 centerOnGround =
            NodePosition(building.entrance) +
            vec3{quadrant.x * kQuadrantOffset.x * ring, 0.0F,
                 quadrant.y * kQuadrantOffset.y * ring};
        const bool habitat = building.kind == sim::BuildingKind::Habitat;
        const float height = static_cast<float>(building.floors) * kFloorHeight;
        BuildingShape& shape = shapes_[building.id.value];
        shape = {.center = centerOnGround,
                 .height = height,
                 .floors = building.floors,
                 // The street is to the north (-z) when the building sits
                 // south of its intersection: turn its plans to face it.
                 .turned = quadrant.y > 0.0F};
        buildingBlocks_.emplace(
            building.id.value,
            StaticItem{.mesh = assets_.cube.get(),
                       .transform = {.position = centerOnGround,
                                     .rotationDegrees = vec3(0.0F),
                                     .scale = {kBuildingWidth, height,
                                               kBuildingDepth}},
                       .material = habitat ? assets_.habitat
                                           : assets_.workplace});

        // A sign high on the street side, and one per store at street level
        // above its door.
        const float front = shape.turned ? -1.0F : 1.0F;
        neonSigns_.push_back(
            {.position = centerOnGround +
                         vec3{0.0F, height * 0.6F,
                              front * ((kBuildingDepth * 0.5F) + (0.6F * kS))},
             .color = habitat ? kHabitatNeon : kWorkplaceNeon,
             .building = building.id});
        std::vector<const sim::Room*> stores;
        for (const sim::Room& room : building.rooms) {
            if (room.floor == 0 && sim::IsVenue(room.kind)) {
                stores.push_back(&room);
            }
        }
        const float storeWidth =
            21.0F / static_cast<float>(std::max<std::size_t>(stores.size(), 1));
        for (std::size_t index = 0; index < stores.size(); ++index) {
            const float x = -8.0F + (storeWidth * (static_cast<float>(index) + 0.5F));
            neonSigns_.push_back(
                {.position = ToWorld(shape, 0,
                                     {x, 2.9F, (kFootprintDepthMeters * 0.5F) + 0.8F}),
                 .color = StoreNeon(stores[index]->kind),
                 .building = building.id});
        }
    }
    for (const sim::Building& building : city_.buildings) {
        const BuildingShape& shape = shapes_.at(building.id.value);
        AddFacade(shape.center, kBuildingWidth, kBuildingDepth, shape.floors);
    }

    // Corners without a building are small parks.
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed, kDecorationStream);
    for (const sim::CityNode& node : city_.nodes) {
        const vec3 corner = NodePosition(node.id);
        for (std::size_t slot = 0; slot < kQuadrants.size(); ++slot) {
            const vec2 quadrant = kQuadrants.at(slot);
            const vec3 position =
                corner + vec3{quadrant.x * kQuadrantOffset.x, 0.0F,
                              quadrant.y * kQuadrantOffset.y};
            const bool insideCity =
                position.x > minimum.x && position.z > minimum.z &&
                position.x < maximum.x && position.z < maximum.z;
            if (insideCity && !occupied[node.id.value].at(slot)) {
                AddPark(position, random);
            }
        }
    }
}

void CityView::AddPark(vec3 center, sim::Pcg32& random) {
    const std::shared_ptr<const Material> grass = assets_.palette->Get(kGrass);
    const std::shared_ptr<const Material> path = assets_.palette->Get(kPavement);
    statics_.push_back({.mesh = assets_.cube.get(),
                        .transform = {.position = center,
                                      .rotationDegrees = vec3(0.0F),
                                      .scale = {kBuildingWidth * 0.95F, 0.15F * kS,
                                                kBuildingDepth * 0.95F}},
                        .material = grass});
    statics_.push_back({.mesh = assets_.cube.get(),
                        .transform = {.position = center,
                                      .rotationDegrees = vec3(0.0F),
                                      .scale = {kBuildingWidth * 0.95F, 0.17F * kS,
                                                2.0F * kS}},
                        .material = path});
    // A few trees, and benches along the path.
    const int trees = static_cast<int>(random.UniformInt(4, 7));
    for (int tree = 0; tree < trees; ++tree) {
        const float x = (static_cast<float>(random.UniformInt(0, 100)) / 100.0F - 0.5F) *
                        (kFootprintWidthMeters - 4.0F);
        const float side = tree % 2 == 0 ? 1.0F : -1.0F;
        const float z = side * (3.0F + (static_cast<float>(random.UniformInt(0, 100)) /
                                        100.0F * (kFootprintDepthMeters * 0.5F - 5.0F)));
        AddStaticModel(Prop(PropKind::Tree), center + (vec3{x, 0.0F, z} * kS),
                       static_cast<float>(random.UniformInt(0, 359)));
    }
    for (const float x : {-6.0F, 6.0F}) {
        AddStaticModel(Prop(PropKind::Bench), center + (vec3{x, 0.0F, 1.6F} * kS), 180.0F);
    }
}

void CityView::AddFacade(vec3 center, float width, float depth,
                         std::uint32_t floors) {
    // Windows every 3.5 m on every side and floor, standing a little proud
    // of the wall so they read from a distance.
    const std::shared_ptr<const Material> glass =
        assets_.palette->Get(kFacadeGlass);
    constexpr float kSpacing = 3.5F * kS;
    constexpr float kWidth = 1.6F * kS;
    constexpr float kHeight = 1.4F * kS;
    constexpr float kSill = 0.9F * kS;
    constexpr float kProud = 0.06F * kS;
    const auto side = [&](float length, bool alongX, float offset) {
        const int count = std::max(1, static_cast<int>(length / kSpacing));
        for (std::uint32_t floor = 0; floor < floors; ++floor) {
            const float y = (static_cast<float>(floor) * kFloorHeight) + kSill;
            for (int window = 0; window < count; ++window) {
                const float along =
                    (-length * 0.5F) +
                    (length / static_cast<float>(count) *
                     (static_cast<float>(window) + 0.5F));
                Transform transform{.position = center, .rotationDegrees = vec3(0.0F)};
                if (alongX) {
                    transform.position += vec3{along, y, offset};
                    transform.scale = {kWidth, kHeight, kProud * 2.0F};
                } else {
                    transform.position += vec3{offset, y, along};
                    transform.scale = {kProud * 2.0F, kHeight, kWidth};
                }
                statics_.push_back({.mesh = assets_.cube.get(),
                                    .transform = transform,
                                    .material = glass});
            }
        }
    };
    side(width, true, depth * 0.5F);
    side(width, true, -depth * 0.5F);
    side(depth, false, width * 0.5F);
    side(depth, false, -width * 0.5F);
}

void CityView::AddStreetLamps() {
    const PropModel& lamp = Prop(PropKind::StreetLamp);
    for (const sim::CityEdge& edge : city_.edges) {
        const vec3 from = NodePosition(edge.from);
        const vec3 to = NodePosition(edge.to);
        const float length = glm::length(to - from);
        if (length <= 0.0F) {
            continue;
        }
        const vec3 along = (to - from) / length;
        const vec3 across{-along.z, 0.0F, along.x};
        const int count =
            std::max(1, static_cast<int>(length / kStreetLampSpacing));
        for (int index = 0; index < count; ++index) {
            // Between intersections, on alternating pavements, the head
            // reaching out over the road.
            const float side = index % 2 == 0 ? 1.0F : -1.0F;
            const vec3 facing = -across * side;
            const vec3 base =
                from + (along * (length * (static_cast<float>(index) + 0.5F) /
                                 static_cast<float>(count))) +
                (across * side * ((kStreetWidth * 0.5F) + (0.6F * kS))) +
                vec3{0.0F, kStreetHeight, 0.0F};
            const float yaw = glm::degrees(std::atan2(facing.x, facing.z));
            AddStaticModel(lamp, base, yaw);
            streetLamps_.push_back(base + (facing * kStreetLampBulb.z) +
                                   vec3{0.0F, kStreetLampBulb.y, 0.0F});
        }
    }
}

vec3 CityView::StationPlatform(std::uint32_t station) const {
    return NodePosition(city_.stations.at(station).node) +
           vec3{0.0F, kTrackHeight, 0.0F};
}

vec3 CityView::StationStairsBottom(std::uint32_t station) const {
    // On the pavement beside the street that runs along x.
    return NodePosition(city_.stations.at(station).node) +
           vec3{kStairsRun + kPlatformHalf, kStreetHeight, kStairsAcross};
}

vec3 CityView::StationStairsTop(std::uint32_t station) const {
    return NodePosition(city_.stations.at(station).node) +
           vec3{kPlatformHalf, kTrackHeight, kStairsAcross};
}

void CityView::AddStaticBox(vec3 minimum, vec3 maximum, vec3 color) {
    statics_.push_back(
        {.mesh = assets_.cube.get(),
         .transform = {.position = {(minimum.x + maximum.x) * 0.5F, minimum.y,
                                    (minimum.z + maximum.z) * 0.5F},
                       .rotationDegrees = vec3(0.0F),
                       .scale = maximum - minimum},
         .material = assets_.palette->Get(color)});
}

void CityView::AddStations() {
    const float deck = 0.7F * kS;
    for (std::uint32_t station = 0; station < city_.stations.size(); ++station) {
        const vec3 platform = StationPlatform(station);
        // The platform deck, on four columns, under a canopy.
        AddStaticBox(platform - vec3{kPlatformHalf, deck, kPlatformHalf},
                     platform + vec3{kPlatformHalf, 0.0F, kPlatformHalf}, kTrackSteel);
        for (const float x : {-1.0F, 1.0F}) {
            for (const float z : {-1.0F, 1.0F}) {
                const vec3 column = NodePosition(city_.stations[station].node) +
                                    vec3{x * (kPlatformHalf - kS), 0.0F,
                                         z * (kPlatformHalf - kS)};
                AddStaticBox(column - vec3{0.45F * kS, 0.0F, 0.45F * kS},
                             column + vec3{0.45F * kS, kTrackHeight - deck, 0.45F * kS},
                             kTrackSteel);
            }
        }
        // A shelter over the crossing; the corners stay open to the sky.
        AddStaticBox(platform + vec3{-2.4F * kS, 3.2F * kS, -2.4F * kS},
                     platform + vec3{2.4F * kS, 3.4F * kS, 2.4F * kS}, kCanopy);
        // Stairs up from the pavement, and the M sign at their foot.
        const vec3 bottom = StationStairsBottom(station);
        const vec3 top = StationStairsTop(station);
        constexpr int kSteps = 16;
        for (int step = 0; step < kSteps; ++step) {
            const float t = static_cast<float>(step + 1) / static_cast<float>(kSteps);
            const vec3 at = glm::mix(bottom, top, t);
            AddStaticBox(vec3{at.x - (kStairsRun / kSteps), bottom.y, at.z - (0.8F * kS)},
                         vec3{at.x, at.y, at.z + (0.8F * kS)}, kTrackSteel);
        }
        AddStaticBox(bottom + vec3{1.0F * kS, 0.0F, -1.2F * kS},
                     bottom + vec3{1.15F * kS, 3.0F * kS, -1.05F * kS}, kTrackSteel);
        AddStaticBox(bottom + vec3{0.8F * kS, 3.0F * kS, -1.3F * kS},
                     bottom + vec3{1.35F * kS, 3.6F * kS, -0.95F * kS}, kStationSign);
    }
}

void CityView::AddElevatedLines() {
    // Lines run over the streets of the station rows and columns, from the
    // first station to the last, on columns between intersections.
    std::map<std::int32_t, std::pair<std::int32_t, std::int32_t>> rows;
    std::map<std::int32_t, std::pair<std::int32_t, std::int32_t>> columns;
    for (const sim::Station& station : city_.stations) {
        const sim::CityNode& node = city_.nodes.at(station.node.value);
        auto [row, newRow] = rows.try_emplace(node.z, node.x, node.x);
        row->second = {std::min(row->second.first, node.x), std::max(row->second.second, node.x)};
        auto [column, newColumn] = columns.try_emplace(node.x, node.z, node.z);
        column->second = {std::min(column->second.first, node.z),
                          std::max(column->second.second, node.z)};
    }
    const float deck = 0.7F * kS;
    const float halfWidth = 2.0F * kS;
    const auto line = [&](vec3 from, vec3 to) {
        const vec3 span = glm::abs(to - from);
        const vec3 half = span.x > span.z ? vec3{0.0F, 0.0F, halfWidth}
                                          : vec3{halfWidth, 0.0F, 0.0F};
        const vec3 lift{0.0F, kTrackHeight, 0.0F};
        AddStaticBox(glm::min(from, to) - half + lift - vec3{0.0F, deck, 0.0F},
                     glm::max(from, to) + half + lift, kTrackSteel);
        // Two rails on the deck.
        const vec3 rail = half * 0.4F;
        for (const float side : {-1.0F, 1.0F}) {
            const vec3 offset = rail * side;
            const vec3 thin = span.x > span.z ? vec3{0.0F, 0.0F, 0.08F * kS}
                                              : vec3{0.08F * kS, 0.0F, 0.0F};
            AddStaticBox(glm::min(from, to) + offset - thin + lift,
                         glm::max(from, to) + offset + thin + lift + vec3{0.0F, 0.2F * kS, 0.0F},
                         kRail);
        }
        // Columns every half block, in the middle of the street.
        const float length = glm::distance(from, to);
        const int count = static_cast<int>(length / (40.0F * kS));
        for (int index = 1; index < count; index += 2) {
            const vec3 at = glm::mix(from, to, static_cast<float>(index) / static_cast<float>(count));
            AddStaticBox(at - vec3{0.45F * kS, 0.0F, 0.45F * kS},
                         at + vec3{0.45F * kS, kTrackHeight - deck, 0.45F * kS}, kTrackSteel);
        }
    };
    for (const auto& [z, extent] : rows) {
        line(vec3{static_cast<float>(extent.first) * kS, 0.0F, static_cast<float>(z) * kS},
             vec3{static_cast<float>(extent.second) * kS, 0.0F, static_cast<float>(z) * kS});
    }
    for (const auto& [x, extent] : columns) {
        line(vec3{static_cast<float>(x) * kS, 0.0F, static_cast<float>(extent.first) * kS},
             vec3{static_cast<float>(x) * kS, 0.0F, static_cast<float>(extent.second) * kS});
    }
}

void CityView::SubmitTrains(RenderQueue& queue, const WorldFrame& frame,
                            double renderTick) {
    // Riders who boarded together share a train.
    std::set<std::tuple<std::uint32_t, std::uint32_t, sim::Tick>> trains;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.alightsAt <= robot.boardsAt ||
            robot.rideFrom >= city_.stations.size() ||
            robot.rideTo >= city_.stations.size()) {
            continue;
        }
        const double departs = static_cast<double>(robot.boardsAt + sim::kMetroWaitMinutes);
        const double arrives = static_cast<double>(robot.alightsAt);
        if (renderTick < departs || renderTick >= arrives ||
            !trains.insert({robot.rideFrom, robot.rideTo, robot.boardsAt}).second) {
            continue;
        }
        // Along the row of the first station, then the column of the last.
        const vec3 from = StationPlatform(robot.rideFrom);
        const vec3 to = StationPlatform(robot.rideTo);
        const vec3 turn{to.x, from.y, from.z};
        const float first = glm::distance(from, turn);
        const float total = first + glm::distance(turn, to);
        const float travelled =
            static_cast<float>((renderTick - departs) / std::max(arrives - departs, 1e-6)) *
            total;
        const bool alongRow = travelled < first;
        const vec3 position = alongRow
                                  ? glm::mix(from, turn, first > 0.0F ? travelled / first : 1.0F)
                                  : glm::mix(turn, to, (travelled - first) /
                                                           std::max(total - first, 1e-6F));
        const vec3 direction = alongRow ? turn - from : to - turn;
        const float yaw = glm::degrees(std::atan2(direction.x, direction.z));
        const vec3 forward = glm::length(direction) > 0.0F ? glm::normalize(direction)
                                                           : vec3{0.0F, 0.0F, 1.0F};
        for (const float car : {-6.8F, 6.8F}) {
            SubmitModel(queue, Prop(PropKind::TrainCar),
                        position + (forward * (car * kS)), yaw, 0.0F, vec3(1.0F), kS);
        }
    }
}

const std::vector<vec3>& CityView::StationRoute(sim::EntityId building,
                                                std::uint32_t station,
                                                bool toStation) {
    const auto key = std::make_tuple(building.value, station, toStation);
    if (const auto cached = stationRoutes_.find(key); cached != stationRoutes_.end()) {
        return cached->second;
    }
    std::vector<vec3> points;
    const sim::Building* found = city_.FindBuilding(building);
    if (found != nullptr && station < city_.stations.size()) {
        points.push_back(shapes_.at(building.value).center);
        for (const sim::NodeId node :
             routes_.Path(found->entrance, city_.stations[station].node)) {
            points.push_back(NodePosition(node));
        }
        // Up the stairs to the platform.
        points.push_back(StationStairsBottom(station));
        points.push_back(StationStairsTop(station));
        points.push_back(StationPlatform(station));
        if (!toStation) {
            std::ranges::reverse(points);
        }
    }
    return stationRoutes_.emplace(key, std::move(points)).first->second;
}

void CityView::AddStaticModel(const PropModel& model, vec3 origin,
                              float yawDegrees) {
    ForEachPart(model, origin, yawDegrees, 0.0F, kS,
                [&](const PropPart& part, const Transform& transform) {
                    statics_.push_back(
                        {.mesh = assets_.cube.get(),
                         .transform = transform,
                         .material = assets_.palette->Get(part.color)});
                });
}

std::optional<vec3> CityView::DrawnRobotPosition(sim::EntityId robot) const {
    for (const Marker& marker : robotMarkers_) {
        if (marker.id == robot) {
            return marker.position;
        }
    }
    return std::nullopt;
}

void CityView::AddLights(Scene& scene) {
    scene.AddDirectionalLight(DirectionalLight(0.0F, -1.0F, 0.0F, 0.0F, 0.0F,
                                               0.0F));  // sun
    scene.AddDirectionalLight(DirectionalLight(0.4F, -1.0F, 0.6F, 0.0F, 0.0F,
                                               0.0F));  // sky fill
    scene.AddDirectionalLight(DirectionalLight(-0.3F, -1.0F, -0.4F, 0.0F, 0.0F,
                                               0.0F));  // inspection
    for (const NeonSign& sign : neonSigns_) {
        neons_.push_back({.lightIndex = scene.GetPointLights().size(),
                          .color = sign.color,
                          .building = sign.building});
        scene.AddPointLight(PointLight(sign.position.x, sign.position.y,
                                       sign.position.z, sign.color.r,
                                       sign.color.g, sign.color.b, kNeonPower));
    }
    lampPoolStart_ = scene.GetPointLights().size();
    for (std::size_t lamp = 0; lamp < kLampPool; ++lamp) {
        scene.AddPointLight(PointLight(0.0F, kHiddenDepth, 0.0F, 0.0F, 0.0F,
                                       0.0F, kLampPower));
    }
    for (const vec3& bulb : streetLamps_) {
        scene.AddSpotLight(SpotLight(bulb.x, 0.0F, bulb.z, 0.0F, 0.0F, 0.0F,
                                     bulb.x, bulb.y, bulb.z, kStreetLampCutoff));
    }
}

void CityView::UpdateLights(Scene& scene, double minuteOfDay,
                            const Selection& selection) const {
    const float sunHeight = SunHeight(minuteOfDay);
    const float daylight = Smoothstep(-0.1F, 0.25F, sunHeight);
    const auto directional = scene.GetDirectionalLights();
    if (directional.size() >= 3) {
        // The sun travels from east (+x) to west (-x) across the south.
        const double phase = std::asin(std::clamp(sunHeight, -1.0F, 1.0F));
        const bool morning = minuteOfDay < 13.0 * 60.0;
        const auto horizontal =
            static_cast<float>(std::cos(phase) * (morning ? 1.0 : -1.0));
        directional[0].SetDirection(
            -glm::normalize(vec3{horizontal, std::max(sunHeight, 0.05F), 0.35F}));
        directional[0].SetColor(kSunColor * std::max(sunHeight, 0.0F) * 1.1F);
        directional[1].SetColor(glm::mix(kNightSky, kDaySky, daylight));
        // Brighter at night, when the sky no longer lights the rooms.
        directional[2].SetColor(selection.building.IsValid()
                                    ? kInspectionLight * (2.0F - daylight)
                                    : vec3(0.0F));
    }
    // Neon dims in daylight but never goes out. Lights shine through walls,
    // so the open building's own signs are off while it is inspected.
    const float neon = 1.0F - (0.75F * daylight);
    const auto points = scene.GetPointLights();
    for (const Neon& sign : neons_) {
        if (sign.lightIndex < points.size()) {
            const bool inspected = selection.building.IsValid() &&
                                   sign.building == selection.building;
            points[sign.lightIndex].SetColor(inspected ? vec3(0.0F)
                                                       : sign.color * neon);
        }
    }
    for (std::size_t lamp = 0; lamp < kLampPool; ++lamp) {
        const std::size_t index = lampPoolStart_ + lamp;
        if (index >= points.size()) {
            break;
        }
        if (lamp < litLamps_.size()) {
            points[index].SetPos(litLamps_[lamp].position);
            points[index].SetColor(litLamps_[lamp].color);
        } else {
            points[index].SetPos({0.0F, kHiddenDepth, 0.0F});
            points[index].SetColor(vec3(0.0F));
        }
    }
    // Street lamps come on at dusk. By day they point nowhere, which the
    // renderer skips.
    const vec3 street = kStreetLampColor * (1.0F - daylight);
    const bool lit = daylight < 1.0F;
    for (SpotLight& lamp : scene.GetSpotLights()) {
        const vec3 bulb = lamp.GetPos();
        lamp.SetColor(street);
        lamp.SetTarget(lit ? vec3{bulb.x, 0.0F, bulb.z} : bulb);
    }
}

vec3 CityView::ToWorld(const BuildingShape& shape, std::uint32_t floor,
                       vec3 meters) const {
    const vec3 local = shape.turned ? vec3{-meters.x, meters.y, -meters.z} : meters;
    return shape.center +
           vec3{0.0F,
                (static_cast<float>(floor) * kFloorHeight) + (kSlabMeters * kS),
                0.0F} +
           (local * kS);
}

const FloorPlan& CityView::PlanOf(sim::EntityId building, std::uint32_t floor) {
    const auto key = std::make_pair(building.value, floor);
    auto found = plans_.find(key);
    if (found == plans_.end()) {
        const sim::Building* source = city_.FindBuilding(building);
        found = plans_.emplace(key, source != nullptr ? PlanFloor(*source, floor)
                                                      : FloorPlan{})
                    .first;
    }
    return found->second;
}

void CityView::ForEachPart(
    const PropModel& model, vec3 origin, float yawDegrees, float tiltDegrees,
    float scale,
    const std::function<void(const PropPart&, const Transform&)>& part) {
    const glm::mat4 base =
        glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0F), origin),
                                glm::radians(yawDegrees), vec3{0.0F, 1.0F, 0.0F}),
                    glm::radians(tiltDegrees), vec3{1.0F, 0.0F, 0.0F});
    for (const PropPart& piece : model) {
        const glm::mat4 joint =
            glm::rotate(glm::translate(base, piece.pivot * scale),
                        glm::radians(piece.angleX), vec3{1.0F, 0.0F, 0.0F});
        part(piece,
             Transform{.position = vec3(joint * glm::vec4(piece.base * scale, 1.0F)),
                       .rotationDegrees = {tiltDegrees + piece.angleX, yawDegrees,
                                           0.0F},
                       .scale = piece.size * scale});
    }
}

void CityView::SubmitModel(RenderQueue& queue, const PropModel& model,
                           vec3 origin, float yawDegrees, float tiltDegrees,
                           vec3 tint, float scale) {
    ForEachPart(model, origin, yawDegrees, tiltDegrees, scale,
                [&](const PropPart& part, const Transform& transform) {
                    const auto material =
                        assets_.palette->Get(part.tinted ? tint : part.color);
                    if (part.glass) {
                        queue.SubmitTransparentMesh(*assets_.cube, transform,
                                                    material, kGlassOpacity);
                    } else {
                        queue.SubmitOpaqueMesh(*assets_.cube, transform,
                                               material);
                    }
                });
}

void CityView::SubmitBox(RenderQueue& queue, vec3 minimum, vec3 maximum,
                         vec3 color, bool glass) {
    const Transform transform{
        .position = {(minimum.x + maximum.x) * 0.5F, minimum.y,
                     (minimum.z + maximum.z) * 0.5F},
        .rotationDegrees = vec3(0.0F),
        .scale = maximum - minimum};
    const auto material = assets_.palette->Get(color);
    if (glass) {
        queue.SubmitTransparentMesh(*assets_.cube, transform, material,
                                    kGlassOpacity);
    } else {
        queue.SubmitOpaqueMesh(*assets_.cube, transform, material);
    }
}

void CityView::Submit(RenderQueue& queue, const WorldFrame& frame,
                      double renderTick, const Selection& selection,
                      float streetFigureScale) {
    robotMarkers_.clear();
    roomTiles_.clear();
    litLamps_.clear();
    openBuilding_ = selection.building;
    openFloor_ = selection.floor;
    const auto open = shapes_.find(selection.building.value);
    for (const StaticItem& item : statics_) {
        // The open building's facade windows sit on its hidden block.
        if (open != shapes_.end()) {
            const vec3 offset = glm::abs(item.transform.position - open->second.center);
            if (offset.x <= (kBuildingWidth * 0.5F) + (0.1F * kS) &&
                offset.z <= (kBuildingDepth * 0.5F) + (0.1F * kS) &&
                item.mesh == assets_.cube.get() &&
                item.material != assets_.street) {
                continue;
            }
        }
        queue.SubmitOpaqueMesh(*item.mesh, item.transform, item.material);
    }
    for (const auto& [id, block] : buildingBlocks_) {
        if (id != selection.building.value) {
            queue.SubmitOpaqueMesh(*block.mesh, block.transform, block.material);
        }
    }

    const float figureScale = kS * streetFigureScale;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking ||
            !robot.from.IsValid()) {
            continue;
        }
        const auto placed = RobotPosition(robot, renderTick);
        if (!placed) {
            continue;  // Underground on the metro.
        }
        const auto [position, direction] = *placed;
        const bool selected = robot.id == selection.robot;
        // Legs swing once per 1.5 m walked.
        const float walked = static_cast<float>(
            (renderTick - static_cast<double>(robot.departedAt)) * 75.0);
        const float phase = walked / 0.75F * std::numbers::pi_v<float>;
        const float yaw = glm::degrees(std::atan2(direction.x, direction.z));
        // Shopping comes home in a bag; the black market wraps it up.
        Carried carried = Carried::Nothing;
        if (const auto left = rooms_.Find(robot.location)) {
            if (left->room->kind == sim::RoomKind::Shop) {
                carried = Carried::ShoppingBag;
            } else if (left->room->kind == sim::RoomKind::BlackMarket) {
                carried = Carried::Parcel;
            }
        }
        SubmitModel(queue, RobotFigure(Pose::Walking, phase,
                                       selected ? kSelected : Accent(robot.id), carried),
                    position, yaw, 0.0F, vec3(1.0F),
                    figureScale * (selected ? 1.2F : 1.0F));
        robotMarkers_.push_back(
            {.id = robot.id,
             .position = position + vec3{0.0F, 1.0F * figureScale, 0.0F},
             .radius = kWalkerPickRadius * streetFigureScale});
    }
    SubmitTrains(queue, frame, renderTick);
    if (open != shapes_.end()) {
        SubmitInterior(queue, frame, renderTick, selection);
    }
}

void CityView::SubmitInterior(RenderQueue& queue, const WorldFrame& frame,
                              double renderTick, const Selection& selection) {
    const BuildingShape& shape = shapes_.at(selection.building.value);
    const std::uint32_t current = std::min(selection.floor, shape.floors - 1);
    const float halfWidth = kFootprintWidthMeters * 0.5F;
    const float halfDepth = kFootprintDepthMeters * 0.5F;
    const float yawOffset = shape.turned ? 180.0F : 0.0F;

    // Floors below are solid; the one being looked at stands on its slab.
    if (current > 0) {
        SubmitBox(queue,
                  shape.center - vec3{kBuildingWidth * 0.5F, 0.0F, kBuildingDepth * 0.5F},
                  shape.center + vec3{kBuildingWidth * 0.5F,
                                      static_cast<float>(current) * kFloorHeight,
                                      kBuildingDepth * 0.5F},
                  kConcrete, false);
    }
    const auto worldBox = [&](vec3 minimum, vec3 maximum) {
        const vec3 a = ToWorld(shape, current, minimum);
        const vec3 b = ToWorld(shape, current, maximum);
        return std::make_pair(glm::min(a, b), glm::max(a, b));
    };
    {
        const auto [minimum, maximum] =
            worldBox({-halfWidth, -kSlabMeters, -halfDepth}, {halfWidth, 0.0F, halfDepth});
        SubmitBox(queue, minimum, maximum, kConcrete, false);
    }

    const FloorPlan& plan = PlanOf(selection.building, current);
    for (const WallBox& box : plan.boxes) {
        const auto [minimum, maximum] = worldBox(box.minimum, box.maximum);
        SubmitBox(queue, minimum, maximum, box.color, box.glass);
    }
    for (const PlacedProp& prop : plan.props) {
        SubmitModel(queue, Prop(prop.kind), ToWorld(shape, current, prop.position),
                    prop.yawDegrees + yawOffset, 0.0F, prop.tint, kS);
    }

    // Robots inside, in id order per room.
    std::map<std::uint64_t, std::vector<sim::EntityId>> occupants;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            occupants[robot.location.value].push_back(robot.id);
        }
    }
    const double minute = std::fmod(renderTick, static_cast<double>(sim::kMinutesPerDay));
    const bool night = minute >= kBedtime || minute < kWakeUp;

    // Lamps are on where they would be: in corridors always, at home while
    // someone is up, in venues while open or busy, at work while working.
    for (const Lamp& lamp : plan.lamps) {
        if (litLamps_.size() >= kLampPool) {
            break;
        }
        bool on = true;
        vec3 color = kWarmLamp;
        if (const auto room = rooms_.Find(lamp.room)) {
            const bool occupied = occupants.contains(lamp.room.value);
            if (const sim::VenueRules* rules = sim::FindVenueRules(room->room->kind)) {
                on = occupied ||
                     sim::IsOpen(*rules, static_cast<std::uint32_t>(minute));
            } else if (room->room->kind == sim::RoomKind::Apartment) {
                on = occupied && !night;
            } else {
                on = occupied;
                color = kCoolLamp;
            }
        }
        if (on) {
            litLamps_.push_back(
                {.position = ToWorld(shape, current, lamp.position), .color = color});
        }
    }
    for (const RoomPlan& room : plan.rooms) {
        const auto [minimum, maximum] = worldBox(room.minimum, room.maximum);
        roomTiles_.push_back({.room = room.room, .box = {minimum, maximum}});
        if (room.room == selection.room) {
            const Transform overlay{
                .position = {(minimum.x + maximum.x) * 0.5F, minimum.y + (0.03F * kS),
                             (minimum.z + maximum.z) * 0.5F},
                .rotationDegrees = vec3(0.0F),
                .scale = {maximum.x - minimum.x, 0.02F * kS, maximum.z - minimum.z}};
            queue.SubmitTransparentMesh(*assets_.cube, overlay,
                                        assets_.palette->Get(kSelected),
                                        kHighlightOpacity);
        }
        const auto inside = occupants.find(room.room.value);
        if (inside == occupants.end() || room.spots.empty()) {
            continue;
        }
        for (std::size_t index = 0; index < inside->second.size(); ++index) {
            const sim::EntityId robot = inside->second[index];
            const std::size_t spotIndex =
                room.home ? (night ? 0 : 1) : index % room.spots.size();
            const Spot& spot = room.spots.at(std::min(spotIndex, room.spots.size() - 1));
            const bool selected = robot == selection.robot;
            const bool lying = spot.pose == Pose::Lying;
            const vec3 origin = ToWorld(shape, current, spot.position);
            SubmitModel(queue,
                        RobotFigure(lying ? Pose::Standing : spot.pose, 0.0F,
                                    selected ? kSelected : Accent(robot)),
                        origin, spot.yawDegrees + yawOffset, lying ? -90.0F : 0.0F,
                        vec3(1.0F), kS);
            if (spot.item) {
                SubmitModel(queue, Prop(spot.item->kind),
                            ToWorld(shape, current, spot.item->position),
                            spot.item->yawDegrees + yawOffset, 0.0F, spot.item->tint, kS);
            }
            robotMarkers_.push_back(
                {.id = robot,
                 .position = origin + vec3{0.0F, (lying ? 0.2F : 0.9F) * kS, 0.0F},
                 .radius = 0.6F * kS});
        }
    }
}

std::optional<Pick> CityView::PickAt(const Ray& ray) const {
    float nearest = std::numeric_limits<float>::max();
    std::optional<Pick> result;
    const auto consider = [&](float distance, Pick pick) {
        if (distance >= 0.0F && distance < nearest) {
            nearest = distance;
            result = pick;
        }
    };
    // Slab test: distance along the ray to where it enters `box`.
    const auto hitBox = [&](const Box& box) -> std::optional<float> {
        float enter = 0.0F;
        float exit = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis) {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            if (std::abs(direction) < 1e-6F) {
                if (origin < box.minimum[axis] || origin > box.maximum[axis]) {
                    return std::nullopt;
                }
                continue;
            }
            float nearSide = (box.minimum[axis] - origin) / direction;
            float farSide = (box.maximum[axis] - origin) / direction;
            if (nearSide > farSide) {
                std::swap(nearSide, farSide);
            }
            enter = std::max(enter, nearSide);
            exit = std::min(exit, farSide);
            if (enter > exit) {
                return std::nullopt;
            }
        }
        return enter;
    };

    for (const Marker& marker : robotMarkers_) {
        const vec3 offset = ray.origin - marker.position;
        const float b = glm::dot(offset, ray.direction);
        const float c = glm::dot(offset, offset) - (marker.radius * marker.radius);
        const float discriminant = (b * b) - c;
        if (discriminant >= 0.0F) {
            // Robots sit inside rooms; let them win over the room's box.
            consider(-b - std::sqrt(discriminant) - (0.5F * kS),
                     {.robot = marker.id});
        }
    }
    for (const Tile& tile : roomTiles_) {
        if (const auto distance = hitBox(tile.box)) {
            const auto room = rooms_.Find(tile.room);
            consider(*distance,
                     {.building = room ? room->building->id : sim::EntityId{},
                      .room = tile.room});
        }
    }
    for (const auto& [id, shape] : shapes_) {
        const vec3 half{kBuildingWidth * 0.5F, 0.0F, kBuildingDepth * 0.5F};
        // The open building is cut away above the floor being viewed, so its
        // rooms and robots are not hidden behind its full height.
        const float height =
            id == openBuilding_.value
                ? (static_cast<float>(openFloor_) * kFloorHeight) + (kSlabMeters * kS)
                : shape.height;
        const Box box{.minimum = shape.center - half,
                      .maximum = shape.center + half + vec3{0.0F, height, 0.0F}};
        if (const auto distance = hitBox(box)) {
            consider(*distance + (0.01F * kS), {.building = sim::EntityId{id}});
        }
    }
    return result;
}

std::uint32_t CityView::Floors(sim::EntityId building) const {
    const auto found = shapes_.find(building.value);
    return found != shapes_.end() ? found->second.floors : 0;
}

vec3 CityView::FloorCenter(sim::EntityId building, std::uint32_t floor) const {
    const auto found = shapes_.find(building.value);
    if (found == shapes_.end()) {
        return center_;
    }
    return found->second.center +
           vec3{0.0F, (static_cast<float>(floor) * kFloorHeight) + (1.0F * kS), 0.0F};
}

vec3 CityView::NodePosition(sim::NodeId node) const {
    const sim::CityNode& cityNode = city_.nodes.at(node.value);
    return {static_cast<float>(cityNode.x) * kUnitsPerMeter, 0.0F,
            static_cast<float>(cityNode.z) * kUnitsPerMeter};
}

const std::vector<vec3>& CityView::Route(sim::EntityId from, sim::EntityId to) {
    const auto key = std::make_pair(from.value, to.value);
    if (const auto cached = routeCache_.find(key); cached != routeCache_.end()) {
        return cached->second;
    }
    std::vector<vec3> points;
    const sim::Building* origin = city_.FindBuilding(from);
    const sim::Building* destination = city_.FindBuilding(to);
    if (origin != nullptr && destination != nullptr) {
        points.push_back(shapes_.at(from.value).center);
        for (const sim::NodeId node :
             routes_.Path(origin->entrance, destination->entrance)) {
            points.push_back(NodePosition(node));
        }
        points.push_back(shapes_.at(to.value).center);
    }
    return routeCache_.emplace(key, std::move(points)).first->second;
}

std::optional<std::pair<vec3, vec3>> CityView::RobotPosition(
    const RobotFrame& robot, double renderTick) {
    // On foot the whole way, or to a station, underground, and on from the
    // other station.
    const std::vector<vec3>* walk = &Route(robot.from, robot.to);
    double start = static_cast<double>(robot.departedAt);
    double end = static_cast<double>(robot.arrivesAt);
    if (robot.alightsAt > robot.boardsAt) {
        const double departs = static_cast<double>(robot.boardsAt + sim::kMetroWaitMinutes);
        if (renderTick < static_cast<double>(robot.boardsAt)) {
            walk = &StationRoute(robot.from, robot.rideFrom, true);
            end = static_cast<double>(robot.boardsAt);
        } else if (renderTick < departs) {
            // Waiting in one of the platform's open corners, clear of the
            // tracks that cross it.
            const std::uint64_t hash = robot.id.value * 2654435761ULL;
            const auto corner = [&](unsigned shift) {
                const float side = ((hash >> (shift + 8U)) & 1U) != 0U ? 1.0F : -1.0F;
                const float along = static_cast<float>((hash >> shift) & 0xFFU) / 255.0F;
                return side * ((2.8F * kS) + (along * (kPlatformHalf - (3.6F * kS))));
            };
            const vec3 spot{corner(0U), 0.0F, corner(16U)};
            return std::make_pair(StationPlatform(robot.rideFrom) + spot,
                                  vec3{0.0F, 0.0F, 1.0F});
        } else if (renderTick < static_cast<double>(robot.alightsAt)) {
            return std::nullopt;  // On the train.
        } else {
            walk = &StationRoute(robot.to, robot.rideTo, false);
            start = static_cast<double>(robot.alightsAt);
        }
    }
    const std::vector<vec3>& route = *walk;
    // Spread robots sharing a street so they do not overlap exactly.
    const std::uint64_t hash = robot.id.value * 2654435761ULL;
    const vec3 spread{
        ((static_cast<float>(hash & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread,
        kStreetHeight,
        ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread};
    if (route.size() < 2) {
        return std::make_pair(spread, vec3{0.0F, 0.0F, 1.0F});
    }
    const double duration = end - start;
    const double progress =
        duration > 0.0 ? std::clamp((renderTick - start) / duration, 0.0, 1.0) : 1.0;
    float total = 0.0F;
    for (std::size_t index = 1; index < route.size(); ++index) {
        total += glm::distance(route[index - 1], route[index]);
    }
    float remaining = static_cast<float>(progress) * total;
    for (std::size_t index = 1; index < route.size(); ++index) {
        const vec3 segment = route[index] - route[index - 1];
        const float length = glm::length(segment);
        if (remaining <= length && length > 0.0F) {
            return std::make_pair(
                glm::mix(route[index - 1], route[index], remaining / length) + spread,
                segment / length);
        }
        remaining -= length;
    }
    const vec3 last = route.back() - route[route.size() - 2];
    const float length = glm::length(last);
    return std::make_pair(route.back() + spread,
                          length > 0.0F ? last / length : vec3{0.0F, 0.0F, 1.0F});
}

}  // namespace mechapunk
