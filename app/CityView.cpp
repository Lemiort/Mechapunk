#include "CityView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
constexpr float kStreetWidth = 0.5F;
constexpr float kSlabThickness = 0.04F;
constexpr float kRoomWallHeight = 0.12F;
constexpr float kRoomInset = 0.06F;
constexpr float kInteriorRobotRadius = 0.05F;
// Walking robots are small on screen; accept clicks a little around them.
constexpr float kWalkerPickRadius = 0.35F;
constexpr float kInteriorPickRadius = 0.09F;
constexpr glm::vec3 kLampColor{0.9F, 0.9F, 1.0F};
constexpr float kLampPower = 4.0F;
constexpr float kLampHeight = 1.2F;
constexpr float kStoreSignHeight = 0.3F;
constexpr float kStoreSignSpacing = 0.65F;
constexpr float kStreetHeight = 0.03F;
constexpr float kBuildingFootprint = 2.0F;
// Distance from an intersection to the center of a building beside it.
constexpr float kQuadrantOffset = 1.7F;
constexpr float kRobotRadius = 0.2F;
constexpr float kRobotSpread = 0.4F;
constexpr float kNeonPower = 2.5F;
// Chance, in percent, that a free corner of a block gets a filler tower and
// that such a tower carries a neon sign.
constexpr std::uint32_t kTowerChance = 60;
constexpr std::uint32_t kTowerNeonChance = 15;
// Separate random stream so decoration never affects the simulation.
constexpr std::uint64_t kDecorationStream = 100;

constexpr glm::vec3 kHabitatNeon{0.1F, 0.9F, 1.0F};
constexpr glm::vec3 kWorkplaceNeon{1.0F, 0.2F, 0.8F};
constexpr glm::vec3 kTowerNeon{1.0F, 0.6F, 0.1F};

glm::vec3 StoreNeon(sim::RoomKind kind) {
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
constexpr glm::vec3 kSunColor{1.0F, 0.86F, 0.7F};
constexpr glm::vec3 kDaySky{0.45F, 0.47F, 0.55F};
constexpr glm::vec3 kNightSky{0.04F, 0.04F, 0.1F};

// Quadrants around an intersection: (+x,+z), (-x,+z), (+x,-z), (-x,-z).
constexpr std::array<glm::vec2, 4> kQuadrants{
    glm::vec2{1.0F, 1.0F}, glm::vec2{-1.0F, 1.0F}, glm::vec2{1.0F, -1.0F},
    glm::vec2{-1.0F, -1.0F}};

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
}  // namespace

