#include "Massing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Interior.hpp"

namespace mechapunk {
namespace {

constexpr float kHalfWidth = kFootprintWidthMeters * 0.5F;
constexpr float kHalfDepth = kFootprintDepthMeters * 0.5F;

// The footprint pulled in by `insetX` and `insetZ` on every side.
MassBox Inset(float insetX, float insetZ, std::uint32_t first, std::uint32_t end) {
    return {.x0 = -kHalfWidth + insetX,
            .x1 = kHalfWidth - insetX,
            .z0 = -kHalfDepth + insetZ,
            .z1 = kHalfDepth - insetZ,
            .firstFloor = first,
            .endFloor = end};
}

constexpr float kSetbackMeters = 1.5F;
constexpr float kTwinGapHalfMeters = 3.0F;
constexpr float kRoofSlabMeters = 0.3F;
constexpr float kGablePitchDegrees = 20.0F;
constexpr float kSawtoothPitchDegrees = 20.0F;
constexpr int kSawteeth = 4;

float Radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0F;
}

// A pitched roof with its ridge along x: two slabs meeting over the middle,
// and the gable walls at both ends, stepped in three layers.
std::vector<RoofPiece> GabledRoof(float top) {
    const float tangent = std::tan(Radians(kGablePitchDegrees));
    const float rise = kHalfDepth * tangent;
    const float slope = (kHalfDepth / std::cos(Radians(kGablePitchDegrees))) + 0.5F;
    std::vector<RoofPiece> pieces;
    for (const float side : {1.0F, -1.0F}) {
        pieces.push_back({.x = 0.0F,
                          .y = top + (rise * 0.5F),
                          .z = side * kHalfDepth * 0.5F,
                          .sizeX = kFootprintWidthMeters + 1.0F,
                          .sizeY = kRoofSlabMeters,
                          .sizeZ = slope,
                          .rollXDegrees = side * kGablePitchDegrees});
    }
    for (const float end : {1.0F, -1.0F}) {
        for (int layer = 0; layer < 3; ++layer) {
            const float share = 1.0F - (static_cast<float>(layer) / 3.0F);
            pieces.push_back({.x = end * (kHalfWidth - 0.15F),
                              .y = top + (rise * static_cast<float>(layer) / 3.0F),
                              .z = 0.0F,
                              .sizeX = 0.3F,
                              .sizeY = rise / 3.0F,
                              .sizeZ = kFootprintDepthMeters * share});
        }
    }
    return pieces;
}

// A row of teeth along x: each a tilted pane of glass rising toward +x and a
// steel wall closing its high side.
std::vector<RoofPiece> SawtoothRoof(float top) {
    const float tooth = kFootprintWidthMeters / static_cast<float>(kSawteeth);
    const float rise = tooth * std::tan(Radians(kSawtoothPitchDegrees));
    const float slope = tooth / std::cos(Radians(kSawtoothPitchDegrees));
    std::vector<RoofPiece> pieces;
    for (int index = 0; index < kSawteeth; ++index) {
        const float left = -kHalfWidth + (tooth * static_cast<float>(index));
        pieces.push_back({.x = left + (tooth * 0.5F),
                          .y = top + (rise * 0.5F),
                          .z = 0.0F,
                          .sizeX = slope,
                          .sizeY = 0.25F,
                          .sizeZ = kFootprintDepthMeters - 2.0F,
                          .rollZDegrees = kSawtoothPitchDegrees,
                          .glass = true});
        pieces.push_back({.x = left + tooth - 0.15F,
                          .y = top,
                          .z = 0.0F,
                          .sizeX = 0.3F,
                          .sizeY = rise,
                          .sizeZ = kFootprintDepthMeters - 2.0F});
    }
    return pieces;
}

}  // namespace

std::vector<MassBox> MassingOf(sim::BuildingStyle style, std::uint32_t floors) {
    const MassBox whole = Inset(0.0F, 0.0F, 0, floors);
    switch (style) {
        case sim::BuildingStyle::Setback:
            if (floors >= 3) {
                const std::uint32_t top = std::max(1U, floors / 3);
                return {Inset(0.0F, 0.0F, 0, floors - top),
                        Inset(kSetbackMeters, kSetbackMeters, floors - top, floors)};
            }
            break;
        case sim::BuildingStyle::Stepped:
            if (floors >= 3) {
                const std::uint32_t top = std::max(1U, floors / 3);
                const std::uint32_t base = floors - (2 * top);
                return {Inset(0.0F, 0.0F, 0, base),
                        Inset(kSetbackMeters, kSetbackMeters, base, floors - top),
                        Inset(2 * kSetbackMeters, 2 * kSetbackMeters, floors - top, floors)};
            }
            break;
        case sim::BuildingStyle::TwinTowers:
            if (floors >= 2) {
                const std::uint32_t base = std::min(2U, floors - 1);
                return {Inset(0.0F, 0.0F, 0, base),
                        {-kHalfWidth, -kTwinGapHalfMeters, -kHalfDepth, kHalfDepth, base, floors},
                        {kTwinGapHalfMeters, kHalfWidth, -kHalfDepth, kHalfDepth, base, floors}};
            }
            break;
        case sim::BuildingStyle::Plinth:
            if (floors >= 3) {
                return {Inset(0.0F, 0.0F, 0, 2), Inset(2.5F, 2.0F, 2, floors)};
            }
            break;
        case sim::BuildingStyle::Tower:
            return {whole, {-4.0F, 4.0F, -4.0F, 4.0F, floors, floors + 1}};
        case sim::BuildingStyle::Plain:
        case sim::BuildingStyle::Gabled:
        case sim::BuildingStyle::Sawtooth:
            break;
    }
    return {whole};
}

std::vector<RoofPiece> RoofOf(sim::BuildingStyle style, std::uint32_t floors) {
    const float top = static_cast<float>(floors) * kFloorHeightMeters;
    switch (style) {
        case sim::BuildingStyle::Gabled:
            return GabledRoof(top);
        case sim::BuildingStyle::Sawtooth:
            return SawtoothRoof(top);
        default:
            return {};
    }
}

float SignHeightMeters(sim::BuildingStyle style, std::uint32_t floors) {
    const float height = static_cast<float>(floors) * kFloorHeightMeters;
    const float street =
        static_cast<float>(MassingOf(style, floors).front().endFloor) * kFloorHeightMeters;
    return std::min(height * 0.6F, street - 1.0F);
}

}  // namespace mechapunk
