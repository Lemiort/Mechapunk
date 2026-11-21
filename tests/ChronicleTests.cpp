// Tests for step C2 of docs/tasks/C-chronicle.md: `World::Emit` feeds
// `World::Chronicle`, which keeps only notable events in
// `state_.chronicle` (the keeping rules live in sim/src/Chronicle.cpp).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

// Distinct entry kinds with their counts, so a failed threshold below can
// be read off instead of guessed at.
std::string KindCounts(const std::vector<sim::ChronicleEntry>& entries) {
    std::set<sim::EventKind> kinds;
    for (const auto& entry : entries) {
        kinds.insert(entry.kind);
    }
    std::string out;
    for (const sim::EventKind kind : kinds) {
        std::size_t count = 0;
        for (const auto& entry : entries) {
            count += entry.kind == kind ? 1 : 0;
        }
        out += std::to_string(static_cast<int>(kind)) + ": " +
               std::to_string(count) + "  ";
    }
    return out;
}

TEST(Chronicle, RecordsNotableEvents) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    world.RunFor(30 * sim::kMinutesPerDay);
    const auto& chronicle = world.State().chronicle;

    EXPECT_FALSE(chronicle.empty()) << KindCounts(chronicle);
    EXPECT_LE(chronicle.size(), sim::kChronicleCap);
    // Oldest first, the same rule ValidateWorldState checks on every load.
    EXPECT_TRUE(std::ranges::is_sorted(chronicle, std::less{},
                                       &sim::ChronicleEntry::tick));

    std::set<sim::EventKind> kinds;
    for (const auto& entry : chronicle) {
        kinds.insert(entry.kind);
    }
    // Several kinds: joins and memories alone are everyday in a 1000
    // robot city over 30 nights, and a whole night of talks, rents and
    // visits fills out the rest.
    EXPECT_GE(kinds.size(), 4U) << KindCounts(chronicle);
    EXPECT_TRUE(kinds.contains(sim::EventKind::JoinedGang));
    EXPECT_TRUE(kinds.contains(sim::EventKind::Remembered));

    // The keeping rule bites: thousands of Departed and ArrivedHome events
    // happened over 30 days, and none of them reached the chronicle.
    EXPECT_EQ(kinds.count(sim::EventKind::Departed), 0U);
    EXPECT_EQ(kinds.count(sim::EventKind::ArrivedHome), 0U);
    EXPECT_EQ(kinds.count(sim::EventKind::WeatherChanged), 0U);
}

TEST(Chronicle, SurvivesSave) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    world.RunFor(30 * sim::kMinutesPerDay);
    const auto& before = world.State().chronicle;
    ASSERT_FALSE(before.empty());

    auto json = sim::SerializeWorld(world.State());
    ASSERT_TRUE(json.has_value());
    auto state = sim::DeserializeWorld(*json);
    ASSERT_TRUE(state.has_value()) << state.error();

    // FromState runs ValidateWorldState, so a chronicle that broke its
    // order or length rule would be refused here.
    auto reloaded = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error();
    const auto& after = reloaded->State().chronicle;

    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i != before.size(); ++i) {
        EXPECT_EQ(before[i].tick, after[i].tick) << "entry " << i;
        EXPECT_EQ(before[i].kind, after[i].kind) << "entry " << i;
        EXPECT_EQ(before[i].robot.value, after[i].robot.value) << "entry " << i;
        EXPECT_EQ(before[i].otherRobot.value, after[i].otherRobot.value)
            << "entry " << i;
        EXPECT_EQ(before[i].room.value, after[i].room.value) << "entry " << i;
        EXPECT_EQ(before[i].credits, after[i].credits) << "entry " << i;
        EXPECT_EQ(before[i].memory, after[i].memory) << "entry " << i;
    }
}
