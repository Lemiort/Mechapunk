// World::ReviewBanks: a growing town founds a bank when its robots outnumber
// `economy.robotsPerBank` to a bank. Its capital is bought as shares by the
// robots with credits to spare, and the central bank lends what the shares
// leave short of the bank floor; that loan is the only money created.

#include <algorithm>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

void World::ReviewBanks() {
    const std::int64_t perBank = state_.settings.economy.robotsPerBank;
    if (perBank == 0 || !centralBank_.IsValid()) {
        return;
    }
    Economy& economy = state_.economy;
    const auto banks = static_cast<std::int64_t>(std::ranges::count_if(
        economy.organizations,
        [](const Organization& organization) { return organization.kind == OrganizationKind::Bank; }));
    if (banks == 0 || static_cast<std::int64_t>(state_.robots.size()) <= banks * perBank) {
        return;
    }
    const EntityId id = state_.ids.Allocate();
    economy.organizations.push_back({.id = id,
                                     .name = std::format("Civic Savings Bank {}", banks + 1),
                                     .kind = OrganizationKind::Bank,
                                     .sector = Sector::Banking,
                                     .bank = centralBank_});
    IndexEconomy();
    const auto floor = static_cast<std::int64_t>(state_.settings.economy.bankFloor);
    SellShares({{id, floor}}, id, centralBank_,
               static_cast<std::int64_t>(state_.settings.economy.comfortableSavings));
    Emit({.tick = state_.tick, .kind = EventKind::BankFounded, .account = id});
}

}  // namespace mechapunk::sim
