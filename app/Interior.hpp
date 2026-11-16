#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "Props.hpp"
#include "mechapunk/sim/City.hpp"

namespace mechapunk {

// Real-world dimensions of every building, in meters.
inline constexpr float kFloorHeightMeters = 3.2F;
inline constexpr float kWallHeightMeters = 2.8F;
inline constexpr float kFootprintWidthMeters = 26.0F;  // Along x.
inline constexpr float kFootprintDepthMeters = 22.0F;  // Along z.

// An axis-aligned box of wall, window glass or floor finish.
struct WallBox {
    glm::vec3 minimum{0.0F};
    glm::vec3 maximum{0.0F};
    glm::vec3 color{1.0F};
    bool glass{false};
};

struct PlacedProp {
    PropKind kind{};
    glm::vec3 position{0.0F};
    float yawDegrees{};  // Rotation of the prop's +z front around +y.
    glm::vec3 tint{1.0F};
};

// Where a robot in a room stands, sits or lies.
struct Spot {
    glm::vec3 position{0.0F};
    float yawDegrees{};
    Pose pose{Pose::Standing};
    // Shown while the spot is taken: a drink on the table, a basket in hand.
    std::optional<PlacedProp> item;
};

// A ceiling lamp. Lamps of a sim room light up while the room is in use;
// lamps of corridors and the core (no room) are always on.
struct Lamp {
    glm::vec3 position{0.0F};  // The bulb, just under the shade.
    sim::EntityId room;
};

struct RoomPlan {
    sim::EntityId room;
    glm::vec3 minimum{0.0F};
    glm::vec3 maximum{0.0F};
    // Places for occupants, in the order they are used. In an apartment the
    // first is the bed and the second the sofa.
    std::vector<Spot> spots;
    bool home{false};
};

// The interior of one floor: walls with doors and windows, furniture, and the
// sim rooms laid out on it. Coordinates are meters from the building's center
// with y = 0 on the floor. The same building and floor always give the same
// plan, so nothing about it needs saving.
struct FloorPlan {
    std::vector<WallBox> boxes;
    std::vector<PlacedProp> props;
    std::vector<RoomPlan> rooms;
    std::vector<Lamp> lamps;
};

[[nodiscard]] FloorPlan PlanFloor(const sim::Building& building,
                                  std::uint32_t floor);

}  // namespace mechapunk
