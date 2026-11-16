#pragma once

#include <cstdint>
#include <vector>

#include <glm/ext/vector_float3.hpp>

namespace mechapunk {

// One box of a prop, in meters. The box's base-center sits at `pivot + base`
// before rotating by `angleX` degrees around the X axis through `pivot`, so
// limbs can swing about a joint. Colors are linear RGB in [0, 1].
struct PropPart {
    glm::vec3 pivot{0.0F};
    glm::vec3 base{0.0F};
    glm::vec3 size{1.0F};
    float angleX{};
    glm::vec3 color{1.0F};
    // Takes the color the prop is placed with instead of `color`.
    bool tinted{false};
    // Drawn see-through, like a window pane.
    bool glass{false};
};

// A piece of furniture or equipment made of boxes, standing on y = 0 with
// its front facing +z. A catalog entry could later point to a model file
// instead; everything that places props only uses the kind.
using PropModel = std::vector<PropPart>;

enum class PropKind : std::uint8_t {
    // Homes.
    Bed,
    Nightstand,
    Wardrobe,
    Sofa,
    CoffeeTable,
    WallScreen,
    KitchenCounter,
    Fridge,
    DiningTable,
    Chair,
    Toilet,
    Sink,
    Shower,
    Rug,
    Plant,
    // Stores.
    BarCounter,
    BottleShelf,
    Stool,
    BarTable,
    Booth,
    ChargingPod,
    Workbench,
    CarLift,
    PartsShelf,
    ArcadeCabinet,
    GoodsShelf,
    Checkout,
    // Workplaces.
    Furnace,
    Conveyor,
    RobotArm,
    Desk,
    ServerRack,
    PrepTable,
    // Building.
    Stairs,
    ElevatorDoors,
    Door,
    // Lighting: a pendant hanging from the ceiling, and a street lamp whose
    // head reaches 1.6 m out over +z at 6 m.
    CeilingLamp,
    StreetLamp,
    // Clinic: a table a robot lies on under a repair arm.
    RepairCradle,
    // Black market: a trestle stall with goods, and a crate of parts.
    MarketStall,
    Crate,
    // Stairs down to the metro under a glass canopy, with an M sign; the
    // stairs run along z.
    MetroEntrance,
};

[[nodiscard]] const PropModel& Prop(PropKind kind);

enum class Pose : std::uint8_t { Standing, Walking, Sitting, Lying };

// A humanoid robot about 1.7 m tall, facing +z with its feet on y = 0 (or,
// lying, on its back along -z). `phase` animates walking, in radians.
[[nodiscard]] PropModel RobotFigure(Pose pose, float phase, glm::vec3 accent);

}  // namespace mechapunk
