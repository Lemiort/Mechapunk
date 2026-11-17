#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"

namespace mechapunk::sim {

// The city's money is closed: credits only move from one account to
// another (robots and organizations hold accounts). The one exception is
// the central bank, which creates money when a bank runs short and
// destroys what banks pay it back; every such change is logged as an
// Emission, so the money in all accounts always equals what was ever
// created minus what was destroyed.
//
// Corporations own every workspace and venue, and the apartments nobody
// else does; robots own the apartments they start in, and buy and sell
// them later. Corporations have owners (robots, banks or other
// corporations) who share their profits.

enum class OrganizationKind : std::uint8_t { Corporation, Bank, CentralBank };

// What a corporation lives on.
enum class Sector : std::uint8_t {
    Industry,   // Workspaces: paid for the supplies every other sector buys.
    Venues,     // Bars, diners, shops...: paid by their guests.
    Housing,    // Apartments nobody else owns: paid rent.
    Utilities,  // Power and water: paid upkeep for every lived-in apartment.
    Transit,    // The metro: paid fares.
    Banking,    // Banks and the central bank: paid back with interest.
};

struct Share {
    EntityId owner;  // A robot or an organization.
    std::uint32_t shares{};
};

// What an organization paid and earned over a day.
struct Books {
    std::int64_t revenue{};  // Sales, rent, upkeep, fares, supplies, interest.
    std::int64_t wages{};
    std::int64_t supplies{};  // Paid into the supply market.
    std::int64_t upkeep{};    // Paid for apartments it lets.
    std::int64_t rent{};      // Paid for apartments it bought from robots.
    std::int64_t dividends{};
    std::int64_t repaid{};    // Paid back on loans.
};

struct Organization {
    EntityId id;
    std::string name;
    OrganizationKind kind{OrganizationKind::Corporation};
    Sector sector{Sector::Industry};
    std::int64_t credits{};
    // Sorted by owner id; the central bank belongs to nobody.
    std::vector<Share> owners;
    // Where it borrows: a bank for corporations, the central bank for banks.
    EntityId bank;
    // Industry: wages paid, in percent of the workplaces' base wages. It
    // falls while the corporation loses money and rises while it earns.
    std::uint32_t payPercent{100};
    Books today;
    Books yesterday;
};

struct Loan {
    EntityId id;
    EntityId lender;    // A bank, or the central bank lending to a bank.
    EntityId borrower;  // A robot, a corporation or a bank.
    std::int64_t owed{};  // Still to pay back, interest included.
    Tick takenAt{};
};

enum class EmissionReason : std::uint8_t {
    Founding,         // The money the city started with.
    NewcomerSavings,  // Savings a newcomer brought to the city.
    BankRescue,       // Lent to a bank short of money to lend or pay out.
    Repaid,           // A bank paid the central bank back (destroyed).
    Interest,         // Interest paid to the central bank (destroyed).
};

// A change to the amount of money in the city: positive when created,
// negative when destroyed.
struct Emission {
    Tick tick{};
    EntityId account;  // Who got the money, or paid it back.
    std::int64_t amount{};
    EmissionReason reason{};
};

// Counted through the day, and turned into DailyStats at midnight.
struct DayCounters {
    std::int64_t walks{};
    std::int64_t metroRides{};
    std::int64_t visits{};
    std::int64_t lateArrivals{};
    std::int64_t talks{};
    std::int64_t arguments{};
    std::int64_t wages{};
    std::int64_t venueSales{};
    std::int64_t rent{};
    std::int64_t upkeep{};
    std::int64_t fares{};
    std::int64_t supplies{};
    std::int64_t dividends{};
    std::int64_t interest{};
    std::int64_t created{};
    std::int64_t destroyed{};
    std::int64_t loansTaken{};
    std::int64_t lent{};
    std::int64_t writtenOff{};
    std::int64_t propertySales{};
    std::int64_t propertySalesValue{};
    std::int64_t promotions{};
    std::int64_t jobsLost{};
    std::int64_t jobsFound{};
    std::int64_t movedUp{};
    std::int64_t evictions{};
    std::int64_t newFriendships{};
    std::int64_t newRivalries{};
    std::int64_t arrivals{};
    std::int64_t scrapped{};
    std::int64_t badParts{};
};

// The state of the city at the end of a day: how robots live and how the
// money is spread, for measuring the economy and society over time.
// Credits; ratios in per mille.
struct DailyStats {
    std::uint64_t day{};
    DayCounters counted;
    // Society.
    std::int64_t population{};
    std::int64_t employed{};
    std::int64_t unemployed{};
    std::int64_t averageCharge{};
    std::int64_t averageCondition{};
    std::int64_t averageSocial{};
    std::int64_t averageFun{};
    std::int64_t averageAge{};
    std::int64_t averageWear{};
    std::int64_t withFriend{};
    std::int64_t withCloseFriend{};
    std::int64_t withRival{};
    std::int64_t lonely{};  // No friend at all.
    std::array<std::int64_t, 4> jobLevels{};
    // Money.
    std::int64_t moneySupply{};
    std::int64_t robotMoney{};
    std::int64_t robotSavings{};  // Held by banks for robots.
    std::int64_t corporationMoney{};
    std::int64_t bankMoney{};
    std::int64_t createdTotal{};
    std::int64_t destroyedTotal{};
    std::int64_t robotDebt{};
    std::int64_t corporationDebt{};
    std::int64_t bankDebt{};  // Owed to the central bank.
    std::int64_t indebtedRobots{};
    std::int64_t meanCredits{};
    std::int64_t medianCredits{};
    std::int64_t meanWealth{};  // Credits and property, less debts.
    std::int64_t medianWealth{};
    std::int64_t giniWealth{};      // Per mille: 0 equal, 1000 one has all.
    std::int64_t topTenthShare{};   // Per mille of all wealth.
    std::int64_t bottomHalfShare{}; // Per mille of all wealth.
    std::int64_t broke{};           // Robots with less than tonight's bill.
    std::int64_t behindOnBills{};
    std::int64_t averagePayPercent{};
    std::int64_t supplyPercent{};
    // Property.
    std::int64_t homeowners{};
    std::int64_t tenants{};
    std::int64_t landlords{};
    std::int64_t corporateApartments{};
    std::int64_t vacantApartments{};
    std::int64_t shareholders{};  // Robots owning part of an organization.
};

struct Economy {
    // Sorted by id.
    std::vector<Organization> organizations;
    // Sorted by id.
    std::vector<Loan> loans;
    std::int64_t created{};
    std::int64_t destroyed{};
    // The latest emissions, oldest first.
    std::vector<Emission> emissions;
    // Paid today for supplies; shared among industry at midnight in
    // proportion to the wages each corporation paid.
    std::int64_t supplyMarket{};
    // The price of supplies: the share of their takings, in percent, venues
    // pay industry. It rises while industry loses money and falls while it
    // makes a profit.
    std::int64_t supplyPercent{60};
};

// The price of an apartment: so many nights of its rent.
inline constexpr std::int64_t kPriceNights = 100;
// Owners of a lived-in apartment pay this much of its rent each night for
// power and water, in percent.
inline constexpr std::int64_t kUpkeepPercent = 50;
// Robots sell to the housing trust at this much of the price, in percent.
inline constexpr std::int64_t kSellPercent = 90;
// Interest added to every loan each night, in units of 1/10000.
inline constexpr std::int64_t kInterestPerNight = 10;
// Days of statistics kept.
inline constexpr std::size_t kHistoryDays = 400;
// Emissions kept in the log.
inline constexpr std::size_t kEmissionLogSize = 500;

[[nodiscard]] std::string_view SectorName(Sector sector) noexcept;
[[nodiscard]] std::string_view EmissionReasonName(EmissionReason reason) noexcept;
// Wealth inequality of a population: 0 when equal, 1000 when one has it all.
[[nodiscard]] std::int64_t GiniPerMille(std::vector<std::int64_t> wealth);

}  // namespace mechapunk::sim
