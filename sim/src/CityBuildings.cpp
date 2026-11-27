#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "CityInternal.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk::sim::city_build {

namespace {
constexpr double kTypicalResidents = 45.0;
// Habitats this central or more have penthouses on top.
constexpr double kLuxuryCentrality = 0.3;

template <typename T>
const T& Pick(std::span<const T> items, Pcg32& random) {
    return items[random.UniformInt(0, static_cast<std::uint32_t>(items.size() - 1))];
}

// Store kinds in the order habitats get them, the ones robots cannot do
// without first, so even a small city can repair and charge its robots.
constexpr std::array kStoreOrder{
    RoomKind::RepairShop, RoomKind::ChargingBay, RoomKind::Diner,
    RoomKind::Clinic,     RoomKind::Bar,         RoomKind::Arcade,
    RoomKind::Shop,       RoomKind::BlackMarket};

std::uint32_t RentPercent(ApartmentClass kind) {
    switch (kind) {
        case ApartmentClass::OneBedroom: return 150;
        case ApartmentClass::TwoBedroom: return 240;
        case ApartmentClass::Penthouse: return 600;
        default: return 100;
    }
}

std::string_view RoomLabel(ApartmentClass kind) {
    switch (kind) {
        case ApartmentClass::Dorm: return "Dorm Room";
        case ApartmentClass::Studio: return "Studio";
        case ApartmentClass::OneBedroom: return "Flat";
        case ApartmentClass::TwoBedroom: return "Two-Bedroom Flat";
        case ApartmentClass::Penthouse: return "Penthouse";
        default: return "Apartment";
    }
}
}  // namespace

// Ground-floor stores and homes above. A dormitory is dorm rooms on every
// floor; other habitats have studios low down, flats above, two-bedroom
// flats near the top, and in the middle of the city penthouses on top.
// Store kinds rotate through every venue kind so each city has some of each.
Building MakeHabitat(const CityParams& params, Pcg32& random,
                     std::vector<std::uint32_t>& buildingsAt,
                     EntityIdAllocator& ids, std::uint32_t number,
                     std::uint32_t& storeRotation, bool dormitory) {
    Building building{.id = ids.Allocate(),
                      .name = std::format("{} {}", dormitory ? "Dormitory" : "Hab Block", number),
                      .kind = BuildingKind::Habitat,
                      .entrance = RandomNodeInColumns(params, random, 0,
                                                      dormitory ? params.gridSize / 4
                                                                : params.gridSize / 2,
                                                      buildingsAt),
                      .floors = 1,
                      .shift = {},
                      .rooms = {}};
    const double centrality =
        Centrality(building.entrance, params.gridSize) * Urbanity(params.residents);
    const std::uint32_t floors = random.UniformInt(Lerp(static_cast<std::uint32_t>(params.city.minFloors), static_cast<std::uint32_t>(params.city.minCentralFloors), centrality),
                                                   Lerp(static_cast<std::uint32_t>(params.city.maxFloors), static_cast<std::uint32_t>(params.city.maxCentralFloors), centrality));
    std::vector<ApartmentClass> classes;
    const bool luxury = !dormitory && centrality >= kLuxuryCentrality;
    for (std::uint32_t floor = 1; floor <= floors; ++floor) {
        if (dormitory) {
            classes.push_back(ApartmentClass::Dorm);
        } else if (luxury && floor + 2 > floors) {
            classes.push_back(ApartmentClass::Penthouse);
        } else if (floor * 3 <= floors) {
            classes.push_back(ApartmentClass::Studio);
        } else if (floor * 3 <= floors * 2) {
            classes.push_back(ApartmentClass::OneBedroom);
        } else {
            classes.push_back(ApartmentClass::TwoBedroom);
        }
    }
    std::uint32_t residents = 0;
    for (const ApartmentClass kind : classes) {
        residents += ApartmentsPerFloor(kind) * Sleeps(kind);
    }
    const std::uint32_t stores = random.UniformInt(static_cast<std::uint32_t>(params.city.minStores), static_cast<std::uint32_t>(params.city.maxStores)) +
                                 (residents >= static_cast<std::uint32_t>(params.city.residentsForExtraStore) ? 1 : 0);
    const double kAverageStores = (static_cast<std::uint32_t>(params.city.minStores) + static_cast<std::uint32_t>(params.city.maxStores)) / 2.0;
    const double capacityScale =
        std::max(1.0, (static_cast<double>(residents) * kAverageStores) /
                          (kTypicalResidents * static_cast<double>(stores)));
    for (std::uint32_t store = 0; store < stores; ++store) {
        const RoomKind kind = kStoreOrder.at(storeRotation++ % kStoreOrder.size());
        const VenueRules& rules = *FindVenueRules(kind);
        const std::uint32_t capacity =
            random.UniformInt(rules.minCapacity, rules.maxCapacity);
        building.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::string{Pick(VenueNames(kind), random)},
             .kind = kind,
             .floor = 0,
             .capacity = static_cast<std::uint32_t>(
                 std::lround(static_cast<double>(capacity) * capacityScale))});
    }
    const std::uint32_t baseRent = random.UniformInt(static_cast<std::uint32_t>(params.city.minBaseRent), static_cast<std::uint32_t>(params.city.maxBaseRent));
    for (std::uint32_t floor = 1; floor <= floors; ++floor) {
        const ApartmentClass kind = classes[floor - 1];
        const std::uint32_t perFloor = ApartmentsPerFloor(kind);
        for (std::uint32_t index = 0; index < perFloor; ++index) {
            building.rooms.push_back(
                {.id = ids.Allocate(),
                 .name = perFloor == 1 ? std::format("{} {}", RoomLabel(kind), floor)
                                       : std::format("{} {}-{:02}", RoomLabel(kind), floor, index + 1),
                 .kind = RoomKind::Apartment,
                 .floor = floor,
                 .capacity = Sleeps(kind),
                 .rent = kind == ApartmentClass::Dorm
                             ? static_cast<std::uint32_t>(params.city.dormRent) + (floor / 4)
                             : (baseRent + floor) * RentPercent(kind) / 100,
                 .apartmentClass = kind});
        }
    }
    building.floors = floors + 1;
    building.style = StyleOf(building.kind, building.id, building.floors);
    return building;
}

