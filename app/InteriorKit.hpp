#pragma once

// The interior kit: sizes, walls, windows, frames, spots and lamps that every
// kind of floor plan is built from.
// Private to the Interior*.cpp files that lay out floor plans: never include
// it from anywhere else.

#include "Interior.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/common.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/trigonometric.hpp>
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {
namespace interior_detail {

using glm::vec2;

using glm::vec3;

constexpr float kHalfWidth = kFootprintWidthMeters * 0.5F;

// 13 m
constexpr float kHalfDepth = kFootprintDepthMeters * 0.5F;

// 11 m
// The stair and elevator core fills the west end, up to this x.
constexpr float kCoreEdge = -8.0F;

constexpr float kCorridorHalf = 1.2F;

constexpr float kExteriorThickness = 0.3F;

constexpr float kInteriorThickness = 0.12F;

constexpr float kGlassThickness = 0.04F;

constexpr float kDoorTop = 2.2F;

constexpr float kSill = 0.9F;

constexpr float kLintel = 2.3F;

constexpr float kWindowWidth = 1.6F;

constexpr float kWindowSpacing = 3.5F;

constexpr float kFinish = 0.02F;

// The way through the core: in from the corridor, past the stairs, which
// climb towards -z from their foot to the landing of the floor above, and
// out of the lobby door to the pavement.
constexpr vec3 kCorridorMouth{kCoreEdge + 0.4F, 0.0F, 0.0F};

constexpr vec3 kConcrete{0.42F, 0.44F, 0.48F};

constexpr vec3 kPlaster{0.8F, 0.8F, 0.82F};

constexpr vec3 kShaft{0.2F, 0.2F, 0.23F};

constexpr vec3 kCorridorFloor{0.3F, 0.31F, 0.35F};

constexpr std::array kFabrics{
    vec3{0.6F, 0.2F, 0.25F}, vec3{0.2F, 0.35F, 0.6F}, vec3{0.3F, 0.5F, 0.3F},
    vec3{0.55F, 0.45F, 0.3F}, vec3{0.4F, 0.25F, 0.5F}, vec3{0.15F, 0.5F, 0.55F}};

inline vec3 Fabric(std::uint64_t seed) {
    return kFabrics.at(static_cast<std::size_t>((seed * 2654435761ULL) >> 7U) %
                       kFabrics.size());
}

// A gap along a wall, in world coordinates along the wall's axis.
struct Opening {
    float from{};
    float to{};
    float bottom{};
    float top{};
    bool glass{false};
};

inline Opening DoorAt(float center, float width) {
    return {.from = center - (width * 0.5F),
            .to = center + (width * 0.5F),
            .bottom = 0.0F,
            .top = kDoorTop};
}

// A wall from a to b (x, z), straight along x or z, with openings cut out.
inline void Wall(FloorPlan& plan, vec2 a, vec2 b, float thickness,
          std::vector<Opening> openings, vec3 color) {
    const bool alongX = std::abs(a.y - b.y) < 1e-4F;
    const float start = alongX ? std::min(a.x, b.x) : std::min(a.y, b.y);
    const float end = alongX ? std::max(a.x, b.x) : std::max(a.y, b.y);
    const float across = alongX ? a.y : a.x;
    std::ranges::sort(openings, {}, &Opening::from);
    const auto emit = [&](float s, float e, float y0, float y1, bool glass) {
        if (e - s < 1e-3F || y1 - y0 < 1e-3F) {
            return;
        }
        const float half = (glass ? kGlassThickness : thickness) * 0.5F;
        WallBox box{.color = glass ? vec3{0.6F, 0.8F, 0.92F} : color,
                    .glass = glass};
        if (alongX) {
            box.minimum = {s, y0, across - half};
            box.maximum = {e, y1, across + half};
        } else {
            box.minimum = {across - half, y0, s};
            box.maximum = {across + half, y1, e};
        }
        plan.boxes.push_back(box);
    };
    float cursor = start;
    for (const Opening& opening : openings) {
        const float from = std::clamp(opening.from, start, end);
        const float to = std::clamp(opening.to, start, end);
        emit(cursor, from, 0.0F, kWallHeightMeters, false);
        emit(from, to, 0.0F, opening.bottom, false);
        emit(from, to, opening.top, kWallHeightMeters, false);
        if (opening.glass) {
            emit(from, to, opening.bottom, opening.top, true);
        }
        cursor = std::max(cursor, to);
    }
    emit(cursor, end, 0.0F, kWallHeightMeters, false);
}

// Windows spread evenly over each stretch of wall between `cuts`, where
// partitions meet it, avoiding `doors`; returns windows and doors together.
inline std::vector<Opening> WithWindows(float start, float end, std::vector<float> cuts,
                                 const std::vector<Opening>& doors) {
    cuts.push_back(start);
    cuts.push_back(end);
    std::ranges::sort(cuts);
    std::vector<Opening> openings = doors;
    for (std::size_t index = 1; index < cuts.size(); ++index) {
        const float from = cuts[index - 1];
        const float length = cuts[index] - from;
        int count = static_cast<int>((length - 0.6F) / kWindowSpacing);
        if (count == 0 && length >= 2.2F) {
            count = 1;
        }
        for (int window = 0; window < count; ++window) {
            const float center =
                from + (length / static_cast<float>(count) *
                        (static_cast<float>(window) + 0.5F));
            const Opening candidate{.from = center - (kWindowWidth * 0.5F),
                                    .to = center + (kWindowWidth * 0.5F),
                                    .bottom = kSill,
                                    .top = kLintel,
                                    .glass = true};
            const bool clear = std::ranges::none_of(doors, [&](const Opening& d) {
                return candidate.from < d.to + 0.3F && candidate.to > d.from - 0.3F;
            });
            if (clear) {
                openings.push_back(candidate);
            }
        }
    }
    return openings;
}

inline void FloorFinish(FloorPlan& plan, vec2 a, vec2 b, vec3 color) {
    plan.boxes.push_back({.minimum = {std::min(a.x, b.x), 0.0F, std::min(a.y, b.y)},
                          .maximum = {std::max(a.x, b.x), kFinish, std::max(a.y, b.y)},
                          .color = color});
}

inline float YawOf(vec2 facing) {
    return glm::degrees(std::atan2(facing.x, facing.y));
}

inline void Place(FloorPlan& plan, PropKind kind, vec3 position, vec2 facing,
           vec3 tint = vec3{1.0F}) {
    plan.props.push_back({.kind = kind,
                          .position = position,
                          .yawDegrees = YawOf(facing),
                          .tint = tint});
}

// Local coordinates of a room: X across it, U into it. World x = x0 + mx * X
// and z = z0 + sz * U, so mirrored rooms reuse one layout.
struct Frame {
    float x0{};
    float mx{1.0F};
    float z0{};
    float sz{1.0F};

    [[nodiscard]] vec3 At(float x, float u, float y = 0.0F) const {
        return {x0 + (mx * x), y, z0 + (sz * u)};
    }
    [[nodiscard]] vec2 At2(float x, float u) const {
        return {x0 + (mx * x), z0 + (sz * u)};
    }
    [[nodiscard]] vec2 Dir(float x, float u) const { return {mx * x, sz * u}; }
    // A local span along X or U as a world span along x or z.
    [[nodiscard]] Opening SpanX(float from, float to, Opening o) const {
        const float a = x0 + (mx * from);
        const float b = x0 + (mx * to);
        o.from = std::min(a, b);
        o.to = std::max(a, b);
        return o;
    }
    [[nodiscard]] Opening SpanU(float from, float to, Opening o) const {
        const float a = z0 + (sz * from);
        const float b = z0 + (sz * to);
        o.from = std::min(a, b);
        o.to = std::max(a, b);
        return o;
    }
};

// A pendant lamp hanging into the room at `position` (meters on the floor).
inline void HangLamp(FloorPlan& plan, vec3 position, sim::EntityId room) {
    constexpr float kShadeBottom = 2.3F;
    position.y = kShadeBottom;
    Place(plan, PropKind::CeilingLamp, position, {0.0F, 1.0F});
    plan.lamps.push_back({.position = position - vec3{0.0F, 0.05F, 0.0F},
                          .room = room});
}

// Lamps on a grid of roughly one per `spacing` meters over a room.
inline void LampGrid(FloorPlan& plan, const Frame& f, float width, float depth,
              sim::EntityId room, float spacing) {
    const int columns = std::max(1, static_cast<int>(std::round(width / spacing)));
    const int rows = std::max(1, static_cast<int>(std::round(depth / spacing)));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            HangLamp(plan,
                     f.At(width * (static_cast<float>(column) + 0.5F) /
                              static_cast<float>(columns),
                          depth * (static_cast<float>(row) + 0.5F) /
                              static_cast<float>(rows)),
                     room);
        }
    }
}

