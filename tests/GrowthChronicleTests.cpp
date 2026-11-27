// The growth of a town is in its chronicle (task K, step K19).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

TEST(GrowthChronicle, ATownTellsItsGrowth) {
    sim::SimSettings settings;
    settings.city.seedTown = 1;
    settings.city.freePlotsMin = 4;
    settings.population.wealthSpread = 1;
    settings.population.immigration = 1;
    settings.population.immigrantsPerNight = 5;
    settings.construction.streakNights = 2;
    settings.construction.freeBedsPercent = 10;
    settings.metro.minBuildings = 6;
    settings.economy.robotsPerBank = 60;
    settings.economy.workplacesPerCorporation = 2;
    settings.crime.minGangs = 0;
    settings.crime.robotsPerGang = 40;
    sim::World world = sim::World::Generate({.seed = 1, .robots = 30, .settings = settings});
    // The chronicle keeps a few hundred entries, so it is read every night, while
    // an event is fresh; the sink counts what the simulation emitted.
    std::array<std::int64_t, 4> emitted{};
    std::array<bool, 4> told{};
    const std::array kinds{sim::EventKind::StationOpened, sim::EventKind::BankFounded,
                           sim::EventKind::CorporationFounded, sim::EventKind::GangFormed};
    world.SetEventSink([&](const sim::SimEvent& event) {
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            emitted[index] += event.kind == kinds[index] ? 1 : 0;
        }
    });
    std::array<std::int64_t, 4> before{};
    for (int day = 0; day < 300; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            if (emitted[index] > before[index]) {
                before[index] = emitted[index];
                const auto found = std::ranges::find_if(
                    world.State().chronicle,
                    [&](const sim::ChronicleEntry& entry) { return entry.kind == kinds[index]; });
                ASSERT_NE(found, world.State().chronicle.end()) << "kind " << index << " day " << day;
                told[index] = true;
                // It reads as a sentence.
                const sim::SimEvent event{.tick = found->tick, .kind = found->kind,
                                          .credits = found->credits, .account = found->account};
                EXPECT_EQ(world.Describe(event).find("unknown"), std::string::npos)
                    << world.Describe(event);
            }
        }
    }
    EXPECT_GE(emitted[0], 2) << "stations";
    EXPECT_GE(emitted[1], 1) << "banks";
    EXPECT_GE(emitted[2], 1) << "corporations";
    EXPECT_GE(emitted[3], 1) << "gangs";
    EXPECT_TRUE(told[0] && told[1] && told[2] && told[3]);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
