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

namespace {
using glm::vec2;
using glm::vec3;

constexpr float kHalfWidth = kFootprintWidthMeters * 0.5F;  // 13 m
constexpr float kHalfDepth = kFootprintDepthMeters * 0.5F;  // 11 m
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
constexpr float kFinish = 0.02F;  // Floor finish thickness.
// Apartments: three per side of the corridor, 7 m wide and 9.8 m deep.
constexpr float kUnitWidth = 7.0F;
constexpr float kUnitDepth = kHalfDepth - kCorridorHalf;
// The way through the core: in from the corridor, past the stairs, which
// climb towards -z from their foot to the landing of the floor above, and
// out of the lobby door to the pavement.
constexpr vec3 kCorridorMouth{kCoreEdge + 0.4F, 0.0F, 0.0F};
constexpr vec3 kCoreHall{-9.2F, 0.0F, 0.0F};
constexpr vec3 kStairsFoot{-10.5F, 0.0F, 8.6F};
constexpr vec3 kBesideFoot{-9.2F, 0.0F, 8.6F};
constexpr vec3 kStairsLanding{-10.5F, 0.0F, 3.0F};
constexpr vec3 kBesideLanding{-9.2F, 0.0F, 3.0F};
constexpr vec3 kLobby{-10.5F, 0.0F, kHalfDepth - 0.8F};
constexpr vec3 kLobbyOutside{-10.5F, 0.0F, kHalfDepth + 0.8F};

constexpr vec3 kConcrete{0.42F, 0.44F, 0.48F};
constexpr vec3 kPlaster{0.8F, 0.8F, 0.82F};
constexpr vec3 kShaft{0.2F, 0.2F, 0.23F};
constexpr vec3 kWoodFloor{0.5F, 0.36F, 0.24F};
constexpr vec3 kTileFloor{0.7F, 0.74F, 0.78F};
constexpr vec3 kCorridorFloor{0.3F, 0.31F, 0.35F};
constexpr vec3 kStoreFloor{0.22F, 0.22F, 0.27F};
constexpr vec3 kIndustrialFloor{0.36F, 0.36F, 0.34F};
constexpr vec3 kDoorWood{0.42F, 0.28F, 0.17F};
constexpr std::array kFabrics{
    vec3{0.6F, 0.2F, 0.25F}, vec3{0.2F, 0.35F, 0.6F}, vec3{0.3F, 0.5F, 0.3F},
    vec3{0.55F, 0.45F, 0.3F}, vec3{0.4F, 0.25F, 0.5F}, vec3{0.15F, 0.5F, 0.55F}};

vec3 Fabric(std::uint64_t seed) {
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

Opening DoorAt(float center, float width) {
    return {.from = center - (width * 0.5F),
            .to = center + (width * 0.5F),
            .bottom = 0.0F,
            .top = kDoorTop};
}

// A wall from a to b (x, z), straight along x or z, with openings cut out.
void Wall(FloorPlan& plan, vec2 a, vec2 b, float thickness,
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
std::vector<Opening> WithWindows(float start, float end, std::vector<float> cuts,
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

void FloorFinish(FloorPlan& plan, vec2 a, vec2 b, vec3 color) {
    plan.boxes.push_back({.minimum = {std::min(a.x, b.x), 0.0F, std::min(a.y, b.y)},
                          .maximum = {std::max(a.x, b.x), kFinish, std::max(a.y, b.y)},
                          .color = color});
}

float YawOf(vec2 facing) {
    return glm::degrees(std::atan2(facing.x, facing.y));
}

void Place(FloorPlan& plan, PropKind kind, vec3 position, vec2 facing,
           vec3 tint = vec3{1.0F}) {
    plan.props.push_back({.kind = kind,
                          .position = position,
                          .yawDegrees = YawOf(facing),
                          .tint = tint});
}

// An open door leaf hinged at `hinge`, lying along `along`.
void DoorLeaf(FloorPlan& plan, vec3 hinge, vec2 along) {
    plan.props.push_back({.kind = PropKind::Door,
                          .position = hinge,
                          .yawDegrees = glm::degrees(std::atan2(-along.y, along.x)),
                          .tint = kDoorWood});
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
void HangLamp(FloorPlan& plan, vec3 position, sim::EntityId room) {
    constexpr float kShadeBottom = 2.3F;
    position.y = kShadeBottom;
    Place(plan, PropKind::CeilingLamp, position, {0.0F, 1.0F});
    plan.lamps.push_back({.position = position - vec3{0.0F, 0.05F, 0.0F},
                          .room = room});
}

// Lamps on a grid of roughly one per `spacing` meters over a room.
void LampGrid(FloorPlan& plan, const Frame& f, float width, float depth,
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

Spot MakeSpot(const Frame& frame, float x, float u, vec2 facing, Pose pose,
              float y = 0.0F) {
    return {.position = frame.At(x, u, y),
            .yawDegrees = YawOf(frame.Dir(facing.x, facing.y)),
            .pose = pose};
}

// `spot`, with `kind` set out at frame coordinates (x, u, y) while taken.
Spot WithItem(Spot spot, const Frame& frame, PropKind kind, float x, float u,
              float y) {
    spot.item = PlacedProp{.kind = kind,
                           .position = frame.At(x, u, y),
                           .yawDegrees = spot.yawDegrees};
    return spot;
}

// `spot`, reached by way of `approach`.
Spot Via(Spot spot, std::vector<vec3> approach) {
    spot.approach = std::move(approach);
    return spot;
}

// Standing spots on a grid, for rooms with more occupants than furniture.
void FillStanding(RoomPlan& room, const Frame& frame, float x0, float x1,
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

RoomPlan AreaOf(const sim::Room& room, const Frame& frame, float width,
                float depth) {
    const vec3 a = frame.At(0.0F, 0.0F);
    const vec3 b = frame.At(width, depth, kWallHeightMeters);
    return {.room = room.id,
            .minimum = glm::min(a, b),
            .maximum = glm::max(a, b)};
}

// Stairs, the elevator shaft and the core's floor, west of kCoreEdge.
void Core(FloorPlan& plan) {
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

void Exterior(FloorPlan& plan, const std::vector<float>& cutsX,
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

// One apartment: bathroom by the door, bedroom and living room with kitchen
// towards the windows.
void Apartment(FloorPlan& plan, const Frame& f, const sim::Room* room) {
    // Floors: tiles in the bathroom, wood elsewhere.
    FloorFinish(plan, f.At2(0.0F, 0.0F), f.At2(2.6F, 3.2F), kTileFloor);
    FloorFinish(plan, f.At2(2.6F, 0.0F), f.At2(kUnitWidth, 3.2F), kWoodFloor);
    FloorFinish(plan, f.At2(0.0F, 3.2F), f.At2(kUnitWidth, kUnitDepth), kWoodFloor);

    // Bathroom back wall (also the bedroom's), bathroom side wall with its
    // door, and the bedroom wall with its door.
    Wall(plan, f.At2(0.0F, 3.2F), f.At2(3.4F, 3.2F), kInteriorThickness, {},
         kPlaster);
    Wall(plan, f.At2(2.6F, 0.0F), f.At2(2.6F, 3.2F), kInteriorThickness,
         {f.SpanU(1.5F, 2.3F, {.top = kDoorTop})}, kPlaster);
    Wall(plan, f.At2(3.4F, 3.2F), f.At2(3.4F, kUnitDepth), kInteriorThickness,
         {f.SpanU(3.8F, 4.7F, {.top = kDoorTop})}, kPlaster);
    DoorLeaf(plan, f.At(2.66F, 1.5F), f.Dir(1.0F, 0.0F));
    DoorLeaf(plan, f.At(3.46F, 3.8F), f.Dir(1.0F, 0.0F));
    DoorLeaf(plan, f.At(4.1F, 0.08F), f.Dir(0.0F, 1.0F));

    if (room == nullptr) {
        return;  // A vacant unit: walls and floors only.
    }
    const vec3 fabric = Fabric(room->id.value);
    const vec3 rug = Fabric(room->id.value + 3);
    // Bathroom.
    Place(plan, PropKind::Toilet, f.At(0.45F, 2.85F), f.Dir(0, -1));
    Place(plan, PropKind::Sink, f.At(1.6F, 2.9F), f.Dir(0, -1));
    Place(plan, PropKind::Shower, f.At(0.5F, 0.5F), f.Dir(0, 1));
    // Bedroom.
    Place(plan, PropKind::Bed, f.At(1.5F, 8.6F), f.Dir(0, -1), fabric);
    Place(plan, PropKind::Nightstand, f.At(0.35F, 9.4F), f.Dir(0, -1));
    Place(plan, PropKind::Wardrobe, f.At(0.35F, 4.8F), f.Dir(1, 0));
    Place(plan, PropKind::Rug, f.At(1.7F, 6.6F), f.Dir(0, 1), rug);
    // Living room and kitchen.
    Place(plan, PropKind::KitchenCounter, f.At(6.68F, 4.6F), f.Dir(-1, 0));
    Place(plan, PropKind::Fridge, f.At(6.65F, 6.3F), f.Dir(-1, 0));
    Place(plan, PropKind::DiningTable, f.At(4.6F, 5.2F), f.Dir(0, 1));
    Place(plan, PropKind::Chair, f.At(4.6F, 4.55F), f.Dir(0, 1), fabric);
    Place(plan, PropKind::Chair, f.At(4.6F, 5.85F), f.Dir(0, -1), fabric);
    Place(plan, PropKind::Sofa, f.At(6.5F, 8.2F), f.Dir(-1, 0), fabric);
    Place(plan, PropKind::Rug, f.At(5.3F, 8.2F), f.Dir(1, 0), rug);
    Place(plan, PropKind::CoffeeTable, f.At(5.3F, 8.2F), f.Dir(1, 0));
    Place(plan, PropKind::WallScreen, f.At(3.5F, 8.2F), f.Dir(1, 0));
    Place(plan, PropKind::Plant, f.At(6.6F, 9.5F), f.Dir(0, 1));
    // A lamp in the bathroom, the bedroom and the living room.
    HangLamp(plan, f.At(1.3F, 1.6F), room->id);
    HangLamp(plan, f.At(1.7F, 6.4F), room->id);
    HangLamp(plan, f.At(5.2F, 6.6F), room->id);

    RoomPlan area = AreaOf(*room, f, kUnitWidth, kUnitDepth);
    area.home = true;
    area.entry = {kCorridorMouth, vec3{f.At(4.6F, 0.0F).x, 0.0F, 0.0F}, f.At(4.6F, 0.7F)};
    // Lying on the bed with the head on the pillow, then the sofa, then the
    // kitchen.
    area.spots.push_back(Via(MakeSpot(f, 1.5F, 7.75F, {0.0F, -1.0F}, Pose::Lying, 0.67F),
                             {f.At(3.9F, 2.8F), f.At(3.9F, 4.25F), f.At(2.8F, 4.25F),
                              f.At(1.5F, 6.9F)}));
    area.spots.push_back(Via(MakeSpot(f, 6.45F, 8.2F, {-1.0F, 0.0F}, Pose::Sitting),
                             {f.At(5.8F, 2.8F), f.At(5.8F, 7.2F)}));
    area.spots.push_back(Via(WithItem(MakeSpot(f, 6.0F, 4.6F, {1.0F, 0.0F}, Pose::Standing),
                                      f, PropKind::Meal, 6.55F, 4.3F, 0.92F),
                             {f.At(5.8F, 2.8F)}));
    plan.rooms.push_back(std::move(area));
}

// A dorm room, 3.5 m wide: a bed by the window, a desk and a chair, a
// wardrobe and a washbasin by the door.
void DormRoom(FloorPlan& plan, const Frame& f, const sim::Room* room) {
    constexpr float kWidth = 3.5F;
    FloorFinish(plan, f.At2(0.0F, 0.0F), f.At2(kWidth, kUnitDepth), kWoodFloor);
    DoorLeaf(plan, f.At(1.3F, 0.08F), f.Dir(0.0F, 1.0F));
    if (room == nullptr) {
        return;
    }
    const vec3 fabric = Fabric(room->id.value);
    Place(plan, PropKind::Bed, f.At(0.95F, 8.65F), f.Dir(0, -1), fabric);
    Place(plan, PropKind::Desk, f.At(3.05F, 5.2F), f.Dir(-1, 0));
    Place(plan, PropKind::Chair, f.At(2.35F, 5.2F), f.Dir(1, 0), fabric);
    Place(plan, PropKind::Wardrobe, f.At(3.15F, 1.6F), f.Dir(-1, 0));
    Place(plan, PropKind::Sink, f.At(0.35F, 2.2F), f.Dir(1, 0));
    Place(plan, PropKind::Plant, f.At(3.15F, 9.4F), f.Dir(0, -1));
    HangLamp(plan, f.At(1.75F, 5.6F), room->id);
    RoomPlan area = AreaOf(*room, f, kWidth, kUnitDepth);
    area.home = true;
    area.entry = {kCorridorMouth, vec3{f.At(1.75F, 0.0F).x, 0.0F, 0.0F}, f.At(1.75F, 0.7F)};
    area.spots.push_back(Via(MakeSpot(f, 0.95F, 7.8F, {0.0F, -1.0F}, Pose::Lying, 0.67F),
                             {f.At(2.0F, 6.9F)}));
    area.spots.push_back(Via(MakeSpot(f, 2.35F, 5.2F, {1.0F, 0.0F}, Pose::Sitting),
                             {f.At(1.75F, 4.2F)}));
    plan.rooms.push_back(std::move(area));
}

// The spots of a home in the order residents use them: every resident's
// bed and then its seat, and after them the kitchen.
struct HomeSpots {
    std::vector<Spot> beds;
    std::vector<Spot> seats;
    std::vector<Spot> extras;

    void AppendTo(std::vector<Spot>& spots) const {
        for (std::size_t index = 0; index < beds.size(); ++index) {
            spots.push_back(beds[index]);
            if (index < seats.size()) {
                spots.push_back(seats[index]);
            }
        }
        spots.insert(spots.end(), extras.begin(), extras.end());
    }
};

// A bedroom across the end of a flat, from local x to x + 3.4, with its
// door into the living room; `far` puts it at the flat's far end.
void Bedroom(FloorPlan& plan, const Frame& f, float width, bool far, const sim::Room& room,
             HomeSpots& spots, const std::vector<vec3>& via) {
    // Local x of the bedroom's inner wall and the bed's middle.
    const float wall = far ? width - 3.4F : 3.4F;
    const float side = far ? -1.0F : 1.0F;  // Toward the living room.
    const auto x = [&](float fromEnd) { return far ? width - fromEnd : fromEnd; };
    Wall(plan, f.At2(far ? wall : 0.0F, 3.2F), f.At2(far ? width : wall, 3.2F),
         kInteriorThickness, {}, kPlaster);
    Wall(plan, f.At2(wall, 3.2F), f.At2(wall, kUnitDepth), kInteriorThickness,
         {f.SpanU(3.8F, 4.7F, {.top = kDoorTop})}, kPlaster);
    DoorLeaf(plan, f.At(wall + (0.06F * side), 3.8F), f.Dir(side, 0.0F));
    const vec3 fabric = Fabric(room.id.value + (far ? 7U : 0U));
    Place(plan, PropKind::Bed, f.At(x(1.5F), 8.6F), f.Dir(0, -1), fabric);
    Place(plan, PropKind::Nightstand, f.At(x(0.35F), 9.4F), f.Dir(0, -1));
    Place(plan, PropKind::Wardrobe, f.At(x(0.35F), 4.8F), f.Dir(side, 0));
    Place(plan, PropKind::Rug, f.At(x(1.7F), 6.6F), f.Dir(0, 1), Fabric(room.id.value + 3));
    HangLamp(plan, f.At(x(1.7F), 6.4F), room.id);
    std::vector<vec3> approach = via;
    for (const vec3 point : {f.At(wall + (0.5F * side), 2.8F), f.At(wall + (0.5F * side), 4.25F),
                             f.At(wall - (0.6F * side), 4.25F), f.At(x(1.5F), 6.9F)}) {
        approach.push_back(point);
    }
    spots.beds.push_back(
        Via(MakeSpot(f, x(1.5F), 7.75F, {0.0F, -1.0F}, Pose::Lying, 0.67F), approach));
}

// A flat of `width` meters: a bathroom by the door, one bedroom at the near
// end (and with two, another at the far end), and between them a living
// room with a kitchen along the corridor wall, a dining table, and sofas
// facing a screen on the bedroom wall. `via` leads to its front door.
void Flat(FloorPlan& plan, const Frame& f, const sim::Room& room, float width, int bedrooms,
          HomeSpots& spots, const std::vector<vec3>& via) {
    const float living = bedrooms == 2 ? width - 3.4F : width;
    FloorFinish(plan, f.At2(0.0F, 0.0F), f.At2(2.6F, 3.2F), kTileFloor);
    FloorFinish(plan, f.At2(2.6F, 0.0F), f.At2(width, 3.2F), kWoodFloor);
    FloorFinish(plan, f.At2(0.0F, 3.2F), f.At2(width, kUnitDepth), kWoodFloor);
    // The bathroom.
    Wall(plan, f.At2(2.6F, 0.0F), f.At2(2.6F, 3.2F), kInteriorThickness,
         {f.SpanU(1.5F, 2.3F, {.top = kDoorTop})}, kPlaster);
    DoorLeaf(plan, f.At(2.66F, 1.5F), f.Dir(1.0F, 0.0F));
    Place(plan, PropKind::Toilet, f.At(0.45F, 2.85F), f.Dir(0, -1));
    Place(plan, PropKind::Sink, f.At(1.6F, 2.9F), f.Dir(0, -1));
    Place(plan, PropKind::Shower, f.At(0.5F, 0.5F), f.Dir(0, 1));
    HangLamp(plan, f.At(1.3F, 1.6F), room.id);
    Bedroom(plan, f, width, false, room, spots, via);
    if (bedrooms == 2) {
        Bedroom(plan, f, width, true, room, spots, via);
    }
    const vec3 fabric = Fabric(room.id.value);
    const vec3 rug = Fabric(room.id.value + 3);
    // Kitchen, and the dining table clear of the ways to the sofas.
    const float dining = bedrooms == 2 ? 11.5F : 9.6F;
    Place(plan, PropKind::KitchenCounter, f.At(7.7F, 0.35F), f.Dir(0, 1));
    Place(plan, PropKind::Fridge, f.At(9.35F, 0.38F), f.Dir(0, 1));
    Place(plan, PropKind::DiningTable, f.At(dining, 3.9F), f.Dir(0, 1));
    Place(plan, PropKind::Chair, f.At(dining, 3.25F), f.Dir(0, 1), fabric);
    Place(plan, PropKind::Chair, f.At(dining, 4.55F), f.Dir(0, -1), fabric);
    // The lounge: a screen on the bedroom wall, a sofa for each bedroom.
    Place(plan, PropKind::WallScreen, f.At(3.5F, 8.2F), f.Dir(1, 0));
    Place(plan, PropKind::Rug, f.At(5.3F, 8.2F), f.Dir(1, 0), rug);
    Place(plan, PropKind::CoffeeTable, f.At(5.1F, 8.2F), f.Dir(1, 0));
    for (int sofa = 0; sofa < bedrooms; ++sofa) {
        const float x = 6.5F + (2.0F * static_cast<float>(sofa));
        Place(plan, PropKind::Sofa, f.At(x, 8.2F), f.Dir(-1, 0), fabric);
        // Down the gap in front of the sofa, then onto it.
        std::vector<vec3> approach = via;
        approach.push_back(f.At(x - 0.75F, 2.2F));
        approach.push_back(f.At(x - 0.75F, 7.2F));
        spots.seats.push_back(
            Via(MakeSpot(f, x - 0.05F, 8.2F, {-1.0F, 0.0F}, Pose::Sitting), approach));
    }
    if (bedrooms == 2) {
        // More room: four at the table, a study corner, and plants.
        Place(plan, PropKind::Chair, f.At(dining - 0.85F, 3.9F), f.Dir(1, 0), fabric);
        Place(plan, PropKind::Chair, f.At(dining + 0.85F, 3.9F), f.Dir(-1, 0), fabric);
        Place(plan, PropKind::Desk, f.At(living - 2.0F, 0.6F), f.Dir(0, 1));
        Place(plan, PropKind::Chair, f.At(living - 2.0F, 1.3F), f.Dir(0, -1), fabric);
        Place(plan, PropKind::Rug, f.At(12.5F, 6.0F), f.Dir(0, 1), rug);
        Place(plan, PropKind::Plant, f.At(living - 0.5F, 9.4F), f.Dir(0, -1));
        Place(plan, PropKind::Plant, f.At(12.6F, 0.4F), f.Dir(0, 1));
    } else {
        Place(plan, PropKind::Plant, f.At(living - 0.5F, 9.4F), f.Dir(0, -1));
    }
    LampGrid(plan, Frame{.x0 = f.At(3.4F, 0.0F).x, .mx = f.mx, .z0 = f.z0, .sz = f.sz},
             living - 3.4F, kUnitDepth, room.id, 4.5F);
    std::vector<vec3> kitchen = via;
    kitchen.push_back(f.At(7.7F, 1.6F));
    spots.extras.push_back(Via(WithItem(MakeSpot(f, 7.7F, 0.95F, {0.0F, -1.0F}, Pose::Standing),
                                        f, PropKind::Meal, 7.7F, 0.35F, 0.92F),
                               kitchen));
}

// Floors of one class of home, side by side on both sides of the corridor.
struct UnitStyle {
    float width{};
    float doorFrom{};  // The front door, in the unit's local x.
    float doorTo{};
    std::vector<float> wallsX;  // Interior walls meeting the outside wall.
    std::function<void(FloorPlan&, const Frame&, const sim::Room*)> furnish;
};

void UnitsFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms,
                const UnitStyle& style) {
    Core(plan);
    FloorFinish(plan, {kCoreEdge, -kCorridorHalf}, {kHalfWidth, kCorridorHalf},
                kCorridorFloor);
    for (int lamp = 0; lamp < 3; ++lamp) {
        HangLamp(plan,
                 {kCoreEdge + (kUnitWidth * (static_cast<float>(lamp) + 0.5F)), 0.0F,
                  0.0F},
                 {});
    }
    const int perSide = static_cast<int>(std::round((kHalfWidth - kCoreEdge) / style.width));
    std::vector<float> cuts{kCoreEdge};
    std::array<std::vector<Opening>, 2> corridorDoors;
    for (int row = 0; row < 2; ++row) {
        const float side = row == 0 ? 1.0F : -1.0F;
        for (int slot = 0; slot < perSide; ++slot) {
            const float left = kCoreEdge + (style.width * static_cast<float>(slot));
            const bool mirrored = slot % 2 == 1;
            const Frame frame{.x0 = mirrored ? left + style.width : left,
                              .mx = mirrored ? -1.0F : 1.0F,
                              .z0 = side * kCorridorHalf,
                              .sz = side};
            const auto index = static_cast<std::size_t>((row * perSide) + slot);
            style.furnish(plan, frame, index < rooms.size() ? rooms[index] : nullptr);
            corridorDoors.at(static_cast<std::size_t>(row))
                .push_back(frame.SpanX(style.doorFrom, style.doorTo, {.top = kDoorTop}));
            if (row == 0) {
                for (const float wall : style.wallsX) {
                    cuts.push_back(frame.At2(wall, 0.0F).x);
                }
            }
            if (slot > 0) {
                Wall(plan, {left, side * kCorridorHalf}, {left, side * kHalfDepth},
                     kInteriorThickness, {}, kPlaster);
                if (row == 0) {
                    cuts.push_back(left);
                }
            }
        }
        Wall(plan, {kCoreEdge, side * kCorridorHalf}, {kHalfWidth, side * kCorridorHalf},
             kInteriorThickness, corridorDoors.at(static_cast<std::size_t>(row)),
             kPlaster);
    }
    Exterior(plan, cuts, {});
}

// A flat behind a corridor door, as one sim room.
void FlatUnit(FloorPlan& plan, const Frame& f, const sim::Room* room, float width,
              int bedrooms) {
    if (room == nullptr) {
        FloorFinish(plan, f.At2(0.0F, 0.0F), f.At2(width, kUnitDepth), kWoodFloor);
        DoorLeaf(plan, f.At(4.1F, 0.08F), f.Dir(0.0F, 1.0F));
        return;
    }
    DoorLeaf(plan, f.At(4.1F, 0.08F), f.Dir(0.0F, 1.0F));
    HomeSpots spots;
    Flat(plan, f, *room, width, bedrooms, spots, {});
    RoomPlan area = AreaOf(*room, f, width, kUnitDepth);
    area.home = true;
    area.entry = {kCorridorMouth, vec3{f.At(4.6F, 0.0F).x, 0.0F, 0.0F}, f.At(4.6F, 0.7F)};
    spots.AppendTo(area.spots);
    plan.rooms.push_back(std::move(area));
}

// A penthouse takes the whole floor: two-bedroom flats' worth on the north
// side, a third bedroom and a bar on the south, and the corridor between
// them open as a hall.
void PenthouseFloor(FloorPlan& plan, const sim::Room* room) {
    Core(plan);
    const float width = kHalfWidth - kCoreEdge;
    FloorFinish(plan, {kCoreEdge, -kCorridorHalf}, {kHalfWidth, kCorridorHalf}, kWoodFloor);
    if (room == nullptr) {
        Exterior(plan, {kCoreEdge}, {});
        return;
    }
    const Frame north{.x0 = kCoreEdge, .mx = 1.0F, .z0 = kCorridorHalf, .sz = 1.0F};
    const Frame south{.x0 = kCoreEdge, .mx = 1.0F, .z0 = -kCorridorHalf, .sz = -1.0F};
    HomeSpots northSpots;
    HomeSpots southSpots;
    Flat(plan, north, *room, width, 2, northSpots, {north.At(4.6F, 0.7F)});
    Flat(plan, south, *room, width, 1, southSpots, {south.At(4.6F, 0.7F)});
    // The bar along the far end of the south side, and the hall.
    const vec3 fabric = Fabric(room->id.value + 11);
    Place(plan, PropKind::BottleShelf, south.At(width - 3.0F, kUnitDepth - 0.3F), south.Dir(0, -1));
    Place(plan, PropKind::BarCounter, south.At(width - 3.0F, kUnitDepth - 2.2F), south.Dir(0, -1));
    for (int stool = 0; stool < 3; ++stool) {
        Place(plan, PropKind::Stool,
              south.At(width - 4.0F + static_cast<float>(stool), kUnitDepth - 3.1F),
              south.Dir(0, 1), fabric);
    }
    for (const float x : {9.0F, width - 1.0F}) {
        Place(plan, PropKind::Plant, {kCoreEdge + x, 0.0F, 0.0F}, {0.0F, 1.0F});
    }
    for (int lamp = 0; lamp < 3; ++lamp) {
        HangLamp(plan, {kCoreEdge + (width * (static_cast<float>(lamp) + 0.5F) / 3.0F), 0.0F, 0.0F},
                 room->id);
    }
    RoomPlan area{.room = room->id,
                  .minimum = {kCoreEdge, 0.0F, -kHalfDepth},
                  .maximum = {kHalfWidth, kWallHeightMeters, kHalfDepth}};
    area.home = true;
    area.entry = {kCorridorMouth, vec3{kCoreEdge + 1.5F, 0.0F, 0.0F}};
    HomeSpots all;
    for (const HomeSpots* half : {&northSpots, &southSpots}) {
        all.beds.insert(all.beds.end(), half->beds.begin(), half->beds.end());
        all.seats.insert(all.seats.end(), half->seats.begin(), half->seats.end());
        all.extras.insert(all.extras.end(), half->extras.begin(), half->extras.end());
    }
    all.AppendTo(area.spots);
    plan.rooms.push_back(std::move(area));
    Exterior(plan, {kCoreEdge, kCoreEdge + 3.4F, kHalfWidth - 3.4F}, {});
}

// A floor of homes of the class of its first apartment; studios for a floor
// without any.
void HomeFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms) {
    const sim::ApartmentClass kind =
        rooms.empty() ? sim::ApartmentClass::Studio : rooms.front()->apartmentClass;
    switch (kind) {
        case sim::ApartmentClass::Dorm:
            UnitsFloor(plan, rooms, {.width = 3.5F, .doorFrom = 1.3F, .doorTo = 2.2F,
                                     .wallsX = {}, .furnish = DormRoom});
            return;
        case sim::ApartmentClass::OneBedroom:
            UnitsFloor(plan, rooms,
                       {.width = 10.5F, .doorFrom = 4.1F, .doorTo = 5.1F, .wallsX = {3.4F},
                        .furnish = [](FloorPlan& p, const Frame& f, const sim::Room* r) {
                            FlatUnit(p, f, r, 10.5F, 1);
                        }});
            return;
        case sim::ApartmentClass::TwoBedroom:
            UnitsFloor(plan, rooms,
                       {.width = 21.0F, .doorFrom = 4.1F, .doorTo = 5.1F,
                        .wallsX = {3.4F, 17.6F},
                        .furnish = [](FloorPlan& p, const Frame& f, const sim::Room* r) {
                            FlatUnit(p, f, r, 21.0F, 2);
                        }});
            return;
        case sim::ApartmentClass::Penthouse:
            PenthouseFloor(plan, rooms.empty() ? nullptr : rooms.front());
            return;
        default:
            UnitsFloor(plan, rooms, {.width = kUnitWidth, .doorFrom = 4.1F, .doorTo = 5.1F,
                                     .wallsX = {3.4F}, .furnish = Apartment});
            return;
    }
}

// Stores fill the ground floor of a habitat, side by side behind glass.
void Store(FloorPlan& plan, const sim::Room& room, const Frame& f, float width) {
    constexpr float kDepth = kFootprintDepthMeters;
    RoomPlan area = AreaOf(room, f, width, kDepth);
    const vec3 fabric = Fabric(room.id.value);
    const float middle = width * 0.5F;
    area.entry = {f.At(middle, kDepth + 0.8F), f.At(middle, kDepth - 1.0F)};
    area.streetDoor = true;
    auto& spots = area.spots;
    switch (room.kind) {
        case sim::RoomKind::Bar:
        case sim::RoomKind::Diner: {
            const bool bar = room.kind == sim::RoomKind::Bar;
            Place(plan, bar ? PropKind::BottleShelf : PropKind::KitchenCounter,
                  f.At(middle, 0.6F), f.Dir(0, 1));
            Place(plan, PropKind::BarCounter, f.At(middle, 4.5F), f.Dir(0, 1));
            const int columns = std::max(1, static_cast<int>((width - 1.0F) / 3.2F));
            const float spacing = width / static_cast<float>(columns);
            // The gap beside a column of tables that robots walk down.
            const auto aisle = [&](int column) {
                if (columns == 1) {
                    return 0.7F;
                }
                const float x = spacing * (static_cast<float>(column) + 0.5F);
                return column < columns - 1 ? x + (spacing * 0.5F) : x - (spacing * 0.5F);
            };
            const float inside = kDepth - 2.0F;
            for (int stool = 0; stool < 4; ++stool) {
                const float x = middle - 1.2F + (0.8F * static_cast<float>(stool));
                Place(plan, PropKind::Stool, f.At(x, 5.6F), f.Dir(0, -1), fabric);
                spots.push_back(
                    Via(WithItem(MakeSpot(f, x, 5.6F, {0, -1}, Pose::Sitting, 0.31F), f,
                                 bar ? PropKind::Drink : PropKind::Meal, x, 4.95F, 1.1F),
                        {f.At(aisle(0), inside), f.At(aisle(0), 6.8F), f.At(x, 6.8F)}));
            }
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const float x = (width / static_cast<float>(columns)) *
                                    (static_cast<float>(column) + 0.5F);
                    const float u = 9.5F + (4.0F * static_cast<float>(row));
                    if (bar) {
                        Place(plan, PropKind::BarTable, f.At(x, u), f.Dir(0, 1));
                        for (const float side : {-1.0F, 1.0F}) {
                            const float spotX = x + (side * 0.6F);
                            spots.push_back(Via(
                                WithItem(MakeSpot(f, spotX, u, {-side, 0}, Pose::Standing), f,
                                         PropKind::Drink, x + (side * 0.18F), u + (side * 0.1F),
                                         1.04F),
                                {f.At(aisle(column), inside), f.At(aisle(column), u + 0.7F),
                                 f.At(spotX, u + 0.7F)}));
                        }
                    } else {
                        Place(plan, PropKind::Booth, f.At(x, u), f.Dir(0, 1), fabric);
                        for (const float side : {-1.0F, 1.0F}) {
                            const float spotU = u + (side * 0.75F);
                            spots.push_back(Via(
                                WithItem(MakeSpot(f, x, spotU, {0, -side}, Pose::Sitting), f,
                                         PropKind::Meal, x + (side * 0.2F), u + (side * 0.18F),
                                         0.77F),
                                {f.At(aisle(column), inside), f.At(aisle(column), spotU)}));
                        }
                    }
                }
            }
            Place(plan, PropKind::Plant, f.At(0.5F, kDepth - 0.6F), f.Dir(0, -1));
            break;
        }
        case sim::RoomKind::ChargingBay: {
            const int perRow = std::max(1, static_cast<int>((width - 0.4F) / 1.2F));
            for (int row = 0; row < 4; ++row) {
                for (int pod = 0; pod < perRow; ++pod) {
                    const float x = 0.8F + (1.2F * static_cast<float>(pod));
                    const float u = 0.6F + (4.0F * static_cast<float>(row));
                    Place(plan, PropKind::ChargingPod, f.At(x, u), f.Dir(0, 1));
                    spots.push_back(MakeSpot(f, x, u + 0.05F, {0, 1}, Pose::Standing, 0.1F));
                }
            }
            break;
        }
        case sim::RoomKind::RepairShop: {
            const int lifts = width >= 10.0F ? 2 : 1;
            for (int lift = 0; lift < lifts; ++lift) {
                const float x = width * (static_cast<float>(lift) + 1.0F) /
                                (static_cast<float>(lifts) + 1.0F);
                Place(plan, PropKind::CarLift, f.At(x, 8.0F), f.Dir(0, 1));
                spots.push_back(MakeSpot(f, x, 8.0F, {0, 1}, Pose::Standing));
            }
            const int benches = std::max(1, static_cast<int>(width / 2.4F));
            for (int bench = 0; bench < benches; ++bench) {
                const float x = 1.2F + (2.4F * static_cast<float>(bench));
                Place(plan, PropKind::Workbench, f.At(x, 0.6F), f.Dir(0, 1));
                Place(plan, PropKind::Stool, f.At(x, 1.4F), f.Dir(0, -1), fabric);
                spots.push_back(MakeSpot(f, x, 1.4F, {0, -1}, Pose::Sitting, 0.31F));
            }
            for (int shelf = 0; shelf < 3; ++shelf) {
                Place(plan, PropKind::PartsShelf,
                      f.At(0.3F, 12.0F + (2.4F * static_cast<float>(shelf))),
                      f.Dir(1, 0));
            }
            break;
        }
        case sim::RoomKind::Arcade: {
            const int perRow = std::max(1, static_cast<int>((width - 1.0F) / 1.0F));
            for (int row = 0; row < 4; ++row) {
                for (int cabinet = 0; cabinet < perRow; ++cabinet) {
                    const float x = 1.0F + static_cast<float>(cabinet);
                    const float u = 1.0F + (4.5F * static_cast<float>(row));
                    Place(plan, PropKind::ArcadeCabinet, f.At(x, u), f.Dir(0, 1),
                          Fabric(room.id.value + static_cast<std::uint64_t>(cabinet)));
                    spots.push_back(MakeSpot(f, x, u + 1.0F, {0, -1}, Pose::Standing));
                }
            }
            break;
        }
        case sim::RoomKind::Shop: {
            const int perRow = std::max(1, static_cast<int>((width - 1.0F) / 2.6F));
            for (int row = 0; row < 3; ++row) {
                for (int shelf = 0; shelf < perRow; ++shelf) {
                    const float x = 1.5F + (2.6F * static_cast<float>(shelf));
                    const float u = 3.0F + (4.0F * static_cast<float>(row));
                    Place(plan, PropKind::GoodsShelf, f.At(x, u), f.Dir(0, 1));
                    const Spot browsing = MakeSpot(f, x, u + 1.2F, {0, -1}, Pose::Standing);
                    spots.push_back(
                        WithItem(browsing, f, PropKind::Basket, x, u + 1.2F, 0.0F));
                }
            }
            Place(plan, PropKind::Checkout, f.At(width - 1.4F, kDepth - 3.0F),
                  f.Dir(-1, 0));
            break;
        }
        case sim::RoomKind::Clinic: {
            // Repair cradles in two rows, patients lying on them, and a
            // reception desk by the door.
            const int perRow = std::max(1, static_cast<int>((width - 1.0F) / 2.4F));
            for (int row = 0; row < 2; ++row) {
                for (int cradle = 0; cradle < perRow; ++cradle) {
                    const float x = 1.4F + (2.4F * static_cast<float>(cradle));
                    const float u = 3.0F + (6.0F * static_cast<float>(row));
                    Place(plan, PropKind::RepairCradle, f.At(x, u), f.Dir(0, 1));
                    spots.push_back(MakeSpot(f, x, u + 0.6F, {0, -1}, Pose::Lying, 0.95F));
                }
            }
            Place(plan, PropKind::Desk, f.At(middle, kDepth - 3.0F), f.Dir(0, 1));
            Place(plan, PropKind::Plant, f.At(0.5F, kDepth - 0.6F), f.Dir(0, -1));
            break;
        }
        case sim::RoomKind::BlackMarket: {
            // Stalls down both walls with crates between them.
            const int stalls = std::max(1, static_cast<int>((kDepth - 6.0F) / 3.2F));
            for (int stall = 0; stall < stalls; ++stall) {
                const float u = 1.5F + (3.2F * static_cast<float>(stall));
                for (const float side : {0.0F, 1.0F}) {
                    const float x = side == 0.0F ? 0.8F : width - 0.8F;
                    const vec2 facing = side == 0.0F ? vec2{1, 0} : vec2{-1, 0};
                    Place(plan, PropKind::MarketStall, f.At(x, u), f.Dir(facing.x, facing.y));
                    spots.push_back(MakeSpot(f, x + (facing.x * 1.1F), u,
                                             {-facing.x, 0}, Pose::Standing));
                }
                Place(plan, PropKind::Crate, f.At(middle, u + 1.6F), f.Dir(0, 1));
            }
            break;
        }
        case sim::RoomKind::VacantStore:
            // Bare floor, a few crates left behind, nobody inside.
            for (int crate = 0; crate < 3; ++crate) {
                Place(plan, PropKind::Crate,
                      f.At(1.2F + (1.4F * static_cast<float>(crate)), 1.0F + static_cast<float>(crate % 2)),
                      f.Dir(0, 1));
            }
            LampGrid(plan, f, width, kDepth, room.id, 6.0F);
            plan.rooms.push_back(std::move(area));
            return;
        default:
            break;
    }
    FillStanding(area, f, 1.0F, width - 1.0F, kDepth - 6.0F, kDepth - 1.0F,
                 room.capacity);
    // Staff on shift, where the work is done.
    const std::uint32_t staff = std::min<std::uint32_t>(sim::StaffPerShift(room), 3);
    const auto worker = [&](float x, float u, vec2 facing, std::vector<vec3> approach = {}) {
        area.spots.push_back(Via(MakeSpot(f, x, u, facing, Pose::Standing), std::move(approach)));
        ++area.staffSpots;
    };
    for (std::uint32_t index = 0; index < staff; ++index) {
        const auto i = static_cast<float>(index);
        switch (room.kind) {
            case sim::RoomKind::Bar:
            case sim::RoomKind::Diner:
                // Behind the counter, round its end by the left wall.
                worker(middle - 1.0F + i, 2.6F, {0, 1},
                       {f.At(0.7F, kDepth - 2.0F), f.At(0.7F, 2.6F)});
                break;
            case sim::RoomKind::Shop:
                worker(width - 0.5F, kDepth - 3.0F - i, {-1, 0});
                break;
            case sim::RoomKind::Clinic:
                worker(middle - 0.8F + (0.8F * i), kDepth - 3.7F, {0, 1});
                break;
            case sim::RoomKind::RepairShop:
                worker(1.2F + (2.4F * i), 2.4F, {0, -1});
                break;
            case sim::RoomKind::BlackMarket:
                worker(0.3F, 1.5F + (3.2F * i), {1, 0});
                break;
            default:
                // An attendant by the door.
                worker(width - 0.8F, kDepth - 4.0F - i, {-1, 0});
                break;
        }
    }
    LampGrid(plan, f, width, kDepth, room.id, 6.0F);
    plan.rooms.push_back(std::move(area));
}

void StoreFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms) {
    Core(plan);
    const float span = kHalfWidth - kCoreEdge;
    const auto count = static_cast<float>(std::max<std::size_t>(rooms.size(), 1));
    const float width = span / count;
    std::vector<float> cuts{kCoreEdge};
    std::vector<Opening> doors{DoorAt(-10.5F, 2.0F)};  // Lobby entrance.
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        const float left = kCoreEdge + (width * static_cast<float>(index));
        const Frame frame{.x0 = left, .mx = 1.0F, .z0 = -kHalfDepth, .sz = 1.0F};
        FloorFinish(plan, {left, -kHalfDepth}, {left + width, kHalfDepth}, kStoreFloor);
        if (index > 0) {
            Wall(plan, {left, -kHalfDepth}, {left, kHalfDepth}, kInteriorThickness, {},
                 kPlaster);
            cuts.push_back(left);
        }
        doors.push_back(DoorAt(left + (width * 0.5F), 1.8F));
        Store(plan, *rooms[index], frame, width);
    }
    Exterior(plan, cuts, doors);
}

// Workspaces of a workplace floor, fitted out for their trade.
void Workspace(FloorPlan& plan, const sim::Room& room, const Frame& f, float width) {
    constexpr float kDepth = kFootprintDepthMeters;
    RoomPlan area = AreaOf(room, f, width, kDepth);
    area.entry = {kCorridorMouth, f.At(0.8F, kDepth * 0.5F)};
    auto& spots = area.spots;
    const std::string_view name = room.name;
    const auto starts = [&](std::string_view prefix) {
        return name.starts_with(prefix);
    };
    if (starts("Smelting")) {
        for (int furnace = 0; furnace < 3; ++furnace) {
            const float x = 1.8F + (3.4F * static_cast<float>(furnace));
            Place(plan, PropKind::Furnace, f.At(x, 1.5F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x, 3.4F, {0, -1}, Pose::Standing));
        }
        for (const float x : {width * 0.3F, width * 0.7F}) {
            Place(plan, PropKind::Conveyor, f.At(x, 8.0F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x - 1.0F, 7.1F, {0, 1}, Pose::Standing));
            spots.push_back(MakeSpot(f, x + 1.0F, 8.9F, {0, -1}, Pose::Standing));
        }
    } else if (starts("Assembly")) {
        for (int line = 0; line < 3; ++line) {
            const float u = 4.0F + (5.5F * static_cast<float>(line));
            Place(plan, PropKind::Conveyor, f.At(width * 0.5F, u), f.Dir(0, 1));
            Place(plan, PropKind::RobotArm, f.At(width * 0.5F + 2.6F, u), f.Dir(-1, 0));
            spots.push_back(MakeSpot(f, width * 0.5F - 1.0F, u - 0.9F, {0, 1}, Pose::Standing));
            spots.push_back(MakeSpot(f, width * 0.5F + 0.5F, u + 0.9F, {0, -1}, Pose::Standing));
        }
    } else if (starts("Service")) {
        for (const float x : {width * 0.3F, width * 0.7F}) {
            Place(plan, PropKind::CarLift, f.At(x, 7.0F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x + 1.5F, 7.0F, {-1, 0}, Pose::Standing));
        }
        for (int bench = 0; bench < 4; ++bench) {
            const float x = 1.2F + (2.4F * static_cast<float>(bench));
            Place(plan, PropKind::Workbench, f.At(x, 0.6F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x, 1.4F, {0, -1}, Pose::Standing));
        }
    } else if (starts("Kitchen")) {
        for (int counter = 0; counter < 3; ++counter) {
            const float x = 1.6F + (3.0F * static_cast<float>(counter));
            Place(plan, PropKind::KitchenCounter, f.At(x, 0.4F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x, 1.2F, {0, -1}, Pose::Standing));
            Place(plan, PropKind::PrepTable, f.At(x, 5.0F), f.Dir(0, 1));
            spots.push_back(MakeSpot(f, x, 5.8F, {0, -1}, Pose::Standing));
        }
        Place(plan, PropKind::Fridge, f.At(width - 0.4F, 3.0F), f.Dir(-1, 0));
        Place(plan, PropKind::Fridge, f.At(width - 0.4F, 4.0F), f.Dir(-1, 0));
    } else if (starts("Server")) {
        for (int row = 0; row < 4; ++row) {
            const float u = 2.0F + (4.5F * static_cast<float>(row));
            for (int rack = 0; rack < 12; ++rack) {
                Place(plan, PropKind::ServerRack,
                      f.At(1.2F + (0.65F * static_cast<float>(rack)), u), f.Dir(0, 1));
            }
            spots.push_back(MakeSpot(f, 2.5F, u + 1.4F, {0, -1}, Pose::Standing));
            spots.push_back(MakeSpot(f, 6.5F, u + 1.4F, {0, -1}, Pose::Standing));
        }
    } else {
        // Office floors and workspaces from early saves: rows of desks.
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                const float x = 1.4F + (2.3F * static_cast<float>(column));
                const float u = 2.0F + (3.2F * static_cast<float>(row));
                Place(plan, PropKind::Desk, f.At(x, u), f.Dir(0, 1));
                Place(plan, PropKind::Chair, f.At(x, u + 0.7F), f.Dir(0, -1),
                      Fabric(room.id.value));
                spots.push_back(WithItem(MakeSpot(f, x, u + 0.7F, {0, -1}, Pose::Sitting), f,
                                         PropKind::Mug, x + 0.5F, u + 0.15F, 0.78F));
            }
        }
        Place(plan, PropKind::Plant, f.At(0.5F, kDepth - 0.6F), f.Dir(0, -1));
    }
    FillStanding(area, f, 1.0F, width - 1.0F, kDepth - 6.0F, kDepth - 1.0F,
                 room.capacity);
    LampGrid(plan, f, width, kDepth, room.id, 6.0F);
    plan.rooms.push_back(std::move(area));
}

void WorkplaceFloor(FloorPlan& plan, const std::vector<const sim::Room*>& rooms,
                    bool ground) {
    Core(plan);
    const float span = kHalfWidth - kCoreEdge;
    const auto count = static_cast<float>(std::max<std::size_t>(rooms.size(), 1));
    const float width = span / count;
    std::vector<float> cuts{kCoreEdge};
    std::vector<Opening> doors;
    if (ground) {
        doors.push_back(DoorAt(-10.5F, 2.0F));
    }
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        const float left = kCoreEdge + (width * static_cast<float>(index));
        const Frame frame{.x0 = left, .mx = 1.0F, .z0 = -kHalfDepth, .sz = 1.0F};
        FloorFinish(plan, {left, -kHalfDepth}, {left + width, kHalfDepth},
                    kIndustrialFloor);
        if (index > 0) {
            Wall(plan, {left, -kHalfDepth}, {left, kHalfDepth}, kInteriorThickness,
                 {DoorAt(0.0F, 1.6F)}, kPlaster);
            cuts.push_back(left);
        }
        if (rooms[index]->kind == sim::RoomKind::Workspace) {
            Workspace(plan, *rooms[index], frame, width);
        } else {
            // A venue opened in a workplace, or a unit standing empty: a
            // store with its own door to the street.
            Store(plan, *rooms[index], frame, width);
            doors.push_back(DoorAt(left + (width * 0.5F), 1.8F));
        }
    }
    Exterior(plan, cuts, doors);
}
}  // namespace

