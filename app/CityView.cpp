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
// Chance, in percent, that a free corner of a block gets a filler tower and
// that such a tower carries a neon sign.
constexpr std::uint32_t kTowerChance = 60;
constexpr std::uint32_t kTowerNeonChance = 15;
// Separate random stream so decoration never affects the simulation.
constexpr std::uint64_t kDecorationStream = 100;
// Robots in apartments sleep in this part of the day, else sit on the sofa.
constexpr double kBedtime = (21.0 * 60.0) + 30.0;
constexpr double kWakeUp = 7.0 * 60.0;

constexpr vec3 kHabitatNeon{0.1F, 0.9F, 1.0F};
constexpr vec3 kWorkplaceNeon{1.0F, 0.2F, 0.8F};
constexpr vec3 kTowerNeon{1.0F, 0.6F, 0.1F};
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
        default:
            return {1.0F, 1.0F, 1.0F};
    }
}
constexpr vec3 kSunColor{1.0F, 0.86F, 0.7F};
constexpr vec3 kDaySky{0.45F, 0.47F, 0.55F};
constexpr vec3 kNightSky{0.04F, 0.04F, 0.1F};
// Extra light from above while a building is open, so interiors read at
// night too.
constexpr vec3 kInspectionLight{0.5F, 0.5F, 0.55F};

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
        neonSigns_.emplace_back(
            centerOnGround + vec3{0.0F, height * 0.6F,
                                  front * ((kBuildingDepth * 0.5F) + (0.6F * kS))},
            habitat ? kHabitatNeon : kWorkplaceNeon);
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
            neonSigns_.emplace_back(
                ToWorld(shape, 0, {x, 2.9F, (kFootprintDepthMeters * 0.5F) + 0.8F}),
                StoreNeon(stores[index]->kind));
        }
    }
    for (const sim::Building& building : city_.buildings) {
        const BuildingShape& shape = shapes_.at(building.id.value);
        AddFacade(shape.center, kBuildingWidth, kBuildingDepth, shape.floors);
    }

    // Decorative towers fill the remaining corners for a denser skyline.
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed, kDecorationStream);
    const float maxCoordinate = extent_;
    const std::size_t buildingItems = statics_.size();
    for (const sim::CityNode& node : city_.nodes) {
        const vec3 corner = NodePosition(node.id);
        for (std::size_t slot = 0; slot < kQuadrants.size(); ++slot) {
            const vec2 quadrant = kQuadrants.at(slot);
            const vec3 position =
                corner + vec3{quadrant.x * kQuadrantOffset.x, 0.0F,
                              quadrant.y * kQuadrantOffset.y};
            const bool insideCity =
                position.x > 0.0F && position.z > 0.0F &&
                position.x < maxCoordinate && position.z < maxCoordinate;
            if (!insideCity || occupied[node.id.value].at(slot) ||
                random.UniformInt(1, 100) > kTowerChance) {
                continue;
            }
            const std::uint32_t floors = random.UniformInt(3, 14);
            const float height = static_cast<float>(floors) * kFloorHeight;
            const float width =
                kBuildingWidth * (0.6F + (static_cast<float>(random.UniformInt(0, 4)) * 0.1F));
            const float depth = kBuildingDepth * 0.9F;
            statics_.push_back({.mesh = assets_.cube.get(),
                                .transform = {.position = position,
                                              .rotationDegrees = vec3(0.0F),
                                              .scale = {width, height, depth}},
                                .material = assets_.tower});
            AddFacade(position, width, depth, floors);
            if (random.UniformInt(1, 100) <= kTowerNeonChance) {
                neonSigns_.emplace_back(
                    position - (vec3{quadrant.x * width, 0.0F, quadrant.y * depth} *
                                0.55F) +
                        vec3{0.0F, height * 0.7F, 0.0F},
                    kTowerNeon);
            }
        }
    }
    (void)buildingItems;
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

