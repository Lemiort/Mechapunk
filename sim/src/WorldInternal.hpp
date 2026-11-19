#pragma once

// Constants and helpers shared by the source files that implement World.
// Private to the World*.cpp, Behaviour.cpp, Generation.cpp, Residents.cpp,
// Describe.cpp and Validation.cpp parts of World: never include it from
// anywhere else.

#include "mechapunk/sim/World.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Names.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk::sim {
namespace world_detail {

// Needs change every this many minutes.
constexpr Tick kNeedPeriod = 10;

// Need changes per ten minutes of each activity; visits add venue effects.
// Resting at home does not wear a robot down.
constexpr NeedEffects kResting{.charge = 25, .condition = 0, .social = -3,
                               .fun = -2};

}  // namespace world_detail
using namespace world_detail;
}  // namespace mechapunk::sim
