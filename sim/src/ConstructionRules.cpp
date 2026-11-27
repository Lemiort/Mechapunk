#include "ConstructionRules.hpp"

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
        case ApartmentClass::Studio: return "Studio";
        case ApartmentClass::OneBedroom: return "Flat";
        case ApartmentClass::TwoBedroom: return "Two-Bedroom Flat";
        default: return "Apartment";
    }
}

}  // namespace mechapunk::sim::construction
