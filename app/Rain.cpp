#include "Rain.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "mechapunk/sim/Weather.hpp"

namespace mechapunk {

namespace {
constexpr std::size_t kDrops = 20000;
// Fraction of full rain the drops fade by per second.
constexpr float kFadePerSecond = 0.4F;
// Real rain falls at about 9 m/s; drops never fall slower.
constexpr float kFallMetersPerSecond = 9.0F;
// Seen from `d` away, rain fills a box d * kSpread to each side and
// d * kHeight tall, falls d * kFallPerDistance per second, and draws drops
// d * kWidthPerDistance wide in streaks d * kStreakPerDistance long.
constexpr float kSpread = 0.9F;
constexpr float kHeight = 1.3F;
// The rain's top stays d * kBelowEye under the camera, and at least
// d * kMinHeight above the ground.
constexpr float kBelowEye = 0.3F;
constexpr float kMinHeight = 0.2F;
constexpr float kFallPerDistance = 1.1F;
constexpr float kWidthPerDistance = 0.0012F;
constexpr float kStreakPerDistance = 0.035F;
constexpr glm::vec3 kDropColor{0.62F, 0.68F, 0.8F};
}  // namespace

RainEffect::RainEffect(std::shared_ptr<Shader> updateShader,
                       std::shared_ptr<Shader> renderShader, float unitsPerMeter)
    : particles_(gsl_lite::not_null{std::move(updateShader)},
                 gsl_lite::not_null{std::move(renderShader)}, kDrops),
      unitsPerMeter_(unitsPerMeter) {}

void RainEffect::Update(float seconds, std::uint32_t rain, glm::vec3 focus,
                        glm::vec3 eye, float viewDistance, float daylight) {
    const float target =
        static_cast<float>(rain) / static_cast<float>(sim::kMaxRain);
    const float step = kFadePerSecond * seconds;
    const bool wasDry = level_ <= 0.0F;
    level_ = level_ < target ? std::min(target, level_ + step)
                             : std::max(target, level_ - step);
    if (level_ <= 0.0F) {
        return;
    }
    if (wasDry) {
        // Fill the view at once instead of waiting for drops to be born.
        particles_.Reset();
    }
    const float spread = viewDistance * kSpread;
    const float speed = std::max(kFallMetersPerSecond * unitsPerMeter_,
                                 viewDistance * kFallPerDistance);
    // Up to kHeight above the ground, but well below the camera.
    const float height = std::max(
        viewDistance * kMinHeight,
        std::min(focus.y + (viewDistance * kHeight),
                 eye.y - (viewDistance * kBelowEye)));
    particles_.SetEmitter(
        {.boxMin = {focus.x - spread, 0.0F, focus.z - spread},
         .boxMax = {focus.x + spread, height, focus.z + spread},
         // A little wind from the west.
         .velocity = {speed * 0.08F, -speed, 0.0F},
         .velocityJitter = {speed * 0.02F, speed * 0.1F, speed * 0.02F},
         .lifetime = height / speed});
    // Drizzle shows faintly, a downpour as a curtain.
    particles_.SetLook({.color = kDropColor * (0.3F + (0.7F * daylight)),
                        .opacity = level_ * (0.25F + (0.35F * level_)),
                        .width = viewDistance * kWidthPerDistance,
                        .stretch = (viewDistance * kStreakPerDistance) / speed});
    particles_.Update(std::min(seconds, 0.1F));
}

void RainEffect::Submit(RenderQueue& queue) const {
    if (level_ > 0.0F) {
        queue.SubmitParticles(particles_);
    }
}

}  // namespace mechapunk
