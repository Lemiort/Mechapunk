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

// Distance from an intersection to the center of a building beside it:
// half a street, a pavement and half the building.
constexpr vec2 kQuadrantOffset{(kStreetWidth * 0.5F) + (1.6F * kS) +
                                   (kBuildingWidth * 0.5F),
                               (kStreetWidth * 0.5F) + (1.6F * kS) +
                                   (kBuildingDepth * 0.5F)};

constexpr float kWalkerPickRadius = 0.7F * kS;

// Separate random stream so decoration never affects the simulation.
constexpr std::uint64_t kDecorationStream = 100;

constexpr vec3 kHabitatNeon{0.1F, 0.9F, 1.0F};

constexpr vec3 kWorkplaceNeon{1.0F, 0.2F, 0.8F};

constexpr vec3 kGlassColor{0.6F, 0.8F, 0.92F};

// A building whose project is still raising floors: its ground floor holds
// the site office, a room kind that exists only while the project is up
// (task D, step D3d closes it into an ordinary unit).
bool IsSite(const sim::Building& building) {
    for (const sim::Room& room : building.rooms) {
        if (room.kind == sim::RoomKind::SiteOffice) {
            return true;
        }
    }
    return false;
}

// Quadrants around an intersection: (+x,+z), (-x,+z), (+x,-z), (-x,-z).
constexpr std::array<vec2, 4> kQuadrants{vec2{1.0F, 1.0F}, vec2{-1.0F, 1.0F},
                                         vec2{1.0F, -1.0F}, vec2{-1.0F, -1.0F}};

float Smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

}  // namespace

// namespace

