#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "GameRenderer.hpp"
#include "Interior.hpp"
#include "Massing.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "MeshBatch.hpp"
#include "Mesh.hpp"
#include "Props.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "SimulationHost.hpp"
#include "Transform.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Metro.hpp"
#include "mechapunk/sim/Random.hpp"

namespace mechapunk {

struct Ray {
    glm::vec3 origin{0.0F};
    glm::vec3 direction{0.0F, 0.0F, -1.0F};  // Normalized.
};

// What the player has opened and selected. Ids are none when unset.
struct Selection {
    // Building shown as a cutaway, and the floor being looked at.
    sim::EntityId building;
    std::uint32_t floor{};
    sim::EntityId room;
    sim::EntityId robot;
    // The organization shown in the inspector; the camera and the cutaway
    // ignore it.
    sim::EntityId organization;
    // A gang selected from the Statistics window or a member's page.
    sim::EntityId gang;
    // The building site being looked at: the id of the building whose project
    // is raising it (see sim::Project), set by a project link; the camera and
    // the cutaway ignore it, as they ignore the gang.
    sim::EntityId project;
};

// What a click hit: a robot, a room of the open building, or a building.
struct Pick {
    sim::EntityId building;
    sim::EntityId room;
    sim::EntityId robot;
};

// Turns the simulated city into render commands at real-world scale: streets,
// buildings with windows, robots walking outside, and the open building as a
// cutaway with walls, doors, windows, furniture and robots in their rooms.
// Owned and used on the render thread.
class CityView {
public:
    // Render units per meter: one unit is 20 m.
    static constexpr float kUnitsPerMeter = 0.05F;
    static constexpr float kFloorHeight = kFloorHeightMeters * kUnitsPerMeter;

    CityView(const sim::City& city, std::uint64_t seed, RenderAssets& assets);
    CityView(const CityView&) = delete;
    CityView& operator=(const CityView&) = delete;
    CityView(CityView&&) = delete;
    CityView& operator=(CityView&&) = delete;
    ~CityView() = default;

    // Adds the lights the city owns (sun, sky, a faint fill for open
    // buildings, neon signs, street lamps and the lamps of an open floor) to
    // `scene`.
    void AddLights(Scene& scene);
    // Gives every sign added since (a building a project raised, task I,
    // step I4) a point light, and takes the lights of removed buildings'
    // signs over first. Call once a frame before UpdateLights.
    void SyncLights(Scene& scene);
    // Day-night cycle, street lamps at night, and the lamps switched on in
    // the open floor as of the last Submit.
    // `focus` is where the camera looks: only the neon and street lamps
    // nearest it light the scene, the rest show by their glow alone.
    // `overcast`, 0 to 1, dims the sun and sky while it rains.
    void UpdateLights(Scene& scene, double minuteOfDay,
                      const Selection& selection, glm::vec3 focus,
                      float overcast) const;
    // 0 at night, 1 in full daylight, easing through dawn and dusk.
    [[nodiscard]] static float DaylightAt(double minuteOfDay);

    // Queues the city and robots for one frame and remembers where things
    // were drawn, for PickAt. Robots outdoors are drawn `streetFigureScale`
    // times life size so they stay visible from far away.
    void Submit(RenderQueue& queue, const WorldFrame& frame, double renderTick,
                const Selection& selection, float streetFigureScale);
    // The closest robot, room or building along `ray` in the last frame.
    [[nodiscard]] std::optional<Pick> PickAt(const Ray& ray) const;
    // Where a robot was drawn in the last frame; nothing when it was not
    // drawn, being inside a building that is not open.
    [[nodiscard]] std::optional<glm::vec3> DrawnRobotPosition(
        sim::EntityId robot) const;

