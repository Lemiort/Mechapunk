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

constexpr vec3 kCoreHall{-9.2F, 0.0F, 0.0F};

constexpr vec3 kStairsFoot{-10.5F, 0.0F, 8.6F};

constexpr vec3 kBesideFoot{-9.2F, 0.0F, 8.6F};

constexpr vec3 kStairsLanding{-10.5F, 0.0F, 3.0F};

constexpr vec3 kBesideLanding{-9.2F, 0.0F, 3.0F};

constexpr vec3 kLobby{-10.5F, 0.0F, kHalfDepth - 0.8F};

constexpr vec3 kLobbyOutside{-10.5F, 0.0F, kHalfDepth + 0.8F};

}  // namespace

// namespace

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
