// The parts of the city that never move, built once into static batches:
// buildings, facades, parks, street lamps.

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

constexpr vec3 kGrass{0.16F, 0.32F, 0.17F};

constexpr vec3 kPavement{0.36F, 0.36F, 0.38F};

constexpr vec3 kFacadeGlass{0.13F, 0.17F, 0.24F};

// Light from a lit window at night, and the share of windows that are lit.
constexpr vec3 kWindowLight{0.55F, 0.42F, 0.25F};

constexpr std::uint32_t kLitWindowsInTen = 4;

// Street lamps: one every 60 m along each street, alternating sides, a
// 6 m pole and a downward cone.
constexpr float kStreetLampSpacing = 60.0F * kS;

constexpr vec3 kStreetLampBulb{0.0F, 5.82F * kS, 1.45F * kS};

}  // namespace

void CityView::BuildStaticBatches() {
    // Statics away from buildings are grouped in square cells this wide, so
    // the renderer can skip the cells out of view.
    constexpr float kCell = 100.0F * kS;
    using Key = std::tuple<std::uint64_t, std::int64_t, std::int64_t, const Mesh*,
                           const Material*>;
    struct Group {
        const Mesh* mesh;
        std::shared_ptr<const Material> material;
        std::vector<Transform> transforms;
    };
    std::map<Key, Group> groups;
    const auto add = [&](std::uint64_t building, const StaticItem& item) {
        std::int64_t cellX = 0;
        std::int64_t cellZ = 0;
        if (building == 0) {
            cellX = static_cast<std::int64_t>(std::floor(item.transform.position.x / kCell));
            cellZ = static_cast<std::int64_t>(std::floor(item.transform.position.z / kCell));
        }
        Group& group = groups[{building, cellX, cellZ, item.mesh, item.material.get()}];
        group.mesh = item.mesh;
        group.material = item.material;
        group.transforms.push_back(item.transform);
    };
    // Cubes on a building's footprint (windows, signs) belong to it, and
    // hide with its block while it is open; streets never do.
    const auto owner = [&](const StaticItem& item) -> std::uint64_t {
        if (item.mesh != assets_.cube.get() || item.material == assets_.street) {
            return 0;
        }
        for (const auto& [id, shape] : shapes_) {
            const vec3 offset = glm::abs(item.transform.position - shape.center);
            if (offset.x <= (kBuildingWidth * 0.5F) + (0.1F * kS) &&
                offset.z <= (kBuildingDepth * 0.5F) + (0.1F * kS)) {
                return id;
            }
        }
        return 0;
    };
    for (const StaticItem& item : statics_) {
        add(owner(item), item);
    }
    for (const auto& [id, blocks] : buildingBlocks_) {
        for (const StaticItem& block : blocks) {
            add(id, block);
        }
    }
    staticBatches_.reserve(groups.size());
    for (auto& [key, group] : groups) {
        staticBatches_.push_back(
            {.building = std::get<0>(key),
             .batch = MeshBatch(*group.mesh, gsl_lite::not_null{group.material},
                                group.transforms)});
    }
    statics_.clear();
    statics_.shrink_to_fit();
}