    [[nodiscard]] const sim::City& City() const noexcept { return city_; }
    // Takes on rooms that opened, closed or changed hands in the simulation.
    void UpdateCity(const sim::City& city);
    [[nodiscard]] const sim::Metro& Transit() const noexcept { return metro_; }
    [[nodiscard]] const sim::RoomIndex& Rooms() const noexcept {
        return rooms_;
    }
    [[nodiscard]] std::uint32_t Floors(sim::EntityId building) const;
    // Middle of a floor of a building, for the camera to look at.
    [[nodiscard]] glm::vec3 FloorCenter(sim::EntityId building,
                                        std::uint32_t floor) const;
    [[nodiscard]] glm::vec3 Center() const noexcept { return center_; }
    [[nodiscard]] float Extent() const noexcept { return extent_; }

private:
    struct StaticItem {
        const Mesh* mesh;
        Transform transform;
        std::shared_ptr<const Material> material;
    };
    struct BuildingShape {
        glm::vec3 center;  // On the ground.
        float height{};
        std::uint32_t floors{};
        // Buildings face their street: plans are turned half way round when
        // the street is to the north.
        bool turned{false};
        // Drawn as a scaffold of floor slabs, not a finished block: the
        // building's project is still raising floors (task D).
        bool scaffold{false};
    };
    struct Box {
        glm::vec3 minimum;
        glm::vec3 maximum;
    };
    struct NeonSign {
        glm::vec3 position;
        glm::vec3 color;
        sim::EntityId building;
        // The ground-floor unit whose sign it lights, or none for a
        // building's own sign. The room's id, not its place in `unitSigns_`,
        // so adding and removing a building's signs cannot shift it.
        sim::EntityId unit{};
        // Whether a point light of the scene already stands for it.
        bool lit{false};
    };
    struct Neon {
        std::size_t lightIndex;
        glm::vec3 color;
        sim::EntityId building;
        glm::vec3 position;
        sim::EntityId unit{};
        // Its building was removed: the light stays in the scene, dark and
        // out of sight, until a new sign takes it over.
        bool retired{false};
    };
    // A ground-floor unit's sign over its door.
    struct UnitSign {
        sim::EntityId room;
        glm::vec3 panel;
        float width{};
        glm::vec3 light;
    };
    struct Marker {
        sim::EntityId id;
        glm::vec3 position;
        float radius;
    };
    struct Tile {
        sim::EntityId room;
        Box box;
    };
    // A lamp of the open floor that is switched on.
    struct LitLamp {
        glm::vec3 position;
        glm::vec3 color;
    };
    // A point of a robot's walk. Indoor points belong to a building floor.
    struct WalkPoint {
        glm::vec3 position;
        float spread;  // How far robots fan out across the street, 0 to 1.
        sim::EntityId building;  // None outdoors.
        std::uint32_t floor;
    };
    using Walk = std::vector<WalkPoint>;
    // The ways out of a room to the pavement in front of its building.
    struct RoomWays {
        bool home{false};
        std::size_t staffSpots{0};  // The last ones, for staff.
        std::vector<Walk> fromSpot;
        Walk fromDoor;  // For a room without spots.
    };
    // Where a robot is this frame; inside a building, on which floor.
    struct Placement {
        glm::vec3 position;
        glm::vec3 direction;
        sim::EntityId building;  // None outdoors.
        std::uint32_t floor{};
        // Boarding or riding a train: drawn life size, standing once aboard.
        bool aboard{false};
        bool riding{false};
    };
    // Where a train is: the middle of its two cars on the track, and the
    // way it faces.
    struct TrainPose {
        glm::vec3 position;
        glm::vec3 forward;
    };

