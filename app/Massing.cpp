#include "Massing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

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
                          .material = RoofMaterial::Glass});
        pieces.push_back({.x = left + tooth - 0.15F,
                          .y = top,
                          .z = 0.0F,
                          .sizeX = 0.3F,
                          .sizeY = rise,
                          .sizeZ = kFootprintDepthMeters - 2.0F,
                          .material = RoofMaterial::Steel});
    }
    return pieces;
}

std::uint32_t Hash(std::uint64_t id, std::uint32_t salt) {
    std::uint64_t hash = (id + 1) * 0x9E3779B97F4A7C15ULL + (salt * 0xC2B2AE3D27D4EB4FULL);
    hash ^= hash >> 31U;
    hash *= 0xD6E8FEB86659FD93ULL;
    hash ^= hash >> 29U;
    return static_cast<std::uint32_t>(hash >> 16U);
}

// Four slabs round a box at height `y`, `outset` meters outside its walls
// (inside when negative): a cornice, or a parapet.
void Ring(std::vector<RoofPiece>& pieces, const MassBox& box, float y, float height,
          float outset, float thick, RoofMaterial material) {
    const float width = (box.x1 - box.x0) + (2.0F * outset);
    const float depth = (box.z1 - box.z0) + (2.0F * outset);
    const float cx = (box.x0 + box.x1) * 0.5F;
    const float cz = (box.z0 + box.z1) * 0.5F;
    for (const float side : {1.0F, -1.0F}) {
        pieces.push_back({.x = cx, .y = y, .z = cz + (side * ((depth - thick) * 0.5F)),
                          .sizeX = width, .sizeY = height, .sizeZ = thick, .material = material});
        pieces.push_back({.x = cx + (side * ((width - thick) * 0.5F)), .y = y, .z = cz,
                          .sizeX = thick, .sizeY = height, .sizeZ = depth - (2.0F * thick),
                          .material = material});
    }
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
                        {-kHalfWidth, -kTwinGapHalfMeters, -kHalfDepth, kHalfDepth, base, floors, 1U << 3U},
                        {kTwinGapHalfMeters, kHalfWidth, -kHalfDepth, kHalfDepth, base, floors, 1U << 2U}};
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

WindowRhythm WindowRhythmOf(sim::BuildingStyle style) {
    switch (style) {
        case sim::BuildingStyle::Gabled:
            return {.spacingMeters = 2.8F};
        case sim::BuildingStyle::Sawtooth:
            return {.spacingMeters = 4.5F};
        case sim::BuildingStyle::Setback:
        case sim::BuildingStyle::Stepped:
        case sim::BuildingStyle::TwinTowers:
        case sim::BuildingStyle::Plinth:
        case sim::BuildingStyle::Tower:
            return {.spacingMeters = 3.5F, .bandEvery = 3};
        case sim::BuildingStyle::Plain:
            break;
    }
    return {};
}

