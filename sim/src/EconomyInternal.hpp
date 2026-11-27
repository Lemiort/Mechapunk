#pragma once

// Constants and helpers shared by the source files that implement the economy.
// Private to the source files that implement the economy (Economy.cpp,
// Money.cpp, Founding.cpp, Units.cpp, Property.cpp, Settlement.cpp,
// EconomyStatistics.cpp): never include it from anywhere else.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {
namespace economy_detail {

}  // namespace economy_detail
using namespace economy_detail;
}  // namespace mechapunk::sim
