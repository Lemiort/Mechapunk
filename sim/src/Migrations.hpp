#pragma once

// The save migrations this game still reads, one per version step. Each edits
// the untyped JSON of the `world` object and never calls game code.

#include <expected>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

// Version 27 to 28: the simulation's settings, saved with the city.
[[nodiscard]] std::expected<void, std::string> V27ToV28(SaveDocument& world);

// Version 28 to 29: the rest of the simulation's settings.
[[nodiscard]] std::expected<void, std::string> V28ToV29(SaveDocument& world);

}  // namespace mechapunk::sim::migrations
