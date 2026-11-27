#pragma once

// The settings of a simulation: the numbers that decide how a city behaves
// and that a test may want to change to reach a corner case quickly. A city
// is made from a seed *and* a set of settings (`WorldParams`), and every
// default below is the value the game has always used, so `SimSettings{}`
// plays exactly as before.
//
// The settings are part of the saved state (`WorldState::settings`, save
// version 28): a city keeps the knobs it was made with after a save and a
// load, so a saved experiment repeats.
//
// Every field is an int64 so one table can name and range-check them all
// (`ApplySetting`, used by `mechapunk-sim --set name=value`); the code that
// reads a setting converts it to the type it needs.

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace mechapunk::sim {

// How crime and its policing run: the knobs a test turns to reach a corner
// case fast (every pair robs, no gang hangs out at venues, officers never
// patrol).
struct CrimeSettings {
    // B4: percent of the eligible pairs that try a robbery at all. An officer on
    // shift in the room turns such an attempt into an arrest (B5).
    std::int64_t robberyPercent{3};

    // B14b: percent of a member's outings that hang round a gang. Members used to
    // pick their outing out of the city's ~484 venues as if they were not in a
    // gang, so two rival members shared a room barely at all and the 1% fight roll
    // had almost nothing to act on (24 fights in 40 days at 10000 robots).
    std::int64_t gangOutingPercent{50};

    // How near a gang's turf counts as the neighbourhood it hangs round, in street
    // meters: 40 of them is the turf's own street corner, where up to four
    // buildings stand and where its ground floor venues are. Narrow on purpose:
    // it is what crowds a gang's members into a handful of venues, so the members
    // of the gang it fights turn up in the same rooms.
    std::int64_t turfReachMeters{40};

    // Officers patrol venues within this walk of the station, and look round
    // each for this long before walking back.
    std::int64_t patrolReachMinutes{10};

    // How long an officer looks round a venue before walking back.
    std::int64_t patrolStayMinutes{15};

    // B14: how far back the chronicle is searched for robberies when an officer
    // picks the venue to patrol. A uniform pick out of the city's ~484 venues
    // left B13's arrest rule invisible at 9 arrests in 40 days, because an
    // officer almost never stood where the robbers were standing; robbers of a
    // fresh crime still are at its venue, so the officer goes to a venue the
    // city robbed within this many minutes instead, when one is within reach.
    std::int64_t crimeLookbackMinutes{60};

    bool operator==(const CrimeSettings&) const = default;
};

// When the city raises a building and what a site is: a test sets the streak
// to one night to see a site go up at once, or the shares to 0 or 100 to
// force or forbid one.
struct ConstructionSettings {
    // Nights of the same shortage before the city raises another building. One
    // tight night is weather; a week of it is the city running short of homes
    // or of jobs.
    std::int64_t streakNights{7};

    // Floors a new site raises above its ground floor, one of them a night, so
    // `daysLeft` is also the floors still to come. The ground floor stands from
    // the first night and holds only the site office (step D3b): `ReviewUnits`,
    // which only works on ground-floor units, branches that loop by kind - a venue,
    // a workspace, an empty unit - and so leaves the office alone.
    std::int64_t habitatFloors{6};

    // Floors above the ground floor of a new workplace (a habitat: see above).
    std::int64_t workplaceFloors{4};

    // Builders a site's office holds: the crew that raises its floors while the
    // project is up, the jobs step D3c gives out. Until that step lands nobody
    // works there, and the capacity only has to be the non-zero number
    // `ValidateWorldState` asks of every room.
    std::int64_t siteBuilders{6};

    // Credits a night the site asks for. `PaySupplies` (Money.cpp) moves the
    // owner's sector's share of them into the supply market: half for the
    // housing trust, and nothing for an industry corporation, whose supplies
    // are what every other sector buys from it.
    std::int64_t siteSuppliesNight{200};

    // The share of the market's homes that may stand empty before homes are short.
    // The market's homes are the ones `SetPrices` (Units.cpp) counts class by class
    // when it moves market rents: every apartment but a government dorm room, that
    // is the homes a robot can afford and move into. A quarter of them standing
    // empty is room enough to live in; under that for a week the city is running
    // out of homes. Measured: a young city leaves 75 to 85 per cent of them empty
    // around its twelfth night (most robots still live in a dorm bed) and fills
    // them by its thirtieth, so this bites when the market really runs out.
    // Free dorm beds (beds no robot sleeps in, in finished dormitories) under this
    // share of all dorm beds, for `streakNights` nights, make the city raise a
    // dormitory: a habitat of dorm rooms on every floor, owned by the housing trust.
    // 0 turns the rule off, as in a full city, whose dorms start about full and
    // empty as robots move out; a growing town sets 10.
    std::int64_t freeBedsPercent{0};

    std::int64_t marketVacancyPercent{25};

    // The share of the population out of work above which jobs are short: the
    // same share at which `ReviewUnits` (Units.cpp) reopens an empty unit as a
    // workspace. Below it the city has work for everyone it needs; above it for a
    // week it needs more, and a workplace is raised when no empty unit is left.
    // Measured: a young city has 0 to 3 per cent of its robots out of work, so
    // this bites only in a city whose jobs really have gone.
    std::int64_t unemploymentPercent{5};

    // Workplaces on a finished floor of a new workplace, and the places in one.
    std::int64_t workspacesPerFloor{2};

    std::int64_t workspacePlaces{8};

    // Daily rent of a new habitat's lowest floor, a credit more a floor up,
    // as generation asks of its own habitats (City.cpp).
    std::int64_t baseRent{30};

    bool operator==(const ConstructionSettings&) const = default;
};

// How robots spend their day: outings, breaks at home, wear and repairs, the
// weights of the choice of where to go next, and an officer's patrol.
struct BehaviourSettings {
    // Daily variation added to a robot's punctuality.
    std::int64_t maxDailyExtraMinutes{10};

    // A day off starts no earlier than this and may start up to two hours later.
    std::int64_t outingEarliestMinute{540};

    std::int64_t outingSpreadMinutes{120};

    // No new outings from home after this minute of the day.
    std::int64_t lastOutingMinute{1320};

    // A break at home between outings on a free day.
    std::int64_t minHomeBreak{90};

    std::int64_t maxHomeBreak{240};

    // A shorter break at home after work before thinking about going out.
    std::int64_t minEveningBreak{30};

    std::int64_t maxEveningBreak{90};

    // An outing needs at least this long before the robot must be back.
    std::int64_t minOutingMinutes{60};

    std::int64_t wearPerClinicPoint{3};

    // Chance in percent that a black market part turns out dodgy, and the wear
    // it causes.
    std::int64_t badPartPercent{10};

    std::int64_t badPartWear{60};

    // Arriving late costs this much performance.
    std::int64_t latePerformance{3};

    // Weights of the evening decision. Going home is judged by an hour of rest.
    std::int64_t homePeriods{6};

    std::int64_t homeComfort{120};

    std::int64_t bedtimeUrge{400};

    std::int64_t bedtimeWindow{120};

    std::int64_t walkCostPerMinute{6};

    std::int64_t priceCostPerCredit{3};

    std::int64_t maxWhim{80};

    // Going straight from one venue to another of the same kind is less
    // appealing than trying something different.
    std::int64_t sameKindPenalty{150};

    // Below this condition a robot pays for repairs from its savings.
    std::int64_t repairUrgentCondition{400};

    // A robot at a clinic loses this much wear every ten minutes, and values a
    // clinic visit by a third of its wear.
    std::int64_t clinicWearRepair{2};

    bool operator==(const BehaviourSettings&) const = default;
};

// Jobs: raises, promotions, firing, hiring, moving up and sharing a home.
struct CareersSettings {
    // Nights of rent owed that get a tenant evicted, or an owner of upkeep
    // owed that make it sell its home.
    std::int64_t evictionNights{5};

    // Performance below this gets a robot fired.
    std::int64_t firingPerformance{-12};

    // A shift earns 0 to 2 points of performance, less for a robot that turned
    // up worn down or miserable.
    std::int64_t bestShift{2};

    std::int64_t wornDownPenalty{3};

    std::int64_t miserablePenalty{1};

    std::int64_t fitCondition{300};

    std::int64_t fitCharge{150};

    std::int64_t contentNeed{200};

    // Chance in percent that a robot out of work is hired on a given night, if
    // there is an opening.
    std::int64_t hirePercent{50};

    // Owing this many nights of rent gets a robot evicted.
    // Robots with this much saved sometimes move to a nicer, pricier apartment
    // they can pay for this many nights from savings.
    std::int64_t moveUpSavings{500};

    std::int64_t moveUpPercent{10};

    std::int64_t affordNights{12};

    // Each night, the chance in percent that a robot living alone in a rented
    // home asks its closest friend to move in together.
    std::int64_t shareHomePercent{10};

    // Sharing is how robots afford more sooner: each needs only this many
    // nights of its share saved, where moving alone takes kAffordNights.
    std::int64_t shareAffordNights{10};

    bool operator==(const CareersSettings&) const = default;
};

// How a new city is laid out: street gaps, building heights, rents, stores,
// jobs per building and the metro stations.
struct CitySettings {
    // One candidate street segment in this many is considered for removal.
    std::int64_t streetRemovalOdds{10};

    // Habitats at the city's edge have five to ten floors of homes; toward the
    // middle they rise to fifteen to thirty.
    std::int64_t minFloors{5};

    std::int64_t maxFloors{10};

    std::int64_t minCentralFloors{15};

    std::int64_t maxCentralFloors{30};

    // Tall habitats get one more store, and bigger stores, so there is as much
    // room in venues for each resident as in a habitat of kTypicalResidents
    // with an average number of stores.
    std::int64_t residentsForExtraStore{100};

    // Daily rent of a habitat's lowest studios; each floor up costs a credit
    // more. Other classes cost a multiple, in percent; dorm rooms are cheap
    // whatever the floor.
    std::int64_t minBaseRent{25};

    std::int64_t maxBaseRent{40};

    std::int64_t dormRent{8};

    // A new city has a dorm bed for every robot, and homes on the market for
    // this share of them, in percent.
    std::int64_t marketHomesPercent{60};

    std::int64_t minStores{2};

    std::int64_t maxStores{3};

    // Workplaces grow toward the middle the same way.
    std::int64_t minWorkplaceJobs{15};

    std::int64_t maxWorkplaceJobs{40};

    std::int64_t minCentralJobs{60};

    std::int64_t maxCentralJobs{160};

    std::int64_t minWorkspaceSize{5};

    std::int64_t maxWorkspaceSize{10};

    // Workplaces on a finished floor of a new workplace, and the places in one.
    std::int64_t workspacesPerFloor{2};

    // Workplaces of this many jobs are office towers, one workspace per floor.
    std::int64_t towerJobs{60};

    std::int64_t stationSpacing{2};

    std::int64_t stationOffset{1};

    // 1 generates a seed town instead of a full city: one dormitory and one
    // workshop on a small grid, with no metro. It grows on its own (task K).
    std::int64_t seedTown{0};

    // Intersections per side of a seed town's grid.
    std::int64_t seedGridSize{3};

    // Floors of homes in the seed town's dormitory (twelve beds a floor).
    std::int64_t seedDormFloors{4};

    // Workspaces of the seed town's workshop.
    std::int64_t seedWorkspaces{2};

    bool operator==(const CitySettings&) const = default;
};

// Money: taxes, benefits, rents, supply shares, the central bank, savings,
// mortgages and the floors banks and corporations keep.
struct EconomySettings {
    std::int64_t minPolicyRate{2};

    std::int64_t maxPolicyRate{50};

    std::int64_t bankMargin{5};

    // City Hall's taxes, in percent: of every wage, withheld on payday; of a
    // home's rent each night, paid by its owner; and of a day's profit, paid by
    // corporations and banks. It pays kBenefit credits a night to every robot
    // out of work.
    std::int64_t wageTaxPercent{10};

    std::int64_t propertyTaxPercent{5};

    std::int64_t profitTaxPercent{20};

    std::int64_t benefit{40};

    // The price of an apartment: so many nights of its rent.
    std::int64_t priceNights{100};

    // Owners of a lived-in apartment pay this much of its rent each night for
    // power and water, in percent.
    std::int64_t upkeepPercent{50};

    // Robots sell to the housing trust at this much of the price, in percent.
    std::int64_t sellPercent{90};

    // What a bank keeps before lending or paying out, plus a share of what it
    // has lent, in percent.
    std::int64_t bankFloor{1000};

    std::int64_t corporationFloor{200};

    std::int64_t utilitySupplyPercent{95};

    std::int64_t transitSupplyPercent{95};

    std::int64_t housingSupplyPercent{50};

    // A robot with a job may owe up to this many shifts' wages.
    std::int64_t robotDebtShifts{5};

    // A robot's daily budget for outings: this share of its average day's pay,
    // plus 1/kSavingsSpendDays of what it has saved beyond kSavingsTargetNights
    // of its bills (savings put by for a rainy day), plus a little.
    std::int64_t budgetPercent{80};

    std::int64_t savingsSpendDays{10};

    std::int64_t savingsTargetNights{30};

    std::int64_t baseBudget{10};

    std::int64_t workDaysPerWeek{5};

    // Robots with more saved than this spend a quarter of what is left of
    // their budget on every visit, on better drinks, parts and service.
    std::int64_t comfortableSavings{1000};

    // Each night, the chance in percent that a robot able to buy an apartment
    // does, and the nights of rent it keeps after paying.
    std::int64_t buyPercent{3};

    std::int64_t buyReserveNights{20};

    // A mortgage needs this much of the price down, and lends at most this many
    // shifts' pay.
    std::int64_t depositPercent{20};

    std::int64_t mortgageShifts{60};

    // Each night, the chance a robot sells an apartment nobody lives in.
    std::int64_t sellVacantPercent{20};

    std::int64_t bankReservePercent{10};

    // Of every credit a sector takes, this much buys supplies from industry.
    std::int64_t minSupplyPercent{20};

    std::int64_t maxSupplyPercent{95};

    std::int64_t minPayPercent{50};

    std::int64_t maxPayPercent{150};

    // Of what a corporation has beyond twice its reserve, the share paid out
    // each night.
    std::int64_t dividendPercent{50};

    bool operator==(const EconomySettings&) const = default;
};

// The robots a city starts with and the newcomers who arrive later: their
// needs, credits, ages, punctuality, and how they wear with time.
struct PopulationSettings {
    std::int64_t minPunctualityMinutes{5};

    std::int64_t maxPunctualityMinutes{20};

    std::int64_t minStartingNeed{550};

    std::int64_t maxStartingNeed{950};

    std::int64_t minStartingCredits{40};

    std::int64_t maxStartingCredits{200};

    // Robots in a new city are up to about a year old.
    std::int64_t maxStartingAge{360};

    // Robots start somewhere between junior and senior.
    std::int64_t highestStartingLevel{2};

    std::int64_t interestsPerRobot{2};

    std::int64_t neglectWear{5};

    std::int64_t neglectedCondition{150};

    // Robots moving to the city are fairly new.
    std::int64_t minNewcomerAge{10};

    std::int64_t maxNewcomerAge{120};

    std::int64_t minNewcomerNeed{550};

    std::int64_t maxNewcomerNeed{950};

    std::int64_t minNewcomerCredits{40};

    std::int64_t maxNewcomerCredits{200};

    std::int64_t minPunctuality{5};

    std::int64_t maxPunctuality{20};

    std::int64_t newcomerInterests{2};

    // 1 gives robots poor-to-rich savings: `richPercent` of them (in percent)
    // start, or arrive, with `richMultiplier` times the usual credits. 0 gives
    // everyone a draw from the usual range, as a full city always did.
    std::int64_t wealthSpread{0};

    std::int64_t richPercent{10};

    std::int64_t richMultiplier{20};

    // 1 lets newcomers arrive beyond the ones that replace scrapped robots: each
    // night up to `immigrantsPerNight` of them, while the city has a free dorm
    // bed and a job with a free place. 0 is a full city, whose population only
    // replaces scrapped robots.
    std::int64_t immigration{0};

    std::int64_t immigrantsPerNight{3};

    bool operator==(const PopulationSettings&) const = default;
};

// Talking, friendships, rivalries and memories.
struct SocialSettings {
    // Chance in percent that a robot starts a talk in ten minutes.
    std::int64_t venueTalkPercent{40};

    std::int64_t workTalkPercent{5};

    // Affinity a talk adds: shared interests make talks go well; without any,
    // talks tend to turn into arguments.
    std::int64_t talkSpread{8};

    std::int64_t sharedTalkBase{-3};

    std::int64_t perSharedInterest{2};

    std::int64_t strangerTalkBase{-5};

    // Beyond friendship or rivalry, each talk moves feelings half as far.
    // A good talk lifts social, more with a friend; an argument costs fun.
    std::int64_t talkSocial{6};

    std::int64_t affinityPerExtraSocial{10};

    std::int64_t argumentFun{4};

    // Partners are picked with weight kPartnerWeight plus affinity / 10.
    std::int64_t partnerWeight{2};

    std::int64_t affinityPerWeight{10};

    std::int64_t forgettable{10};

    // Bonds fade by kFadePerDay each night once the two have not talked for
    // kFadeAfter.
    std::int64_t fadeAfter{2880};

    std::int64_t fadePerDay{2};

    // How much robots want to be where their friends are, or not where their
    // rivals are, when picking a venue.
    std::int64_t friendPull{250};

    std::int64_t closeFriendPull{500};

    std::int64_t rivalPush{200};

    bool operator==(const SocialSettings&) const = default;
};

// Venues and workspaces: prices, rents, vacancies, opening and closing.
struct VenuesSettings {
    // Venue prices move a step a night between these, in percent of base;
    // a venue counts as busy when it had kBusyTurns times its places in guests.
    std::int64_t priceStep{1};

    std::int64_t minPricePercent{50};

    std::int64_t maxPricePercent{300};

    std::int64_t busyTurns{3};

    // Market rents move a point a night while fewer homes than the tight share
    // stand empty, or more than the loose share; within these bounds.
    std::int64_t tightVacancyPercent{5};

    std::int64_t looseVacancyPercent{15};

    std::int64_t minRentPercent{50};

    std::int64_t maxRentPercent{300};

    // A venue closes after this many nights in a row with fewer guests than
    // half its places, its prices already cut to kQuietPricePercent or less; a
    // workspace after as many under half staffed, while under the low share of
    // robots, in percent, is out of work.
    std::int64_t closeAfterNights{10};

    std::int64_t quietPricePercent{80};

    std::int64_t lowUnemploymentPercent{2};

    // Each night an empty unit may open, with this chance in percent: as a
    // workspace in a workplace while over the high share of robots is out of
    // work, otherwise as the venue the city is most short of.
    std::int64_t openUnitPercent{10};

    std::int64_t highUnemploymentPercent{5};

    // Venues of a kind count as in demand while their prices average this, in
    // percent of usual.
    std::int64_t inDemandPercent{105};

    std::int64_t minWorkspaceStaff{5};

    std::int64_t maxWorkspaceStaff{10};

    bool operator==(const VenuesSettings&) const = default;
};

struct SimSettings {
    CrimeSettings crime;
    ConstructionSettings construction;
    BehaviourSettings behaviour;
    CareersSettings careers;
    CitySettings city;
    EconomySettings economy;
    PopulationSettings population;
    SocialSettings social;
    VenuesSettings venues;

    bool operator==(const SimSettings&) const = default;
};

// Sets the setting called `name` ("crime.robberyPercent") to `value`. Fails
// on an unknown name or a value outside the range that setting makes sense in.
[[nodiscard]] std::expected<void, std::string> ApplySetting(SimSettings& settings,
                                                            std::string_view name,
                                                            std::int64_t value);

// Fails when any setting is outside the range `ApplySetting` allows; a
// loaded save is checked with it (`ValidateWorldState`).
[[nodiscard]] std::expected<void, std::string> ValidateSettings(const SimSettings& settings);

// Every name `ApplySetting` knows, in declaration order.
[[nodiscard]] std::span<const std::string_view> SettingNames() noexcept;

}  // namespace mechapunk::sim
