// Drawing the open floor of a selected building: its floor plan, props, unit
// signs and the robots inside.

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

constexpr float kGlassOpacity = 0.3F;

constexpr float kHighlightOpacity = 0.3F;

// Game minutes a robot takes to walk between its sofa and its bed.
constexpr double kHomeWalkMinutes = 0.5;

constexpr vec3 kToLetBoard{0.55F, 0.55F, 0.5F};

constexpr vec3 kWarmLamp{1.0F, 0.82F, 0.6F};

constexpr vec3 kCoolLamp{0.85F, 0.92F, 1.0F};

}  // namespace

vec3 CityView::ToWorld(const BuildingShape& shape, std::uint32_t floor,
                       vec3 meters) const {
    const vec3 local = shape.turned ? vec3{-meters.x, meters.y, -meters.z} : meters;
    return shape.center +
           vec3{0.0F,
                (static_cast<float>(floor) * kFloorHeight) + (kSlabMeters * kS),
                0.0F} +
           (local * kS);
}

const FloorPlan& CityView::PlanOf(sim::EntityId building, std::uint32_t floor) {
    const auto key = std::make_pair(building.value, floor);
    auto found = plans_.find(key);
    if (found == plans_.end()) {
        const sim::Building* source = city_.FindBuilding(building);
        found = plans_.emplace(key, source != nullptr ? PlanFloor(*source, floor)
                                                      : FloorPlan{})
                    .first;
    }
    return found->second;
}

void CityView::ForEachPart(
    const PropModel& model, vec3 origin, float yawDegrees, float tiltDegrees,
    float scale,
    const std::function<void(const PropPart&, const Transform&)>& part) {
    const glm::mat4 base =
        glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0F), origin),
                                glm::radians(yawDegrees), vec3{0.0F, 1.0F, 0.0F}),
                    glm::radians(tiltDegrees), vec3{1.0F, 0.0F, 0.0F});
    for (const PropPart& piece : model) {
        const glm::mat4 joint =
            glm::rotate(glm::translate(base, piece.pivot * scale),
                        glm::radians(piece.angleX), vec3{1.0F, 0.0F, 0.0F});
        part(piece,
             Transform{.position = vec3(joint * glm::vec4(piece.base * scale, 1.0F)),
                       .rotationDegrees = {tiltDegrees + piece.angleX, yawDegrees,
                                           0.0F},
                       .scale = piece.size * scale});
    }
}

void CityView::SubmitModel(RenderQueue& queue, const PropModel& model,
                           vec3 origin, float yawDegrees, float tiltDegrees,
                           vec3 tint, float scale) {
    ForEachPart(model, origin, yawDegrees, tiltDegrees, scale,
                [&](const PropPart& part, const Transform& transform) {
                    const auto material = PartMaterial(part, tint);
                    if (part.glass) {
                        queue.SubmitTransparentMesh(*assets_.cube, transform,
                                                    material, kGlassOpacity);
                    } else {
                        queue.SubmitOpaqueMesh(*assets_.cube, transform,
                                               material);
                    }
                });
}

void CityView::SubmitBox(RenderQueue& queue, vec3 minimum, vec3 maximum,
                         vec3 color, bool glass) {
    const Transform transform{
        .position = {(minimum.x + maximum.x) * 0.5F, minimum.y,
                     (minimum.z + maximum.z) * 0.5F},
        .rotationDegrees = vec3(0.0F),
        .scale = maximum - minimum};
    const auto material = assets_.palette->Get(color);
    if (glass) {
        queue.SubmitTransparentMesh(*assets_.cube, transform, material,
                                    kGlassOpacity);
    } else {
        queue.SubmitOpaqueMesh(*assets_.cube, transform, material);
    }
}

void CityView::SubmitUnitSigns(RenderQueue& queue) const {
    for (const UnitSign& sign : unitSigns_) {
        const auto room = rooms_.Find(sign.room);
        if (!room) {
            continue;
        }
        const bool venue = KindHasNeonSign(room->room->kind);
        if (!venue && room->room->kind != sim::RoomKind::VacantStore) {
            continue;
        }
        // Venues glow in their colour; an empty unit has a dull "to let"
        // board.
        const vec3 color = venue ? StoreNeon(room->room->kind) : kToLetBoard;
        const Transform transform{.position = sign.panel,
                                  .rotationDegrees = vec3(0.0F),
                                  .scale = {sign.width, 0.45F * kS, 0.15F * kS}};
        queue.SubmitOpaqueMesh(*assets_.cube, transform,
                               venue ? assets_.palette->GetGlowing(color, color * kGlow)
                                     : assets_.palette->Get(color));
    }
}