CityView::CityView(const sim::City& city, std::uint64_t seed,
                   RenderAssets& assets)
    : city_(city), rooms_(city_), routes_(city_), metro_(city_), assets_(assets) {
    vec3 minimum{0.0F};
    vec3 maximum{0.0F};
    for (const sim::CityNode& node : city_.nodes) {
        const vec3 position = NodePosition(node.id);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    center_ = (minimum + maximum) * 0.5F;
    extent_ = std::max(maximum.x - minimum.x, maximum.z - minimum.z);

    const float groundSize = extent_ + 8.0F;
    statics_.push_back({.mesh = assets_.plane.get(),
                        .transform = {.position = center_,
                                      .rotationDegrees = vec3(0.0F),
                                      .scale = {groundSize, 1.0F, groundSize}},
                        .material = assets_.ground});

    for (const sim::CityEdge& edge : city_.edges) {
        const vec3 from = NodePosition(edge.from);
        const vec3 to = NodePosition(edge.to);
        const vec3 span = glm::abs(to - from);
        statics_.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = (from + to) * 0.5F,
                           .rotationDegrees = vec3(0.0F),
                           .scale = {std::max(span.x, kStreetWidth),
                                     kStreetHeight,
                                     std::max(span.z, kStreetWidth)}},
             .material = assets_.street});
    }

    AddStreetLamps();
    AddElevatedLines();
    AddStations();

    // Simulated buildings take the corners next to their entrance first.
    // Which corners are left free is saved state (`City::lots`), so the
    // placement below only needs to know how far along a node's buildings it
    // has come; the parks are drawn from the plots themselves.
    std::map<std::uint32_t, std::uint32_t> usedSlots;
    for (const sim::Building& building : city_.buildings) {
        const std::uint32_t slot = usedSlots[building.entrance.value]++;
        const vec2 quadrant = kQuadrants.at(slot % kQuadrants.size());
        // A fifth building at one corner moves to the far side of the block.
        const float ring = slot < kQuadrants.size() ? 1.0F : 3.0F;
        const vec3 centerOnGround =
            NodePosition(building.entrance) +
            vec3{quadrant.x * kQuadrantOffset.x * ring, 0.0F,
                 quadrant.y * kQuadrantOffset.y * ring};
        const bool habitat = building.kind == sim::BuildingKind::Habitat;
        const float height = static_cast<float>(building.floors) * kFloorHeight;
        BuildingShape& shape = shapes_[building.id.value];
        shape = {.center = centerOnGround,
                 .height = height,
                 .floors = building.floors,
                 // The street is to the north (-z) when the building sits
                 // south of its intersection: turn its plans to face it.
                 .turned = quadrant.y > 0.0F};
        buildingBlocks_.emplace(building.id.value, MassingBlocks(building, centerOnGround));
        // The sign hangs on the wall that reaches the street, not in the air
        // beside a setback or between twin towers.
        const float signY = SignHeightMeters(building.style, building.floors) * kS;

        // A sign high on the street side, and one per store at street level
        // above its door.
        const float front = shape.turned ? -1.0F : 1.0F;
        AddSignPanel(centerOnGround +
                      vec3{0.0F, signY - (0.7F * kS),
                           front * ((kBuildingDepth * 0.5F) + (0.075F * kS))},
                  kBuildingWidth * 0.45F, 1.4F * kS,
                  habitat ? kHabitatNeon : kWorkplaceNeon);
        neonSigns_.push_back(
            {.position = centerOnGround +
                         vec3{0.0F, signY,
                              front * ((kBuildingDepth * 0.5F) + (2.5F * kS))},
             .color = habitat ? kHabitatNeon : kWorkplaceNeon,
             .building = building.id});
        // A sign over the door of each ground-floor unit, lit while it is a
        // venue; units open and close, so these are drawn every frame.
        std::vector<const sim::Room*> units;
        for (const sim::Room& room : building.rooms) {
            if (room.floor == 0) {
                units.push_back(&room);
            }
        }
        const float unitWidth =
            21.0F / static_cast<float>(std::max<std::size_t>(units.size(), 1));
        for (std::size_t index = 0; index < units.size(); ++index) {
            const float x = -8.0F + (unitWidth * (static_cast<float>(index) + 0.5F));
            unitSigns_.push_back(
                {.room = units[index]->id,
                 .panel = ToWorld(shape, 0, {x, 2.6F, (kFootprintDepthMeters * 0.5F) + 0.075F}),
                 .width = std::min(unitWidth - 1.0F, 3.0F) * kS,
                 .light = ToWorld(shape, 0, {x, 2.9F, (kFootprintDepthMeters * 0.5F) + 2.0F})});
            neonSigns_.push_back({.position = unitSigns_.back().light,
                                  .color = vec3(0.0F),
                                  .building = building.id,
                                  .unit = units[index]->id});
        }
    }
    for (const sim::Building& building : city_.buildings) {
        const BuildingShape& shape = shapes_.at(building.id.value);
        AddFacade(shape.center, MassingOf(building.style, building.floors), building.style);
        AddRoof(building, shape.center);
    }

    // Corners without a building are small parks. The city saves exactly
    // those corners as its free plots, sorted by node and then corner, which
    // is the order the corners of `nodes` were walked in before, so the same
    // parks get the same decoration draws.
    sim::Pcg32 random = sim::Pcg32::FromSeed(seed, kDecorationStream);
    for (const sim::CityLot& lot : city_.lots) {
        const vec2 quadrant = kQuadrants.at(lot.corner);
        const vec3 position =
            NodePosition(lot.node) +
            vec3{quadrant.x * kQuadrantOffset.x, 0.0F, quadrant.y * kQuadrantOffset.y};
        // A corner outside the grid's bounding box gets no park.
        const bool insideCity = position.x > minimum.x && position.z > minimum.z &&
                                position.x < maximum.x && position.z < maximum.z;
        if (insideCity) {
            AddPark(position, random);
        }
    }
    BuildStaticBatches();
}

vec3 CityView::LinePoint(const LineFrame& frame, float meters, float across) const {
    return frame.origin + (frame.along * (meters * kS)) + (frame.across * (across * kS));
}

float CityView::SidingOffset(const sim::MetroLine& line, std::uint32_t train) {
    return (static_cast<float>(train) - (static_cast<float>(line.trains - 1) * 0.5F)) *
           kSidingSpacing;
}

// namespace

float CityView::DaylightAt(double minuteOfDay) {
    return Smoothstep(-0.1F, 0.25F, SunHeight(minuteOfDay));
}

bool CityView::UnitIsVenue(sim::EntityId room) const {
    const auto found = rooms_.Find(room);
    return found && KindHasNeonSign(found->room->kind);
}