void CityView::AddLights(Scene& scene) {
    scene.AddDirectionalLight(DirectionalLight(0.0F, -1.0F, 0.0F, 0.0F, 0.0F,
                                               0.0F));  // sun
    scene.AddDirectionalLight(DirectionalLight(0.4F, -1.0F, 0.6F, 0.0F, 0.0F,
                                               0.0F));  // sky fill
    scene.AddDirectionalLight(DirectionalLight(-0.3F, -1.0F, -0.4F, 0.0F, 0.0F,
                                               0.0F));  // inspection
    for (const auto& [position, color] : neonSigns_) {
        neons_.push_back({.lightIndex = scene.GetPointLights().size(),
                          .color = color});
        scene.AddPointLight(PointLight(position.x, position.y, position.z,
                                       color.r, color.g, color.b, kNeonPower));
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
        directional[2].SetColor(selection.building.IsValid() ? kInspectionLight
                                                             : vec3(0.0F));
    }
    // Neon dims in daylight but never goes out.
    const float neon = 1.0F - (0.75F * daylight);
    const auto points = scene.GetPointLights();
    for (const Neon& sign : neons_) {
        if (sign.lightIndex < points.size()) {
            points[sign.lightIndex].SetColor(sign.color * neon);
        }
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

void CityView::SubmitModel(RenderQueue& queue, const PropModel& model,
                           vec3 origin, float yawDegrees, float tiltDegrees,
                           vec3 tint, float scale) {
    const glm::mat4 base =
        glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0F), origin),
                                glm::radians(yawDegrees), vec3{0.0F, 1.0F, 0.0F}),
                    glm::radians(tiltDegrees), vec3{1.0F, 0.0F, 0.0F});
    for (const PropPart& part : model) {
        const glm::mat4 joint =
            glm::rotate(glm::translate(base, part.pivot * scale),
                        glm::radians(part.angleX), vec3{1.0F, 0.0F, 0.0F});
        const Transform transform{
            .position = vec3(joint * glm::vec4(part.base * scale, 1.0F)),
            .rotationDegrees = {tiltDegrees + part.angleX, yawDegrees, 0.0F},
            .scale = part.size * scale};
        const auto material =
            assets_.palette->Get(part.tinted ? tint : part.color);
        if (part.glass) {
            queue.SubmitTransparentMesh(*assets_.cube, transform, material,
                                        kGlassOpacity);
        } else {
            queue.SubmitOpaqueMesh(*assets_.cube, transform, material);
        }
    }
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
        const auto [position, direction] = RobotPosition(robot, renderTick);
        const bool selected = robot.id == selection.robot;
        // Legs swing once per 1.5 m walked.
        const float walked = static_cast<float>(
            (renderTick - static_cast<double>(robot.departedAt)) * 75.0);
        const float phase = walked / 0.75F * std::numbers::pi_v<float>;
        const float yaw = glm::degrees(std::atan2(direction.x, direction.z));
        SubmitModel(queue, RobotFigure(Pose::Walking, phase,
                                       selected ? kSelected : Accent(robot.id)),
                    position, yaw, 0.0F, vec3(1.0F),
                    figureScale * (selected ? 1.2F : 1.0F));
        robotMarkers_.push_back(
            {.id = robot.id,
             .position = position + vec3{0.0F, 1.0F * figureScale, 0.0F},
             .radius = kWalkerPickRadius * streetFigureScale});
    }
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

std::pair<vec3, vec3> CityView::RobotPosition(const RobotFrame& robot,
                                              double renderTick) {
    const std::vector<vec3>& route = Route(robot.from, robot.to);
    // Spread robots sharing a street so they do not overlap exactly.
    const std::uint64_t hash = robot.id.value * 2654435761ULL;
    const vec3 spread{
        ((static_cast<float>(hash & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread,
        kStreetHeight,
        ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread};
    if (route.size() < 2) {
        return {spread, vec3{0.0F, 0.0F, 1.0F}};
    }
    const double duration = static_cast<double>(robot.arrivesAt - robot.departedAt);
    const double progress =
        duration > 0.0
            ? std::clamp((renderTick - static_cast<double>(robot.departedAt)) / duration,
                         0.0, 1.0)
            : 1.0;
    float total = 0.0F;
    for (std::size_t index = 1; index < route.size(); ++index) {
        total += glm::distance(route[index - 1], route[index]);
    }
    float remaining = static_cast<float>(progress) * total;
    for (std::size_t index = 1; index < route.size(); ++index) {
        const vec3 segment = route[index] - route[index - 1];
        const float length = glm::length(segment);
        if (remaining <= length && length > 0.0F) {
            return {glm::mix(route[index - 1], route[index], remaining / length) + spread,
                    segment / length};
        }
        remaining -= length;
    }
    const vec3 last = route.back() - route[route.size() - 2];
    const float length = glm::length(last);
    return {route.back() + spread,
            length > 0.0F ? last / length : vec3{0.0F, 0.0F, 1.0F}};
}

}  // namespace mechapunk
