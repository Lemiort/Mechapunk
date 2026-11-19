// The day's economic and social statistics, and names for sectors and
// emission reasons.

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

// namespace

std::string_view SectorName(Sector sector) noexcept {
    switch (sector) {
        case Sector::Industry: return "industry";
        case Sector::Venues: return "venues";
        case Sector::Housing: return "housing";
        case Sector::Utilities: return "utilities";
        case Sector::Transit: return "transit";
        case Sector::Banking: return "banking";
        case Sector::Government: return "government";
    }
    return "";
}

std::string_view EmissionReasonName(EmissionReason reason) noexcept {
    switch (reason) {
        case EmissionReason::Founding: return "founding";
        case EmissionReason::NewcomerSavings: return "newcomer's savings";
        case EmissionReason::BankRescue: return "bank rescue";
        case EmissionReason::Repaid: return "repaid";
        case EmissionReason::Interest: return "interest";
    }
    return "";
}

std::int64_t GiniPerMille(std::vector<std::int64_t> wealth) {
    // Debts count as having nothing, so the measure stays within 0..1000.
    for (std::int64_t& each : wealth) {
        each = std::max<std::int64_t>(each, 0);
    }
    std::ranges::sort(wealth);
    std::int64_t total = 0;
    std::int64_t weighted = 0;
    const auto count = static_cast<std::int64_t>(wealth.size());
    for (std::int64_t index = 0; index < count; ++index) {
        total += wealth[static_cast<std::size_t>(index)];
        weighted += (index + 1) * wealth[static_cast<std::size_t>(index)];
    }
    if (total == 0 || count == 0) {
        return 0;
    }
    // G = 2 sum(i x_i) / (n sum x) - (n + 1) / n
    const long double gini = (2.0L * static_cast<long double>(weighted)) /
                                 (static_cast<long double>(count) * static_cast<long double>(total)) -
                             (static_cast<long double>(count + 1) / static_cast<long double>(count));
    return std::clamp<std::int64_t>(static_cast<std::int64_t>(gini * 1000.0L), 0, 1000);
}

