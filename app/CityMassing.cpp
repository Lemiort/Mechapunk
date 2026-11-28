// The walls and roof of a finished building by its style (task I, step I2).
// The geometry itself is in Massing.cpp; this turns it into statics.

#include "CityViewInternal.hpp"
#include "Massing.hpp"

namespace mechapunk {
namespace {

constexpr vec3 kTile{0.24F, 0.17F, 0.16F};
constexpr vec3 kSteel{0.26F, 0.28F, 0.32F};
constexpr vec3 kRoofLight{0.55F, 0.7F, 0.8F};
constexpr vec3 kPaleConcrete{0.42F, 0.43F, 0.46F};
constexpr vec3 kWindowGlass{0.13F, 0.17F, 0.24F};
constexpr vec3 kBeaconRed{1.0F, 0.1F, 0.1F};

}  // namespace

std::vector<CityView::StaticItem> CityView::MassingBlocks(const sim::Building& building,
                                                          vec3 center) const {
    const std::shared_ptr<const Material> material =
        building.kind == sim::BuildingKind::Habitat ? assets_.habitat : assets_.workplace;
    std::vector<StaticItem> blocks;
    for (const MassBox& box : MassingOf(building.style, building.floors)) {
        const float first = static_cast<float>(box.firstFloor) * kFloorHeight;
        const float end = static_cast<float>(box.endFloor) * kFloorHeight;
        blocks.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = center + vec3{(box.x0 + box.x1) * 0.5F * kS, first,
                                                     (box.z0 + box.z1) * 0.5F * kS},
                           .rotationDegrees = vec3(0.0F),
                           .scale = {(box.x1 - box.x0) * kS, end - first,
                                     (box.z1 - box.z0) * kS}},
             .material = material});
    }
    return blocks;
}

// A building going up: its finished floors as bare concrete plates on columns,
// wrapped in scaffolding (poles a little out from the walls, a plank deck and a
// guard rail at every floor) that rises with it, and a tower crane beside it.
// All lengths in meters.
void CityView::AddScaffold(std::uint32_t floors, vec3 center) {
    constexpr vec3 kScaffoldSteel{0.78F, 0.56F, 0.14F};
    constexpr vec3 kPlank{0.42F, 0.31F, 0.18F};
    constexpr float kOut = 1.3F;          // Scaffold poles out from the walls.
    constexpr float kDeck = 0.9F;         // Width of the plank deck.
    constexpr float kPole = 0.12F;
    constexpr float kPoleSpacing = 3.0F;
    constexpr float kRail = 1.0F;         // Guard rail over each deck.
    const float halfW = kFootprintWidthMeters * 0.5F;
    const float halfD = kFootprintDepthMeters * 0.5F;
    const float floorMeters = kFloorHeight / kS;
    const float top = static_cast<float>(floors) * floorMeters;
    const auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, vec3 color) {
        AddStaticBox(center + (vec3{x0, y0, z0} * kS), center + (vec3{x1, y1, z1} * kS), color);
    };

    // The floor plates, the lowest just clear of the ground so the two never
    // share a plane, and the columns that carry them.
    for (std::uint32_t floor = 0; floor < floors; ++floor) {
        const float base = (static_cast<float>(floor) * floorMeters) + (floor == 0 ? 0.03F : 0.0F);
        box(-halfW, base, -halfD, halfW, base + kSlabMeters, halfD, kConcrete);
    }
    for (const float x : {-halfW + 0.3F, 0.0F, halfW - 0.8F}) {
        for (const float z : {-halfD + 0.3F, halfD - 0.8F}) {
            box(x, 0.0F, z, x + 0.5F, top, z + 0.5F, kConcrete);
        }
    }

    // Poles along the four sides, out from the walls.
    const float outerW = halfW + kOut;
    const float outerD = halfD + kOut;
    const auto poles = [&](float from, float to, bool alongX, float at) {
        const int count = std::max(2, static_cast<int>((to - from) / kPoleSpacing) + 1);
        for (int index = 0; index < count; ++index) {
            const float t = from + ((to - from) * static_cast<float>(index) / static_cast<float>(count - 1));
            const float x = alongX ? t : at;
            const float z = alongX ? at : t;
            box(x - (kPole * 0.5F), 0.0F, z - (kPole * 0.5F), x + (kPole * 0.5F), top + kRail,
                z + (kPole * 0.5F), kScaffoldSteel);
        }
    };
    poles(-outerW, outerW, true, -outerD);
    poles(-outerW, outerW, true, outerD);
    poles(-outerD, outerD, false, -outerW);
    poles(-outerD, outerD, false, outerW);

    // A plank deck and a guard rail at every floor, up to the one going up now.
    for (std::uint32_t level = 1; level <= floors; ++level) {
        const float y = static_cast<float>(level) * floorMeters;
        const float inW = outerW - kDeck;
        const float inD = outerD - kDeck;
        box(-outerW, y, -outerD, outerW, y + 0.06F, -inD, kPlank);
        box(-outerW, y, inD, outerW, y + 0.06F, outerD, kPlank);
        box(-outerW, y, -inD, -inW, y + 0.06F, inD, kPlank);
        box(inW, y, -inD, outerW, y + 0.06F, inD, kPlank);
        const float rail = y + kRail;
        box(-outerW, rail, -outerD - 0.04F, outerW, rail + 0.08F, -outerD + 0.04F, kScaffoldSteel);
        box(-outerW, rail, outerD - 0.04F, outerW, rail + 0.08F, outerD + 0.04F, kScaffoldSteel);
        box(-outerW - 0.04F, rail, -outerD, -outerW + 0.04F, rail + 0.08F, outerD, kScaffoldSteel);
        box(outerW - 0.04F, rail, -outerD, outerW + 0.04F, rail + 0.08F, outerD, kScaffoldSteel);
    }

    // A tower crane at the east side, its jib reaching over the building and a
    // counterweight behind; it stands a few meters over the floors going up.
    const float mastX = outerW + 2.5F;
    const float craneTop = top + 8.0F;
    box(mastX - 0.8F, 0.0F, -0.8F, mastX + 0.8F, craneTop, 0.8F, kScaffoldSteel);
    box(-halfW, craneTop, -0.5F, mastX + 0.8F, craneTop + 1.0F, 0.5F, kScaffoldSteel);
    box(mastX + 0.8F, craneTop, -0.5F, mastX + 8.0F, craneTop + 1.0F, 0.5F, kScaffoldSteel);
    box(mastX + 5.5F, craneTop - 2.0F, -1.0F, mastX + 8.0F, craneTop, 1.0F, kConcrete);
    box(mastX - 1.2F, craneTop - 2.2F, 0.8F, mastX + 0.8F, craneTop - 0.2F, 2.4F, kScaffoldSteel);
    // The hook's cable and its load, hanging over the middle of the building.
    box(-0.05F, top + 2.0F, -0.05F, 0.05F, craneTop, 0.05F, kConcrete);
    box(-1.0F, top + 1.2F, -0.6F, 1.0F, top + 2.0F, 0.6F, kPlank);
}

