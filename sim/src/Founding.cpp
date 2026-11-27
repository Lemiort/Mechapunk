// World::FoundEconomy: the city's organizations, who owns what, and the
// starting money; IndexEconomy rebuilds lookups after loading.

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

#include "EconomyInternal.hpp"

namespace mechapunk::sim {
namespace {

constexpr std::uint64_t kEconomyStream = 7;

constexpr std::array<std::string_view, 12> kIndustryNames{
    "Kessler", "Omni", "Vortek", "Hexa", "Ironclad", "Synthex",
    "Chrome", "Atlas", "Raijin", "Brassworks", "Quantum", "Meridian"};

constexpr std::array<std::string_view, 8> kIndustryKinds{
    "Dynamics", "Forge", "Works", "Systems", "Industries", "Fabrication",
    "Logistics", "Robotics"};

constexpr std::array<std::string_view, 6> kChainNames{
    "Neon", "Volt", "Rust", "Static", "Glow", "Byte"};

constexpr std::array<std::string_view, 3> kBankNames{
    "First Chrome Bank", "Ironside Savings", "Neon Credit Union"};

std::string_view ChainKind(RoomKind kind) {
    switch (kind) {
        case RoomKind::Bar: return "Taps";
        case RoomKind::Diner: return "Noodle Co.";
        case RoomKind::ChargingBay: return "Charge";
        case RoomKind::RepairShop: return "Repairs";
        case RoomKind::Arcade: return "Arcades";
        case RoomKind::Shop: return "Goods";
        case RoomKind::Clinic: return "Clinics";
        case RoomKind::BlackMarket: return "Syndicate";
        default: return "Holdings";
    }
}

}  // namespace

void World::FoundEconomy(std::uint64_t seed) {
    Pcg32 random = Pcg32::FromSeed(seed, kEconomyStream);
    Economy& economy = state_.economy;
    const auto found = [&](std::string name, OrganizationKind kind, Sector sector) -> Organization& {
        economy.organizations.push_back(
            {.id = state_.ids.Allocate(), .name = std::move(name), .kind = kind, .sector = sector});
        return economy.organizations.back();
    };
    found("Central Reserve", OrganizationKind::CentralBank, Sector::Banking);
    std::vector<std::size_t> banks;
    for (const std::string_view name : kBankNames) {
        banks.push_back(economy.organizations.size());
        found(std::string{name}, OrganizationKind::Bank, Sector::Banking);
    }
    found("Habitat Trust", OrganizationKind::Corporation, Sector::Housing);
    found("City Hall", OrganizationKind::Government, Sector::Government);
    found("Gridline Power & Water", OrganizationKind::Corporation, Sector::Utilities);
    found("Metro Authority", OrganizationKind::Corporation, Sector::Transit);

    // Industry: corporations of about four workplaces each.
    std::vector<Building*> workplaces;
    // Metro depots belong to the Metro Authority, not to industry.
    for (Building& building : state_.city.buildings) {
        const bool depot = std::ranges::all_of(building.rooms, [](const Room& room) {
            return room.kind == RoomKind::CrewRoom;
        });
        if (building.kind == BuildingKind::Workplace && !depot) {
            workplaces.push_back(&building);
        }
    }
    const std::size_t corporations =
        std::clamp<std::size_t>(workplaces.size() / 4, 1, kIndustryNames.size() * 2);
    const std::size_t firstIndustry = economy.organizations.size();
    for (std::size_t index = 0; index < corporations; ++index) {
        found(std::format("{} {}", kIndustryNames.at(index % kIndustryNames.size()),
                          kIndustryKinds.at((index + (index / kIndustryNames.size())) %
                                            kIndustryKinds.size())),
              OrganizationKind::Corporation, Sector::Industry);
    }
    for (std::size_t index = 0; index < workplaces.size(); ++index) {
        // A random corporation for each, but every corporation gets one.
        const std::size_t owner =
            index < corporations
                ? index
                : random.UniformInt(0, static_cast<std::uint32_t>(corporations - 1));
        Organization& corporation = economy.organizations[firstIndustry + owner];
        for (Room& room : workplaces[index]->rooms) {
            room.owner = corporation.id;
            // Three days of base wages to start with.
            corporation.credits += 3 * static_cast<std::int64_t>(room.capacity) *
                                   workplaces[index]->shift.wage;
        }
    }

    // Venues: up to three chains of each kind.
    std::unordered_map<std::uint8_t, std::vector<Room*>> venues;
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (IsVenue(room.kind)) {
                venues[static_cast<std::uint8_t>(room.kind)].push_back(&room);
            }
        }
    }
    std::size_t chain = 0;
    for (const RoomKind kind : VenueKinds()) {
        const auto rooms = venues.find(static_cast<std::uint8_t>(kind));
        if (rooms == venues.end()) {
            continue;
        }
        const std::size_t chains = std::clamp<std::size_t>(rooms->second.size() / 12, 1, 3);
        const std::size_t first = economy.organizations.size();
        for (std::size_t index = 0; index < chains; ++index) {
            found(std::format("{} {}", kChainNames.at(chain++ % kChainNames.size()), ChainKind(kind)),
                  OrganizationKind::Corporation, Sector::Venues);
        }
        for (std::size_t index = 0; index < rooms->second.size(); ++index) {
            Organization& owner = economy.organizations[first + (index % chains)];
            rooms->second[index]->owner = owner.id;
            owner.credits += 100;
        }
    }

    // The city's housing office owns the dorms everyone starts in; the
    // trust owns every other apartment, to let or sell.
    const auto trustIt = std::ranges::find_if(
        economy.organizations, [](const Organization& organization) {
            return organization.sector == Sector::Housing;
        });
    const EntityId trust = trustIt->id;
    const auto officeIt = std::ranges::find_if(
        economy.organizations, [](const Organization& organization) {
            return organization.kind == OrganizationKind::Government;
        });
    const EntityId office = officeIt->id;
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.kind == RoomKind::Apartment) {
                room.owner = room.apartmentClass == ApartmentClass::Dorm ? office : trust;
            }
        }
    }

    // City Hall owns every police station; the Metro Authority owns every
    // crew room.
    const auto transitIt = std::ranges::find_if(
        economy.organizations, [](const Organization& organization) {
            return organization.sector == Sector::Transit;
        });
    const EntityId transit = transitIt->id;
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.kind == RoomKind::PoliceStation) {
                room.owner = office;
            } else if (room.kind == RoomKind::CrewRoom) {
                room.owner = transit;
            }
        }
    }

    // Starting money: robots' savings, and capital for every organization.
    const auto population = static_cast<std::int64_t>(state_.robots.size());
    for (Organization& organization : economy.organizations) {
        switch (organization.sector) {
            case Sector::Housing:
            case Sector::Utilities:
            case Sector::Transit:
            case Sector::Government:
                organization.credits += 1000 + (population / 2);
                break;
            case Sector::Banking:
                organization.credits = organization.kind == OrganizationKind::Bank
                                           ? static_cast<std::int64_t>(state_.settings.economy.bankFloor) + (population * 4)
                                           : 0;
                break;
            default:
                break;
        }
    }
    std::int64_t savings = 0;
    for (Robot& robot : state_.robots) {
        savings += robot.credits;
        robot.bank = economy.organizations.at(banks.at(
            random.UniformInt(0, static_cast<std::uint32_t>(banks.size() - 1)))).id;
    }
    const EntityId central = economy.organizations.front().id;
    for (Organization& organization : economy.organizations) {
        organization.bank = organization.kind == OrganizationKind::Corporation ||
                                    organization.kind == OrganizationKind::Government
                                ? economy.organizations.at(banks.at(random.UniformInt(
                                      0, static_cast<std::uint32_t>(banks.size() - 1)))).id
                                : (organization.kind == OrganizationKind::Bank ? central : EntityId{});
        economy.created += organization.credits;
        if (organization.credits > 0) {
            economy.emissions.push_back({.tick = state_.tick,
                                         .account = organization.id,
                                         .amount = organization.credits,
                                         .reason = EmissionReason::Founding});
        }
    }
    economy.created += savings;
    economy.emissions.push_back(
        {.tick = state_.tick, .account = {}, .amount = savings, .reason = EmissionReason::Founding});

    // Owners: one to three robots or banks for every corporation and bank.
    for (Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank ||
            organization.kind == OrganizationKind::Government) {
            continue;  // Public: nobody's to own.
        }
        const std::uint32_t owners = random.UniformInt(1, 3);
        for (std::uint32_t index = 0; index < owners; ++index) {
            EntityId owner;
            if (organization.kind == OrganizationKind::Corporation &&
                random.UniformInt(1, 100) <= 25) {
                owner = economy.organizations.at(banks.at(
                    random.UniformInt(0, static_cast<std::uint32_t>(banks.size() - 1)))).id;
            } else if (!state_.robots.empty()) {
                owner = state_.robots.at(random.UniformInt(
                    0, static_cast<std::uint32_t>(state_.robots.size() - 1))).id;
            }
            if (!owner.IsValid() || std::ranges::contains(organization.owners, owner, &Share::owner)) {
                continue;
            }
            organization.owners.push_back({.owner = owner, .shares = random.UniformInt(10, 100)});
        }
        std::ranges::sort(organization.owners, {}, [](const Share& share) { return share.owner.value; });
    }
    // The metro belongs to the city.
    for (Organization& organization : economy.organizations) {
        if (organization.sector == Sector::Transit) {
            organization.owners = {{.owner = office, .shares = 100}};
        }
    }
}

void World::IndexEconomy() {
    loanOf_.clear();
    for (std::size_t index = 0; index < state_.economy.loans.size(); ++index) {
        loanOf_[state_.economy.loans[index].borrower.value] = index;
    }
    for (const Organization& organization : state_.economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank) {
            centralBank_ = organization.id;
        }
        if (organization.kind == OrganizationKind::Government) {
            cityHall_ = organization.id;
        }
        switch (organization.sector) {
            case Sector::Housing: housing_ = organization.id; break;
            case Sector::Utilities: utilities_ = organization.id; break;
            case Sector::Transit: transit_ = organization.id; break;
            default: break;
        }
    }
}

}  // namespace mechapunk::sim
