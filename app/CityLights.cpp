// Street and window lights, and how they follow the time of day.

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

// Signs glow by themselves; their lights only spill onto nearby walls and
// pavement.
constexpr float kNeonPower = 0.03F;

constexpr vec3 kSunColor{1.0F, 0.86F, 0.7F};

constexpr vec3 kDaySky{0.45F, 0.47F, 0.55F};

constexpr vec3 kNightSky{0.04F, 0.04F, 0.1F};

// Faint light from above while a building is open, so dark rooms still read.
constexpr vec3 kInspectionLight{0.18F, 0.18F, 0.21F};

constexpr float kLampPower = 0.009F;

// Where unused lights wait, far under the city.
constexpr float kHiddenDepth = -1000.0F;

constexpr float kStreetLampCutoff = 62.0F;

constexpr vec3 kStreetLampColor{1.2F, 0.94F, 0.6F};

// Lights kept lit nearest the camera's focus; every one costs two passes of
// the deferred renderer. Glowing signs and lamp bulbs still show far away.
constexpr std::size_t kNeonLightBudget = 96;

constexpr std::size_t kStreetLampBudget = 64;

// The farthest quarter of a budget fades out, so lights don't pop as the
// camera moves.
constexpr float kBudgetFade = 0.25F;

// How bright each of `positions` should be: 1 for the `budget` nearest
// `focus` (ground distance), fading over the farthest of them, 0 beyond.
std::vector<float> NearestWeights(std::span<const vec3> positions, vec3 focus,
                                  std::size_t budget) {
    std::vector<float> weights(positions.size(), 0.0F);
    std::vector<std::pair<float, std::size_t>> byDistance;
    byDistance.reserve(positions.size());
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const vec2 offset{positions[index].x - focus.x, positions[index].z - focus.z};
        byDistance.emplace_back(glm::dot(offset, offset), index);
    }
    const std::size_t kept = std::min(budget, byDistance.size());
    std::ranges::partial_sort(byDistance, byDistance.begin() + static_cast<std::ptrdiff_t>(kept));
    const auto fadeFrom = static_cast<std::size_t>(static_cast<float>(budget) * (1.0F - kBudgetFade));
    for (std::size_t rank = 0; rank < kept; ++rank) {
        weights[byDistance[rank].second] =
            rank < fadeFrom ? 1.0F
                            : 1.0F - (static_cast<float>(rank - fadeFrom + 1) /
                                      static_cast<float>(budget - fadeFrom + 1));
    }
    return weights;
}

}  // namespace

void CityView::AddLights(Scene& scene) {
    scene.AddDirectionalLight(DirectionalLight(0.0F, -1.0F, 0.0F, 0.0F, 0.0F,
                                               0.0F));  // sun
    scene.AddDirectionalLight(DirectionalLight(0.4F, -1.0F, 0.6F, 0.0F, 0.0F,
                                               0.0F));  // sky fill
    scene.AddDirectionalLight(DirectionalLight(-0.3F, -1.0F, -0.4F, 0.0F, 0.0F,
                                               0.0F));  // inspection
    SyncLights(scene);
    lampPoolStart_ = scene.GetPointLights().size();
    for (std::size_t lamp = 0; lamp < kLampPool; ++lamp) {
        scene.AddPointLight(PointLight(0.0F, kHiddenDepth, 0.0F, 0.0F, 0.0F,
                                       0.0F, kLampPower));
    }
    for (const vec3& bulb : streetLamps_) {
        scene.AddSpotLight(SpotLight(bulb.x, 0.0F, bulb.z, 0.0F, 0.0F, 0.0F,
                                     bulb.x, bulb.y, bulb.z, kStreetLampCutoff));
    }
}

void CityView::SyncLights(Scene& scene) {
    for (NeonSign& sign : neonSigns_) {
        if (sign.lit) {
            continue;
        }
        sign.lit = true;
        const Neon fresh{.lightIndex = scene.GetPointLights().size(),
                         .color = sign.color,
                         .building = sign.building,
                         .position = sign.position,
                         .unit = sign.unit};
        const auto spare = std::ranges::find_if(neons_, [](const Neon& neon) { return neon.retired; });
        if (spare != neons_.end()) {
            const std::size_t light = spare->lightIndex;
            *spare = fresh;
            spare->lightIndex = light;
            continue;
        }
        neons_.push_back(fresh);
        scene.AddPointLight(PointLight(sign.position.x, sign.position.y, sign.position.z,
                                       sign.color.r, sign.color.g, sign.color.b, kNeonPower));
    }
}