CityView::CityView(const sim::City& city, std::uint64_t seed,
                   RenderAssets& assets)
    : city_(city), rooms_(city_), routes_(city_), assets_(assets) {
    glm::vec3 minimum{0.0F};
    glm::vec3 maximum{0.0F};
    for (const sim::CityNode& node : city_.nodes) {
        const glm::vec3 position = NodePosition(node.id);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    center_ = (minimum + maximum) * 0.5F;
    extent_ = std::max(maximum.x - minimum.x, maximum.z - minimum.z);

    const float groundSize = extent_ + 8.0F;
    statics_.push_back({.mesh = assets_.plane.get(),
                        .transform = {.position = center_,
                                      .rotationDegrees = glm::vec3(0.0F),
                                      .scale = {groundSize, 1.0F, groundSize}},
                        .material = assets_.ground});

    for (const sim::CityEdge& edge : city_.edges) {
        const glm::vec3 from = NodePosition(edge.from);
        const glm::vec3 to = NodePosition(edge.to);
        const glm::vec3 span = glm::abs(to - from);
        statics_.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = (from + to) * 0.5F,
                           .rotationDegrees = glm::vec3(0.0F),
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
        const glm::vec2 quadrant = kQuadrants.at(slot % kQuadrants.size());
        // A fifth building at one corner moves to the far side of the block.
        const float ring = slot < kQuadrants.size() ? 1.0F : 2.5F;
        if (slot < kQuadrants.size()) {
            occupied[building.entrance.value].at(slot) = true;
        }
        const glm::vec3 entrance = NodePosition(building.entrance);
        const glm::vec3 centerOnGround =
            entrance + glm::vec3{quadrant.x, 0.0F, quadrant.y} *
                           (kQuadrantOffset * ring);
        const bool habitat = building.kind == sim::BuildingKind::Habitat;
        const float height =
            0.3F + (static_cast<float>(building.floors) * kFloorHeight);
        BuildingShape& shape = shapes_[building.id.value];
        shape.center = centerOnGround;
        shape.height = height;
        shape.floors = building.floors;
        shape.floorRooms.resize(building.floors);
        for (const sim::Room& room : building.rooms) {
            if (room.floor < building.floors) {
                shape.floorRooms[room.floor].push_back(&room);
            }
        }
        buildingBlocks_.emplace(
            building.id.value,
            StaticItem{.mesh = assets_.cube.get(),
                       .transform = {.position = centerOnGround,
                                     .rotationDegrees = glm::vec3(0.0F),
                                     .scale = {kBuildingFootprint, height,
                                               kBuildingFootprint}},
                       .material = habitat ? assets_.habitat
                                           : assets_.workplace});
        // Sign on the corner facing the street, halfway up.
        const glm::vec3 sign =
            centerOnGround -
            (glm::vec3{quadrant.x, 0.0F, quadrant.y} *
             (kBuildingFootprint * 0.5F + 0.3F)) +
            glm::vec3{0.0F, height * 0.5F, 0.0F};
        neonSigns_.emplace_back(sign, habitat ? kHabitatNeon : kWorkplaceNeon);
        // Every ground-floor store gets its own sign at street level, side by
        // side along the street-facing wall.
        const glm::vec3 storefront =
            centerOnGround - (glm::vec3{quadrant.x, 0.0F, quadrant.y} *
                              (kBuildingFootprint * 0.5F + 0.2F));
        float offset = -kStoreSignSpacing * 0.5F;
        for (const sim::Room& room : building.rooms) {
            if (room.floor == 0 && sim::IsVenue(room.kind)) {
                neonSigns_.emplace_back(
                    storefront + glm::vec3{offset * quadrant.x,
                                           kStoreSignHeight, 0.0F},
                    StoreNeon(room.kind));
                offset += kStoreSignSpacing;
            }
        }
    }

    // Decorative towers fill the remaining corners for a denser skyline.
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed, kDecorationStream);
    const float maxCoordinate = extent_;
    for (const sim::CityNode& node : city_.nodes) {
        const glm::vec3 corner = NodePosition(node.id);
        for (std::size_t slot = 0; slot < kQuadrants.size(); ++slot) {
            const glm::vec2 quadrant = kQuadrants.at(slot);
            const glm::vec3 position =
                corner + glm::vec3{quadrant.x, 0.0F, quadrant.y} *
                             kQuadrantOffset;
            const bool insideCity =
                position.x > 0.0F && position.z > 0.0F &&
                position.x < maxCoordinate && position.z < maxCoordinate;
            if (!insideCity || occupied[node.id.value].at(slot) ||
                random.UniformInt(1, 100) > kTowerChance) {
                continue;
            }
            const float height =
                0.6F + (static_cast<float>(random.UniformInt(0, 30)) * 0.1F);
            const float footprint =
                1.3F + (static_cast<float>(random.UniformInt(0, 6)) * 0.1F);
            statics_.push_back(
                {.mesh = assets_.cube.get(),
                 .transform = {.position = position,
                               .rotationDegrees = glm::vec3(0.0F),
                               .scale = {footprint, height, footprint}},
                 .material = assets_.tower});
            if (random.UniformInt(1, 100) <= kTowerNeonChance) {
                neonSigns_.emplace_back(
                    position -
                        (glm::vec3{quadrant.x, 0.0F, quadrant.y} *
                         (footprint * 0.5F + 0.3F)) +
                        glm::vec3{0.0F, height * 0.7F, 0.0F},
                    kTowerNeon);
            }
        }
    }
}