inline Spot MakeSpot(const Frame& frame, float x, float u, vec2 facing, Pose pose,
              float y = 0.0F) {
    return {.position = frame.At(x, u, y),
            .yawDegrees = YawOf(frame.Dir(facing.x, facing.y)),
            .pose = pose};
}

// `spot`, with `kind` set out at frame coordinates (x, u, y) while taken.
inline Spot WithItem(Spot spot, const Frame& frame, PropKind kind, float x, float u,
              float y) {
    spot.item = PlacedProp{.kind = kind,
                           .position = frame.At(x, u, y),
                           .yawDegrees = spot.yawDegrees};
    return spot;
}

// `spot`, reached by way of `approach`.
inline Spot Via(Spot spot, std::vector<vec3> approach) {
    spot.approach = std::move(approach);
    return spot;
}

// Standing spots on a grid, for rooms with more occupants than furniture.
inline void FillStanding(RoomPlan& room, const Frame& frame, float x0, float x1,
                  float u0, float u1, std::size_t wanted) {
    if (room.spots.size() >= wanted) {
        return;
    }
    const auto missing = static_cast<float>(wanted - room.spots.size());
    const int columns = std::max(
        1, static_cast<int>(std::ceil(std::sqrt(missing * (x1 - x0) / (u1 - u0)))));
    const int rows = static_cast<int>(std::ceil(missing / static_cast<float>(columns)));
    for (int row = 0; row < rows && room.spots.size() < wanted; ++row) {
        for (int column = 0; column < columns && room.spots.size() < wanted;
             ++column) {
            const float x = x0 + ((x1 - x0) * (static_cast<float>(column) + 0.5F) /
                                  static_cast<float>(columns));
            const float u = u0 + ((u1 - u0) * (static_cast<float>(row) + 0.5F) /
                                  static_cast<float>(rows));
            room.spots.push_back(
                MakeSpot(frame, x, u, {column % 2 == 0 ? 1.0F : -1.0F, 0.0F},
                         Pose::Standing));
        }
    }
}