void CityView::UpdateLights(Scene& scene, double minuteOfDay,
                            const Selection& selection, vec3 focus,
                            float overcast) const {
    const float sunHeight = SunHeight(minuteOfDay);
    const float daylight = DaylightAt(minuteOfDay);
    // Clouds hide most of the sun and gray the sky.
    const float sun = 1.0F - (0.75F * overcast);
    const float sky = 1.0F - (0.35F * overcast);
    const auto directional = scene.GetDirectionalLights();
    if (directional.size() >= 3) {
        // The sun travels from east (+x) to west (-x) across the south.
        const double phase = std::asin(std::clamp(sunHeight, -1.0F, 1.0F));
        const bool morning = minuteOfDay < 13.0 * 60.0;
        const auto horizontal =
            static_cast<float>(std::cos(phase) * (morning ? 1.0 : -1.0));
        directional[0].SetDirection(
            -glm::normalize(vec3{horizontal, std::max(sunHeight, 0.05F), 0.35F}));
        directional[0].SetColor(kSunColor * std::max(sunHeight, 0.0F) * 1.1F * sun);
        directional[1].SetColor(glm::mix(kNightSky, kDaySky, daylight) * sky);
        // Brighter at night, when the sky no longer lights the rooms.
        directional[2].SetColor(selection.building.IsValid()
                                    ? kInspectionLight * (2.0F - daylight)
                                    : vec3(0.0F));
    }
    // Neon dims in daylight but never goes out. Lights shine through walls,
    // so the open building's own signs are off while it is inspected.
    const float neon = 1.0F - (0.75F * daylight);
    assets_.palette->SetGlow(neon);
    const auto points = scene.GetPointLights();
    std::vector<vec3> neonPositions;
    neonPositions.reserve(neons_.size());
    for (const Neon& sign : neons_) {
        // Units that are no venue now keep their light far underground.
        const bool dark = sign.retired || (sign.unit.IsValid() && !UnitIsVenue(sign.unit));
        neonPositions.push_back(dark ? vec3{0.0F, kHiddenDepth, 0.0F} : sign.position);
    }
    const std::vector<float> neonWeights =
        NearestWeights(neonPositions, focus, kNeonLightBudget);
    for (std::size_t index = 0; index < neons_.size(); ++index) {
        const Neon& sign = neons_[index];
        if (sign.lightIndex < points.size()) {
            const bool inspected = selection.building.IsValid() &&
                                   sign.building == selection.building;
            const vec3 color = sign.unit.IsValid() ? UnitColor(sign.unit) : sign.color;
            points[sign.lightIndex].SetPos(neonPositions[index]);
            points[sign.lightIndex].SetColor(
                inspected || sign.retired ? vec3(0.0F) : color * neon * neonWeights[index]);
        }
    }
    for (std::size_t lamp = 0; lamp < kLampPool; ++lamp) {
        const std::size_t index = lampPoolStart_ + lamp;
        if (index >= points.size()) {
            break;
        }
        if (lamp < litLamps_.size()) {
            points[index].SetPos(litLamps_[lamp].position);
            points[index].SetColor(litLamps_[lamp].color);
        } else {
            points[index].SetPos({0.0F, kHiddenDepth, 0.0F});
            points[index].SetColor(vec3(0.0F));
        }
    }
    // Street lamps come on at dusk. By day they point nowhere, which the
    // renderer skips.
    const vec3 street = kStreetLampColor * (1.0F - daylight);
    const bool lit = daylight < 1.0F;
    const auto spots = scene.GetSpotLights();
    std::vector<vec3> bulbs;
    bulbs.reserve(spots.size());
    for (const SpotLight& lamp : spots) {
        bulbs.push_back(lamp.GetPos());
    }
    const std::vector<float> lampWeights =
        NearestWeights(bulbs, focus, kStreetLampBudget);
    for (std::size_t index = 0; index < spots.size(); ++index) {
        SpotLight& lamp = spots[index];
        const vec3 bulb = bulbs[index];
        lamp.SetColor(street * lampWeights[index]);
        lamp.SetTarget(lit ? vec3{bulb.x, 0.0F, bulb.z} : bulb);
    }
}

}  // namespace mechapunk
