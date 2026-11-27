#pragma once

// The shape of a building by its style (task I, step I2): which boxes make
// up its walls and which pieces its roof. Pure geometry in meters from the
// middle of the footprint, so tests can check it without a renderer. Every
// box and piece lies inside the footprint of Interior.hpp, so picking, the
// open floor and the interiors work as they did for a plain slab.

#include <cstdint>
#include <vector>

#include "mechapunk/sim/City.hpp"

namespace mechapunk {

// One block of wall, from the floor `firstFloor` up to (not including)
// `endFloor`; the Tower's crown goes one floor above the building.
struct MassBox {
    float x0{};
    float x1{};
    float z0{};
    float z1{};
    std::uint32_t firstFloor{};
    std::uint32_t endFloor{};
    // Faces left without windows, bit (alongX ? 0 : 2) + (offset > 0): the
    // faces of twin towers that look into the gap.
    std::uint32_t blankFaces{};
};

// How a style spaces its windows: a dormitory block packs them, a tower
// leaves a blank band every `bandEvery` floors (0 for none).
struct WindowRhythm {
    float spacingMeters{3.5F};
    std::uint32_t bandEvery{};
};

enum class RoofMaterial : std::uint8_t { Tile, Steel, Concrete, Glass, Window, Beacon };

// A slab of roof: its middle of the underside at (x, y, z), y up from the
// ground, tilted about the x axis or the z axis. `glass` pieces are the
// workshops' roof lights; the rest is tile or steel.
struct RoofPiece {
    float x{};
    float y{};
    float z{};
    float sizeX{};
    float sizeY{};
    float sizeZ{};
    float rollXDegrees{};
    float rollZDegrees{};
    RoofMaterial material{RoofMaterial::Tile};
};

// The walls of a building of `floors` floors in `style`. The first box is
// always the base that reaches the street across the whole footprint from
// the ground floor. A style the floor count is too low for falls back to
// the plain box.
[[nodiscard]] std::vector<MassBox> MassingOf(sim::BuildingStyle style,
                                             std::uint32_t floors);

// The roof of that building, empty for a flat one.
[[nodiscard]] std::vector<RoofPiece> RoofOf(sim::BuildingStyle style,
                                            std::uint32_t floors);

[[nodiscard]] WindowRhythm WindowRhythmOf(sim::BuildingStyle style);

// Everything else a style adds to its walls and roof (task I, step I3):
// ledges and parapets, bay windows, the arcade of a plinth and roof clutter.
// Placed by a hash of `id`, never by a random draw, so two runs look alike.
[[nodiscard]] std::vector<RoofPiece> DetailOf(sim::BuildingStyle style,
                                              std::uint32_t floors, std::uint64_t id);

// How high the street sign hangs, in meters: 60% of the height, but never
// above the wall that reaches the street, or it would float in the air.
[[nodiscard]] float SignHeightMeters(sim::BuildingStyle style,
                                     std::uint32_t floors);

}  // namespace mechapunk
