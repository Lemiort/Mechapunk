// Accounts and every way money moves: Pay, Lend, Withdraw, PayBill,
// PayOrBorrow, Sell, wages and what robots can afford. The money invariant
// lives here.

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

constexpr std::int64_t kUtilitySupplyPercent = 95;

constexpr std::int64_t kTransitSupplyPercent = 95;

constexpr std::int64_t kHousingSupplyPercent = 50;

// A robot with a job may owe up to this many shifts' wages.
constexpr std::int64_t kRobotDebtShifts = 5;

// A robot's daily budget for outings: this share of its average day's pay,
// plus 1/kSavingsSpendDays of what it has saved beyond kSavingsTargetNights
// of its bills (savings put by for a rainy day), plus a little.
constexpr std::int64_t kBudgetPercent = 80;

constexpr std::int64_t kSavingsSpendDays = 10;

constexpr std::int64_t kSavingsTargetNights = 30;

constexpr std::int64_t kBaseBudget = 10;

constexpr std::int64_t kWorkDaysPerWeek = 5;

// Robots with more saved than this spend a quarter of what is left of
// their budget on every visit, on better drinks, parts and service.
constexpr std::int64_t kComfortableSavings = 1000;

}  // namespace

const Organization* World::FindOrganization(EntityId id) const noexcept {
    const auto& organizations = state_.economy.organizations;
    const auto found = std::ranges::lower_bound(organizations, id.value, {},
                                                [](const Organization& each) { return each.id.value; });
    return found != organizations.end() && found->id == id ? &*found : nullptr;
}

Organization* World::MutableOrganization(EntityId id) noexcept {
    return const_cast<Organization*>(std::as_const(*this).FindOrganization(id));
}

Room& World::MutableRoom(EntityId id) {
    const auto room = rooms_.Find(id);
    if (!room) {
        throw std::logic_error("No such room");
    }
    // The index points into state_.city, which this world owns and may
    // change; only the room's owner changes, never what the index keys on.
    return const_cast<Room&>(*room->room);
}

const Loan* World::LoanOf(EntityId borrower) const noexcept {
    const auto found = loanOf_.find(borrower.value);
    return found != loanOf_.end() ? &state_.economy.loans[found->second] : nullptr;
}

std::string_view World::AccountName(EntityId account) const {
    if (const Organization* organization = FindOrganization(account)) {
        return organization->name;
    }
    return NameOf(account);
}

std::int64_t& World::Balance(EntityId account) {
    if (Organization* organization = MutableOrganization(account)) {
        return organization->credits;
    }
    if (Robot* robot = MutableRobot(account)) {
        return robot->credits;
    }
    throw std::logic_error(std::format("No account {}", account.value));
}

Books* World::BooksOf(EntityId account) {
    Organization* organization = MutableOrganization(account);
    return organization != nullptr ? &organization->today : nullptr;
}

void World::Pay(EntityId from, EntityId to, std::int64_t amount) {
    if (amount <= 0 || from == to) {
        return;
    }
    std::int64_t& payer = Balance(from);
    if (payer < amount) {
        throw std::logic_error(std::format("Account {} cannot pay {}", from.value, amount));
    }
    payer -= amount;
    Balance(to) += amount;
}

void World::ChangeMoney(EntityId account, std::int64_t amount, EmissionReason reason) {
    Economy& economy = state_.economy;
    Balance(account) += amount;
    if (amount >= 0) {
        economy.created += amount;
        state_.today.created += amount;
    } else {
        economy.destroyed -= amount;
        state_.today.destroyed -= amount;
    }
    economy.emissions.push_back(
        {.tick = state_.tick, .account = account, .amount = amount, .reason = reason});
    if (economy.emissions.size() > kEmissionLogSize) {
        economy.emissions.erase(economy.emissions.begin());
    }
    Emit({.tick = state_.tick,
          .kind = EventKind::MoneySupplyChanged,
          .credits = amount,
          .account = account,
          .emission = reason});
}

void World::Lend(EntityId lender, EntityId borrower, std::int64_t amount) {
    if (amount <= 0) {
        return;
    }
    Organization* bank = MutableOrganization(lender);
    if (bank == nullptr) {
        throw std::logic_error("Lending without a lender");
    }
    if (bank->kind == OrganizationKind::CentralBank) {
        // Money the city did not have.
        ChangeMoney(lender, amount, EmissionReason::BankRescue);
    } else if (bank->credits - amount < kBankFloor) {
        Lend(bank->bank, bank->id, amount + kBankFloor - bank->credits + (amount / 2));
        bank = MutableOrganization(lender);
    }
    Pay(lender, borrower, amount);
    auto& loans = state_.economy.loans;
    if (const auto found = loanOf_.find(borrower.value); found != loanOf_.end()) {
        loans[found->second].owed += amount;
    } else {
        loanOf_[borrower.value] = loans.size();
        loans.push_back({.id = state_.ids.Allocate(),
                         .lender = lender,
                         .borrower = borrower,
                         .owed = amount,
                         .takenAt = state_.tick});
    }
    state_.today.loansTaken += 1;
    state_.today.lent += amount;
}

bool World::MayBorrow(const Robot& robot, std::int64_t amount) const {
    if (!robot.workspace.IsValid() || !robot.bank.IsValid()) {
        return false;
    }
    const Loan* loan = LoanOf(robot.id);
    const std::int64_t owed = loan != nullptr ? loan->owed : 0;
    return owed + amount <= kRobotDebtShifts * WageOf(robot);
}