void CityView::AddRoof(const sim::Building& building, vec3 center) {
    const auto material = [this](RoofMaterial kind) -> std::shared_ptr<const Material> {
        switch (kind) {
            case RoofMaterial::Steel:
                return assets_.palette->Get(kSteel);
            case RoofMaterial::Concrete:
                return assets_.palette->Get(kPaleConcrete);
            case RoofMaterial::Glass:
                return assets_.palette->GetGlowing(kRoofLight, kRoofLight * 0.5F);
            case RoofMaterial::Window:
                return assets_.palette->Get(kWindowGlass);
            case RoofMaterial::Beacon:
                return assets_.palette->GetGlowing(kBeaconRed, kBeaconRed * kGlow);
            case RoofMaterial::Tile:
                break;
        }
        return assets_.palette->Get(kTile);
    };
    std::vector<RoofPiece> pieces = RoofOf(building.style, building.floors);
    const std::vector<RoofPiece> detail =
        DetailOf(building.style, building.floors, building.id.value);
    pieces.insert(pieces.end(), detail.begin(), detail.end());
    for (const RoofPiece& piece : pieces) {
        const vec3 at = center + (vec3{piece.x, piece.y, piece.z} * kS);
        if (piece.material == RoofMaterial::Beacon) {
            // The aerial's red light, which shines with the other neon.
            neonSigns_.push_back({.position = at + vec3{0.0F, 0.5F * kS, 0.0F},
                                  .color = kBeaconRed,
                                  .building = building.id});
        }
        statics_.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = at,
                           .rotationDegrees = {piece.rollXDegrees, 0.0F, piece.rollZDegrees},
                           .scale = vec3{piece.sizeX, piece.sizeY, piece.sizeZ} * kS},
             .material = material(piece.material)});
    }
}

}  // namespace mechapunk