// Appends the items just pushed into statics_ as batches owned by `building`,
// so they hide with its block while it is open. Unlike BuildStaticBatches this
// runs after the constructor, one building at a time (task D, step D4).
void CityView::BatchStaticsFor(std::uint64_t building) {
    struct Group {
        const Mesh* mesh;
        std::shared_ptr<const Material> material;
        std::vector<Transform> transforms;
    };
    std::map<std::pair<const Mesh*, const Material*>, Group> groups;
    for (const StaticItem& item : statics_) {
        Group& group = groups[{item.mesh, item.material.get()}];
        group.mesh = item.mesh;
        group.material = item.material;
        group.transforms.push_back(item.transform);
    }
    for (auto& [key, group] : groups) {
        staticBatches_.push_back(
            {.building = building,
             .batch = MeshBatch(*group.mesh, gsl_lite::not_null{group.material},
                                group.transforms)});
    }
    statics_.clear();
    statics_.shrink_to_fit();
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

void CityView::AddFacade(vec3 center, std::span<const MassBox> boxes, sim::BuildingStyle style) {
    const WindowRhythm rhythm = WindowRhythmOf(style);
    // Windows every 3.5 m on every side and floor, standing a little proud
    // of the wall so they read from a distance.
    const std::shared_ptr<const Material> glass =
        assets_.palette->Get(kFacadeGlass);
    const std::shared_ptr<const Material> litGlass =
        assets_.palette->GetGlowing(kFacadeGlass, kWindowLight);
    // Which windows are lit is fixed per window, mixed from its place.
    // `face` also tells the boxes of one building apart.
    const auto lit = [&](std::uint32_t face, std::uint32_t floor, int window) {
        std::uint32_t hash =
            (static_cast<std::uint32_t>(std::lround(center.x)) * 73856093U) ^
            (static_cast<std::uint32_t>(std::lround(center.z)) * 19349663U) ^
            (face * 40503U) ^ (floor * 83492791U) ^
            (static_cast<std::uint32_t>(window) * 2654435761U);
        hash ^= hash >> 15U;
        hash *= 2246822519U;
        hash ^= hash >> 13U;
        return hash % 10U < kLitWindowsInTen;
    };
    const float kSpacing = rhythm.spacingMeters * kS;
    constexpr float kWidth = 1.6F * kS;
    constexpr float kHeight = 1.4F * kS;
    constexpr float kSill = 0.9F * kS;
    constexpr float kProud = 0.06F * kS;
    const auto side = [&](const MassBox& box, std::uint32_t index, float length, bool alongX,
                          float offset) {
        const vec3 middle =
            center + vec3{(box.x0 + box.x1) * 0.5F * kS, 0.0F, (box.z0 + box.z1) * 0.5F * kS};
        const int count = std::max(1, static_cast<int>(length / kSpacing));
        const std::uint32_t face = (alongX ? 0U : 2U) + (offset > 0.0F ? 1U : 0U) + (4U * index);
        if ((box.blankFaces & (1U << (face % 4U))) != 0) {
            return;
        }
        for (std::uint32_t floor = box.firstFloor; floor < box.endFloor; ++floor) {
            // A tower runs its windows in bands: every few floors stays blank.
            if (rhythm.bandEvery != 0 && floor % rhythm.bandEvery == rhythm.bandEvery - 1) {
                continue;
            }
            const float y = (static_cast<float>(floor) * kFloorHeight) + kSill;
            for (int window = 0; window < count; ++window) {
                const float along =
                    (-length * 0.5F) +
                    (length / static_cast<float>(count) *
                     (static_cast<float>(window) + 0.5F));
                Transform transform{.position = middle, .rotationDegrees = vec3(0.0F)};
                if (alongX) {
                    transform.position += vec3{along, y, offset};
                    transform.scale = {kWidth, kHeight, kProud * 2.0F};
                } else {
                    transform.position += vec3{offset, y, along};
                    transform.scale = {kProud * 2.0F, kHeight, kWidth};
                }
                statics_.push_back(
                    {.mesh = assets_.cube.get(),
                     .transform = transform,
                     .material = lit(face, floor, window)
                                     ? litGlass
                                     : glass});
            }
        }
    };
    for (std::uint32_t index = 0; index < boxes.size(); ++index) {
        const MassBox& box = boxes[index];
        const float width = (box.x1 - box.x0) * kS;
        const float depth = (box.z1 - box.z0) * kS;
        side(box, index, width, true, depth * 0.5F);
        side(box, index, width, true, -depth * 0.5F);
        side(box, index, depth, false, width * 0.5F);
        side(box, index, depth, false, -width * 0.5F);
    }
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

void CityView::AddStaticBeam(vec3 from, vec3 to, float width, float height, vec3 color) {
    const vec3 span = to - from;
    statics_.push_back(
        {.mesh = assets_.cube.get(),
         .transform = {.position = (from + to) * 0.5F,
                       .rotationDegrees = {0.0F, glm::degrees(std::atan2(span.x, span.z)), 0.0F},
                       .scale = {width, height, glm::length(vec3{span.x, 0.0F, span.z})}},
         .material = assets_.palette->Get(color)});
}

void CityView::AddStaticBox(vec3 minimum, vec3 maximum, vec3 color, bool glows) {
    statics_.push_back(
        {.mesh = assets_.cube.get(),
         .transform = {.position = {(minimum.x + maximum.x) * 0.5F, minimum.y,
                                    (minimum.z + maximum.z) * 0.5F},
                       .rotationDegrees = vec3(0.0F),
                       .scale = maximum - minimum},
         .material = glows ? assets_.palette->GetGlowing(color, color * kGlow)
                           : assets_.palette->Get(color)});
}

std::shared_ptr<const Material> CityView::PartMaterial(const PropPart& part,
                                                       vec3 tint) const {
    const vec3 color = part.tinted ? tint : part.color;
    return part.glows ? assets_.palette->GetGlowing(color, color * kGlow)
                      : assets_.palette->Get(color);
}

void CityView::AddStaticModel(const PropModel& model, vec3 origin,
                              float yawDegrees) {
    ForEachPart(model, origin, yawDegrees, 0.0F, kS,
                [&](const PropPart& part, const Transform& transform) {
                    statics_.push_back(
                        {.mesh = assets_.cube.get(),
                         .transform = transform,
                         .material = PartMaterial(part, part.color)});
                });
}

}  // namespace mechapunk