void CityView::AddLights(Scene& scene) {
    scene.AddDirectionalLight(DirectionalLight(0.0F, -1.0F, 0.0F, 0.0F, 0.0F,
                                               0.0F));  // sun
    scene.AddDirectionalLight(DirectionalLight(0.4F, -1.0F, 0.6F, 0.0F, 0.0F,
                                               0.0F));  // sky fill
    for (const auto& [position, color] : neonSigns_) {
        neons_.push_back({.lightIndex = scene.GetPointLights().size(),
                          .color = color});
        scene.AddPointLight(PointLight(position.x, position.y, position.z,
                                       color.r, color.g, color.b, kNeonPower));
    }
    // Lights the open floor of an inspected building; dark until then.
    lampIndex_ = scene.GetPointLights().size();
    scene.AddPointLight(
        PointLight(0.0F, -10.0F, 0.0F, 0.0F, 0.0F, 0.0F, kLampPower));
}

void CityView::UpdateLights(Scene& scene, double minuteOfDay,
                            const Selection& selection) const {
    const float sunHeight = SunHeight(minuteOfDay);
    const float daylight = Smoothstep(-0.1F, 0.25F, sunHeight);
    const auto directional = scene.GetDirectionalLights();
    if (directional.size() >= 2) {
        // The sun travels from east (+x) to west (-x) across the south.
        const double phase = std::asin(std::clamp(sunHeight, -1.0F, 1.0F));
        const bool morning = minuteOfDay < 13.0 * 60.0;
        const auto horizontal =
            static_cast<float>(std::cos(phase) * (morning ? 1.0 : -1.0));
        directional[0].SetDirection(
            -glm::normalize(glm::vec3{horizontal, std::max(sunHeight, 0.05F),
                                      0.35F}));
        directional[0].SetColor(kSunColor * std::max(sunHeight, 0.0F) * 1.1F);
        directional[1].SetColor(glm::mix(kNightSky, kDaySky, daylight));
    }
    // Neon dims in daylight but never goes out.
    const float neon = 1.0F - (0.75F * daylight);
    const auto points = scene.GetPointLights();
    for (const Neon& sign : neons_) {
        if (sign.lightIndex < points.size()) {
            points[sign.lightIndex].SetColor(sign.color * neon);
        }
    }
    if (lampIndex_ < points.size()) {
        PointLight& lamp = points[lampIndex_];
        if (selection.building.IsValid()) {
            lamp.SetPos(FloorCenter(selection.building, selection.floor) +
                        glm::vec3{0.0F, kLampHeight, 0.0F});
            lamp.SetColor(kLampColor);
        } else {
            lamp.SetColor(glm::vec3(0.0F));
        }
    }
}

void CityView::Submit(RenderQueue& queue, const WorldFrame& frame,
                      double renderTick, const Selection& selection) {
    robotMarkers_.clear();
    roomTiles_.clear();
    openBuilding_ = selection.building;
    openFloor_ = selection.floor;
    for (const StaticItem& item : statics_) {
        queue.SubmitOpaqueMesh(*item.mesh, item.transform, item.material);
    }
    for (const auto& [id, block] : buildingBlocks_) {
        if (id != selection.building.value) {
            queue.SubmitOpaqueMesh(*block.mesh, block.transform,
                                   block.material);
        }
    }
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking ||
            !robot.from.IsValid()) {
            continue;
        }
        const glm::vec3 position = RobotPosition(robot, renderTick);
        const bool selected = robot.id == selection.robot;
        queue.SubmitOpaqueMesh(
            *assets_.sphere,
            Transform{.position = position,
                      .rotationDegrees = glm::vec3(0.0F),
                      .scale = glm::vec3(kRobotRadius *
                                         (selected ? 1.6F : 1.0F))},
            selected ? assets_.highlight : assets_.robot);
        robotMarkers_.push_back({.id = robot.id,
                                 .position = position,
                                 .radius = kWalkerPickRadius});
    }
    if (selection.building.IsValid()) {
        SubmitInterior(queue, frame, selection);
    }
}

