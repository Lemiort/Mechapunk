#pragma once

// The rules of building that ReviewConstruction (Construction.cpp) applies,
// kept in their own file so that module stays small: the hours of a new
// workplace, and the class, rent and name of the homes on a new habitat's
// floors.

#include <cstdint>
#include <string_view>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/World.hpp"

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

// A dormitory site is a habitat whose floors are all dorm rooms; its name
// starts with this, which is how `raiseFloor` tells it from a hab block
// without a new saved field.
inline constexpr std::string_view kDormitoryPrefix = "Dormitory";

[[nodiscard]] bool IsDormitorySite(const Building& building) noexcept;

// Whether the city is short of dorm beds: for `construction.streakNights`
// nights running, the beds of finished dormitories were filled up to within
// `construction.freeBedsPercent` of all of them. The robots that slept in
// dorms on a past night are `DailyStats::residentsByClass[0]`; the beds are
// counted as they are now, as the homes rule does with the market's homes.
[[nodiscard]] bool DormBedsShort(const WorldState& state, const RoomIndex& rooms);

}  // namespace mechapunk::sim::construction
