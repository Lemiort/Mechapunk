#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "GameRenderer.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "SimulationHost.hpp"
#include "Transform.hpp"
#include "mechapunk/sim/City.hpp"

namespace mechapunk {

// Turns the simulated city into render commands: ground, streets and
// buildings once, robots every frame. Owned and used on the render thread.
class CityView {
public:
    // Render units per simulated meter: one unit is 20 m.
    static constexpr float kUnitsPerMeter = 0.05F;

    CityView(const sim::City& city, std::uint64_t seed, RenderAssets& assets);

    // Adds the lights the city owns (sky, fill and neon signs) to `scene`.
    void AddLights(Scene& scene);
    // Day-night cycle: sun height and neon brightness follow the time of day.
    void UpdateLights(Scene& scene, double minuteOfDay) const;

    // Queues the static city and every robot that is out walking at
    // `renderTick`, positioned along its route.
    void Submit(RenderQueue& queue, const WorldFrame& frame, double renderTick);

    [[nodiscard]] glm::vec3 Center() const noexcept { return center_; }
    [[nodiscard]] float Extent() const noexcept { return extent_; }

private:
    struct StaticItem {
        const Mesh* mesh;
        Transform transform;
        std::shared_ptr<const Material> material;
    };
    struct Neon {
        std::size_t lightIndex;
        glm::vec3 color;
    };

    [[nodiscard]] glm::vec3 NodePosition(sim::NodeId node) const;
    const std::vector<glm::vec3>& Route(sim::EntityId from, sim::EntityId to);
    [[nodiscard]] glm::vec3 RobotPosition(const RobotFrame& robot,
                                          double renderTick);

    sim::City city_;
    sim::RoutePlanner routes_;
    RenderAssets& assets_;
    std::vector<StaticItem> statics_;
    std::map<std::uint64_t, glm::vec3> buildingCenters_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<glm::vec3>>
        routeCache_;
    std::vector<std::pair<glm::vec3, glm::vec3>> neonSigns_;  // position, color
    std::vector<Neon> neons_;
    glm::vec3 center_{0.0F};
    float extent_{1.0F};
};

}  // namespace mechapunk
