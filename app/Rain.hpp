#pragma once

#include <cstdint>
#include <memory>

#include <glm/ext/vector_float3.hpp>

#include "ParticleSystem.hpp"
#include "RenderQueue.hpp"
#include "Shader.hpp"

namespace mechapunk {

// Rain falling around where the camera looks, as streaks of GPU particles.
// It is drawn for the eye rather than the world: drops fall faster and grow
// wider as the camera pulls back, so the rain reads the same at every zoom
// and fills a new view within about a second. Must be created and used
// while the OpenGL context is current.
class RainEffect {
public:
    // `unitsPerMeter` is the world's scale.
    RainEffect(std::shared_ptr<Shader> updateShader,
               std::shared_ptr<Shader> renderShader, float unitsPerMeter);

    // Moves the rain on by `seconds` of real time. `rain` is the weather's
    // rain, 0 to sim::kMaxRain; the drops fade in and out toward it.
    // `daylight`, 0 to 1, dims the drops at night.
    // `eye` is the camera's position: drops stay below it, since one right
    // in front of the lens would look huge.
    void Update(float seconds, std::uint32_t rain, glm::vec3 focus,
                glm::vec3 eye, float viewDistance, float daylight);
    // Queues the drops, unless it is dry.
    void Submit(RenderQueue& queue) const;
    // How heavily it rains now, 0 to 1, eased like the drops.
    [[nodiscard]] float Level() const noexcept { return level_; }

private:
    ParticleSystem particles_;
    float unitsPerMeter_;
    float level_{0.0F};
};

}  // namespace mechapunk
