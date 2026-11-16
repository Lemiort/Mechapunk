#pragma once

#include <algorithm>
#include <cmath>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "Camera.hpp"

namespace mechapunk {

// Camera that circles a point on the ground: drag to orbit, scroll to zoom,
// keys to move the point. Produces an engine Camera for each frame.
class OrbitCamera {
public:
    static constexpr float kFieldOfView = 45.0F;
    static constexpr float kNear = 0.01F;
    // Near plane grows with distance so far streets do not z-fight.
    static constexpr float kNearPerUnit = 0.01F;
    static constexpr float kFar = 500.0F;
    static constexpr float kMinPitch = 10.0F;
    static constexpr float kMaxPitch = 85.0F;
    static constexpr float kMinDistance = 0.2F;
    static constexpr float kMaxDistance = 150.0F;

    OrbitCamera(glm::vec3 target, float distance)
        : target_(target), distance_(distance) {}

    void Orbit(float yawDegrees, float pitchDegrees) {
        yaw_ += yawDegrees;
        pitch_ = std::clamp(pitch_ + pitchDegrees, kMinPitch, kMaxPitch);
    }

    // Positive steps zoom in.
    void Zoom(float steps) {
        distance_ = std::clamp(distance_ * std::pow(0.9F, steps), kMinDistance,
                               kMaxDistance);
    }

    // Looks at `target` from `distance`, keeping the viewing angle.
    void Focus(glm::vec3 target, float distance) {
        target_ = target;
        distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    }
    void FocusTarget(glm::vec3 target) { target_ = target; }
    void SetPitch(float degrees) {
        pitch_ = std::clamp(degrees, kMinPitch, kMaxPitch);
    }
    [[nodiscard]] float Distance() const noexcept { return distance_; }

    // Moves the target in the camera's ground plane; units scale with zoom.
    void Pan(float right, float forward) {
        const float yaw = glm::radians(yaw_);
        const glm::vec3 forwardAxis{-std::sin(yaw), 0.0F, -std::cos(yaw)};
        const glm::vec3 rightAxis{std::cos(yaw), 0.0F, -std::sin(yaw)};
        const float scale = distance_ * 0.02F;
        target_ += ((rightAxis * right) + (forwardAxis * forward)) * scale;
    }

    [[nodiscard]] Camera MakeCamera(int width, int height) const {
        const float yaw = glm::radians(yaw_);
        const float pitch = glm::radians(pitch_);
        const glm::vec3 offset{std::sin(yaw) * std::cos(pitch),
                               std::sin(pitch),
                               std::cos(yaw) * std::cos(pitch)};
        const glm::vec3 position = target_ + (offset * distance_);
        return {width,    height,         kFieldOfView,
                std::max(kNear, distance_ * kNearPerUnit), kFar, position,
                -offset,  {0.0F, 1.0F, 0.0F}};
    }

private:
    glm::vec3 target_;
    float distance_;
    float yaw_{35.0F};
    float pitch_{45.0F};
};

}  // namespace mechapunk