vec3 CityView::UnitColor(sim::EntityId room) const {
    const auto found = rooms_.Find(room);
    return found && KindHasNeonSign(found->room->kind) ? StoreNeon(found->room->kind) : vec3(0.0F);
}

void CityView::UpdateCity(const sim::City& city) {
    // Rooms change all the time: their kinds, names, owners and sizes. The
    // index and every plan of a floor follow. Buildings appear when a
    // project raises one (task D): redraw those whose drawn shape no longer
    // matches - new, taller, scaffold turned finished - and drop any the
    // simulation no longer has.
    city_.buildings = city.buildings;
    rooms_ = sim::RoomIndex(city_);
    plans_.clear();
    roomWays_.clear();
    assignedSpots_.clear();
    crewLines_.clear();
    driverPlans_.clear();
    for (const sim::Building& building : city_.buildings) {
        const bool site = IsSite(building);
        const auto drawn = shapes_.find(building.id.value);
        if (drawn != shapes_.end() && drawn->second.floors == building.floors &&
            drawn->second.scaffold == site) {
            continue;
        }
        RemoveBuilding(building.id);
        AddBuilding(building);
    }
    std::vector<std::uint64_t> gone;
    for (const auto& [id, shape] : shapes_) {
        if (city_.FindBuilding(sim::EntityId{id}) == nullptr) {
            gone.push_back(id);
        }
    }
    for (const std::uint64_t id : gone) {
        RemoveBuilding(sim::EntityId{id});
    }
}

// Draws one building the view has not drawn yet: a habitat or workplace a
// project raised on a free plot. It takes the corner its project consumed -
// the slot after the node's other buildings, which are all older ids and so
// stand before it in the saved order - exactly where the constructor's
// placement loop would put it.
void CityView::AddBuilding(const sim::Building& building) {
    std::uint32_t slot = 0;
    for (const sim::Building& other : city_.buildings) {
        if (other.entrance == building.entrance && other.id != building.id) {
            ++slot;
        }
    }
    const vec2 quadrant = kQuadrants.at(slot % kQuadrants.size());
    // A fifth building at one corner moves to the far side of the block.
    const float ring = slot < kQuadrants.size() ? 1.0F : 3.0F;
    const vec3 centerOnGround =
        NodePosition(building.entrance) +
        vec3{quadrant.x * kQuadrantOffset.x * ring, 0.0F,
             quadrant.y * kQuadrantOffset.y * ring};
    const bool site = IsSite(building);
    const float height = static_cast<float>(building.floors) * kFloorHeight;
    shapes_[building.id.value] = {.center = centerOnGround,
                                  .height = height,
                                  .floors = building.floors,
                                  // The street is to the north (-z) when the
                                  // building sits south of its intersection.
                                  .turned = quadrant.y > 0.0F,
                                  .scaffold = site};
    if (site) {
        // A building going up is a scaffold: one thin slab per finished
        // floor, no block, no facade and no signs until its project ends.
        for (std::uint32_t floor = 0; floor < building.floors; ++floor) {
            const float base = static_cast<float>(floor) * kFloorHeight;
            AddStaticBox(centerOnGround +
                             vec3{-kBuildingWidth * 0.5F, base, -kBuildingDepth * 0.5F},
                         centerOnGround +
                             vec3{kBuildingWidth * 0.5F,
                                  base + (kSlabMeters * kS), kBuildingDepth * 0.5F},
                         kConcrete);
        }
        BatchStaticsFor(building.id.value);
        return;
    }
    const bool habitat = building.kind == sim::BuildingKind::Habitat;
    buildingBlocks_.emplace(building.id.value, MassingBlocks(building, centerOnGround));
    const float signY = SignHeightMeters(building.style, building.floors) * kS;
    const BuildingShape& shape = shapes_.at(building.id.value);
    const float front = shape.turned ? -1.0F : 1.0F;
    AddSignPanel(centerOnGround +
                     vec3{0.0F, signY - (0.7F * kS),
                          front * ((kBuildingDepth * 0.5F) + (0.075F * kS))},
                 kBuildingWidth * 0.45F, 1.4F * kS,
                 habitat ? kHabitatNeon : kWorkplaceNeon);
    neonSigns_.push_back(
        {.position = centerOnGround +
                     vec3{0.0F, signY,
                          front * ((kBuildingDepth * 0.5F) + (2.5F * kS))},
         .color = habitat ? kHabitatNeon : kWorkplaceNeon,
         .building = building.id});
    // A sign over the door of each ground-floor unit, lit while it is a
    // venue; units open and close, so these are drawn every frame.
    std::vector<const sim::Room*> units;
    for (const sim::Room& room : building.rooms) {
        if (room.floor == 0) {
            units.push_back(&room);
        }
    }
    const float unitWidth =
        21.0F / static_cast<float>(std::max<std::size_t>(units.size(), 1));
    for (std::size_t index = 0; index < units.size(); ++index) {
        const float x = -8.0F + (unitWidth * (static_cast<float>(index) + 0.5F));
        unitSigns_.push_back(
            {.room = units[index]->id,
             .panel = ToWorld(shape, 0, {x, 2.6F, (kFootprintDepthMeters * 0.5F) + 0.075F}),
             .width = std::min(unitWidth - 1.0F, 3.0F) * kS,
             .light = ToWorld(shape, 0, {x, 2.9F, (kFootprintDepthMeters * 0.5F) + 2.0F})});
        neonSigns_.push_back({.position = unitSigns_.back().light,
                              .color = vec3(0.0F),
                              .building = building.id,
                              .unit = units[index]->id});
    }
    AddFacade(centerOnGround, MassingOf(building.style, building.floors), building.style);
    AddRoof(building, centerOnGround);
    BatchStaticsFor(building.id.value);
    // The block itself, batched under the building so it hides while it is
    // open, as BuildStaticBatches batches the generated ones.
    std::vector<Transform> blockTransforms;
    for (const StaticItem& block : buildingBlocks_.at(building.id.value)) {
        blockTransforms.push_back(block.transform);
    }
    staticBatches_.push_back(
        {.building = building.id.value,
         .batch = MeshBatch(*assets_.cube.get(),
                            gsl_lite::not_null{habitat ? assets_.habitat
                                                      : assets_.workplace},
                            blockTransforms)});
}

