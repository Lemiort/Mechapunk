#include "mechapunk/sim/Venues.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::uint32_t Hour(std::uint32_t hour) { return hour * 60; }

constexpr VenueRules kBar{.label = "bar",
                          .opensAt = Hour(17),
                          .closesAt = Hour(3),
                          .price = 15,
                          .effects = {.charge = -4, .condition = 0,
                                      .social = 25, .fun = 12},
                          .socialPerGuest = 2,
                          .socialGuestCap = 16,
                          .minVisitMinutes = 60,
                          .maxVisitMinutes = 150,
                          .minCapacity = 12,
                          .maxCapacity = 30,
    .staff = {.shiftStarts = {Hour(17)}, .shifts = 1, .lengthMinutes = 540, .wage = 100, .guestsPerWorker = 12}};
// Noodle diners never close, so night-shift workers have company too.
constexpr VenueRules kDiner{.label = "noodle diner",
                            .opensAt = 0,
                            .closesAt = 0,
                            .price = 8,
                            .effects = {.charge = 6, .condition = 0,
                                        .social = 20, .fun = 6},
                            .socialPerGuest = 1,
                            .socialGuestCap = 8,
                            .minVisitMinutes = 30,
                            .maxVisitMinutes = 70,
                            .minCapacity = 8,
                            .maxCapacity = 20,
    .staff = {.shiftStarts = {Hour(6), Hour(14), Hour(22)}, .shifts = 3, .lengthMinutes = 480, .wage = 90, .guestsPerWorker = 10}};
constexpr VenueRules kChargingBay{.label = "charging bay",
                                  .opensAt = 0,
                                  .closesAt = 0,
                                  .price = 6,
                                  .effects = {.charge = 90, .condition = 0,
                                              .social = 2, .fun = 0},
                                  .socialPerGuest = 0,
                                  .socialGuestCap = 0,
                                  .minVisitMinutes = 30,
                                  .maxVisitMinutes = 80,
                                  .minCapacity = 6,
                                  .maxCapacity = 16,
    .staff = {.shiftStarts = {Hour(6), Hour(14), Hour(22)}, .shifts = 3, .lengthMinutes = 480, .wage = 85, .guestsPerWorker = 16}};
constexpr VenueRules kRepairShop{.label = "repair shop",
                                 .opensAt = Hour(7),
                                 .closesAt = Hour(21),
                                 .price = 30,
                                 .effects = {.charge = 0, .condition = 70,
                                             .social = 3, .fun = 0},
                                 .socialPerGuest = 0,
                                 .socialGuestCap = 0,
                                 .minVisitMinutes = 30,
                                 .maxVisitMinutes = 60,
                                 .minCapacity = 6,
                                 .maxCapacity = 12,
    .staff = {.shiftStarts = {Hour(7), Hour(13)}, .shifts = 2, .lengthMinutes = 480, .wage = 120, .guestsPerWorker = 6}};
constexpr VenueRules kArcade{.label = "arcade",
                             .opensAt = Hour(10),
                             .closesAt = Hour(4),
                             .price = 8,
                             .effects = {.charge = -6, .condition = 0,
                                         .social = 8, .fun = 35},
                             .socialPerGuest = 1,
                             .socialGuestCap = 6,
                             .minVisitMinutes = 45,
                             .maxVisitMinutes = 120,
                             .minCapacity = 12,
                             .maxCapacity = 26,
    .staff = {.shiftStarts = {Hour(10), Hour(18)}, .shifts = 2, .lengthMinutes = 480, .wage = 90, .guestsPerWorker = 14}};
constexpr VenueRules kShop{.label = "parts shop",
                           .opensAt = Hour(9),
                           .closesAt = Hour(21),
                           .price = 20,
                           .effects = {.charge = 0, .condition = 8,
                                       .social = 4, .fun = 15},
                           .socialPerGuest = 0,
                           .socialGuestCap = 0,
                           .minVisitMinutes = 20,
                           .maxVisitMinutes = 50,
                           .minCapacity = 6,
                           .maxCapacity = 14,
    .staff = {.shiftStarts = {Hour(9), Hour(13)}, .shifts = 2, .lengthMinutes = 480, .wage = 95, .guestsPerWorker = 10}};

constexpr VenueRules kClinic{.label = "repair clinic",
                             .opensAt = Hour(8),
                             .closesAt = Hour(20),
                             .price = 100,
                             .effects = {.charge = 0, .condition = 150,
                                         .social = 2, .fun = -2},
                             .socialPerGuest = 0,
                             .socialGuestCap = 0,
                             .minVisitMinutes = 60,
                             .maxVisitMinutes = 120,
                             .minCapacity = 6,
                             .maxCapacity = 12,
    .staff = {.shiftStarts = {Hour(8)}, .shifts = 1, .lengthMinutes = 720, .wage = 150, .guestsPerWorker = 4}};
constexpr VenueRules kBlackMarket{.label = "black market",
                                  .opensAt = Hour(22),
                                  .closesAt = Hour(5),
                                  .price = 12,
                                  .effects = {.charge = 0, .condition = 40,
                                              .social = 6, .fun = 20},
                                  .socialPerGuest = 1,
                                  .socialGuestCap = 6,
                                  .minVisitMinutes = 20,
                                  .maxVisitMinutes = 60,
                                  .minCapacity = 6,
                                  .maxCapacity = 14,
    .staff = {.shiftStarts = {Hour(22)}, .shifts = 1, .lengthMinutes = 420, .wage = 110, .guestsPerWorker = 10}};

