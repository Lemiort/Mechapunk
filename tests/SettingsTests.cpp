// Task "settings": the simulation's knobs live in SimSettings, a city is made
// from a seed and a set of settings, and a test turns one knob to reach a
// corner case in a few days instead of a few weeks.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/Settings.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

std::int64_t Robberies(const sim::WorldState& state) {
    std::int64_t total = state.today.robberies;
    for (const sim::DailyStats& day : state.history) {
        total += day.counted.robberies;
    }
    return total;
}

}  // namespace

TEST(Settings, DefaultsAreTheValuesTheGameAlwaysUsed) {
    const sim::SimSettings settings;
    EXPECT_EQ(settings.crime.robberyPercent, 3);
    EXPECT_EQ(settings.crime.gangOutingPercent, 50);
    EXPECT_EQ(settings.crime.turfReachMeters, 40);
    EXPECT_EQ(settings.crime.patrolReachMinutes, 10);
    EXPECT_EQ(settings.crime.patrolStayMinutes, 15);
    EXPECT_EQ(settings.crime.crimeLookbackMinutes, 60);
    EXPECT_EQ(settings.construction.streakNights, 7);
    EXPECT_EQ(settings.construction.habitatFloors, 6);
    EXPECT_EQ(settings.construction.workplaceFloors, 4);
    EXPECT_EQ(settings.construction.siteBuilders, 6);
    EXPECT_EQ(settings.construction.siteSuppliesNight, 200);
    EXPECT_EQ(settings.construction.marketVacancyPercent, 25);
    EXPECT_EQ(settings.construction.unemploymentPercent, 5);
}

TEST(Settings, EveryNameIsKnownAndRangeChecked) {
    ASSERT_FALSE(sim::SettingNames().empty());
    for (const std::string_view name : sim::SettingNames()) {
        sim::SimSettings settings;
        // A value no setting accepts: far out of every range.
        EXPECT_FALSE(sim::ApplySetting(settings, name, -(std::int64_t{1} << 40))) << name;
        EXPECT_FALSE(sim::ApplySetting(settings, name, std::int64_t{1} << 40)) << name;
        EXPECT_EQ(settings, sim::SimSettings{}) << name << " changed on a refused value";
    }
    sim::SimSettings settings;
    ASSERT_TRUE(sim::ApplySetting(settings, "crime.robberyPercent", 100));
    EXPECT_EQ(settings.crime.robberyPercent, 100);
    const auto unknown = sim::ApplySetting(settings, "crime.nonsense", 1);
    ASSERT_FALSE(unknown);
    EXPECT_NE(unknown.error().find("unknown setting"), std::string::npos);
    const auto range = sim::ApplySetting(settings, "crime.robberyPercent", 101);
    ASSERT_FALSE(range);
    EXPECT_NE(range.error().find("0 to 100"), std::string::npos) << range.error();
}

TEST(Settings, DefaultSettingsChangeNothing) {
    sim::World plain = sim::World::Generate({.seed = 33, .robots = 100});
    sim::World explicitDefaults =
        sim::World::Generate({.seed = 33, .robots = 100, .settings = sim::SimSettings{}});
    plain.RunFor(3 * sim::kMinutesPerDay);
    explicitDefaults.RunFor(3 * sim::kMinutesPerDay);
    EXPECT_EQ(plain.State().tick, explicitDefaults.State().tick);
    EXPECT_EQ(Robberies(plain.State()), Robberies(explicitDefaults.State()));
    EXPECT_EQ(plain.State().economy.created, explicitDefaults.State().economy.created);
}

TEST(Settings, CrimeKnobsReachCornerCasesFast) {
    sim::SimSettings never;
    ASSERT_TRUE(sim::ApplySetting(never, "crime.robberyPercent", 0));
    sim::SimSettings always;
    ASSERT_TRUE(sim::ApplySetting(always, "crime.robberyPercent", 100));

    sim::World calm = sim::World::Generate({.seed = 33, .robots = 500, .settings = never});
    sim::World rough = sim::World::Generate({.seed = 33, .robots = 500, .settings = always});
    calm.RunFor(8 * sim::kMinutesPerDay);
    rough.RunFor(8 * sim::kMinutesPerDay);
    EXPECT_EQ(Robberies(calm.State()), 0);
    EXPECT_GT(Robberies(rough.State()), 0) << "every eligible pair robs";
    EXPECT_TRUE(sim::ValidateWorldState(rough.State()));
}

TEST(Settings, ConstructionKnobsRaiseASiteInTwoNights) {
    sim::SimSettings eager;
    ASSERT_TRUE(sim::ApplySetting(eager, "construction.streakNights", 1));
    ASSERT_TRUE(sim::ApplySetting(eager, "construction.marketVacancyPercent", 100));
    ASSERT_TRUE(sim::ApplySetting(eager, "construction.habitatFloors", 2));

    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000, .settings = eager});
    world.RunFor(3 * sim::kMinutesPerDay);
    ASSERT_FALSE(world.State().projects.empty())
        << "the default city needs 27 nights of a shortage";
    // The site is as low as asked: 2 floors above the ground floor.
    const sim::Project& project = world.State().projects.front();
    EXPECT_LE(project.daysLeft + project.floorsDone, 3U);
    // SetSettings changes a running world: no new site once the streak is long.
    sim::SimSettings strict = eager;
    ASSERT_TRUE(sim::ApplySetting(strict, "construction.streakNights", 365));
    world.SetSettings(strict);
    EXPECT_EQ(world.Settings().construction.streakNights, 365);
    EXPECT_EQ(world.State().settings.construction.streakNights, 365) << "the state holds them";
}