void CityView::RemoveBuilding(sim::EntityId building) {
    shapes_.erase(building.value);
    buildingBlocks_.erase(building.value);
    std::erase_if(staticBatches_, [id = building.value](const StaticBatch& batch) {
        return batch.building == id;
    });
    for (Neon& neon : neons_) {
        neon.retired = neon.retired || neon.building == building;
    }
    std::erase_if(neonSigns_, [building](const NeonSign& sign) {
        return sign.building == building;
    });
    // A unit sign names its room; the room is gone with the building, so
    // either it still resolves to this building or it resolves to nothing.
    std::erase_if(unitSigns_, [this, building](const UnitSign& sign) {
        const auto room = rooms_.Find(sign.room);
        return !room || room->building->id == building;
    });
}

void CityView::AddSignPanel(vec3 base, float width, float tall, vec3 color) {
    statics_.push_back(
        {.mesh = assets_.cube.get(),
         .transform = {.position = base,
                       .rotationDegrees = vec3(0.0F),
                       .scale = {width, tall, 0.15F * kS}},
         .material = assets_.palette->GetGlowing(color, color * kGlow)});
}

void CityView::IndexResidents(const WorldFrame& frame) {
    if (residentsTick_ == frame.tick && !residentIndex_.empty()) {
        return;
    }
    residentsTick_ = frame.tick;
    residentIndex_.clear();
    std::unordered_map<std::uint64_t, std::size_t> counts;
    // Frames list robots by id, so flatmates are numbered in id order.
    for (const RobotFrame& robot : frame.robots) {
        residentIndex_[robot.id.value] = counts[robot.apartment.value]++;
    }
}

