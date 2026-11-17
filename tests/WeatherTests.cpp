#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Weather.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
// The spells of `count` changes of weather from a new world of `seed`.
std::vector<sim::Weather> Spells(std::uint64_t seed, std::uint32_t count) {
    std::vector<sim::Weather> spells{sim::StartingWeather(seed, 0)};
    for (std::uint32_t index = 0; index < count; ++index) {
        spells.push_back(
            sim::NextWeather(spells.back(), seed, spells.back().changesAt));
    }
    return spells;
}
}  // namespace

TEST(Weather, StartsDryAndAlternatesDrySpellsAndRain) {
    const auto spells = Spells(7, 200);
    EXPECT_EQ(spells.front().rain, 0U);
    sim::Tick raining = 0;
    for (std::size_t index = 0; index + 1 < spells.size(); ++index) {
        const sim::Weather& spell = spells[index];
        const sim::Tick start = index == 0 ? 0 : spells[index - 1].changesAt;
        const sim::Tick length = spell.changesAt - start;
        EXPECT_EQ(spell.spells, index);
        EXPECT_EQ(spell.rain == 0, index % 2 == 0) << "spell " << index;
        if (spell.rain == 0) {
            EXPECT_GE(length, 8U * sim::kMinutesPerHour);
            EXPECT_LE(length, 48U * sim::kMinutesPerHour);
        } else {
            EXPECT_GE(spell.rain, 10U);
            EXPECT_LE(spell.rain, sim::kMaxRain);
            EXPECT_GE(length, 1U * sim::kMinutesPerHour);
            EXPECT_LE(length, 8U * sim::kMinutesPerHour);
            raining += length;
        }
    }
    // Rain about a seventh of the time: 4.5 hours of rain after 28 dry.
    const double share =
        static_cast<double>(raining) / static_cast<double>(spells.back().changesAt);
    EXPECT_GT(share, 0.08);
    EXPECT_LT(share, 0.25);
}

TEST(Weather, LightRainIsCommonerThanHeavy) {
    std::uint32_t light = 0;
    std::uint32_t heavy = 0;
    for (const sim::Weather& spell : Spells(3, 400)) {
        light += spell.rain > 0 && spell.rain < 55 ? 1 : 0;
        heavy += spell.rain >= 55 ? 1 : 0;
    }
    EXPECT_GT(light, heavy);
    EXPECT_GT(heavy, 0U);
}

TEST(Weather, SameSeedSameWeatherAndOtherSeedsDiffer) {
    const auto first = Spells(11, 50);
    const auto again = Spells(11, 50);
    const auto other = Spells(12, 50);
    bool differs = false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        EXPECT_EQ(first[index].rain, again[index].rain);
        EXPECT_EQ(first[index].changesAt, again[index].changesAt);
        differs = differs || first[index].changesAt != other[index].changesAt;
    }
    EXPECT_TRUE(differs);
}

TEST(Weather, WorldsChangeWeatherOnScheduleAndReportIt) {
    sim::World world = sim::World::Generate({.seed = 4, .robots = 40});
    EXPECT_EQ(world.State().weather.rain, 0U);
    std::vector<sim::SimEvent> changes;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::WeatherChanged) {
            changes.push_back(event);
        }
    });
    world.RunFor(5 * sim::kMinutesPerDay);
    ASSERT_FALSE(changes.empty());
    sim::Weather expected = sim::StartingWeather(4, 0);
    for (const sim::SimEvent& event : changes) {
        EXPECT_EQ(event.tick, expected.changesAt);
        expected = sim::NextWeather(expected, 4, event.tick);
        EXPECT_EQ(static_cast<std::uint32_t>(event.change), expected.rain);
        EXPECT_EQ(event.tick + event.minutes, expected.changesAt);
        EXPECT_FALSE(world.Describe(event).empty());
    }
    EXPECT_EQ(world.State().weather.spells, changes.size());
}

TEST(Weather, NamesRain) {
    EXPECT_EQ(sim::RainName(0), "dry");
    EXPECT_EQ(sim::RainName(10), "drizzle");
    EXPECT_EQ(sim::RainName(40), "rain");
    EXPECT_EQ(sim::RainName(70), "heavy rain");
    EXPECT_EQ(sim::RainName(100), "downpour");
}
