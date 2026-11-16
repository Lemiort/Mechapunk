#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "GameRenderer.hpp"
#include "Interior.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Props.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "SimulationHost.hpp"
#include "Transform.hpp"
#include "mechapunk/sim/City.hpp"
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
    void UpdateLights(Scene& scene, double minuteOfDay,
                      const Selection& selection) const;

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
    struct NeonSign {
        glm::vec3 position;
        glm::vec3 color;
        sim::EntityId building;  // None for decorative towers.
    };
    struct Neon {
        std::size_t lightIndex;
        glm::vec3 color;
        sim::EntityId building;
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

    [[nodiscard]] glm::vec3 NodePosition(sim::NodeId node) const;
    const std::vector<glm::vec3>& Route(sim::EntityId from, sim::EntityId to);
    // Position along the route and the direction of travel.
    // Where the entrance of a station stands, on the pavement by its corner.
    [[nodiscard]] glm::vec3 StationEntrance(std::uint32_t station) const;
    // A walk between a building and a station entrance, either way.
    const std::vector<glm::vec3>& StationRoute(sim::EntityId building,
                                               std::uint32_t station,
                                               bool toStation);
    void AddStations();
    // Position along the route and the direction of travel; nothing while
    // the robot rides the metro underground.
    [[nodiscard]] std::optional<std::pair<glm::vec3, glm::vec3>> RobotPosition(
        const RobotFrame& robot, double renderTick);
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

    sim::City city_;
    sim::RoomIndex rooms_;
    sim::RoutePlanner routes_;
    RenderAssets& assets_;
    std::vector<StaticItem> statics_;
    std::map<std::uint64_t, StaticItem> buildingBlocks_;
    std::map<std::uint64_t, BuildingShape> shapes_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, FloorPlan> plans_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<glm::vec3>>
        routeCache_;
    std::map<std::tuple<std::uint64_t, std::uint32_t, bool>, std::vector<glm::vec3>>
        stationRoutes_;
    std::vector<NeonSign> neonSigns_;
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