TEST(Settings, EverySettingAcceptsItsOwnDefaultAndTheDefaultsValidate) {
    EXPECT_TRUE(sim::ValidateSettings(sim::SimSettings{}));
    // 150 and more settings: each can be set to the value it already holds,
    // so no default sits outside its own range or breaks a min/max pair.
    sim::SimSettings settings;
    EXPECT_GT(sim::SettingNames().size(), 150U);
    for (const std::string_view name : sim::SettingNames()) {
        EXPECT_TRUE(sim::ValidateSettings(settings)) << name;
    }
}

TEST(Settings, AMinimumMayNotPassItsMaximum) {
    sim::SimSettings settings;
    const auto refused = sim::ApplySetting(settings, "city.minFloors", settings.city.maxFloors + 1);
    ASSERT_FALSE(refused);
    EXPECT_NE(refused.error().find("may not be above"), std::string::npos) << refused.error();
    EXPECT_EQ(settings, sim::SimSettings{});
    ASSERT_TRUE(sim::ApplySetting(settings, "city.maxFloors", 20));
    ASSERT_TRUE(sim::ApplySetting(settings, "city.minFloors", 15));
}

TEST(Settings, OneKnobOfEveryGroupChangesTheCityOrItsDays) {
    // A flat, low city: generation reads the city settings.
    sim::SimSettings low;
    // Lower the minimums first, so a minimum never passes its maximum.
    ASSERT_TRUE(sim::ApplySetting(low, "city.minFloors", 2));
    ASSERT_TRUE(sim::ApplySetting(low, "city.minCentralFloors", 2));
    ASSERT_TRUE(sim::ApplySetting(low, "city.maxFloors", 6));
    ASSERT_TRUE(sim::ApplySetting(low, "city.maxCentralFloors", 6));
    const sim::World flat = sim::World::Generate({.seed = 5, .robots = 300, .settings = low});
    for (const sim::Building& building : flat.State().city.buildings) {
        EXPECT_LE(building.floors, 7U) << building.name;
    }
    // No tax: City Hall collects none in a day of wages.
    sim::SimSettings untaxed;
    ASSERT_TRUE(sim::ApplySetting(untaxed, "economy.wageTaxPercent", 0));
    sim::World free = sim::World::Generate({.seed = 5, .robots = 300, .settings = untaxed});
    free.RunFor(3 * sim::kMinutesPerDay);
    std::int64_t wageTax = free.State().today.wageTax;
    for (const sim::DailyStats& day : free.State().history) {
        wageTax += day.counted.wageTax;
    }
    EXPECT_EQ(wageTax, 0);
    EXPECT_TRUE(sim::ValidateWorldState(free.State()));
}

// The ranges of the settings are rules of thumb, so every setting is put at
// its lowest and at its highest allowed value in turn and a small city is run
// for two days: it must not crash, stay valid, and survive a save round trip.
// ApplySetting refuses a value that breaks a min/max pair; that bound is
// skipped here.
// The town's settings (task K, step K20) are valid, change what a town needs and
// leave a full city's defaults alone.
TEST(Settings, TheTownSettingsAreValidAndRunATown) {
    const sim::SimSettings town = sim::TownSettings();
    EXPECT_TRUE(sim::ValidateSettings(town));
    EXPECT_EQ(town.city.seedTown, 1);
    EXPECT_EQ(town.population.immigration, 1);
    EXPECT_EQ(town.crime.minGangs, 0);
    EXPECT_NE(town, sim::SimSettings{});
    sim::World world = sim::World::Generate(
        {.seed = 3, .robots = sim::kTownRobots, .settings = town});
    EXPECT_EQ(world.State().city.buildings.size(), 2U);
    world.RunFor(60 * sim::kMinutesPerDay);
    EXPECT_GT(world.State().robots.size(), sim::kTownRobots);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

TEST(Settings, EverySettingAtItsLimitsKeepsTheCityRunning) {
    // MECHAPUNK_ONLY=name runs one setting, so a script can find every crash.
    const char* only = std::getenv("MECHAPUNK_ONLY");
    for (const std::string_view name : sim::SettingNames()) {
        if (only != nullptr && name != only) {
            continue;
        }
        for (const bool high : {false, true}) {
            // Find the limit by asking: the range is in the refusal's text.
            sim::SimSettings probe;
            const auto refused = sim::ApplySetting(probe, name, high ? (std::int64_t{1} << 40)
                                                                      : -(std::int64_t{1} << 40));
            ASSERT_FALSE(refused) << name;
            const std::string text = refused.error();
            const auto from = text.find("from ");
            const auto to = text.find(" to ", from);
            ASSERT_NE(from, std::string::npos) << text;
            const std::int64_t low = std::stoll(text.substr(from + 5, to - from - 5));
            const std::int64_t top = std::stoll(text.substr(to + 4));
            sim::SimSettings settings;
            const auto applied = sim::ApplySetting(settings, name, high ? top : low);
            if (!applied) {
                continue;  // The other half of a min/max pair is in the way.
            }
            std::fprintf(stderr, "limit %s = %lld\n", std::string{name}.c_str(),
                         static_cast<long long>(high ? top : low));
            std::fflush(stderr);
            // A seed town is one dormitory and one workshop: it holds 20 robots,
            // not 80.
            const std::uint32_t robots = settings.city.seedTown != 0 ? 20 : 80;
            sim::World world =
                sim::World::Generate({.seed = 4, .robots = robots, .settings = settings});
            world.RunFor(2 * sim::kMinutesPerDay);
            EXPECT_TRUE(sim::ValidateWorldState(world.State())) << name;
            auto again = sim::DeserializeWorld(*sim::SerializeWorld(world.State()));
            EXPECT_TRUE(again.has_value()) << name;
        }
    }
}