void CityView::Submit(RenderQueue& queue, const WorldFrame& frame,
                      double renderTick, const Selection& selection,
                      float streetFigureScale) {
    IndexResidents(frame);
    SubmitUnitSigns(queue);
    robotMarkers_.clear();
    roomTiles_.clear();
    litLamps_.clear();
    openBuilding_ = selection.building;
    openFloor_ = selection.floor;
    const auto open = shapes_.find(selection.building.value);
    std::uint32_t openFloor = 0;
    if (open != shapes_.end()) {
        openFloor = std::min(selection.floor, open->second.floors - 1);
        AssignSpots(frame, PlanOf(selection.building, openFloor));
    } else {
        assignedSpots_.clear();
    }
    for (const StaticBatch& part : staticBatches_) {
        if (part.building == 0 || part.building != selection.building.value) {
            queue.SubmitOpaqueBatch(part.batch);
        }
    }

    const float figureScale = kS * streetFigureScale;
    // Drivers on shift. Within a crew room (one per line) the drivers of a
    // shift, in id order, take that line's trains in the order they leave:
    // ranked among everyone on the shift, not only those clocked in, so a
    // late colleague never moves anyone to another train.
    driverRank_.clear();
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::vector<sim::EntityId>> crews;
    const sim::StaffRules* crewStaff = sim::FindStaffRules(sim::RoomKind::CrewRoom);
    for (const RobotFrame& robot : frame.robots) {
        if (!robot.workspace.IsValid() || CrewLineOf(robot.workspace) == std::nullopt) {
            continue;
        }
        crews[{robot.workspace.value, sim::ShiftSlot(robot.id, *crewStaff)}].push_back(robot.id);
    }
    std::unordered_map<std::uint64_t, std::uint32_t> rankOf;
    for (auto& [crew, drivers] : crews) {
        std::ranges::sort(drivers);
        for (std::size_t rank = 0; rank < drivers.size(); ++rank) {
            rankOf[drivers[rank].value] = static_cast<std::uint32_t>(rank);
        }
    }
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity == sim::RobotActivity::Working && robot.location == robot.workspace &&
            rankOf.contains(robot.id.value)) {
            driverRank_[robot.id.value] = rankOf.at(robot.id.value);
        }
    }
    for (const RobotFrame& robot : frame.robots) {
        // Drivers on shift are drawn too, although they are not walking.
        const bool driver = driverRank_.contains(robot.id.value);
        if (!driver &&
            (robot.activity != sim::RobotActivity::Walking ||
             !robot.from.IsValid())) {
            continue;
        }
        const auto placed = RobotPosition(robot, renderTick);
        // On the train, or inside a building where it cannot be seen.
        const bool indoors = placed && placed->building.IsValid();
        if (!placed || (indoors && (placed->building != selection.building ||
                                    placed->floor != openFloor))) {
            continue;
        }
        const vec3 position = placed->position;
        const vec3 direction = placed->direction;
        // Indoors and on trains at life size; in the street larger, so
        // robots stay visible from afar.
        const float scale = indoors || placed->aboard ? kS : figureScale;
        const bool selected = robot.id == selection.robot;
        // Legs swing once per 1.5 m walked.
        const float walked = static_cast<float>(
            (renderTick - static_cast<double>(robot.departedAt)) * 75.0);
        const float phase = walked / 0.75F * std::numbers::pi_v<float>;
        const float yaw = glm::degrees(std::atan2(direction.x, direction.z));
        // Shopping comes home in a bag; the black market wraps it up.
        Carried carried = Carried::Nothing;
        if (const auto left = rooms_.Find(robot.location)) {
            if (left->room->kind == sim::RoomKind::Shop) {
                carried = Carried::ShoppingBag;
            } else if (left->room->kind == sim::RoomKind::BlackMarket) {
                carried = Carried::Parcel;
            }
        }
        SubmitModel(queue, RobotFigure(placed->riding ? Pose::Standing : Pose::Walking,
                                       phase, selected ? kSelected : Accent(robot.id, robot.gangColor),
                                       carried),
                    position, yaw, 0.0F, vec3(1.0F),
                    scale * (selected ? 1.2F : 1.0F));
        robotMarkers_.push_back(
            {.id = robot.id,
             .position = position + vec3{0.0F, 1.0F * scale, 0.0F},
             .radius = kWalkerPickRadius * (scale / kS)});
    }
    SubmitTrains(queue, renderTick);
    if (open != shapes_.end()) {
        SubmitInterior(queue, frame, renderTick, selection);
    }
}