std::vector<RoofPiece> DetailOf(sim::BuildingStyle style, std::uint32_t floors, std::uint64_t id) {
    std::vector<RoofPiece> pieces;
    if (style == sim::BuildingStyle::Plain) {
        return pieces;
    }
    const std::vector<MassBox> boxes = MassingOf(style, floors);
    const bool flat = style != sim::BuildingStyle::Gabled && style != sim::BuildingStyle::Sawtooth;
    for (const MassBox& box : boxes) {
        const float top = static_cast<float>(box.endFloor) * kFloorHeightMeters;
        // A ledge under every roof, which is also the setback of the box above.
        Ring(pieces, box, top - 0.3F, 0.3F, 0.4F, 0.5F, RoofMaterial::Concrete);
        if (flat) {
            Ring(pieces, box, top, 0.9F, -0.1F, 0.3F, RoofMaterial::Concrete);
        }
    }
    // Bay windows on both street faces, every second floor of the first
    // box, at a third and two thirds of its width.
    if (style == sim::BuildingStyle::Setback || style == sim::BuildingStyle::Gabled) {
        const MassBox& box = boxes.front();
        for (std::uint32_t floor = 1; floor < box.endFloor; floor += 2) {
            for (const float side : {1.0F, -1.0F}) {
                for (const float share : {-1.0F / 6.0F, 1.0F / 6.0F}) {
                    pieces.push_back({.x = (box.x1 - box.x0) * share,
                                      .y = (static_cast<float>(floor) * kFloorHeightMeters) + 0.6F,
                                      .z = side * (kHalfDepth + 0.35F),
                                      .sizeX = 2.6F, .sizeY = 2.0F, .sizeZ = 0.8F,
                                      .material = RoofMaterial::Window});
                }
            }
        }
    }
    // Columns along the ground floor of a plinth.
    if (style == sim::BuildingStyle::Plinth) {
        for (const float side : {1.0F, -1.0F}) {
            for (int column = -2; column <= 2; ++column) {
                pieces.push_back({.x = static_cast<float>(column) * 5.0F,
                                  .y = 0.0F,
                                  .z = side * (kHalfDepth + 0.2F),
                                  .sizeX = 0.5F, .sizeY = kFloorHeightMeters, .sizeZ = 0.5F,
                                  .material = RoofMaterial::Concrete});
            }
        }
    }
    // Roof clutter on the highest flat roof: a tank, vent stacks, an aerial
    // with a beacon and a dish. A tower's crown takes the middle, so its
    // clutter goes to the sides.
    if (flat) {
        const MassBox& roof = style == sim::BuildingStyle::Tower
                                  ? boxes.front()
                                  : *std::ranges::max_element(
                                        boxes, {}, &MassBox::endFloor);
        const float top = static_cast<float>(roof.endFloor) * kFloorHeightMeters;
        const auto spot = [&](std::uint32_t salt) {
            const float side = (Hash(id, salt) % 2 == 0) ? 1.0F : -1.0F;
            const float across = static_cast<float>(Hash(id, salt + 50) % 100) / 100.0F;
            const float x = style == sim::BuildingStyle::Tower
                                ? side * (6.0F + (across * ((roof.x1 - 1.5F) - 6.0F)))
                                : roof.x0 + 1.5F + (across * ((roof.x1 - roof.x0) - 3.0F));
            const float z = roof.z0 + 1.5F +
                            (static_cast<float>(Hash(id, salt + 99) % 100) / 100.0F *
                             ((roof.z1 - roof.z0) - 3.0F));
            return std::pair{x, z};
        };
        const auto tank = spot(1);
        pieces.push_back({.x = tank.first, .y = top, .z = tank.second, .sizeX = 0.4F,
                          .sizeY = 1.0F, .sizeZ = 0.4F, .material = RoofMaterial::Steel});
        pieces.push_back({.x = tank.first, .y = top + 1.0F, .z = tank.second, .sizeX = 2.2F,
                          .sizeY = 2.2F, .sizeZ = 2.2F, .material = RoofMaterial::Steel});
        const std::uint32_t vents = 1 + (Hash(id, 2) % 3);
        for (std::uint32_t vent = 0; vent < vents; ++vent) {
            const auto at = spot(10 + vent);
            pieces.push_back({.x = at.first, .y = top, .z = at.second, .sizeX = 0.6F,
                              .sizeY = 1.2F + static_cast<float>(Hash(id, 20 + vent) % 3) * 0.4F,
                              .sizeZ = 0.6F, .material = RoofMaterial::Steel});
        }
        const auto aerial = spot(30);
        pieces.push_back({.x = aerial.first, .y = top, .z = aerial.second, .sizeX = 0.12F,
                          .sizeY = 5.0F, .sizeZ = 0.12F, .material = RoofMaterial::Steel});
        pieces.push_back({.x = aerial.first, .y = top + 5.0F, .z = aerial.second, .sizeX = 0.3F,
                          .sizeY = 0.3F, .sizeZ = 0.3F, .material = RoofMaterial::Beacon});
        if (Hash(id, 3) % 2 == 0) {
            const auto dish = spot(40);
            pieces.push_back({.x = dish.first, .y = top, .z = dish.second, .sizeX = 0.2F,
                              .sizeY = 1.2F, .sizeZ = 0.2F, .material = RoofMaterial::Steel});
            pieces.push_back({.x = dish.first, .y = top + 1.2F, .z = dish.second, .sizeX = 1.6F,
                              .sizeY = 0.15F, .sizeZ = 1.6F, .rollXDegrees = 40.0F,
                              .material = RoofMaterial::Concrete});
        }
    }
    return pieces;
}

float SignHeightMeters(sim::BuildingStyle style, std::uint32_t floors) {
    const float height = static_cast<float>(floors) * kFloorHeightMeters;
    const float street =
        static_cast<float>(MassingOf(style, floors).front().endFloor) * kFloorHeightMeters;
    return std::min(height * 0.6F, street - 1.0F);
}

}  // namespace mechapunk
