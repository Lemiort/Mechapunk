// The nightly settlement: supply market, interest, pay, City Hall's taxes and
// spending, and the central bank's policy rate.

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

}  // namespace

void World::SetPolicyRate() {
    Economy& economy = state_.economy;
    // Savers earn half the rate on what their bank holds for them: the bank
    // owes them more, no money moves until they take it out.
    for (Robot& robot : state_.robots) {
        const std::int64_t interest = robot.savings * economy.policyRate / 2 / 10000;
        Organization* bank = MutableOrganization(robot.bank);
        if (interest <= 0 || bank == nullptr) {
            continue;
        }
        robot.savings += interest;
        bank->today.wages += interest;  // A cost of the bank's, like wages.
        state_.today.savingsInterest += interest;
    }
    // Up a point while the central bank created over a thousandth of the
    // money in the city today, down a point after a day it created under a
    // ten-thousandth (newcomers' savings, say).
    const std::int64_t money = std::max<std::int64_t>(1, economy.created - economy.destroyed);
    const std::int64_t created = state_.today.created - state_.today.destroyed;
    if (created * 1000 > money) {
        economy.policyRate = std::min(static_cast<std::int64_t>(state_.settings.economy.maxPolicyRate), economy.policyRate + 1);
    } else if (created * 10000 < money) {
        economy.policyRate = std::max(static_cast<std::int64_t>(state_.settings.economy.minPolicyRate), economy.policyRate - 1);
    }
}

void World::RunCityHall() {
    Organization* city = MutableOrganization(cityHall_);
    if (city == nullptr) {
        return;
    }
    // A fifth of every corporation's and bank's profit today.
    for (Organization& organization : state_.economy.organizations) {
        if (organization.kind != OrganizationKind::Corporation &&
            organization.kind != OrganizationKind::Bank) {
            continue;
        }
        const Books& books = organization.today;
        const std::int64_t profit =
            books.revenue - (books.wages + books.supplies + books.upkeep + books.rent + books.taxes);
        const std::int64_t tax = std::min(organization.credits, std::max<std::int64_t>(0, profit) *
                                                                    static_cast<std::int64_t>(state_.settings.economy.profitTaxPercent) / 100);
        if (tax <= 0) {
            continue;
        }
        Pay(organization.id, cityHall_, tax);
        organization.today.taxes += tax;
        city->today.revenue += tax;
        state_.today.profitTax += tax;
    }
    // Support for every robot out of work, borrowed if the treasury is bare.
    for (Robot& robot : state_.robots) {
        if (!robot.workspace.IsValid()) {
            PayOrBorrow(*city, robot.id, static_cast<std::int64_t>(state_.settings.economy.benefit));
            city->today.wages += static_cast<std::int64_t>(state_.settings.economy.benefit);
            state_.today.benefits += static_cast<std::int64_t>(state_.settings.economy.benefit);
        }
    }
    // Public works: half of what City Hall has beyond twice its reserve
    // buys supplies from industry, unless it is in debt.
    const std::int64_t reserve = std::max<std::int64_t>(
        static_cast<std::int64_t>(state_.settings.economy.corporationFloor) * 5, 3 * (city->today.wages + city->today.upkeep));
    const std::int64_t spare = city->credits - (2 * reserve);
    if (spare > 0 && LoanOf(cityHall_) == nullptr) {
        const std::int64_t works = spare / 2;
        city->credits -= works;
        city->today.supplies += works;
        state_.economy.supplyMarket += works;
        state_.today.supplies += works;
        state_.today.publicSpending += works;
    }
}

