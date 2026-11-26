// Buying and selling homes: transfers, mortgages, inheritance and the nightly
// property market.

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
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/World.hpp"

#include "EconomyInternal.hpp"

namespace mechapunk::sim {
namespace {

// Each night, the chance in percent that a robot able to buy an apartment
// does, and the nights of rent it keeps after paying.
constexpr std::uint32_t kBuyPercent = 3;

constexpr std::int64_t kBuyReserveNights = 20;

// A mortgage needs this much of the price down, and lends at most this many
// shifts' pay.
constexpr std::int64_t kDepositPercent = 20;

constexpr std::int64_t kMortgageShifts = 60;

// Each night, the chance a robot sells an apartment nobody lives in.
constexpr std::uint32_t kSellVacantPercent = 20;

constexpr std::int64_t kBankReservePercent = 10;

}  // namespace

void World::Transfer(EntityId apartment, EntityId buyer, std::int64_t price) {
    Room& room = MutableRoom(apartment);
    const EntityId seller = room.owner;
    if (Organization* organization = MutableOrganization(buyer)) {
        PayOrBorrow(*organization, seller, price);
    } else {
        Pay(buyer, seller, price);
    }
    MutableRoom(apartment).owner = buyer;
    state_.today.propertySales += 1;
    state_.today.propertySalesValue += price;
    if (Robot* robot = MutableRobot(buyer)) {
        Emit({.tick = state_.tick, .kind = EventKind::BoughtProperty, .robot = buyer,
              .room = apartment, .credits = price});
        Remember(*robot, MemoryKind::BoughtProperty, {}, apartment);
    }
    if (FindRobot(seller) != nullptr) {
        Emit({.tick = state_.tick, .kind = EventKind::SoldProperty, .robot = seller,
              .room = apartment, .credits = price});
    }
}

void World::Bequeath(Robot& robot) {
    // Its best friend gets everything, or failing that its bank.
    EntityId heir = robot.bank;
    std::int32_t closest = 0;
    for (const Bond& bond : robot.bonds) {
        if ((bond.level == BondLevel::Friend || bond.level == BondLevel::CloseFriend) &&
            bond.affinity > closest && FindRobot(bond.robot) != nullptr) {
            closest = bond.affinity;
            heir = bond.robot;
        }
    }
    // Its savings come out of the bank; debts are paid first, and what
    // cannot be is lost to the lender.
    Withdraw(robot, robot.savings);
    if (const auto found = loanOf_.find(robot.id.value); found != loanOf_.end()) {
        Loan& loan = state_.economy.loans[found->second];
        const std::int64_t paid = std::min(loan.owed, robot.credits);
        Pay(robot.id, loan.lender, paid);
        state_.today.writtenOff += loan.owed - paid;
        loan.owed = 0;
    }
    Pay(robot.id, heir, robot.credits);
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.owner == robot.id) {
                room.owner = heir;
            }
        }
    }
    for (Organization& organization : state_.economy.organizations) {
        auto& owners = organization.owners;
        const auto share = std::ranges::find(owners, robot.id, &Share::owner);
        if (share == owners.end()) {
            continue;
        }
        const std::uint32_t shares = share->shares;
        owners.erase(share);
        if (heir == organization.id) {
            continue;  // A bank does not own itself.
        }
        if (const auto existing = std::ranges::find(owners, heir, &Share::owner);
            existing != owners.end()) {
            existing->shares += shares;
        } else {
            owners.push_back({.owner = heir, .shares = shares});
            std::ranges::sort(owners, {}, [](const Share& each) { return each.owner.value; });
        }
    }
    if (Robot* friendRobot = MutableRobot(heir)) {
        Emit({.tick = state_.tick, .kind = EventKind::Inherited, .robot = heir, .otherRobot = robot.id});
        Remember(*friendRobot, MemoryKind::Inherited, robot.id, {});
    }
}

std::int64_t World::ReserveOf(const Organization& organization) const {
    if (organization.kind == OrganizationKind::Bank) {
        std::int64_t held = 0;
        for (const Loan& loan : state_.economy.loans) {
            held += loan.lender == organization.id ? loan.owed : 0;
        }
        for (const Robot& robot : state_.robots) {
            held += robot.bank == organization.id ? robot.savings : 0;
        }
        return kBankFloor + (held * kBankReservePercent / 100);
    }
    const auto costs = [](const Books& books) {
        return books.wages + books.supplies + books.upkeep + books.rent + books.taxes;
    };
    return std::max(kCorporationFloor,
                    std::max(costs(organization.today), costs(organization.yesterday)));
}

