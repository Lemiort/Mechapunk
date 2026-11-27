// World::EmployerForNewWorkplace: who owns the workplace a construction site
// raises. Without economy.workplacesPerCorporation it is the first corporation
// of industry; with it, the first that owns fewer workplaces than that, and when
// every one is full a new corporation is founded for the site, its capital
// bought as shares by the robots with credits to spare, as in the seed town.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

#include "ConstructionRules.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::array<std::string_view, 6> kNewCorporationNames{
    "Orbital", "Tessera", "Nimbus", "Cobalt", "Harbor", "Ember"};
}  // namespace

EntityId World::EmployerForNewWorkplace() {
    Economy& economy = state_.economy;
    const auto perCorporation = static_cast<std::size_t>(state_.settings.economy.workplacesPerCorporation);
    std::vector<EntityId> industry;
    for (const Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::Corporation &&
            organization.sector == Sector::Industry) {
            industry.push_back(organization.id);
        }
    }
    if (industry.empty()) {
        return {};
    }
    if (perCorporation == 0) {
        return industry.front();
    }
    // Workplaces each corporation owns: buildings holding a room it owns.
    const auto owned = [&](EntityId corporation) {
        return static_cast<std::size_t>(std::ranges::count_if(
            state_.city.buildings, [&](const Building& building) {
                return building.kind == BuildingKind::Workplace &&
                       std::ranges::any_of(building.rooms, [&](const Room& room) {
                           return room.owner == corporation;
                       });
            }));
    };
    for (const EntityId corporation : industry) {
        if (owned(corporation) < perCorporation) {
            return corporation;
        }
    }
    // Every one is full: found another.
    const EntityId id = state_.ids.Allocate();
    EntityId bank;
    for (const Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::Bank) {
            bank = organization.id;
            break;
        }
    }
    const std::size_t number = industry.size();
    economy.organizations.push_back(
        {.id = id,
         .name = std::format("{} Industries {}", kNewCorporationNames.at(number % kNewCorporationNames.size()),
                             (number / kNewCorporationNames.size()) + 1),
         .kind = OrganizationKind::Corporation,
         .sector = Sector::Industry,
         .bank = bank});
    IndexEconomy();
    // Three days of base wages for the jobs the new workplace will have.
    const ConstructionSettings& build = state_.settings.construction;
    const std::int64_t jobs = build.workplaceFloors * build.workspacesPerFloor * build.workspacePlaces;
    SellShares({{id, 3 * jobs * construction::kSiteShift.wage}}, EntityId{}, centralBank_,
               static_cast<std::int64_t>(state_.settings.economy.comfortableSavings));
    Emit({.tick = state_.tick, .kind = EventKind::CorporationFounded, .account = id});
    return id;
}

}  // namespace mechapunk::sim
