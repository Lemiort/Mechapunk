// Workplace floors: workspaces fitted out for their trade, and stores opened
// in them.

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

constexpr vec3 kIndustrialFloor{0.36F, 0.36F, 0.34F};

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

}  // namespace

namespace interior_detail {

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

}  // namespace interior_detail

}  // namespace mechapunk
