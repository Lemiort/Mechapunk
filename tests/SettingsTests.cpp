// Task "settings": the simulation's knobs live in SimSettings, a city is made
// from a seed and a set of settings, and a test turns one knob to reach a
// corner case in a few days instead of a few weeks.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "mechapunk/sim/GameTime.hpp"
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
        EXPECT_FALSE(sim::ApplySetting(settings, name, -1)) << name;
        EXPECT_FALSE(sim::ApplySetting(settings, name, 100000000)) << name;
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
