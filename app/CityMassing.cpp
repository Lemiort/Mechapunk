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
        statics_.push_back(
            {.mesh = assets_.cube.get(),
             .transform = {.position = center + (vec3{piece.x, piece.y, piece.z} * kS),
                           .rotationDegrees = {piece.rollXDegrees, 0.0F, piece.rollZDegrees},
                           .scale = vec3{piece.sizeX, piece.sizeY, piece.sizeZ} * kS},
             .material = material(piece.material)});
    }
}

}  // namespace mechapunk
