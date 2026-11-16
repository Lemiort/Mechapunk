#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "Interior.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;
using mechapunk::FloorPlan;
using mechapunk::NavPoint;
using mechapunk::RoomPlan;
using mechapunk::WallBox;

namespace {
// Chest height: above window sills, below door tops.
constexpr float kChest = 1.2F;

// Whether the segment from a to b on the floor crosses the footprint of
// `box`, shrunk by a centimeter so grazing a door frame is fine.
bool Crosses(glm::vec3 a, glm::vec3 b, const WallBox& box) {
    constexpr float kSlack = 0.01F;
    float enter = 0.0F;
    float exit = 1.0F;
    for (const int axis : {0, 2}) {
        const float low = box.minimum[axis] + kSlack;
        const float high = box.maximum[axis] - kSlack;
        const float delta = b[axis] - a[axis];
        if (std::abs(delta) < 1e-6F) {
            if (a[axis] <= low || a[axis] >= high) {
                return false;
            }
            continue;
        }
        float near = (low - a[axis]) / delta;
        float far = (high - a[axis]) / delta;
        if (near > far) {
            std::swap(near, far);
        }
        enter = std::max(enter, near);
        exit = std::min(exit, far);
        if (enter >= exit) {
            return false;
        }
    }
    return true;
}

bool IsWallAtChest(const WallBox& box) {
    return box.minimum.y <= kChest && box.maximum.y >= kChest;
}
}  // namespace

// From every spot of every room, and from every room's door, the way out
// passes walls only through doors and ends on the pavement of the ground
// floor, changing floor one flight at a time.
TEST(Layout, WaysOutPassThroughDoors) {
    const sim::World world = sim::World::Generate({.seed = 5, .robots = 300});
    std::size_t ways = 0;
    for (const sim::Building& building : world.State().city.buildings) {
        std::vector<FloorPlan> plans;
        for (std::uint32_t floor = 0; floor < building.floors; ++floor) {
            plans.push_back(mechapunk::PlanFloor(building, floor));
        }
        for (std::uint32_t floor = 0; floor < building.floors; ++floor) {
            for (const RoomPlan& room : plans[floor].rooms) {
                for (std::size_t spot = 0; spot <= room.spots.size(); ++spot) {
                    const std::vector<NavPoint> way = mechapunk::WayOut(room, spot, floor);
                    ++ways;
                    ASSERT_GE(way.size(), 2U);
                    if (room.home && spot == 0 && floor > 0) {
                        // Straight from the bed to the street would go
                        // through walls, which the check must notice.
                        EXPECT_TRUE(std::ranges::any_of(
                            plans[floor].boxes, [&](const WallBox& box) {
                                return IsWallAtChest(box) &&
                                       Crosses(way.front().position, way.back().position, box);
                            }));
                    }
                    EXPECT_EQ(way.front().floor, floor);
                    EXPECT_TRUE(way.back().outside);
                    EXPECT_EQ(way.back().floor, 0U);
                    for (std::size_t index = 1; index < way.size(); ++index) {
                        const NavPoint& a = way[index - 1];
                        const NavPoint& b = way[index];
                        if (a.floor != b.floor) {
                            EXPECT_EQ(a.floor, b.floor + 1) << building.name;
                            continue;  // On the stairs.
                        }
                        for (const WallBox& box : plans[a.floor].boxes) {
                            if (IsWallAtChest(box)) {
                                EXPECT_FALSE(Crosses(a.position, b.position, box))
                                    << building.name << " floor " << a.floor << " room "
                                    << room.room.value << " spot " << spot << " leg "
                                    << index << " (" << a.position.x << ", "
                                    << a.position.z << ") -> (" << b.position.x << ", "
                                    << b.position.z << ")";
                            }
                        }
                    }
                }
            }
        }
    }
    EXPECT_GT(ways, 1000U);
}