std::vector<NavPoint> WayOut(const RoomPlan& room, std::size_t spot,
                             std::uint32_t floor) {
    std::vector<NavPoint> way;
    const auto add = [&](vec3 position, std::uint32_t level, bool outside = false) {
        way.push_back({.position = position, .floor = level, .outside = outside});
    };
    if (spot < room.spots.size()) {
        add(room.spots[spot].position, floor);
        const std::vector<vec3>& approach = room.spots[spot].approach;
        for (auto point = approach.rbegin(); point != approach.rend(); ++point) {
            add(*point, floor);
        }
    }
    for (std::size_t index = room.entry.size(); index-- > 0;) {
        add(room.entry[index], floor, room.streetDoor && index == 0);
    }
    if (room.streetDoor) {
        // Along the shop fronts to the lobby door.
        add(kLobbyOutside, 0, true);
        return way;
    }
    // Through the core and down one flight a floor: off each flight at its
    // foot, round it to the landing, and down the next.
    add(kCoreHall, floor);
    if (floor == 0) {
        add(kBesideFoot, 0);
    } else {
        add(kStairsLanding, floor);
        for (std::uint32_t level = floor; level-- > 0;) {
            add(kStairsFoot, level);
            if (level > 0) {
                add(kBesideFoot, level);
                add(kBesideLanding, level);
                add(kStairsLanding, level);
            }
        }
    }
    add(kLobby, 0);
    add(kLobbyOutside, 0, true);
    return way;
}

FloorPlan PlanFloor(const sim::Building& building, std::uint32_t floor) {
    std::vector<const sim::Room*> rooms;
    for (const sim::Room& room : building.rooms) {
        if (room.floor == floor) {
            rooms.push_back(&room);
        }
    }
    FloorPlan plan;
    if (building.kind == sim::BuildingKind::Workplace) {
        WorkplaceFloor(plan, rooms, floor == 0);
    } else if (floor == 0) {
        StoreFloor(plan, rooms);
    } else {
        HomeFloor(plan, rooms);
    }
    return plan;
}

}  // namespace mechapunk
