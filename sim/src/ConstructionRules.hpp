#pragma once

// The rules of building that ReviewConstruction (Construction.cpp) applies,
// kept in their own file so that module stays small: the hours of a new
// workplace, and the class, rent and name of the homes on a new habitat's
// floors.

#include <cstdint>
#include <string_view>

#include "mechapunk/sim/City.hpp"

namespace mechapunk::sim::construction {

// The hours a new workplace's staff stand: an assembly line's shift, the
// plainest of generation's, so a new job is not the easiest in the city.
inline constexpr Shift kSiteShift{.startMinute = 7 * 60, .lengthMinutes = 480, .wage = 100};

// The class of homes on a floor of a new habitat: studios low, flats in
// the middle, two-bedroom flats near the top, the rule generation uses for
// a habitat of the same finished height.
[[nodiscard]] ApartmentClass HomeClassFor(std::uint32_t floor, std::uint32_t floors) noexcept;

// Rent of a class over the floor's base rent, and the word its rooms are
// named with: copies of generation's file-local helpers, so a home raised
// now reads and costs like a home raised with the city.
[[nodiscard]] std::uint32_t RentPercent(ApartmentClass kind) noexcept;

[[nodiscard]] std::string_view HomeLabel(ApartmentClass kind) noexcept;

}  // namespace mechapunk::sim::construction