    [[nodiscard]] glm::vec3 NodePosition(sim::NodeId node) const;
    // The middle of a building's plot on the ground: the corner of its
    // entrance's intersection after those its older buildings take.
    [[nodiscard]] glm::vec3 PlotCenter(const sim::Building& building) const;
    // Intersections walked through between two buildings' doors.
    const std::vector<glm::vec3>& Route(sim::EntityId from, sim::EntityId to);
    // A line's frame: its first station's deck, the way it runs and the
    // way across it, and how far (meters, negative) back to the city's edge.
    struct LineFrame {
        glm::vec3 origin;
        glm::vec3 along;
        glm::vec3 across;
        float edge{};
    };
    struct Stairs {
        glm::vec3 top;
        glm::vec3 bottom;
    };
    [[nodiscard]] LineFrame FrameOf(std::uint32_t line) const;
    // A point `meters` along a line and `across` meters to its side.
    [[nodiscard]] glm::vec3 LinePoint(const LineFrame& frame, float meters,
                                      float across) const;
    // The stairs of a station's platform beside one track (side -1 up, +1
    // down) of its row or column line.
    [[nodiscard]] Stairs StairsOf(std::uint32_t station, sim::LineAxis axis,
                                  float side) const;
    // Where a robot waits for its train on that platform.
    [[nodiscard]] glm::vec3 PlatformSpot(sim::EntityId robot, std::uint32_t station,
                                         sim::LineAxis axis, float side) const;
    // A depot siding's offset across its line.
    [[nodiscard]] static float SidingOffset(const sim::MetroLine& line,
                                            std::uint32_t train);
    // Where a train of the timetable is drawn.
    [[nodiscard]] TrainPose TrainPoseOf(const sim::TrainState& train) const;
    // The trains a rider takes, planned again from its ride.
    const std::vector<sim::Leg>& LegsOf(const RobotFrame& robot);
    // A box turned to run from one point to another.
    void AddStaticBeam(glm::vec3 from, glm::vec3 to, float width, float height,
                       glm::vec3 color);
    void AddElevatedLines();
    // Track pieces in a line's frame: meters along it, across it to its side.
    void AddTrackSlab(const LineFrame& frame, float from, float to, float acrossFrom,
                      float acrossTo, float bottom, float top, glm::vec3 color,
                      bool glows = false);
    void AddRails(const LineFrame& frame, float from, float to, float across);
    void AddSwitchRails(const LineFrame& frame, float fromMeters, float fromAcross,
                        float toMeters, float toAcross);
    void AddBumper(const LineFrame& frame, float meters, float across, float facing);
    // Rails, or a deck with rails, between two points at a deck's height.
    void AddRailsBetween(glm::vec3 from, glm::vec3 to);
    void AddTrackBetween(glm::vec3 from, glm::vec3 to, bool rails = true);
    // A line's depot in a shed on the roof of its depot building: a building on
    // the line's street whose rooms are all crew rooms (CityDepots.cpp). The
    // sidings run across the line on the building's side of it (acrossSign),
    // and a curve off the up track turns into them past the station or before
    // it (alongSign). Meters along the line from its first station.
    struct Depot {
        std::uint64_t building{};
        float alongSign{1.0F};
        float acrossSign{1.0F};
        float lead{};                 // The track from the curve to the throat.
        std::vector<float> sidings;   // One per train.
    };
    [[nodiscard]] std::optional<Depot> FindDepot(std::uint32_t line) const;
    // A building whose rooms are all crew rooms: a metro depot.
    [[nodiscard]] static bool IsDepotBuilding(const sim::Building& building);
    // Whether a line has gained, lost or changed its depot building since the
    // metro was drawn.
    [[nodiscard]] bool DepotsMoved() const;
    // Meters along the line where the curve into a roof depot leaves the up track.
    [[nodiscard]] static float DepotJunction(const Depot& depot);
    // A point of the curve into a roof depot, t from 0 (on the up track) to 1.
    [[nodiscard]] glm::vec3 DepotCurvePoint(const LineFrame& frame, const Depot& depot,
                                            float t, float outward = 0.0F) const;
    // A train's way from its parked place to the first platform, on the up track.
    [[nodiscard]] std::vector<glm::vec3> DepotWay(std::uint32_t line, std::uint32_t train) const;
    // A line's depot: on its building's roof, or beyond the city's edge.
    void AddDepot(std::uint32_t index, const LineFrame& frame);
    // The whole metro, lines and stations, as batches of its own: drawn again
    // whenever stations open, the grid grows or a depot is built.
    void DrawMetro();
    // A box that never moves; `glows` makes it shine with its own color.
    void AddStaticBox(glm::vec3 minimum, glm::vec3 maximum, glm::vec3 color,
                      bool glows = false);
    // The material of a prop part, glowing for neon, bulbs and screens.
    [[nodiscard]] std::shared_ptr<const Material> PartMaterial(const PropPart& part,
                                                               glm::vec3 tint) const;
    // A rider's place in the train, and the door it uses, given the side
    // of the train it comes from (+1 right, -1 left).
    [[nodiscard]] glm::vec3 PlaceAboard(sim::EntityId robot,
                                        const TrainPose& train) const;
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> DoorFor(
        sim::EntityId robot, const TrainPose& train, glm::vec3 from) const;
    // Every train of the timetable.
    void SubmitTrains(RenderQueue& queue, double renderTick);
    // Intersections walked through from a building to a station.
    const std::vector<glm::vec3>& StationRoute(sim::EntityId building,
                                               std::uint32_t station);
    void AddStations();
    // A metro driver's shift: from the crew room up to its line's first
    // platform, into the cab of its train through the front door, driving
    // it until the last time it stands at that platform before the shift
    // ends, then back. Planned once per shift; nothing when it has no train.
    struct DriverPlan {
        std::uint32_t line{};
        std::uint32_t station{};
        sim::LineAxis axis{};
        sim::EntityId room;
        sim::EntityId building;
        double leaves{};      // Out of the crew room.
        double onPlatform{};  // Waiting for the doors to open.
        sim::DriverRun run;
        double back{};        // In the crew room again.
    };
    struct CachedDriverPlan {
        double clockIn{};
        std::uint32_t rank{};
        std::optional<DriverPlan> plan;
    };
    // The line a crew room's depot serves.
    [[nodiscard]] std::optional<std::uint32_t> CrewLineOf(sim::EntityId crewRoom) const;
    [[nodiscard]] std::optional<DriverPlan> PlanDriver(const RobotFrame& robot) const;
    // Whether a driver on shift is out of its crew room.
    [[nodiscard]] bool DriverAway(const RobotFrame& robot, double renderTick) const;
    // Where the driver of a train stands: its driving end.
    [[nodiscard]] glm::vec3 CabOf(std::uint32_t line, const sim::TrainState& state) const;
    // A driver's place while out of the crew room; nothing while in it.
    [[nodiscard]] std::optional<Placement> DriverPlacement(const RobotFrame& robot,
                                                           double renderTick);
    // Along walk_ from start to end, at renderTick.
    [[nodiscard]] std::optional<Placement> FollowWalk(const RobotFrame& robot, double start,
                                                      double end, double renderTick,
                                                      bool boarding) const;
    // Position along the walk and the direction of travel; nothing while
    // the robot rides the metro.
    [[nodiscard]] std::optional<Placement> RobotPosition(const RobotFrame& robot,
                                                         double renderTick);
    // The ways out of a room, made once; nothing for an unknown room.
    const RoomWays* WaysOf(sim::EntityId room);
    // The spot a robot takes in `room`, which has `count` spots, at
    // `tick`: at home the bed by night and the sofa by day, elsewhere the
    // one it was given in that room on the open floor, else its favourite.
    [[nodiscard]] bool UnitIsVenue(sim::EntityId room) const;
    [[nodiscard]] glm::vec3 UnitColor(sim::EntityId room) const;
    void SubmitUnitSigns(RenderQueue& queue) const;
    // Numbers flatmates 0, 1, 2 by id for the frame, for their beds and seats.
    void IndexResidents(const WorldFrame& frame);
    // Staff working in the room take its last staffSpots spots; guests the
    // others.
    [[nodiscard]] std::size_t SpotFor(sim::EntityId robot, sim::EntityId room,
                                      bool home, std::size_t count, double tick,
                                      std::size_t staffSpots, bool staff) const;
    // The spots, first to last (exclusive), for staff or for guests.
    [[nodiscard]] static std::pair<std::size_t, std::size_t> SpotRange(std::size_t count,
                                                                       std::size_t staffSpots,
                                                                       bool staff);
    // Gives the robots in or walking to the rooms of `plan` a spot each, in
    // order of arrival, so nobody shares one while another is free. A robot
    // keeps its spot from frame to frame, and a robot walking out keeps the
    // one it left from.
    void AssignSpots(const WorldFrame& frame, const FloorPlan& plan);
    const FloorPlan& PlanOf(sim::EntityId building, std::uint32_t floor);
    // Converts floor-plan meters to world units for a building and floor.
    [[nodiscard]] glm::vec3 ToWorld(const BuildingShape& shape,
                                    std::uint32_t floor, glm::vec3 meters) const;
    // Calls `part` with the transform of every part of `model` placed at
    // `origin`, turned by `yawDegrees` and tipped by `tiltDegrees`.
    static void ForEachPart(
        const PropModel& model, glm::vec3 origin, float yawDegrees,
        float tiltDegrees, float scale,
        const std::function<void(const PropPart&, const Transform&)>& part);
    void SubmitModel(RenderQueue& queue, const PropModel& model,
                     glm::vec3 origin, float yawDegrees, float tiltDegrees,
                     glm::vec3 tint, float scale);
    // Adds a model that never moves, such as a street lamp, to the statics.
    void AddStaticModel(const PropModel& model, glm::vec3 origin,
                        float yawDegrees);
    // Lamps along the edges from `firstEdge` on (a grown city adds only the new ones).
    void AddStreetLamps(std::size_t firstEdge = 0);
    // Draws the streets, lamps and parks a grown city added: nodes from
    // `firstNode` and edges from `firstEdge` on, and the plots that came inside.
    void AddGrownStreets(std::size_t firstEdge);
    // Batches drawn apart from the others, so each can be replaced whole: the
    // ground, the parks and the metro. Keys no entity id reaches.
    static constexpr std::uint64_t kGroundBatch = ~std::uint64_t{0};
    static constexpr std::uint64_t kParksBatch = ~std::uint64_t{0} - 1;
    static constexpr std::uint64_t kMetroBatch = ~std::uint64_t{0} - 2;
    void BatchApart(std::uint64_t key, const std::function<void()>& draw);
    void DrawGround();
    void DrawParks();
    // Grass, trees and benches on a corner without a building.
    void AddPark(glm::vec3 center, sim::Pcg32& random);
    void SubmitBox(RenderQueue& queue, glm::vec3 minimum, glm::vec3 maximum,
                   glm::vec3 color, bool glass);
    void SubmitInterior(RenderQueue& queue, const WorldFrame& frame,
                        double renderTick, const Selection& selection);
    // Windows on every wall of the boxes a style makes of a building.
    void AddFacade(glm::vec3 center, std::span<const MassBox> boxes,
                   sim::BuildingStyle style);
    // The wall blocks of a finished building by its style, and its roof
    // (task I, step I2); CityMassing.cpp.
    std::vector<StaticItem> MassingBlocks(const sim::Building& building,
                                          glm::vec3 center) const;
    void AddRoof(const sim::Building& building, glm::vec3 center);
    // The thin slab of every standing floor of a building going up.
    void AddScaffold(std::uint32_t floors, glm::vec3 center);
    // Groups statics_ and the building blocks into batches by building,
    // or for the rest by area, mesh and material.
    void BuildStaticBatches();
    // Draws one building the view has not drawn yet - a project raised one
    // on a free plot (task D): its block or scaffold, facade and signs,
    // batched under the building so they hide while it is open.
    void AddBuilding(const sim::Building& building);
    // Drops every static and sign of a building the view drew and the
    // simulation no longer has.
    void RemoveBuilding(sim::EntityId building);
    // Appends the items just pushed into statics_ as batches owned by
    // `building`. statics_ is empty outside the constructor and one AddBuilding.
    void BatchStaticsFor(std::uint64_t building);
    // The glowing panel of a sign, flat against the front wall and spanning
    // x; its light hangs a little further out.
    void AddSignPanel(glm::vec3 base, float width, float tall, glm::vec3 color);

