#pragma once

// Constants and helpers shared by the source files that implement CityView.
// Private to the City*.cpp files that implement CityView: never include it
// from anywhere else.

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

namespace mechapunk {
namespace cityview_detail {

using glm::vec2;

using glm::vec3;

constexpr float kS = CityView::kUnitsPerMeter;

constexpr float kStreetWidth = 10.0F * kS;

constexpr float kStreetHeight = 0.08F * kS;

constexpr float kBuildingWidth = kFootprintWidthMeters * kS;

constexpr float kBuildingDepth = kFootprintDepthMeters * kS;

constexpr float kSlabMeters = 0.2F;

// Robots in apartments sleep in this part of the day, else sit on the sofa.
constexpr double kBedtime = (21.0 * 60.0) + 30.0;

constexpr double kWakeUp = 7.0 * 60.0;

constexpr float kSidingSpacing = 4.2F;

// Neon, bulbs and screens shine this many times their color: above 1, so
// they bloom, while lit windows stay below and don't.
constexpr float kGlow = 3.0F;

constexpr vec3 kConcrete{0.3F, 0.31F, 0.35F};

constexpr vec3 kSelected{1.0F, 1.0F, 1.0F};

constexpr std::array kRobotAccents{
    vec3{0.85F, 0.3F, 0.2F}, vec3{0.2F, 0.55F, 0.85F}, vec3{0.9F, 0.75F, 0.2F},
    vec3{0.3F, 0.7F, 0.4F},  vec3{0.7F, 0.35F, 0.8F},  vec3{0.25F, 0.75F, 0.75F},
    vec3{0.9F, 0.5F, 0.15F}, vec3{0.55F, 0.55F, 0.6F}};

inline vec3 StoreNeon(sim::RoomKind kind) {
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
        case sim::RoomKind::Clinic:
            return {0.6F, 1.0F, 0.9F};
        case sim::RoomKind::BlackMarket:
            return {0.9F, 0.08F, 0.08F};
        case sim::RoomKind::PoliceStation:
            return {0.3F, 0.6F, 1.0F};
        default:
            return {1.0F, 1.0F, 1.0F};
    }
}

// A ground-floor sign that glows in its colour: every venue, plus the police
// station (not a venue, but not "to let" either).
inline bool KindHasNeonSign(sim::RoomKind kind) {
    return sim::IsVenue(kind) || kind == sim::RoomKind::PoliceStation;
}

// Room lamps: warm at home and in venues, cool at work. Lamps beyond the
// pool stay dark.
constexpr std::size_t kLampPool = 32;

// Sun height in [-1, 1]: rises at 06:00, highest at 13:00, sets at 20:00.
inline float SunHeight(double minuteOfDay) {
    constexpr double kSunrise = 6.0 * 60.0;
    constexpr double kDaylight = 14.0 * 60.0;
    const double phase = (minuteOfDay - kSunrise) / kDaylight;
    return static_cast<float>(std::sin(phase * std::numbers::pi));
}

inline vec3 Accent(sim::EntityId robot) {
    return kRobotAccents.at(static_cast<std::size_t>(
        (robot.value * 2654435761ULL >> 9U) % kRobotAccents.size()));
}

// Across a line, toward its left when facing up it: the -1 side is on the
// right, so trains keep right. (Right of a way is cross(way, up).)
inline vec3 AcrossOf(sim::LineAxis axis) {
    return axis == sim::LineAxis::Row ? vec3{0.0F, 0.0F, -1.0F} : vec3{1.0F, 0.0F, 0.0F};
}

// The up track is on the -1 side of a line, the down track on the +1 side:
// each on the right of the trains running on it.
inline float SideOf(sim::Direction direction) {
    return direction == sim::Direction::Up ? -1.0F : 1.0F;
}

// The two cars' middles, ahead of and behind the train's middle.
constexpr std::array<float, 2> kCarOffsets{-6.8F, 6.8F};

constexpr float kCarFloorMeters = 0.45F;

// A door's inside and outside, across the car from its middle.
constexpr float kDoorInsideMeters = 1.1F;

constexpr float kDoorOutsideMeters = 2.0F;

inline vec3 RightOf(vec3 forward) {
    return glm::normalize(glm::cross(forward, vec3{0.0F, 1.0F, 0.0F}));
}

}  // namespace cityview_detail
using namespace cityview_detail;
}  // namespace mechapunk