void World::SettleEconomy() {
    Economy& economy = state_.economy;
    // The supply market pays industry for the day's supplies, in proportion
    // to the wages each corporation paid.
    std::int64_t wageBill = 0;
    std::vector<Organization*> industry;
    for (Organization& organization : economy.organizations) {
        if (organization.sector == Sector::Industry) {
            industry.push_back(&organization);
            wageBill += organization.today.wages;
        }
    }
    if (!industry.empty() && economy.supplyMarket > 0) {
        std::int64_t left = economy.supplyMarket;
        for (std::size_t index = 0; index < industry.size(); ++index) {
            Organization& corporation = *industry[index];
            const std::int64_t share =
                index + 1 == industry.size()
                    ? left
                    : (wageBill > 0 ? economy.supplyMarket * corporation.today.wages / wageBill
                                    : economy.supplyMarket / static_cast<std::int64_t>(industry.size()));
            corporation.credits += share;
            corporation.today.revenue += share;
            left -= share;
        }
        economy.supplyMarket = 0;
    }

    // Interest, then repayments from whatever borrowers can spare.
    for (Loan& loan : economy.loans) {
        // The central bank lends at its rate; banks add their margin.
        const std::int64_t rate =
            economy.policyRate + (loan.lender == centralBank_ ? 0 : static_cast<std::int64_t>(state_.settings.economy.bankMargin));
        const std::int64_t interest = (loan.owed * rate + 9999) / 10000;
        loan.owed += interest;
        state_.today.interest += interest;
        if (Organization* lender = MutableOrganization(loan.lender)) {
            lender->today.revenue += interest;
        }
    }
    for (Loan& loan : economy.loans) {
        std::int64_t spare = 0;
        if (const Robot* robot = FindRobot(loan.borrower)) {
            const auto home = rooms_.Find(robot->apartment);
            spare = robot->credits - (2 * (home ? RentFor(*home->room) : 0));
        } else if (const Organization* organization = FindOrganization(loan.borrower)) {
            spare = organization->credits - ReserveOf(*organization);
        }
        const std::int64_t repaid = std::clamp<std::int64_t>(spare, 0, loan.owed);
        if (repaid <= 0) {
            continue;
        }
        if (loan.lender == centralBank_) {
            ChangeMoney(loan.borrower, -repaid, EmissionReason::Repaid);
        } else {
            Pay(loan.borrower, loan.lender, repaid);
        }
        loan.owed -= repaid;
        if (Books* books = BooksOf(loan.borrower)) {
            books->repaid += repaid;
        }
    }
    if (std::erase_if(economy.loans, [](const Loan& loan) { return loan.owed <= 0; }) > 0) {
        IndexEconomy();
    }

    // The price of supplies follows industry's books: it must cover the
    // wages and pay down a fiftieth of industry's debts a day. It moves by up
    // to five points a day, faster the further off it is.
    {
        std::int64_t revenue = 0;
        std::int64_t needed = 0;
        for (const Organization* corporation : industry) {
            revenue += corporation->today.revenue;
            needed += corporation->today.wages;
            if (const Loan* loan = LoanOf(corporation->id)) {
                needed += loan->owed / 50;
            }
        }
        if (needed > 0) {
            const std::int64_t gapPercent = (needed - revenue) * 100 / needed;
            const std::int64_t step = std::clamp<std::int64_t>(std::abs(gapPercent) / 2, 1, 5);
            std::int64_t& price = economy.supplyPercent;
            if (gapPercent > 0) {
                price = std::min(static_cast<std::int64_t>(state_.settings.economy.maxSupplyPercent), price + step);
            } else if (gapPercent < -2) {
                price = std::max(static_cast<std::int64_t>(state_.settings.economy.minSupplyPercent), price - step);
            }
        }
    }

    // Employers pay what they can afford: less while losing money, more
    // while earning it.
    for (Organization& employer : economy.organizations) {
        if (employer.sector != Sector::Industry && employer.sector != Sector::Venues) {
            continue;
        }
        Organization* corporation = &employer;
        const Books& books = corporation->today;
        const std::int64_t costs = books.wages + books.supplies;
        if (books.revenue < costs) {
            corporation->payPercent = std::max(static_cast<std::uint32_t>(state_.settings.economy.minPayPercent), corporation->payPercent - 1);
        } else if (LoanOf(corporation->id) == nullptr && books.revenue > costs) {
            corporation->payPercent = std::min(static_cast<std::uint32_t>(state_.settings.economy.maxPayPercent), corporation->payPercent + 1);
        }
    }

    RunCityHall();
    SetPolicyRate();

    // Dividends from what organizations without debts have to spare.
    for (Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank || organization.owners.empty() ||
            LoanOf(organization.id) != nullptr) {
            continue;
        }
        const std::int64_t spare = organization.credits - (2 * ReserveOf(organization));
        if (spare <= 0) {
            continue;
        }
        // Dividends come out of the day's profit, never the capital; a
        // bank's credits are mostly its savers' money besides.
        const Books& books = organization.today;
        const std::int64_t profit =
            books.revenue - (books.wages + books.supplies + books.upkeep + books.rent);
        std::int64_t payout = std::min(spare, profit) * static_cast<std::int64_t>(state_.settings.economy.dividendPercent) / 100;
        if (payout <= 0) {
            continue;
        }
        std::int64_t shares = 0;
        for (const Share& share : organization.owners) {
            shares += share.shares;
        }
        const EntityId payer = organization.id;
        for (const Share& share : organization.owners) {
            const std::int64_t dividend = payout * share.shares / shares;
            Pay(payer, share.owner, dividend);
            MutableOrganization(payer)->today.dividends += dividend;
            state_.today.dividends += dividend;
            if (Books* books = BooksOf(share.owner)) {
                books->revenue += dividend;
            }
        }
    }

    // Robots bank what they do not need in their pocket, or take some out,
    // and start a new day's budget.
    for (Robot& robot : state_.robots) {
        robot.spentToday = 0;
        const std::int64_t pocket = PocketOf(robot);
        if (robot.credits > 2 * pocket) {
            const std::int64_t deposit = robot.credits - pocket;
            Pay(robot.id, robot.bank, deposit);
            robot.savings += deposit;
        } else if (robot.credits < pocket) {
            Withdraw(robot, pocket - robot.credits);
        }
    }

    ReviewUnits();
    SetPrices();
    RecordStatistics();
    for (Organization& organization : economy.organizations) {
        organization.yesterday = organization.today;
        organization.today = {};
    }
    state_.today = {};
}

}  // namespace mechapunk::sim