void CityView::SubmitInterior(RenderQueue& queue, const WorldFrame& frame,
                              const Selection& selection) {
    const auto found = shapes_.find(selection.building.value);
    if (found == shapes_.end()) {
        return;
    }
    const BuildingShape& shape = found->second;
    const std::uint32_t current = std::min(selection.floor, shape.floors - 1);

    // Floors below are solid; the one being looked at is an open slab.
    if (current > 0) {
        queue.SubmitOpaqueMesh(
            *assets_.cube,
            Transform{.position = shape.center,
                      .rotationDegrees = glm::vec3(0.0F),
                      .scale = {kBuildingFootprint,
                                static_cast<float>(current) * kFloorHeight,
                                kBuildingFootprint}},
            assets_.tower);
    }
    const float floorBase = static_cast<float>(current) * kFloorHeight;
    queue.SubmitOpaqueMesh(
        *assets_.cube,
        Transform{.position = shape.center + glm::vec3{0.0F, floorBase, 0.0F},
                  .rotationDegrees = glm::vec3(0.0F),
                  .scale = {kBuildingFootprint, kSlabThickness,
                            kBuildingFootprint}},
        assets_.slab);

    // Robots inside rooms, in id order per room.
    std::map<std::uint64_t, std::vector<sim::EntityId>> occupants;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            occupants[robot.location.value].push_back(robot.id);
        }
    }

    for (const sim::Room* room : shape.floorRooms[current]) {
        const Box box = RoomBox(shape, *room);
        const glm::vec3 size = box.maximum - box.minimum;
        const bool selected = room->id == selection.room;
        queue.SubmitOpaqueMesh(
            *assets_.cube,
            Transform{.position = {(box.minimum.x + box.maximum.x) * 0.5F,
                                   box.minimum.y,
                                   (box.minimum.z + box.maximum.z) * 0.5F},
                      .rotationDegrees = glm::vec3(0.0F),
                      .scale = size},
            selected ? assets_.highlight : RoomMaterial(room->kind));
        roomTiles_.push_back({.room = room->id, .box = box});

        // Seat robots on a grid over the room, as many as it can hold.
        const auto seated = occupants.find(room->id.value);
        if (seated == occupants.end()) {
            continue;
        }
        const auto columns = static_cast<std::uint32_t>(
            std::ceil(std::sqrt(static_cast<float>(room->capacity))));
        const float stepX = size.x / static_cast<float>(columns);
        const float stepZ =
            size.z / std::ceil(static_cast<float>(room->capacity) /
                               static_cast<float>(columns));
        for (std::size_t index = 0; index < seated->second.size(); ++index) {
            const auto column = static_cast<float>(index % columns);
            const auto row = static_cast<float>(index / columns);
            const glm::vec3 position{
                box.minimum.x + ((column + 0.5F) * stepX),
                box.maximum.y + kInteriorRobotRadius,
                box.minimum.z + ((row + 0.5F) * stepZ)};
            const bool robotSelected = seated->second[index] == selection.robot;
            queue.SubmitOpaqueMesh(
                *assets_.sphere,
                Transform{.position = position,
                          .rotationDegrees = glm::vec3(0.0F),
                          .scale = glm::vec3(kInteriorRobotRadius *
                                             (robotSelected ? 1.5F : 1.0F))},
                robotSelected ? assets_.highlight : assets_.robot);
            robotMarkers_.push_back({.id = seated->second[index],
                                     .position = position,
                                     .radius = kInteriorPickRadius});
        }
    }
}

CityView::Box CityView::RoomBox(const BuildingShape& shape,
                                const sim::Room& room) const {
    const std::vector<const sim::Room*>& rooms = shape.floorRooms[room.floor];
    const auto index = static_cast<std::uint32_t>(
        std::ranges::find(rooms, &room) - rooms.begin());
    const auto count = static_cast<std::uint32_t>(rooms.size());
    const auto columns = static_cast<std::uint32_t>(
        std::ceil(std::sqrt(static_cast<float>(count))));
    const std::uint32_t rows = (count + columns - 1) / columns;
    const float inner = kBuildingFootprint - (2.0F * kRoomInset);
    const float width = inner / static_cast<float>(columns);
    const float depth = inner / static_cast<float>(rows);
    const float gap = 0.04F;
    const glm::vec3 corner =
        shape.center +
        glm::vec3{-inner * 0.5F,
                  (static_cast<float>(room.floor) * kFloorHeight) +
                      kSlabThickness,
                  -inner * 0.5F};
    const glm::vec3 minimum =
        corner +
        glm::vec3{(static_cast<float>(index % columns) * width) + gap, 0.0F,
                  (static_cast<float>(index / columns) * depth) + gap};
    return {.minimum = minimum,
            .maximum = minimum + glm::vec3{width - (2.0F * gap),
                                           kRoomWallHeight,
                                           depth - (2.0F * gap)}};
}

