#pragma once

// The save migrations this game still reads, one per version step. Each edits
// the untyped JSON of the `world` object and never calls game code. None
// now: version 36 is the oldest supported save.

#include <expected>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

}  // namespace mechapunk::sim::migrations
