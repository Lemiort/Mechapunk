#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Random.hpp"

namespace sim = mechapunk::sim;

// Locks the generator's output. If this changes, every existing save and
// seed produces a different world, so treat a failure as a format break.
TEST(Pcg32, MatchesReferenceSequence) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(42U, 54U);
    // Reference values of pcg32 with seed 42 and sequence 54 (pcg-c demo).
    constexpr std::array<std::uint32_t, 6> kExpected{
        0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
        0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    for (const std::uint32_t expected : kExpected) {
        EXPECT_EQ(random.NextU32(), expected);
    }
}

TEST(Pcg32, SameSeedAndStreamGiveSameSequence) {
    sim::Pcg32 first = sim::Pcg32::FromSeed(7, 3);
    sim::Pcg32 second = sim::Pcg32::FromSeed(7, 3);
    sim::Pcg32 otherStream = sim::Pcg32::FromSeed(7, 4);
    bool streamsDiffer = false;
    for (int index = 0; index < 100; ++index) {
        const std::uint32_t value = first.NextU32();
        EXPECT_EQ(value, second.NextU32());
        streamsDiffer = streamsDiffer || value != otherStream.NextU32();
    }
    EXPECT_TRUE(streamsDiffer);
}

TEST(Pcg32, UniformIntStaysInRangeAndCoversIt) {
    sim::Pcg32 random = sim::Pcg32::FromSeed(1);
    std::array<int, 6> seen{};
    for (int index = 0; index < 6000; ++index) {
        const std::uint32_t value = random.UniformInt(10, 15);
        ASSERT_GE(value, 10U);
        ASSERT_LE(value, 15U);
        ++seen.at(value - 10);
    }
    for (const int count : seen) {
        EXPECT_GT(count, 800);
    }
    EXPECT_EQ(random.UniformInt(5, 5), 5U);
}

TEST(GameTime, FormatsDaysAndClock) {
    EXPECT_EQ(sim::FormatTime(0), "Day 1 00:00");
    EXPECT_EQ(sim::FormatTime((2 * sim::kMinutesPerDay) + (7 * 60) + 42),
              "Day 3 07:42");
    EXPECT_EQ(sim::MinuteOfDay(sim::kMinutesPerDay + 5), 5U);
    EXPECT_EQ(sim::StartOfDay(sim::kMinutesPerDay + 5), sim::kMinutesPerDay);
}
