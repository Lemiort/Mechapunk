// Ground-floor stores: every venue, empty units, police stations and metro
// crew rooms, with spots for guests and staff.

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

constexpr vec3 kStoreFloor{0.22F, 0.22F, 0.27F};

}  // namespace

namespace interior_detail {

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
        case sim::RoomKind::PoliceStation: {
            // A front desk by the street door, two desks for officers on
            // shift, and a holding cell of glass bars in the back corner.
            Place(plan, PropKind::Desk, f.At(middle, kDepth - 3.0F), f.Dir(0, 1));
            const float deskU = 7.0F;
            for (int officer = 0; officer < 2; ++officer) {
                const float x = width * (static_cast<float>(officer) + 1.0F) / 3.0F;
                Place(plan, PropKind::Desk, f.At(x, deskU), f.Dir(0, 1));
                Place(plan, PropKind::Chair, f.At(x, deskU + 0.7F), f.Dir(0, -1), fabric);
            }
            const float barTop = 2.2F;
            const float halfBar = kGlassThickness * 0.5F;
            const auto bar = [&](float ax, float au, float bx, float bu) {
                const vec3 a = f.At(ax, au);
                const vec3 b = f.At(bx, bu);
                plan.boxes.push_back(
                    {.minimum = {std::min(a.x, b.x), 0.0F, std::min(a.z, b.z)},
                     .maximum = {std::max(a.x, b.x), barTop, std::max(a.z, b.z)},
                     .color = vec3{0.6F, 0.8F, 0.92F},
                     .glass = true});
            };
            const float cx0 = 0.8F, cx1 = 3.4F, cu0 = 0.5F, cu1 = 3.7F;
            bar(cx0 - halfBar, cu0, cx0 + halfBar, cu1);
            bar(cx1 - halfBar, cu0, cx1 + halfBar, cu1);
            bar(cx0, cu0 - halfBar, cx1, cu0 + halfBar);
            // The front bar, with a door gap in the middle.
            bar(cx0, cu1 - halfBar, cx0 + 0.85F, cu1 + halfBar);
            bar(cx1 - 0.85F, cu1 - halfBar, cx1, cu1 + halfBar);
            break;
        }
        case sim::RoomKind::CrewRoom: {
            // A briefing corner: a notice board on the back wall and two
            // seats facing it.
            Place(plan, PropKind::WallScreen, f.At(middle, 0.4F), f.Dir(0, 1));
            for (int seat = 0; seat < 2; ++seat) {
                const float x = middle + ((static_cast<float>(seat) - 0.5F) * 1.6F);
                Place(plan, PropKind::Chair, f.At(x, 3.0F), f.Dir(0, -1), fabric);
            }
            break;
        }
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
    // Staff who sit at their work: officers at their desks, drivers in the
    // briefing seats.
    const auto seated = [&](float x, float u, vec2 facing) {
        area.spots.push_back(MakeSpot(f, x, u, facing, Pose::Sitting, 0.5F));
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
            case sim::RoomKind::PoliceStation:
                // Two desks, then behind the front desk.
                if (index < 2) {
                    seated(width * (i + 1.0F) / 3.0F, 7.7F, {0, -1});
                } else {
                    worker(middle, kDepth - 3.8F, {0, 1});
                }
                break;
            case sim::RoomKind::CrewRoom:
                // The two briefing seats, then standing by the board.
                if (index < 2) {
                    seated(middle + ((i - 0.5F) * 1.6F), 3.0F, {0, -1});
                } else {
                    worker(middle + 1.8F, 1.6F, {-1, 0});
                }
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

}  // namespace interior_detail

}  // namespace mechapunk