Building MakeWorkplace(const CityParams& params, Pcg32& random,
                       std::vector<std::uint32_t>& buildingsAt,
                       EntityIdAllocator& ids,
                       std::array<std::uint32_t, kWorkplaceTypes.size()>& counts) {
    const std::uint32_t typeIndex = random.UniformInt(
        0, static_cast<std::uint32_t>(kWorkplaceTypes.size() - 1));
    const WorkplaceType& type = kWorkplaceTypes.at(typeIndex);
    Building building{
        .id = ids.Allocate(),
        .name = std::format("{} {}", type.name, ++counts.at(typeIndex)),
        .kind = BuildingKind::Workplace,
        .entrance = RandomNodeInColumns(params, random, params.gridSize / 2,
                                        params.gridSize - 1, buildingsAt),
        .floors = 1,
        .shift = type.shift,
        .rooms = {}};
    const double centrality =
        Centrality(building.entrance, params.gridSize) * Urbanity(params.residents);
    const std::uint32_t jobs =
        random.UniformInt(Lerp(static_cast<std::uint32_t>(params.city.minWorkplaceJobs), static_cast<std::uint32_t>(params.city.minCentralJobs), centrality),
                          Lerp(static_cast<std::uint32_t>(params.city.maxWorkplaceJobs), static_cast<std::uint32_t>(params.city.maxCentralJobs), centrality));
    std::uint32_t staffed = 0;
    for (std::uint32_t index = 0; staffed < jobs; ++index) {
        const std::uint32_t size = std::min(
            jobs - staffed,
            random.UniformInt(static_cast<std::uint32_t>(params.city.minWorkspaceSize), static_cast<std::uint32_t>(params.city.maxWorkspaceSize)));
        const std::uint32_t floor =
            index / (jobs >= static_cast<std::uint32_t>(params.city.towerJobs) ? 1 : static_cast<std::uint32_t>(params.city.workspacesPerFloor));
        building.rooms.push_back(
            {.id = ids.Allocate(),
             .name = std::format("{} {}", type.workspace, index + 1),
             .kind = RoomKind::Workspace,
             .floor = floor,
             .capacity = size});
        building.floors = floor + 1;
        staffed += size;
    }
    building.style = StyleOf(building.kind, building.id, building.floors);
    return building;
}

}  // namespace mechapunk::sim::city_build
