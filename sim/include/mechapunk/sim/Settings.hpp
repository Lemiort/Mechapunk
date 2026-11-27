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
    std::int64_t marketVacancyPercent{25};

    // The share of the population out of work above which jobs are short: the
    // same share at which `ReviewUnits` (Units.cpp) reopens an empty unit as a
    // workspace. Below it the city has work for everyone it needs; above it for a
    // week it needs more, and a workplace is raised when no empty unit is left.
    // Measured: a young city has 0 to 3 per cent of its robots out of work, so
    // this bites only in a city whose jobs really have gone.
    std::int64_t unemploymentPercent{5};

    bool operator==(const ConstructionSettings&) const = default;
};

struct SimSettings {
    CrimeSettings crime;
    ConstructionSettings construction;

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
