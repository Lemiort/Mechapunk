// The save migrations this game still reads. Once a version is released its
// migration is frozen; append new ones here and register them in
// `kMigrations` in Save.cpp.

#include "Migrations.hpp"

#include <exception>
#include <expected>
#include <format>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

// Version 28 saves the simulation's settings with the city. A version 27
// city was made and played with the defaults of that game, so the migration
// writes those values, spelled out here because a frozen migration keeps the
// numbers of the version it was written for (and never calls game code).
std::expected<void, std::string> V27ToV28(SaveDocument& world) try {
    SaveDocument crime = SaveDocument::object_t{};
    crime["robberyPercent"] = 3;
    crime["gangOutingPercent"] = 50;
    crime["turfReachMeters"] = 40;
    crime["patrolReachMinutes"] = 10;
    crime["patrolStayMinutes"] = 15;
    crime["crimeLookbackMinutes"] = 60;
    SaveDocument construction = SaveDocument::object_t{};
    construction["streakNights"] = 7;
    construction["habitatFloors"] = 6;
    construction["workplaceFloors"] = 4;
    construction["siteBuilders"] = 6;
    construction["siteSuppliesNight"] = 200;
    construction["marketVacancyPercent"] = 25;
    construction["unemploymentPercent"] = 5;
    SaveDocument settings = SaveDocument::object_t{};
    settings["crime"] = std::move(crime);
    settings["construction"] = std::move(construction);
    world["settings"] = std::move(settings);
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 27 world is malformed ({})", error.what()));
}