inline RoomPlan AreaOf(const sim::Room& room, const Frame& frame, float width,
                float depth) {
    const vec3 a = frame.At(0.0F, 0.0F);
    const vec3 b = frame.At(width, depth, kWallHeightMeters);
    return {.room = room.id,
            .minimum = glm::min(a, b),
            .maximum = glm::max(a, b)};
}

// Stairs, the elevator shaft and the core's floor, west of kCoreEdge.
inline void Core(FloorPlan& plan) {
    FloorFinish(plan, {-kHalfWidth, -kHalfDepth}, {kCoreEdge, kHalfDepth},
                kCorridorFloor);
    Place(plan, PropKind::Stairs, {-10.5F, 0.0F, 5.8F}, {0.0F, 1.0F});
    plan.boxes.push_back({.minimum = {-12.4F, 0.0F, -5.2F},
                          .maximum = {-8.6F, kWallHeightMeters, -1.75F},
                          .color = kShaft});
    Place(plan, PropKind::ElevatorDoors, {-10.5F, 0.0F, -1.7F}, {0.0F, 1.0F});
    HangLamp(plan, {-10.5F, 0.0F, 1.2F}, {});
    for (const float sign : {-1.0F, 1.0F}) {
        Wall(plan, {kCoreEdge, sign * kCorridorHalf}, {kCoreEdge, sign * kHalfDepth},
             kInteriorThickness, {}, kPlaster);
    }
}

inline void Exterior(FloorPlan& plan, const std::vector<float>& cutsX,
              const std::vector<Opening>& northDoors) {
    Wall(plan, {-kHalfWidth, kHalfDepth}, {kHalfWidth, kHalfDepth},
         kExteriorThickness, WithWindows(-kHalfWidth, kHalfWidth, cutsX, northDoors),
         kConcrete);
    Wall(plan, {-kHalfWidth, -kHalfDepth}, {kHalfWidth, -kHalfDepth},
         kExteriorThickness, WithWindows(-kHalfWidth, kHalfWidth, cutsX, {}),
         kConcrete);
    for (const float x : {-kHalfWidth, kHalfWidth}) {
        Wall(plan, {x, -kHalfDepth}, {x, kHalfDepth}, kExteriorThickness,
             WithWindows(-kHalfDepth, kHalfDepth, {-kCorridorHalf, kCorridorHalf}, {}),
             kConcrete);
    }
}

// Defined in the part files, used across them.
// A floor of homes of the class of its first apartment; studios for a floor
// without any.
void HomeFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms);

// Stores fill the ground floor of a habitat, side by side behind glass.
void Store(FloorPlan& plan, const sim::Room& room, const Frame& f, float width);

void StoreFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms);

void WorkplaceFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms,
                    bool ground);

}  // namespace interior_detail
using namespace interior_detail;
}  // namespace mechapunk