std::optional<Pick> CityView::PickAt(const Ray& ray) const {
    float nearest = std::numeric_limits<float>::max();
    std::optional<Pick> result;
    const auto consider = [&](float distance, Pick pick) {
        if (distance >= 0.0F && distance < nearest) {
            nearest = distance;
            result = pick;
        }
    };
    // Slab test: distance along the ray to where it enters `box`.
    const auto hitBox = [&](const Box& box) -> std::optional<float> {
        float enter = 0.0F;
        float exit = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis) {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            if (std::abs(direction) < 1e-6F) {
                if (origin < box.minimum[axis] || origin > box.maximum[axis]) {
                    return std::nullopt;
                }
                continue;
            }
            float nearSide = (box.minimum[axis] - origin) / direction;
            float farSide = (box.maximum[axis] - origin) / direction;
            if (nearSide > farSide) {
                std::swap(nearSide, farSide);
            }
            enter = std::max(enter, nearSide);
            exit = std::min(exit, farSide);
            if (enter > exit) {
                return std::nullopt;
            }
        }
        return enter;
    };

    for (const Marker& marker : robotMarkers_) {
        const vec3 offset = ray.origin - marker.position;
        const float b = glm::dot(offset, ray.direction);
        const float c = glm::dot(offset, offset) - (marker.radius * marker.radius);
        const float discriminant = (b * b) - c;
        if (discriminant >= 0.0F) {
            // Robots sit inside rooms; let them win over the room's box.
            consider(-b - std::sqrt(discriminant) - (0.5F * kS),
                     {.robot = marker.id});
        }
    }
    for (const Tile& tile : roomTiles_) {
        if (const auto distance = hitBox(tile.box)) {
            const auto room = rooms_.Find(tile.room);
            consider(*distance,
                     {.building = room ? room->building->id : sim::EntityId{},
                      .room = tile.room});
        }
    }
    for (const auto& [id, shape] : shapes_) {
        const vec3 half{kBuildingWidth * 0.5F, 0.0F, kBuildingDepth * 0.5F};
        // The open building is cut away above the floor being viewed, so its
        // rooms and robots are not hidden behind its full height.
        const float height =
            id == openBuilding_.value
                ? (static_cast<float>(openFloor_) * kFloorHeight) + (kSlabMeters * kS)
                : shape.height;
        const Box box{.minimum = shape.center - half,
                      .maximum = shape.center + half + vec3{0.0F, height, 0.0F}};
        if (const auto distance = hitBox(box)) {
            consider(*distance + (0.01F * kS), {.building = sim::EntityId{id}});
        }
    }
    return result;
}

std::uint32_t CityView::Floors(sim::EntityId building) const {
    const auto found = shapes_.find(building.value);
    return found != shapes_.end() ? found->second.floors : 0;
}

vec3 CityView::FloorCenter(sim::EntityId building, std::uint32_t floor) const {
    const auto found = shapes_.find(building.value);
    if (found == shapes_.end()) {
        return center_;
    }
    return found->second.center +
           vec3{0.0F, (static_cast<float>(floor) * kFloorHeight) + (1.0F * kS), 0.0F};
}

vec3 CityView::NodePosition(sim::NodeId node) const {
    const sim::CityNode& cityNode = city_.nodes.at(node.value);
    return {static_cast<float>(cityNode.x) * kUnitsPerMeter, 0.0F,
            static_cast<float>(cityNode.z) * kUnitsPerMeter};
}

const std::vector<vec3>& CityView::Route(sim::EntityId from, sim::EntityId to) {
    const auto key = std::make_pair(from.value, to.value);
    if (const auto cached = routeCache_.find(key); cached != routeCache_.end()) {
        return cached->second;
    }
    std::vector<vec3> points;
    const sim::Building* origin = city_.FindBuilding(from);
    const sim::Building* destination = city_.FindBuilding(to);
    // Inside one building the way runs along its front, not round the block.
    if (origin != nullptr && destination != nullptr && from != to) {
        for (const sim::NodeId node :
             routes_.Path(origin->entrance, destination->entrance)) {
            points.push_back(NodePosition(node));
        }
    }
    return routeCache_.emplace(key, std::move(points)).first->second;
}

}  // namespace mechapunk