void CityView::SubmitInterior(RenderQueue& queue, const WorldFrame& frame,
                              double renderTick, const Selection& selection) {
    const BuildingShape& shape = shapes_.at(selection.building.value);
    const std::uint32_t current = std::min(selection.floor, shape.floors - 1);
    const float halfWidth = kFootprintWidthMeters * 0.5F;
    const float halfDepth = kFootprintDepthMeters * 0.5F;
    const float yawOffset = shape.turned ? 180.0F : 0.0F;

    // Floors below are solid; the one being looked at stands on its slab.
    if (current > 0) {
        SubmitBox(queue,
                  shape.center - vec3{kBuildingWidth * 0.5F, 0.0F, kBuildingDepth * 0.5F},
                  shape.center + vec3{kBuildingWidth * 0.5F,
                                      static_cast<float>(current) * kFloorHeight,
                                      kBuildingDepth * 0.5F},
                  kConcrete, false);
    }
    const auto worldBox = [&](vec3 minimum, vec3 maximum) {
        const vec3 a = ToWorld(shape, current, minimum);
        const vec3 b = ToWorld(shape, current, maximum);
        return std::make_pair(glm::min(a, b), glm::max(a, b));
    };
    {
        const auto [minimum, maximum] =
            worldBox({-halfWidth, -kSlabMeters, -halfDepth}, {halfWidth, 0.0F, halfDepth});
        SubmitBox(queue, minimum, maximum, kConcrete, false);
    }

    const FloorPlan& plan = PlanOf(selection.building, current);
    for (const WallBox& box : plan.boxes) {
        const auto [minimum, maximum] = worldBox(box.minimum, box.maximum);
        SubmitBox(queue, minimum, maximum, box.color, box.glass);
    }
    for (const PlacedProp& prop : plan.props) {
        SubmitModel(queue, Prop(prop.kind), ToWorld(shape, current, prop.position),
                    prop.yawDegrees + yawOffset, 0.0F, prop.tint, kS);
    }

    // Robots inside, in id order per room.
    std::map<std::uint64_t, std::vector<sim::EntityId>> occupants;
    for (const RobotFrame& robot : frame.robots) {
        // Drivers out on the line are not in their crew room.
        if (robot.activity != sim::RobotActivity::Walking && !DriverAway(robot, renderTick)) {
            occupants[robot.location.value].push_back(robot.id);
        }
    }
    const double minute = std::fmod(renderTick, static_cast<double>(sim::kMinutesPerDay));
    const bool night = minute >= kBedtime || minute < kWakeUp;

    // Lamps are on where they would be: in corridors always, at home while
    // someone is up, in venues while open or busy, at work while working.
    for (const Lamp& lamp : plan.lamps) {
        if (litLamps_.size() >= kLampPool) {
            break;
        }
        bool on = true;
        vec3 color = kWarmLamp;
        if (const auto room = rooms_.Find(lamp.room)) {
            const bool occupied = occupants.contains(lamp.room.value);
            if (const sim::VenueRules* rules = sim::FindVenueRules(room->room->kind)) {
                on = occupied ||
                     sim::IsOpen(*rules, static_cast<std::uint32_t>(minute));
            } else if (room->room->kind == sim::RoomKind::Apartment) {
                on = occupied && !night;
            } else {
                on = occupied;
                color = kCoolLamp;
            }
        }
        if (on) {
            litLamps_.push_back(
                {.position = ToWorld(shape, current, lamp.position), .color = color});
        }
    }
    for (const RoomPlan& room : plan.rooms) {
        const auto [minimum, maximum] = worldBox(room.minimum, room.maximum);
        roomTiles_.push_back({.room = room.room, .box = {minimum, maximum}});
        if (room.room == selection.room) {
            const Transform overlay{
                .position = {(minimum.x + maximum.x) * 0.5F, minimum.y + (0.03F * kS),
                             (minimum.z + maximum.z) * 0.5F},
                .rotationDegrees = vec3(0.0F),
                .scale = {maximum.x - minimum.x, 0.02F * kS, maximum.z - minimum.z}};
            queue.SubmitTransparentMesh(*assets_.cube, overlay,
                                        assets_.palette->Get(kSelected),
                                        kHighlightOpacity);
        }
        const auto inside = occupants.find(room.room.value);
        if (inside == occupants.end() || room.spots.empty()) {
            continue;
        }
        // At bedtime robots at home walk from the sofa to the bed, and back
        // when they get up.
        const auto homeWalk = [&](sim::EntityId robot) -> std::optional<std::pair<vec3, vec3>> {
            const auto found = residentIndex_.find(robot.value);
            const std::size_t bed = 2 * (found != residentIndex_.end() ? found->second : 0);
            if (!room.home || room.spots.size() < bed + 2) {
                return std::nullopt;
            }
            double since = minute - kBedtime;
            std::size_t from = bed + 1;
            std::size_t to = bed;
            if (since < 0.0 || since >= kHomeWalkMinutes) {
                since = minute - kWakeUp;
                std::swap(from, to);
            }
            if (since < 0.0 || since >= kHomeWalkMinutes) {
                return std::nullopt;
            }
            // Back out of one spot the way the robot came in, and into the
            // other the same way.
            const Spot& first = room.spots[from];
            const Spot& second = room.spots[to];
            std::vector<vec3> points{first.position};
            points.insert(points.end(), first.approach.rbegin(), first.approach.rend());
            points.insert(points.end(), second.approach.begin(), second.approach.end());
            points.push_back(second.position);
            float length = 0.0F;
            for (std::size_t index = 1; index < points.size(); ++index) {
                length += glm::distance(points[index - 1], points[index]);
            }
            float remaining = static_cast<float>(since / kHomeWalkMinutes) * length;
            for (std::size_t index = 1; index < points.size(); ++index) {
                const float step = glm::distance(points[index - 1], points[index]);
                if (remaining <= step && step > 0.0F) {
                    const vec3 a = ToWorld(shape, current, points[index - 1]);
                    const vec3 b = ToWorld(shape, current, points[index]);
                    return std::pair{glm::mix(a, b, remaining / step), glm::normalize(b - a)};
                }
                remaining -= step;
            }
            return std::nullopt;
        };
        for (std::size_t index = 0; index < inside->second.size(); ++index) {
            const sim::EntityId robot = inside->second[index];
            if (const auto walking = homeWalk(robot)) {
                const bool selected = robot == selection.robot;
                const auto [origin, direction] = *walking;
                const float phase =
                    static_cast<float>(renderTick * 75.0 / 0.75) * std::numbers::pi_v<float>;
                SubmitModel(queue,
                            RobotFigure(Pose::Walking, phase,
                                        selected ? kSelected : Accent(robot)),
                            origin, glm::degrees(std::atan2(direction.x, direction.z)),
                            0.0F, vec3(1.0F), kS);
                robotMarkers_.push_back({.id = robot,
                                         .position = origin + vec3{0.0F, 0.9F * kS, 0.0F},
                                         .radius = 0.6F * kS});
                continue;
            }
            const RobotFrame* robotFrame = SimulationHost::FindRobot(frame, robot);
            const bool working = robotFrame != nullptr && robotFrame->workspace == room.room;
            const Spot& spot = room.spots.at(SpotFor(robot, room.room, room.home,
                                                     room.spots.size(), renderTick,
                                                     room.staffSpots, working));
            const bool selected = robot == selection.robot;
            const bool lying = spot.pose == Pose::Lying;
            const vec3 origin = ToWorld(shape, current, spot.position);
            SubmitModel(queue,
                        RobotFigure(lying ? Pose::Standing : spot.pose, 0.0F,
                                    selected ? kSelected : Accent(robot)),
                        origin, spot.yawDegrees + yawOffset, lying ? -90.0F : 0.0F,
                        vec3(1.0F), kS);
            if (spot.item) {
                SubmitModel(queue, Prop(spot.item->kind),
                            ToWorld(shape, current, spot.item->position),
                            spot.item->yawDegrees + yawOffset, 0.0F, spot.item->tint, kS);
            }
            robotMarkers_.push_back(
                {.id = robot,
                 .position = origin + vec3{0.0F, (lying ? 0.2F : 0.9F) * kS, 0.0F},
                 .radius = 0.6F * kS});
        }
    }
}

}  // namespace mechapunk
