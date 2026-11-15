#include "mechapunk/sim/Venues.hpp"

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
                          .maxCapacity = 30};
constexpr VenueRules kDiner{.label = "noodle diner",
                            .opensAt = Hour(10),
                            .closesAt = Hour(23),
                            .price = 8,
                            .effects = {.charge = 6, .condition = 0,
                                        .social = 20, .fun = 6},
                            .socialPerGuest = 1,
                            .socialGuestCap = 8,
                            .minVisitMinutes = 30,
                            .maxVisitMinutes = 70,
                            .minCapacity = 8,
                            .maxCapacity = 20};
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
                                  .maxCapacity = 16};
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
                                 .maxCapacity = 12};
constexpr VenueRules kArcade{.label = "arcade",
                             .opensAt = Hour(15),
                             .closesAt = Hour(2),
                             .price = 8,
                             .effects = {.charge = -6, .condition = 0,
                                         .social = 8, .fun = 35},
                             .socialPerGuest = 1,
                             .socialGuestCap = 6,
                             .minVisitMinutes = 45,
                             .maxVisitMinutes = 120,
                             .minCapacity = 8,
                             .maxCapacity = 20};
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
                           .maxCapacity = 14};

constexpr std::array kVenueKinds{RoomKind::Bar,         RoomKind::Diner,
                                 RoomKind::ChargingBay, RoomKind::RepairShop,
                                 RoomKind::Arcade,      RoomKind::Shop};

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
        case RoomKind::Apartment:
        case RoomKind::Workspace:
            return {};
    }
    return {};
}

}  // namespace mechapunk::sim
