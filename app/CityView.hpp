#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "GameRenderer.hpp"
#include "Interior.hpp"
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
    };
    struct Box {
        glm::vec3 minimum;
        glm::vec3 maximum;
    };
    static constexpr std::size_t kNoUnit = static_cast<std::size_t>(-1);
    struct NeonSign {
        glm::vec3 position;
        glm::vec3 color;
        sim::EntityId building;
        // The ground-floor unit whose sign it lights, or kNoUnit for a
        // building's own sign.
        std::size_t unit{kNoUnit};
    };
    struct Neon {
        std::size_t lightIndex;
        glm::vec3 color;
        sim::EntityId building;
        glm::vec3 position;
        std::size_t unit{kNoUnit};
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
    // A crew-room robot's place on shift: on its platform before its train
    // leaves, in the cab of that train for the 480-minute shift.
    [[nodiscard]] std::optional<Placement> DriverPlacement(const RobotFrame& robot,
                                                           double renderTick);
    // Position along the walk and the direction of travel; nothing while
    // the robot rides the metro.
    [[nodiscard]] std::optional<Placement> RobotPosition(const RobotFrame& robot,
                                                         double renderTick);
    // The ways out of a room, made once; nothing for an unknown room.
    const RoomWays* WaysOf(sim::EntityId room);
    // The spot a robot takes in `room`, which has `count` spots, at
    // `tick`: at home the bed by night and the sofa by day, elsewhere the
    // one it was given in that room on the open floor, else its favourite.
    [[nodiscard]] bool UnitIsVenue(std::size_t unit) const;
    [[nodiscard]] glm::vec3 UnitColor(std::size_t unit) const;
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
    void AddStreetLamps();
    // Grass, trees and benches on a corner without a building.
    void AddPark(glm::vec3 center, sim::Pcg32& random);
    void SubmitBox(RenderQueue& queue, glm::vec3 minimum, glm::vec3 maximum,
                   glm::vec3 color, bool glass);
    void SubmitInterior(RenderQueue& queue, const WorldFrame& frame,
                        double renderTick, const Selection& selection);
    void AddFacade(glm::vec3 center, float width, float depth,
                   std::uint32_t floors);
    // Groups statics_ and the building blocks into batches by building,
    // or for the rest by area, mesh and material.
    void BuildStaticBatches();

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
    std::vector<StaticItem> statics_;
    // Statics drawn as batches, made once by BuildStaticBatches. A batch
    // of a building's outside (its block, windows and signs) is skipped
    // while the building is open; `building` is 0 for the rest.
    struct StaticBatch {
        std::uint64_t building;
        MeshBatch batch;
    };
    std::vector<StaticBatch> staticBatches_;
    std::map<std::uint64_t, StaticItem> buildingBlocks_;
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
    std::vector<NeonSign> neonSigns_;
    std::vector<UnitSign> unitSigns_;
    std::vector<Neon> neons_;
    // Bulbs of the street lamps, which shine straight down.
    std::vector<glm::vec3> streetLamps_;
    // Point lights reserved for the lamps of the open floor.
    std::size_t lampPoolStart_{};
    std::vector<LitLamp> litLamps_;
    glm::vec3 center_{0.0F};
    float extent_{1.0F};
    // Where things were drawn in the last frame, for picking.
    std::vector<Marker> robotMarkers_;
    std::vector<Tile> roomTiles_;
    sim::EntityId openBuilding_;
    std::uint32_t openFloor_{};
};

}  // namespace mechapunk