// Version 29 saves the rest of the simulation's settings: behaviour, careers,
// the city's layout, money, population, social life and venues, plus three
// more construction numbers. A version 28 city ran with the defaults of that
// game, so the migration writes those values, spelled out here because a
// frozen migration keeps the numbers of the version it was written for.
std::expected<void, std::string> V28ToV29(SaveDocument& world) try {
    SaveDocument& settings = world["settings"];
    const auto group = [&](const char* name) -> SaveDocument& {
        if (!settings.contains(name)) {
            settings[name] = SaveDocument::object_t{};
        }
        return settings[name];
    };
    {
        SaveDocument& careers = group("careers");
        careers["evictionNights"] = 5;
        careers["firingPerformance"] = -12;
        careers["bestShift"] = 2;
        careers["wornDownPenalty"] = 3;
        careers["miserablePenalty"] = 1;
        careers["fitCondition"] = 300;
        careers["fitCharge"] = 150;
        careers["contentNeed"] = 200;
        careers["hirePercent"] = 50;
        careers["moveUpSavings"] = 500;
        careers["moveUpPercent"] = 10;
        careers["affordNights"] = 12;
        careers["shareHomePercent"] = 10;
        careers["shareAffordNights"] = 10;
    }
    {
        SaveDocument& economy = group("economy");
        economy["minPolicyRate"] = 2;
        economy["maxPolicyRate"] = 50;
        economy["bankMargin"] = 5;
        economy["wageTaxPercent"] = 10;
        economy["propertyTaxPercent"] = 5;
        economy["profitTaxPercent"] = 20;
        economy["benefit"] = 40;
        economy["priceNights"] = 100;
        economy["upkeepPercent"] = 50;
        economy["sellPercent"] = 90;
        economy["bankFloor"] = 1000;
        economy["corporationFloor"] = 200;
        economy["utilitySupplyPercent"] = 95;
        economy["transitSupplyPercent"] = 95;
        economy["housingSupplyPercent"] = 50;
        economy["robotDebtShifts"] = 5;
        economy["budgetPercent"] = 80;
        economy["savingsSpendDays"] = 10;
        economy["savingsTargetNights"] = 30;
        economy["baseBudget"] = 10;
        economy["workDaysPerWeek"] = 5;
        economy["comfortableSavings"] = 1000;
        economy["buyPercent"] = 3;
        economy["buyReserveNights"] = 20;
        economy["depositPercent"] = 20;
        economy["mortgageShifts"] = 60;
        economy["sellVacantPercent"] = 20;
        economy["bankReservePercent"] = 10;
        economy["minSupplyPercent"] = 20;
        economy["maxSupplyPercent"] = 95;
        economy["minPayPercent"] = 50;
        economy["maxPayPercent"] = 150;
        economy["dividendPercent"] = 50;
    }
    {
        SaveDocument& behaviour = group("behaviour");
        behaviour["maxDailyExtraMinutes"] = 10;
        behaviour["outingEarliestMinute"] = 540;
        behaviour["outingSpreadMinutes"] = 120;
        behaviour["lastOutingMinute"] = 1320;
        behaviour["minHomeBreak"] = 90;
        behaviour["maxHomeBreak"] = 240;
        behaviour["minEveningBreak"] = 30;
        behaviour["maxEveningBreak"] = 90;
        behaviour["minOutingMinutes"] = 60;
        behaviour["wearPerClinicPoint"] = 3;
        behaviour["badPartPercent"] = 10;
        behaviour["badPartWear"] = 60;
        behaviour["latePerformance"] = 3;
        behaviour["homePeriods"] = 6;
        behaviour["homeComfort"] = 120;
        behaviour["bedtimeUrge"] = 400;
        behaviour["bedtimeWindow"] = 120;
        behaviour["walkCostPerMinute"] = 6;
        behaviour["priceCostPerCredit"] = 3;
        behaviour["maxWhim"] = 80;
        behaviour["sameKindPenalty"] = 150;
        behaviour["repairUrgentCondition"] = 400;
        behaviour["clinicWearRepair"] = 2;
    }
    {
        SaveDocument& city = group("city");
        city["streetRemovalOdds"] = 10;
        city["minFloors"] = 5;
        city["maxFloors"] = 10;
        city["minCentralFloors"] = 15;
        city["maxCentralFloors"] = 30;
        city["residentsForExtraStore"] = 100;
        city["minBaseRent"] = 25;
        city["maxBaseRent"] = 40;
        city["dormRent"] = 8;
        city["marketHomesPercent"] = 60;
        city["minStores"] = 2;
        city["maxStores"] = 3;
        city["minWorkplaceJobs"] = 15;
        city["maxWorkplaceJobs"] = 40;
        city["minCentralJobs"] = 60;
        city["maxCentralJobs"] = 160;
        city["minWorkspaceSize"] = 5;
        city["maxWorkspaceSize"] = 10;
        city["workspacesPerFloor"] = 2;
        city["towerJobs"] = 60;
        city["stationSpacing"] = 2;
        city["stationOffset"] = 1;
    }
    {
        SaveDocument& construction = group("construction");
        construction["workspacesPerFloor"] = 2;
        construction["workspacePlaces"] = 8;
        construction["baseRent"] = 30;
    }
    {
        SaveDocument& population = group("population");
        population["minPunctualityMinutes"] = 5;
        population["maxPunctualityMinutes"] = 20;
        population["minStartingNeed"] = 550;
        population["maxStartingNeed"] = 950;
        population["minStartingCredits"] = 40;
        population["maxStartingCredits"] = 200;
        population["maxStartingAge"] = 360;
        population["highestStartingLevel"] = 2;
        population["interestsPerRobot"] = 2;
        population["neglectWear"] = 5;
        population["neglectedCondition"] = 150;
        population["minNewcomerAge"] = 10;
        population["maxNewcomerAge"] = 120;
        population["minNewcomerNeed"] = 550;
        population["maxNewcomerNeed"] = 950;
        population["minNewcomerCredits"] = 40;
        population["maxNewcomerCredits"] = 200;
        population["minPunctuality"] = 5;
        population["maxPunctuality"] = 20;
        population["newcomerInterests"] = 2;
    }
    {
        SaveDocument& social = group("social");
        social["venueTalkPercent"] = 40;
        social["workTalkPercent"] = 5;
        social["talkSpread"] = 8;
        social["sharedTalkBase"] = -3;
        social["perSharedInterest"] = 2;
        social["strangerTalkBase"] = -5;
        social["talkSocial"] = 6;
        social["affinityPerExtraSocial"] = 10;
        social["argumentFun"] = 4;
        social["partnerWeight"] = 2;
        social["affinityPerWeight"] = 10;
        social["forgettable"] = 10;
        social["fadeAfter"] = 2880;
        social["fadePerDay"] = 2;
        social["friendPull"] = 250;
        social["closeFriendPull"] = 500;
        social["rivalPush"] = 200;
    }
    {
        SaveDocument& venues = group("venues");
        venues["priceStep"] = 1;
        venues["minPricePercent"] = 50;
        venues["maxPricePercent"] = 300;
        venues["busyTurns"] = 3;
        venues["tightVacancyPercent"] = 5;
        venues["looseVacancyPercent"] = 15;
        venues["minRentPercent"] = 50;
        venues["maxRentPercent"] = 300;
        venues["closeAfterNights"] = 10;
        venues["quietPricePercent"] = 80;
        venues["lowUnemploymentPercent"] = 2;
        venues["openUnitPercent"] = 10;
        venues["highUnemploymentPercent"] = 5;
        venues["inDemandPercent"] = 105;
        venues["minWorkspaceStaff"] = 5;
        venues["maxWorkspaceStaff"] = 10;
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 28 world is malformed ({})", error.what()));
}

}  // namespace mechapunk::sim::migrations
