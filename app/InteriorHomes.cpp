// Floor plans of homes: dorm rooms, studios, flats of every size, shared
// flats and whole-floor penthouses.

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

#include "InteriorKit.hpp"

namespace mechapunk {
namespace {

// Floor finish thickness.
// Apartments: three per side of the corridor, 7 m wide and 9.8 m deep.
constexpr float kUnitWidth = 7.0F;

constexpr float kUnitDepth = kHalfDepth - kCorridorHalf;

constexpr vec3 kWoodFloor{0.5F, 0.36F, 0.24F};

constexpr vec3 kTileFloor{0.7F, 0.74F, 0.78F};

constexpr vec3 kDoorWood{0.42F, 0.28F, 0.17F};

// An open door leaf hinged at `hinge`, lying along `along`.
void DoorLeaf(FloorPlan& plan, vec3 hinge, vec2 along) {
    plan.props.push_back({.kind = PropKind::Door,
                          .position = hinge,
                          .yawDegrees = glm::degrees(std::atan2(-along.y, along.x)),
                          .tint = kDoorWood});
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

}  // namespace

namespace interior_detail {

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

}  // namespace interior_detail

}  // namespace mechapunk