void World::TradeProperty() {
    std::unordered_set<std::uint64_t> lived;
    for (const Robot& robot : state_.robots) {
        lived.insert(robot.apartment.value);
    }
    // Robots with apartments nobody lives in sell them to the trust.
    Organization* trust = MutableOrganization(housing_);
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const Room& room = *apartment.room;
        if (lived.contains(room.id.value) || FindRobot(room.owner) == nullptr ||
            state_.random.UniformInt(1, 100) > kSellVacantPercent || trust == nullptr) {
            continue;
        }
        Transfer(room.id, housing_, PriceOf(room) * kSellPercent / 100);
    }
    // Owners who cannot keep up with their bills sell their home and rent
    // it back.
    for (Robot& robot : state_.robots) {
        const auto home = rooms_.Find(robot.apartment);
        if (!home || home->room->owner != robot.id) {
            continue;
        }
        const std::int64_t upkeep = UpkeepOf(*home->room);
        if (upkeep <= 0 || robot.rentOwed < kEvictionNights * upkeep) {
            continue;
        }
        Transfer(robot.apartment, housing_, PriceOf(*home->room) * kSellPercent / 100);
        Remember(robot, MemoryKind::SoldHome, {}, robot.apartment);
        // The sale pays its debts.
        const std::int64_t bills = std::min(robot.rentOwed, robot.credits);
        Pay(robot.id, utilities_, bills);
        robot.rentOwed -= bills;
        if (const auto found = loanOf_.find(robot.id.value); found != loanOf_.end()) {
            Loan& loan = state_.economy.loans[found->second];
            const std::int64_t repaid = std::min(loan.owed, robot.credits);
            Pay(robot.id, loan.lender, repaid);
            loan.owed -= repaid;
        }
    }
    // Robots with a job and a fifth of the price saved buy their first
    // home with a mortgage: an empty studio or flat of the trust's, and
    // they move in.
    std::unordered_set<std::uint64_t> taken = lived;
    for (Robot& robot : state_.robots) {
        const auto home = rooms_.Find(robot.apartment);
        if (!home || home->room->owner == robot.id || !robot.workspace.IsValid() ||
            robot.activity == RobotActivity::Walking || LoanOf(robot.id) != nullptr ||
            robot.rentOwed > 0 || state_.random.UniformInt(1, 100) > kBuyPercent) {
            continue;
        }
        const std::int64_t means = robot.credits + robot.savings;
        const std::int64_t mortgage = kMortgageShifts * PayOf(robot);
        // The dearest it can put a fifth down on, keeping a month's bills.
        // Homes of a building a project is raising are not candidates: a
        // habitat site's homes belong to its owner, which is the housing
        // trust, so this loop would otherwise buy a robot into a half-built
        // tower (task D, step D2c3), as `ReviewHomes` and a newcomer's choice
        // of home already skip them.
        const Room* best = nullptr;
        for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
            const Room& room = *apartment.room;
            if (room.owner != housing_ || taken.contains(room.id.value) ||
                Sleeps(room.apartmentClass) != 1 ||
                RentFor(room) <= RentFor(*home->room) ||
                IsUnderConstruction(state_.projects, apartment.building->id)) {
                continue;
            }
            const std::int64_t price = PriceOf(room);
            const std::int64_t deposit = price * kDepositPercent / 100;
            if (means < deposit + (kBuyReserveNights * UpkeepOf(room)) || price - deposit > mortgage ||
                (best != nullptr && RentFor(room) <= RentFor(*best))) {
                continue;
            }
            best = &room;
        }
        if (best == nullptr) {
            continue;
        }
        const std::int64_t price = PriceOf(*best);
        const std::int64_t own = std::min(means - (kBuyReserveNights * UpkeepOf(*best)), price);
        // A bank lends only what it can spare beyond its reserve, and only
        // while the interest takes no more than a third of the robot's pay.
        const Organization* bank = FindOrganization(robot.bank);
        const std::int64_t mortgageOwed = price - own;
        const std::int64_t nightly =
            mortgageOwed * (state_.economy.policyRate + kBankMargin) / 10000;
        if (bank == nullptr || bank->credits - mortgageOwed < ReserveOf(*bank) ||
            nightly * 3 > NetPayOf(robot)) {
            continue;
        }
        Withdraw(robot, own - robot.credits);
        Lend(robot.bank, robot.id, price - std::min(own, robot.credits));
        Transfer(best->id, robot.id, price);
        taken.insert(best->id.value);
        robot.apartment = best->id;
        Remember(robot, MemoryKind::MovedUp, {}, best->id);
        if (robot.activity == RobotActivity::Resting) {
            StartWalk(robot, best->id);  // Moving into its own home tonight.
        }
    }
    CountResidents();

    // Robots with savings buy: first the home they rent from the trust,
    // then a let apartment of the trust's to collect its rent.
    std::vector<EntityId> letByTrust;
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        if (apartment.room->owner == housing_ && lived.contains(apartment.room->id.value)) {
            letByTrust.push_back(apartment.room->id);
        }
    }
    for (Robot& robot : state_.robots) {
        if (LoanOf(robot.id) != nullptr || robot.rentOwed > 0 ||
            state_.random.UniformInt(1, 100) > kBuyPercent) {
            continue;
        }
        const auto home = rooms_.Find(robot.apartment);
        const std::int64_t keep = kBuyReserveNights * (home ? RentFor(*home->room) : 0);
        const std::int64_t means = robot.credits + robot.savings;
        EntityId target;
        if (home && home->room->owner == housing_ && means >= PriceOf(*home->room) + keep) {
            target = robot.apartment;
        } else if (!letByTrust.empty()) {
            const EntityId candidate = letByTrust.at(state_.random.UniformInt(
                0, static_cast<std::uint32_t>(letByTrust.size() - 1)));
            const auto room = rooms_.Find(candidate);
            if (room && room->room->owner == housing_ && means >= PriceOf(*room->room) + keep) {
                target = candidate;
            }
        }
        if (target.IsValid()) {
            const auto room = rooms_.Find(target);
            Withdraw(robot, PriceOf(*room->room) - robot.credits);
            Transfer(target, robot.id, PriceOf(*room->room));
            std::erase(letByTrust, target);
        }
    }
}

}  // namespace mechapunk::sim