std::shared_ptr<const Material> CityView::RoomMaterial(
    sim::RoomKind kind) const {
    const auto found = assets_.rooms.find(kind);
    return found != assets_.rooms.end() ? found->second : assets_.tower;
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
        const glm::vec3 offset = ray.origin - marker.position;
        const float b = glm::dot(offset, ray.direction);
        const float c =
            glm::dot(offset, offset) - (marker.radius * marker.radius);
        const float discriminant = (b * b) - c;
        if (discriminant >= 0.0F) {
            consider(-b - std::sqrt(discriminant), {.robot = marker.id});
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
        const glm::vec3 half{kBuildingFootprint * 0.5F, 0.0F,
                             kBuildingFootprint * 0.5F};
        // The open building is cut away above the floor being viewed, so its
        // rooms and robots are not hidden behind its full height.
        const float height =
            id == openBuilding_.value
                ? (static_cast<float>(openFloor_) * kFloorHeight) +
                      kSlabThickness
                : shape.height;
        const Box box{.minimum = shape.center - half,
                      .maximum = shape.center + half +
                                 glm::vec3{0.0F, height, 0.0F}};
        if (const auto distance = hitBox(box)) {
            // Rooms and robots of the open building sit inside its box;
            // prefer them by nudging the building hit back a little.
            consider(*distance + 0.01F, {.building = sim::EntityId{id}});
        }
    }
    return result;
}

std::uint32_t CityView::Floors(sim::EntityId building) const {
    const auto found = shapes_.find(building.value);
    return found != shapes_.end() ? found->second.floors : 0;
}

glm::vec3 CityView::FloorCenter(sim::EntityId building,
                                std::uint32_t floor) const {
    const auto found = shapes_.find(building.value);
    if (found == shapes_.end()) {
        return center_;
    }
    return found->second.center +
           glm::vec3{0.0F, static_cast<float>(floor) * kFloorHeight, 0.0F};
}

glm::vec3 CityView::NodePosition(sim::NodeId node) const {
    const sim::CityNode& cityNode = city_.nodes.at(node.value);
    return {static_cast<float>(cityNode.x) * kUnitsPerMeter, 0.0F,
            static_cast<float>(cityNode.z) * kUnitsPerMeter};
}

const std::vector<glm::vec3>& CityView::Route(sim::EntityId from,
                                              sim::EntityId to) {
    const auto key = std::make_pair(from.value, to.value);
    if (const auto cached = routeCache_.find(key);
        cached != routeCache_.end()) {
        return cached->second;
    }
    std::vector<glm::vec3> points;
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

glm::vec3 CityView::RobotPosition(const RobotFrame& robot, double renderTick) {
    const std::vector<glm::vec3>& route = Route(robot.from, robot.to);
    // Spread robots sharing a street so they do not overlap exactly.
    const std::uint64_t hash = robot.id.value * 2654435761ULL;
    const glm::vec3 spread{
        ((static_cast<float>(hash & 0xFFU) / 255.0F) - 0.5F) * kRobotSpread,
        kStreetHeight + kRobotRadius,
        ((static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F) - 0.5F) *
            kRobotSpread};
    if (route.empty()) {
        return spread;
    }

    const double duration =
        static_cast<double>(robot.arrivesAt - robot.departedAt);
    const double progress =
        duration > 0.0
            ? std::clamp((renderTick - static_cast<double>(robot.departedAt)) /
                             duration,
                         0.0, 1.0)
            : 1.0;

    float total = 0.0F;
    for (std::size_t index = 1; index < route.size(); ++index) {
        total += glm::distance(route[index - 1], route[index]);
    }
    float remaining = static_cast<float>(progress) * total;
    for (std::size_t index = 1; index < route.size(); ++index) {
        const float length = glm::distance(route[index - 1], route[index]);
        if (remaining <= length && length > 0.0F) {
            return glm::mix(route[index - 1], route[index],
                            remaining / length) +
                   spread;
        }
        remaining -= length;
    }
    return route.back() + spread;
}

}  // namespace mechapunk