    sim::City city_;
    sim::RoomIndex rooms_;
    sim::RoutePlanner routes_;
    sim::Metro metro_;
    struct RideLegs {
        bool known{false};
        sim::Tick boardsAt{};
        std::uint32_t from{};
        std::uint32_t to{};
        std::vector<sim::Leg> legs;
    };
    std::unordered_map<std::uint64_t, RideLegs> rideLegs_;
    std::unordered_map<std::uint64_t, std::size_t> residentIndex_;
    sim::Tick residentsTick_{};
    RenderAssets& assets_;
    // The city seed: each park's decoration is drawn from it and the plot.
    std::uint64_t seed_{};
    std::vector<StaticItem> statics_;
    // Statics drawn as batches, made once by BuildStaticBatches. A batch
    // of a building's outside (its block, windows and signs) is skipped
    // while the building is open; `building` is 0 for the rest.
    struct StaticBatch {
        std::uint64_t building;
        MeshBatch batch;
    };
    std::vector<StaticBatch> staticBatches_;
    std::map<std::uint64_t, std::vector<StaticItem>> buildingBlocks_;
    std::map<std::uint64_t, BuildingShape> shapes_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, FloorPlan> plans_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<glm::vec3>>
        routeCache_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::vector<glm::vec3>>
        stationRoutes_;
    std::map<std::uint64_t, RoomWays> roomWays_;
    // Spots of robots in, heading to or just leaving the rooms of the open
    // floor, by robot. A robot keeps its spot while it stays, and starts
    // its walk out from it.
    struct AssignedSpot {
        sim::EntityId room;
        std::size_t spot{};
    };
    std::unordered_map<std::uint64_t, AssignedSpot> assignedSpots_;
    Walk walk_;  // Scratch for RobotPosition.
    // Metro drivers on shift, by id: the rank of their train in its line,
    // rebuilt each frame before robots are drawn.
    std::unordered_map<std::uint64_t, std::uint32_t> driverRank_;
    mutable std::unordered_map<std::uint64_t, std::uint32_t> crewLines_;  // Crew room to line.
    // Per line as last drawn: its roof depot, or none for a shed beyond the edge.
    std::vector<std::optional<Depot>> depots_;
    mutable std::unordered_map<std::uint64_t, CachedDriverPlan> driverPlans_;
    std::vector<NeonSign> neonSigns_;
    std::vector<UnitSign> unitSigns_;
    std::vector<Neon> neons_;
    // Bulbs of the street lamps, which shine straight down.
    std::vector<glm::vec3> streetLamps_;
    // Point lights reserved for the lamps of the open floor.
    std::size_t lampPoolStart_{};
    // How many of `streetLamps_` already have a spot light in the scene.
    std::size_t streetLampsLit_{0};
    std::vector<LitLamp> litLamps_;
    glm::vec3 center_{0.0F};
    // The corners of the grid as drawn, to tell which plots a growth brings inside.
    glm::vec3 minimum_{0.0F};
    glm::vec3 maximum_{0.0F};
    float extent_{1.0F};
    // Where things were drawn in the last frame, for picking.
    std::vector<Marker> robotMarkers_;
    std::vector<Tile> roomTiles_;
    sim::EntityId openBuilding_;
    std::uint32_t openFloor_{};
};

}  // namespace mechapunk
