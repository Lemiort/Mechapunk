#include "mechapunk/sim/Settings.hpp"

#include <array>
#include <format>
#include <optional>

namespace mechapunk::sim {
namespace {

struct Entry {
    std::string_view name;
    std::int64_t& (*field)(SimSettings&);
    std::int64_t minimum;
    std::int64_t maximum;
};

#define MECHAPUNK_SETTING(group, member, low, high)                        \
    Entry {                                                                \
        #group "." #member, [](SimSettings& settings) -> std::int64_t& {   \
            return settings.group.member;                                  \
        }, low, high                                                       \
    }

const std::array kEntries{
    MECHAPUNK_SETTING(crime, robberyPercent, 0, 100),
    MECHAPUNK_SETTING(crime, gangOutingPercent, 0, 100),
    MECHAPUNK_SETTING(crime, turfReachMeters, 0, 100000),
    MECHAPUNK_SETTING(crime, patrolReachMinutes, 0, 1440),
    MECHAPUNK_SETTING(crime, patrolStayMinutes, 1, 480),
    MECHAPUNK_SETTING(crime, crimeLookbackMinutes, 0, 1440),
    MECHAPUNK_SETTING(construction, streakNights, 1, 365),
    MECHAPUNK_SETTING(construction, habitatFloors, 1, 60),
    MECHAPUNK_SETTING(construction, workplaceFloors, 1, 60),
    MECHAPUNK_SETTING(construction, siteBuilders, 1, 100),
    MECHAPUNK_SETTING(construction, siteSuppliesNight, 0, 1000000),
    MECHAPUNK_SETTING(construction, marketVacancyPercent, 0, 100),
    MECHAPUNK_SETTING(construction, freeBedsPercent, 0, 100),
    MECHAPUNK_SETTING(construction, unemploymentPercent, 0, 100),
    MECHAPUNK_SETTING(careers, evictionNights, 0, 1000),
    MECHAPUNK_SETTING(careers, firingPerformance, -100000, 0),
    MECHAPUNK_SETTING(economy, minPolicyRate, 0, 1000),
    MECHAPUNK_SETTING(economy, maxPolicyRate, 0, 5000),
    MECHAPUNK_SETTING(economy, bankMargin, 0, 1000),
    MECHAPUNK_SETTING(economy, wageTaxPercent, 0, 100),
    MECHAPUNK_SETTING(economy, propertyTaxPercent, 0, 100),
    MECHAPUNK_SETTING(economy, profitTaxPercent, 0, 200),
    MECHAPUNK_SETTING(economy, benefit, 0, 4000),
    MECHAPUNK_SETTING(economy, priceNights, 0, 10000),
    MECHAPUNK_SETTING(economy, upkeepPercent, 0, 500),
    MECHAPUNK_SETTING(economy, sellPercent, 0, 900),
    MECHAPUNK_SETTING(behaviour, maxDailyExtraMinutes, 0, 1000),
    MECHAPUNK_SETTING(behaviour, outingEarliestMinute, 0, 54000),
    MECHAPUNK_SETTING(behaviour, outingSpreadMinutes, 0, 12000),
    MECHAPUNK_SETTING(behaviour, lastOutingMinute, 0, 132000),
    MECHAPUNK_SETTING(behaviour, minHomeBreak, 0, 9000),
    MECHAPUNK_SETTING(behaviour, maxHomeBreak, 0, 24000),
    MECHAPUNK_SETTING(behaviour, minEveningBreak, 0, 3000),
    MECHAPUNK_SETTING(behaviour, maxEveningBreak, 0, 9000),
    MECHAPUNK_SETTING(behaviour, minOutingMinutes, 0, 6000),
    MECHAPUNK_SETTING(behaviour, wearPerClinicPoint, 1, 1000),
    MECHAPUNK_SETTING(behaviour, badPartPercent, 0, 100),
    MECHAPUNK_SETTING(behaviour, badPartWear, 0, 6000),
    MECHAPUNK_SETTING(behaviour, latePerformance, 0, 1000),
    MECHAPUNK_SETTING(behaviour, homePeriods, 0, 1000),
    MECHAPUNK_SETTING(behaviour, homeComfort, 0, 12000),
    MECHAPUNK_SETTING(behaviour, bedtimeUrge, 0, 40000),
    MECHAPUNK_SETTING(behaviour, bedtimeWindow, 0, 12000),
    MECHAPUNK_SETTING(behaviour, walkCostPerMinute, 0, 1000),
    MECHAPUNK_SETTING(behaviour, priceCostPerCredit, 0, 1000),
    MECHAPUNK_SETTING(behaviour, maxWhim, 0, 8000),
    MECHAPUNK_SETTING(behaviour, sameKindPenalty, 0, 15000),
    MECHAPUNK_SETTING(behaviour, repairUrgentCondition, 0, 40000),
    MECHAPUNK_SETTING(careers, bestShift, 0, 1000),
    MECHAPUNK_SETTING(careers, wornDownPenalty, 0, 1000),
    MECHAPUNK_SETTING(careers, miserablePenalty, 0, 1000),
    MECHAPUNK_SETTING(careers, fitCondition, 0, 30000),
    MECHAPUNK_SETTING(careers, fitCharge, 0, 15000),
    MECHAPUNK_SETTING(careers, contentNeed, 0, 20000),
    MECHAPUNK_SETTING(careers, hirePercent, 0, 500),
    MECHAPUNK_SETTING(careers, moveUpSavings, 0, 50000),
    MECHAPUNK_SETTING(careers, moveUpPercent, 0, 100),
    MECHAPUNK_SETTING(careers, affordNights, 0, 1200),
    MECHAPUNK_SETTING(careers, shareHomePercent, 0, 100),
    MECHAPUNK_SETTING(careers, shareAffordNights, 0, 1000),
    MECHAPUNK_SETTING(city, streetRemovalOdds, 0, 100),
    MECHAPUNK_SETTING(city, minFloors, 0, 1000),
    MECHAPUNK_SETTING(city, maxFloors, 0, 1000),
    MECHAPUNK_SETTING(city, minCentralFloors, 0, 1500),
    MECHAPUNK_SETTING(city, maxCentralFloors, 0, 3000),
    MECHAPUNK_SETTING(city, residentsForExtraStore, 0, 10000),
    MECHAPUNK_SETTING(city, minBaseRent, 0, 2500),
    MECHAPUNK_SETTING(city, maxBaseRent, 0, 4000),
    MECHAPUNK_SETTING(city, dormRent, 0, 1000),
    MECHAPUNK_SETTING(city, marketHomesPercent, 0, 600),
    MECHAPUNK_SETTING(city, minStores, 0, 1000),
    MECHAPUNK_SETTING(city, maxStores, 0, 1000),
    MECHAPUNK_SETTING(city, minWorkplaceJobs, 0, 1500),
    MECHAPUNK_SETTING(city, maxWorkplaceJobs, 0, 4000),
    MECHAPUNK_SETTING(city, minCentralJobs, 0, 6000),
    MECHAPUNK_SETTING(city, maxCentralJobs, 0, 16000),
    MECHAPUNK_SETTING(city, minWorkspaceSize, 1, 1000),
    MECHAPUNK_SETTING(city, maxWorkspaceSize, 0, 1000),
    MECHAPUNK_SETTING(city, workspacesPerFloor, 1, 1000),
    MECHAPUNK_SETTING(city, towerJobs, 0, 6000),
    MECHAPUNK_SETTING(city, stationSpacing, 1, 1000),
    MECHAPUNK_SETTING(city, stationOffset, 0, 1000),
    MECHAPUNK_SETTING(city, seedTown, 0, 1),
    MECHAPUNK_SETTING(city, seedGridSize, 3, 9),
    MECHAPUNK_SETTING(city, seedDormFloors, 2, 12),
    MECHAPUNK_SETTING(city, seedWorkspaces, 1, 8),
    MECHAPUNK_SETTING(city, freePlotsMin, 0, 1000),
    MECHAPUNK_SETTING(city, maxGridSize, 3, 100),
    MECHAPUNK_SETTING(construction, workspacesPerFloor, 0, 1000),
    MECHAPUNK_SETTING(construction, workspacePlaces, 0, 1000),
    MECHAPUNK_SETTING(construction, baseRent, 0, 3000),
    MECHAPUNK_SETTING(economy, bankFloor, 0, 100000),
    MECHAPUNK_SETTING(economy, corporationFloor, 0, 20000),
    MECHAPUNK_SETTING(population, minPunctualityMinutes, 0, 1000),
    MECHAPUNK_SETTING(population, maxPunctualityMinutes, 0, 2000),
    MECHAPUNK_SETTING(population, minStartingNeed, 0, 55000),
    MECHAPUNK_SETTING(population, maxStartingNeed, 0, 95000),
    MECHAPUNK_SETTING(population, minStartingCredits, 0, 4000),
    MECHAPUNK_SETTING(population, maxStartingCredits, 0, 20000),
    MECHAPUNK_SETTING(population, maxStartingAge, 0, 36000),
    MECHAPUNK_SETTING(population, highestStartingLevel, 0, 3),
    MECHAPUNK_SETTING(population, interestsPerRobot, 0, 8),
    MECHAPUNK_SETTING(population, neglectWear, 0, 1000),
    MECHAPUNK_SETTING(population, neglectedCondition, 0, 15000),
    MECHAPUNK_SETTING(population, minNewcomerAge, 0, 1000),
    MECHAPUNK_SETTING(population, maxNewcomerAge, 0, 12000),
    MECHAPUNK_SETTING(population, minNewcomerNeed, 0, 55000),
    MECHAPUNK_SETTING(population, maxNewcomerNeed, 0, 95000),
    MECHAPUNK_SETTING(population, minNewcomerCredits, 0, 4000),
    MECHAPUNK_SETTING(population, maxNewcomerCredits, 0, 20000),
    MECHAPUNK_SETTING(population, minPunctuality, 0, 1000),
    MECHAPUNK_SETTING(population, maxPunctuality, 0, 2000),
    MECHAPUNK_SETTING(population, newcomerInterests, 0, 1000),
    MECHAPUNK_SETTING(population, wealthSpread, 0, 1),
    MECHAPUNK_SETTING(population, richPercent, 0, 100),
    MECHAPUNK_SETTING(population, richMultiplier, 1, 50),
    MECHAPUNK_SETTING(population, immigration, 0, 1),
    MECHAPUNK_SETTING(population, immigrantsPerNight, 0, 100),
    MECHAPUNK_SETTING(economy, utilitySupplyPercent, 0, 950),
    MECHAPUNK_SETTING(economy, transitSupplyPercent, 0, 950),
    MECHAPUNK_SETTING(economy, housingSupplyPercent, 0, 500),
    MECHAPUNK_SETTING(economy, robotDebtShifts, 0, 1000),
    MECHAPUNK_SETTING(economy, budgetPercent, 0, 800),
    MECHAPUNK_SETTING(economy, savingsSpendDays, 1, 1000),
    MECHAPUNK_SETTING(economy, savingsTargetNights, 0, 3000),
    MECHAPUNK_SETTING(economy, baseBudget, 0, 1000),
    MECHAPUNK_SETTING(economy, workDaysPerWeek, 0, 1000),
    MECHAPUNK_SETTING(economy, comfortableSavings, 0, 100000),
    MECHAPUNK_SETTING(economy, buyPercent, 0, 100),
    MECHAPUNK_SETTING(economy, buyReserveNights, 0, 2000),
    MECHAPUNK_SETTING(economy, depositPercent, 0, 200),
    MECHAPUNK_SETTING(economy, mortgageShifts, 0, 6000),
    MECHAPUNK_SETTING(economy, sellVacantPercent, 0, 200),
    MECHAPUNK_SETTING(economy, bankReservePercent, 0, 100),
    MECHAPUNK_SETTING(economy, minSupplyPercent, 0, 200),
    MECHAPUNK_SETTING(economy, maxSupplyPercent, 0, 950),
    MECHAPUNK_SETTING(economy, minPayPercent, 0, 500),
    MECHAPUNK_SETTING(economy, maxPayPercent, 0, 1500),
    MECHAPUNK_SETTING(economy, dividendPercent, 0, 500),
    MECHAPUNK_SETTING(social, venueTalkPercent, 0, 400),
    MECHAPUNK_SETTING(social, workTalkPercent, 0, 100),
    MECHAPUNK_SETTING(social, talkSpread, 0, 1000),
    MECHAPUNK_SETTING(social, sharedTalkBase, -100000, 0),
    MECHAPUNK_SETTING(social, perSharedInterest, 0, 1000),
    MECHAPUNK_SETTING(social, strangerTalkBase, -100000, 0),
    MECHAPUNK_SETTING(social, talkSocial, 0, 1000),
    MECHAPUNK_SETTING(social, affinityPerExtraSocial, 1, 1000),
    MECHAPUNK_SETTING(social, argumentFun, 0, 1000),
    MECHAPUNK_SETTING(social, partnerWeight, 0, 1000),
    MECHAPUNK_SETTING(social, affinityPerWeight, 1, 1000),
    MECHAPUNK_SETTING(social, forgettable, 0, 1000),
    MECHAPUNK_SETTING(social, fadeAfter, 0, 288000),
    MECHAPUNK_SETTING(social, fadePerDay, 0, 1000),
    MECHAPUNK_SETTING(social, friendPull, 0, 25000),
    MECHAPUNK_SETTING(social, closeFriendPull, 0, 50000),
    MECHAPUNK_SETTING(social, rivalPush, 0, 20000),
    MECHAPUNK_SETTING(metro, stationReachMeters, 1, 100000),
    MECHAPUNK_SETTING(metro, minBuildings, 0, 1000),
    MECHAPUNK_SETTING(metro, stationCost, 0, 1000000),
    MECHAPUNK_SETTING(venues, priceStep, 0, 1000),
    MECHAPUNK_SETTING(venues, minPricePercent, 0, 500),
    MECHAPUNK_SETTING(venues, maxPricePercent, 0, 3000),
    MECHAPUNK_SETTING(venues, busyTurns, 0, 1000),
    MECHAPUNK_SETTING(venues, tightVacancyPercent, 0, 100),
    MECHAPUNK_SETTING(venues, looseVacancyPercent, 0, 150),
    MECHAPUNK_SETTING(venues, minRentPercent, 0, 500),
    MECHAPUNK_SETTING(venues, maxRentPercent, 0, 3000),
    MECHAPUNK_SETTING(venues, closeAfterNights, 0, 1000),
    MECHAPUNK_SETTING(venues, quietPricePercent, 0, 800),
    MECHAPUNK_SETTING(venues, lowUnemploymentPercent, 0, 100),
    MECHAPUNK_SETTING(venues, openUnitPercent, 0, 100),
    MECHAPUNK_SETTING(venues, highUnemploymentPercent, 0, 100),
    MECHAPUNK_SETTING(venues, inDemandPercent, 0, 1050),
    MECHAPUNK_SETTING(venues, minWorkspaceStaff, 0, 1000),
    MECHAPUNK_SETTING(venues, maxWorkspaceStaff, 0, 1000),
    MECHAPUNK_SETTING(behaviour, clinicWearRepair, 0, 1000),
};

#undef MECHAPUNK_SETTING

// A minimum may not pass its maximum: a draw between the two would be empty.
struct Pair {
    std::string_view low;
    std::string_view high;
};

const std::array kPairs{
    Pair{"economy.minPolicyRate", "economy.maxPolicyRate"},
    Pair{"behaviour.minHomeBreak", "behaviour.maxHomeBreak"},
    Pair{"behaviour.minEveningBreak", "behaviour.maxEveningBreak"},
    Pair{"city.minFloors", "city.maxFloors"},
    Pair{"city.minCentralFloors", "city.maxCentralFloors"},
    Pair{"city.minBaseRent", "city.maxBaseRent"},
    Pair{"city.minStores", "city.maxStores"},
    Pair{"city.minWorkplaceJobs", "city.maxWorkplaceJobs"},
    Pair{"city.minCentralJobs", "city.maxCentralJobs"},
    Pair{"city.minWorkspaceSize", "city.maxWorkspaceSize"},
    Pair{"population.minPunctualityMinutes", "population.maxPunctualityMinutes"},
    Pair{"population.minStartingNeed", "population.maxStartingNeed"},
    Pair{"population.minStartingCredits", "population.maxStartingCredits"},
    Pair{"population.minNewcomerAge", "population.maxNewcomerAge"},
    Pair{"population.minNewcomerNeed", "population.maxNewcomerNeed"},
    Pair{"population.minNewcomerCredits", "population.maxNewcomerCredits"},
    Pair{"population.minPunctuality", "population.maxPunctuality"},
    Pair{"economy.minSupplyPercent", "economy.maxSupplyPercent"},
    Pair{"economy.minPayPercent", "economy.maxPayPercent"},
    Pair{"venues.minPricePercent", "venues.maxPricePercent"},
    Pair{"venues.minRentPercent", "venues.maxRentPercent"},
    Pair{"venues.minWorkspaceStaff", "venues.maxWorkspaceStaff"},
};

const Entry* Find(std::string_view name) {
    for (const Entry& entry : kEntries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// The first pair `settings` breaks, or nothing.
std::optional<Pair> BrokenPair(SimSettings& settings) {
    for (const Pair& pair : kPairs) {
        if (Find(pair.low)->field(settings) > Find(pair.high)->field(settings)) {
            return pair;
        }
    }
    return std::nullopt;
}

const std::array kNames = [] {
    std::array<std::string_view, kEntries.size()> names;
    for (std::size_t index = 0; index != kEntries.size(); ++index) {
        names[index] = kEntries[index].name;
    }
    return names;
}();

}  // namespace

std::expected<void, std::string> ApplySetting(SimSettings& settings, std::string_view name,
                                              std::int64_t value) {
    for (const Entry& entry : kEntries) {
        if (entry.name != name) {
            continue;
        }
        if (value < entry.minimum || value > entry.maximum) {
            return std::unexpected(std::format("{} must be from {} to {}, not {}", name,
                                               entry.minimum, entry.maximum, value));
        }
        std::int64_t& field = entry.field(settings);
        const std::int64_t before = field;
        field = value;
        if (const auto broken = BrokenPair(settings)) {
            field = before;
            return std::unexpected(std::format("{} may not be above {}", broken->low, broken->high));
        }
        return {};
    }
    return std::unexpected(std::format("unknown setting '{}'", name));
}

std::expected<void, std::string> ValidateSettings(const SimSettings& settings) {
    SimSettings scratch = settings;
    for (const Entry& entry : kEntries) {
        const std::int64_t value = entry.field(scratch);
        if (value < entry.minimum || value > entry.maximum) {
            return std::unexpected(std::format("Setting {} is {}, outside {} to {}", entry.name,
                                               value, entry.minimum, entry.maximum));
        }
    }
    if (const auto broken = BrokenPair(scratch)) {
        return std::unexpected(std::format("Setting {} is above {}", broken->low, broken->high));
    }
    return {};
}

std::span<const std::string_view> SettingNames() noexcept { return kNames; }

}  // namespace mechapunk::sim