constexpr std::array kVenueKinds{RoomKind::Bar,         RoomKind::Diner,
                                 RoomKind::ChargingBay, RoomKind::RepairShop,
                                 RoomKind::Arcade,      RoomKind::Shop,
                                 RoomKind::Clinic,      RoomKind::BlackMarket};

constexpr std::array<std::string_view, 8> kBarNames{
    "The Rusty Diode", "Chrome Lounge",  "Byte & Brew",   "The Overclock",
    "Static Bar",      "The Cold Solder", "Last Reboot",  "Ion Tap"};
constexpr std::array<std::string_view, 6> kDinerNames{
    "Neon Wok",      "Ramen Relay",  "Synth Noodles",
    "Grease Trap",   "Pho Circuit",  "Midnight Broth"};
constexpr std::array<std::string_view, 5> kChargingNames{
    "Volt Station", "Juice Bay", "Amp Dock", "Live Wire", "Spark Lot"};
constexpr std::array<std::string_view, 5> kRepairNames{
    "Fix-It Clinic", "Servo Surgeons", "Bolt & Patch", "Joint Works",
    "Steady Hands"};
constexpr std::array<std::string_view, 5> kArcadeNames{
    "Pixel Den", "Glitch Arcade", "Retro Grid", "High Score", "Bit Pit"};
constexpr std::array<std::string_view, 5> kShopNames{
    "Parts Bazaar", "Chrome Goods", "Circuit Mart", "Spare Me",
    "Gear Exchange"};
constexpr std::array<std::string_view, 4> kClinicNames{
    "Chassis General", "Mercy Mechanics", "St. Solder's", "Overhaul Ward"};
constexpr std::array<std::string_view, 4> kBlackMarketNames{
    "The Back Door", "Grey Parts", "Under the Bridge", "No Questions"};
}  // namespace

const VenueRules* FindVenueRules(RoomKind kind) noexcept {
    switch (kind) {
        case RoomKind::Bar:
            return &kBar;
        case RoomKind::Diner:
            return &kDiner;
        case RoomKind::ChargingBay:
            return &kChargingBay;
        case RoomKind::RepairShop:
            return &kRepairShop;
        case RoomKind::Arcade:
            return &kArcade;
        case RoomKind::Shop:
            return &kShop;
        case RoomKind::Clinic:
            return &kClinic;
        case RoomKind::BlackMarket:
            return &kBlackMarket;
        case RoomKind::Apartment:
        case RoomKind::Workspace:
            return nullptr;
    }
    return nullptr;
}

bool IsVenue(RoomKind kind) noexcept { return FindVenueRules(kind) != nullptr; }

bool IsOpen(const VenueRules& rules, std::uint32_t minuteOfDay) noexcept {
    if (rules.opensAt == rules.closesAt) {
        return true;
    }
    if (rules.opensAt < rules.closesAt) {
        return minuteOfDay >= rules.opensAt && minuteOfDay < rules.closesAt;
    }
    return minuteOfDay >= rules.opensAt || minuteOfDay < rules.closesAt;
}

std::uint32_t MinutesUntilClose(const VenueRules& rules,
                                std::uint32_t minuteOfDay) noexcept {
    if (rules.opensAt == rules.closesAt) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const auto day = static_cast<std::uint32_t>(kMinutesPerDay);
    return (rules.closesAt + day - minuteOfDay) % day;
}

std::span<const RoomKind> VenueKinds() noexcept { return kVenueKinds; }

std::span<const std::string_view> VenueNames(RoomKind kind) noexcept {
    switch (kind) {
        case RoomKind::Bar:
            return kBarNames;
        case RoomKind::Diner:
            return kDinerNames;
        case RoomKind::ChargingBay:
            return kChargingNames;
        case RoomKind::RepairShop:
            return kRepairNames;
        case RoomKind::Arcade:
            return kArcadeNames;
        case RoomKind::Shop:
            return kShopNames;
        case RoomKind::Clinic:
            return kClinicNames;
        case RoomKind::BlackMarket:
            return kBlackMarketNames;
        case RoomKind::Apartment:
        case RoomKind::Workspace:
            return {};
    }
    return {};
}


std::uint32_t StaffPerShift(const Room& venue) noexcept {
    const VenueRules* rules = FindVenueRules(venue.kind);
    if (rules == nullptr) {
        return 0;
    }
    return std::max<std::uint32_t>(1, venue.capacity / rules->staff.guestsPerWorker);
}

std::uint32_t JobsAt(const Room& room) noexcept {
    if (room.kind == RoomKind::Workspace) {
        return room.capacity;
    }
    const VenueRules* rules = FindVenueRules(room.kind);
    return rules != nullptr ? StaffPerShift(room) * rules->staff.shifts : 0;
}

std::uint32_t ShiftSlot(EntityId robot, const VenueRules& rules) noexcept {
    return static_cast<std::uint32_t>((robot.value * 2654435761ULL >> 8U) % rules.staff.shifts);
}

}  // namespace mechapunk::sim