void World::RecordStatistics() {
    const WorldState& state = state_;
    DailyStats stats{.day = DayNumber(state.tick - 1), .counted = state.today};
    const auto count = static_cast<std::int64_t>(state.robots.size());
    stats.population = count;
    std::unordered_map<std::uint64_t, std::int64_t> property;
    std::unordered_set<std::uint64_t> lived;
    for (const Robot& robot : state.robots) {
        lived.insert(robot.apartment.value);
    }
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const Room& room = *apartment.room;
        property[room.owner.value] += PriceOf(room);
        if (!lived.contains(room.id.value)) {
            ++stats.vacantApartments;
        } else if (FindOrganization(room.owner) != nullptr) {
            ++stats.corporateApartments;
        }
    }
    std::unordered_set<std::uint64_t> shareholders;
    for (const Organization& organization : state.economy.organizations) {
        for (const Share& share : organization.owners) {
            if (FindRobot(share.owner) != nullptr) {
                shareholders.insert(share.owner.value);
            }
        }
        switch (organization.kind) {
            case OrganizationKind::Corporation:
                stats.corporationMoney += organization.credits;
                break;
            case OrganizationKind::Government:
                stats.governmentMoney += organization.credits;
                break;
            default:
                stats.bankMoney += organization.credits;
                break;
        }
    }
    stats.shareholders = static_cast<std::int64_t>(shareholders.size());
    for (const Loan& loan : state.economy.loans) {
        if (FindRobot(loan.borrower) != nullptr) {
            stats.robotDebt += loan.owed;
            ++stats.indebtedRobots;
        } else if (loan.lender == centralBank_) {
            stats.bankDebt += loan.owed;
        } else if (loan.borrower == cityHall_) {
            stats.governmentDebt += loan.owed;
        } else {
            stats.corporationDebt += loan.owed;
        }
    }

    std::vector<std::int64_t> credits;
    std::vector<std::int64_t> wealth;
    credits.reserve(state.robots.size());
    wealth.reserve(state.robots.size());
    std::int64_t payPercent = 0;
    std::int64_t industry = 0;
    for (const Organization& organization : state.economy.organizations) {
        if (organization.sector == Sector::Industry) {
            payPercent += organization.payPercent;
            ++industry;
        }
    }
    stats.averagePayPercent = industry > 0 ? payPercent / industry : 100;
    stats.supplyPercent = state.economy.supplyPercent;
    stats.policyRate = state.economy.policyRate;
    {
        std::array<std::int64_t, 5> rents{};
        std::array<std::int64_t, 5> counts{};
        for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
            const auto kind = static_cast<std::size_t>(apartment.room->apartmentClass);
            if (kind >= 1 && kind <= 5) {
                rents.at(kind - 1) += RentFor(*apartment.room);
                ++counts.at(kind - 1);
            }
        }
        for (std::size_t kind = 0; kind < rents.size(); ++kind) {
            stats.averageRent.at(kind) = counts[kind] > 0 ? rents[kind] / counts[kind] : 0;
        }
        std::int64_t percent = 0;
        std::int64_t venues = 0;
        for (const RoomKind kind : VenueKinds()) {
            for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
                percent += venue.room->pricePercent;
                ++venues;
            }
        }
        stats.venuePricePercent = venues > 0 ? percent / venues : 100;
        stats.openVenues = venues;
        stats.emptyUnits = static_cast<std::int64_t>(rooms_.RoomsOfKind(RoomKind::VacantStore).size());
    }
    for (const Robot& robot : state.robots) {
        stats.robotMoney += robot.credits;
        stats.robotSavings += robot.savings;
        if (robot.workspace.IsValid()) {
            ++stats.employed;
            ++stats.jobLevels.at(std::min<std::size_t>(robot.jobLevel, stats.jobLevels.size() - 1));
        }
        stats.averageCharge += robot.needs.charge;
        stats.averageCondition += robot.needs.condition;
        stats.averageSocial += robot.needs.social;
        stats.averageFun += robot.needs.fun;
        stats.averageAge += robot.age;
        stats.averageWear += robot.wear;
        bool friendly = false;
        bool close = false;
        bool rival = false;
        for (const Bond& bond : robot.bonds) {
            friendly = friendly || bond.level == BondLevel::Friend || bond.level == BondLevel::CloseFriend;
            close = close || bond.level == BondLevel::CloseFriend;
            rival = rival || bond.level == BondLevel::Rival;
        }
        stats.withFriend += friendly ? 1 : 0;
        stats.withCloseFriend += close ? 1 : 0;
        stats.withRival += rival ? 1 : 0;
        stats.lonely += friendly ? 0 : 1;
        const auto home = rooms_.Find(robot.apartment);
        stats.broke += robot.credits + robot.savings < RentOf(robot) ? 1 : 0;
        if (home && home->room->apartmentClass != ApartmentClass::None) {
            ++stats.residentsByClass.at(static_cast<std::size_t>(home->room->apartmentClass) - 1);
            stats.sharing += ResidentsOf(robot.apartment) > 1 ? 1 : 0;
        }
        stats.behindOnBills += robot.rentOwed > 0 ? 1 : 0;
        if (home && home->room->owner == robot.id) {
            ++stats.homeowners;
        } else {
            ++stats.tenants;
        }
        const auto owned = property.find(robot.id.value);
        const std::int64_t propertyValue = owned != property.end() ? owned->second : 0;
        if (owned != property.end() &&
            propertyValue > (home && home->room->owner == robot.id ? PriceOf(*home->room) : 0)) {
            ++stats.landlords;
        }
        const Loan* loan = LoanOf(robot.id);
        credits.push_back(robot.credits + robot.savings);
        wealth.push_back(robot.credits + robot.savings + propertyValue - robot.rentOwed - (loan != nullptr ? loan->owed : 0));
    }
    stats.unemployed = count - stats.employed;
    if (count > 0) {
        stats.averageCharge /= count;
        stats.averageCondition /= count;
        stats.averageSocial /= count;
        stats.averageFun /= count;
        stats.averageAge /= count;
        stats.averageWear /= count;
        stats.meanCredits = (stats.robotMoney + stats.robotSavings) / count;
        std::int64_t total = 0;
        for (const std::int64_t each : wealth) {
            total += each;
        }
        stats.meanWealth = total / count;
        std::ranges::sort(credits);
        std::vector<std::int64_t> sorted = wealth;
        std::ranges::sort(sorted);
        stats.medianCredits = credits[credits.size() / 2];
        stats.medianWealth = sorted[sorted.size() / 2];
        std::int64_t positive = 0;
        std::int64_t top = 0;
        std::int64_t bottom = 0;
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            const std::int64_t each = std::max<std::int64_t>(sorted[index], 0);
            positive += each;
            top += index >= sorted.size() - std::max<std::size_t>(sorted.size() / 10, 1) ? each : 0;
            bottom += index < sorted.size() / 2 ? each : 0;
        }
        stats.topTenthShare = positive > 0 ? top * 1000 / positive : 0;
        stats.bottomHalfShare = positive > 0 ? bottom * 1000 / positive : 0;
    }
    stats.giniWealth = GiniPerMille(std::move(wealth));
    stats.moneySupply = stats.robotMoney + stats.corporationMoney + stats.bankMoney +
                        stats.governmentMoney + state.economy.supplyMarket;
    stats.createdTotal = state.economy.created;
    stats.destroyedTotal = state.economy.destroyed;
    state_.history.push_back(stats);
    if (state_.history.size() > kHistoryDays) {
        state_.history.erase(state_.history.begin());
    }
}

}  // namespace mechapunk::sim
