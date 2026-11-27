#include "ConstructionRules.hpp"

#include <cstddef>

#include "mechapunk/sim/Construction.hpp"

namespace mechapunk::sim::construction {

[[nodiscard]] ApartmentClass HomeClassFor(std::uint32_t floor,
                                          std::uint32_t floors) noexcept {
    if (floor * 3 <= floors) {
        return ApartmentClass::Studio;
    }
    if (floor * 3 <= floors * 2) {
        return ApartmentClass::OneBedroom;
    }
    return ApartmentClass::TwoBedroom;
}

[[nodiscard]] std::uint32_t RentPercent(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::OneBedroom: return 150;
        case ApartmentClass::TwoBedroom: return 240;
        default: return 100;
    }
}

[[nodiscard]] std::string_view HomeLabel(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::Dorm: return "Dorm Room";
        case ApartmentClass::Studio: return "Studio";
        case ApartmentClass::OneBedroom: return "Flat";
        case ApartmentClass::TwoBedroom: return "Two-Bedroom Flat";
        default: return "Apartment";
    }
}

bool IsDormitorySite(const Building& building) noexcept {
    return building.kind == BuildingKind::Habitat &&
           building.name.starts_with(kDormitoryPrefix);
}

bool DormBedsShort(const WorldState& state, const RoomIndex& rooms) {
    const auto streak = static_cast<std::size_t>(state.settings.construction.streakNights);
    if (state.history.size() < streak) {
        return false;
    }
    std::int64_t beds = 0;
    for (const RoomRef& apartment : rooms.RoomsOfKind(RoomKind::Apartment)) {
        if (apartment.room->apartmentClass == ApartmentClass::Dorm &&
            !IsUnderConstruction(state.projects, apartment.building->id)) {
            beds += apartment.room->capacity;
        }
    }
    if (beds == 0) {
        return false;
    }
    for (std::size_t day = state.history.size() - streak; day != state.history.size(); ++day) {
        const std::int64_t lived = state.history[day].residentsByClass.at(0);
        const std::int64_t free = beds > lived ? beds - lived : 0;
        if (free * 100 >= state.settings.construction.freeBedsPercent * beds) {
            return false;
        }
    }
    return true;
}

}  // namespace mechapunk::sim::construction
