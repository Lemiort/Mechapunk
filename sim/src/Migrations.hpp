#pragma once

#include <expected>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

// Version 1 to 2: buildings gain floors and rooms, robots get an apartment,
// a workspace, needs and credits.
//
// Every migration is frozen once released. It works on the untyped JSON of
// the save and must never call current game code (generators, rules), which
// will keep changing while old saves must keep loading the same way.
[[nodiscard]] std::expected<void, std::string> V1ToV2(SaveDocument& world);

}  // namespace mechapunk::sim::migrations