std::int64_t World::PocketOf(const Robot& robot) const {
    return std::max<std::int64_t>(60, 6 * RentOf(robot));
}

void World::Withdraw(Robot& robot, std::int64_t amount) {
    amount = std::min(amount, robot.savings);
    if (amount <= 0) {
        return;
    }
    Organization* bank = MutableOrganization(robot.bank);
    if (bank->credits - amount < 0) {
        // A run on the bank: the central bank lends it what it lacks.
        Lend(bank->bank, bank->id, amount - bank->credits + kBankFloor);
    }
    robot.savings -= amount;
    Pay(robot.bank, robot.id, amount);
}

std::int64_t World::PayBill(Robot& robot, EntityId to, std::int64_t amount) {
    if (amount <= 0) {
        return 0;
    }
    if (robot.credits < amount) {
        Withdraw(robot, amount - robot.credits);
    }
    if (robot.credits < amount && MayBorrow(robot, amount - robot.credits)) {
        const bool first = LoanOf(robot.id) == nullptr;
        const std::int64_t borrowed = amount - robot.credits;
        Lend(robot.bank, robot.id, borrowed);
        Emit({.tick = state_.tick, .kind = EventKind::Borrowed, .robot = robot.id, .credits = borrowed});
        if (first) {
            Remember(robot, MemoryKind::TookLoan, {}, {});
        }
    }
    const std::int64_t paid = std::min(robot.credits, amount);
    Pay(robot.id, to, paid);
    robot.rentOwed += amount - paid;
    return paid;
}

void World::PayOrBorrow(Organization& payer, EntityId to, std::int64_t amount) {
    if (amount <= 0) {
        return;
    }
    const EntityId id = payer.id;
    if (payer.credits < amount) {
        Lend(payer.bank, id, amount - payer.credits);
    }
    Pay(id, to, amount);
}

void World::PaySupplies(Organization& buyer, std::int64_t revenue) {
    std::int64_t percent = 0;
    switch (buyer.sector) {
        case Sector::Venues: percent = state_.economy.supplyPercent; break;
        case Sector::Utilities: percent = kUtilitySupplyPercent; break;
        case Sector::Transit: percent = kTransitSupplyPercent; break;
        case Sector::Housing: percent = kHousingSupplyPercent; break;
        default: return;
    }
    const std::int64_t supplies = std::min(buyer.credits, revenue * percent / 100);
    if (supplies <= 0) {
        return;
    }
    buyer.credits -= supplies;
    buyer.today.supplies += supplies;
    state_.economy.supplyMarket += supplies;
    state_.today.supplies += supplies;
}

std::int64_t World::LuxuryOf(const Robot& robot, std::int64_t price) const {
    if (robot.savings <= kComfortableSavings) {
        return 0;
    }
    return std::max<std::int64_t>(0, BudgetOf(robot) - robot.spentToday - price) / 4;
}

std::int64_t World::PayOf(const Robot& robot) const {
    const auto job = rooms_.Find(robot.workspace);
    const Organization* employer = job ? FindOrganization(job->room->owner) : nullptr;
    const std::int64_t percent = employer != nullptr ? employer->payPercent : 100;
    return static_cast<std::int64_t>(WageOf(robot)) * percent / 100;
}

std::int64_t World::NetPayOf(const Robot& robot) const {
    const std::int64_t wage = PayOf(robot);
    return wage - (wage * kWageTaxPercent / 100);
}

std::int64_t World::BudgetOf(const Robot& robot) const {
    // Out of work, a robot lives on City Hall's support.
    const std::int64_t pay =
        robot.workspace.IsValid() ? NetPayOf(robot) * kWorkDaysPerWeek / kDaysPerWeek : kBenefit;
    const std::int64_t target = kSavingsTargetNights * RentOf(robot);
    const std::int64_t spare = std::max<std::int64_t>(0, robot.savings - target);
    return (pay * kBudgetPercent / 100) + (spare / kSavingsSpendDays) + kBaseBudget;
}

void World::Sell(Robot& buyer, EntityId seller, std::int64_t price) {
    const std::int64_t paid = std::min(buyer.credits, price);
    if (paid <= 0) {
        return;
    }
    Pay(buyer.id, seller, paid);
    if (Organization* organization = MutableOrganization(seller)) {
        organization->today.revenue += paid;
        PaySupplies(*organization, paid);
    }
}

void World::PayWage(Robot& robot) {
    const auto workspace = rooms_.Find(robot.workspace);
    Organization* employer = workspace ? MutableOrganization(workspace->room->owner) : nullptr;
    if (employer == nullptr) {
        throw std::logic_error("A workspace without an owner");
    }
    // The employer pays the wage, City Hall's share of it straight to City
    // Hall.
    const std::int64_t wage = PayOf(robot);
    const std::int64_t tax = wage * kWageTaxPercent / 100;
    employer->today.wages += wage;
    PayOrBorrow(*employer, robot.id, wage - tax);
    PayOrBorrow(*MutableOrganization(employer->id), cityHall_, tax);
    if (Organization* city = MutableOrganization(cityHall_)) {
        city->today.revenue += tax;
    }
    state_.today.wages += wage;
    state_.today.wageTax += tax;
    Emit({.tick = state_.tick,
          .kind = EventKind::ShiftEnded,
          .robot = robot.id,
          .room = robot.workspace,
          .credits = wage - tax});
}

}  // namespace mechapunk::sim
