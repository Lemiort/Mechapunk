#pragma once

// Helpers shared by the frozen save migrations.
// Private to the Migrations*.cpp files: never include it from anywhere else.

#include "Migrations.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {
namespace migration_detail {

using Array = SaveDocument::array_t;

using Object = SaveDocument::object_t;

}  // namespace migration_detail
using namespace migration_detail;
}  // namespace mechapunk::sim::migrations
