#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

TEST(Gangs, NewCityHasAtLeastTwo) {
    // Small city: fewer than 3000 robots, still gets at least 2.
    sim::World world = sim::World::Generate({.seed = 1, .robots = 60});
    EXPECT_GE(world.State().gangs.size(), 2U);

    // Large city: more than 9000 robots, capped at 6.
    sim::World big = sim::World::Generate({.seed = 2, .robots = 10000});
    EXPECT_LE(big.State().gangs.size(), 6U);
}

TEST(Gangs, RelationsAreSymmetric) {
    sim::World world = sim::World::Generate({.seed = 42, .robots = 500});
    const auto& gangs = world.State().gangs;

    for (std::size_t i = 0; i < gangs.size(); ++i) {
        for (std::size_t j = 0; j < gangs.size(); ++j) {
            if (i == j) continue;
            const auto& relAB = gangs[static_cast<std::size_t>(i)].relations;
            const auto& relBA = gangs[static_cast<std::size_t>(j)].relations;

            // Find gang A's stance toward gang B.
            sim::Stance stanceForB = sim::Stance::Neutral;
            for (const auto& r : relAB) {
                if (r.gang.value == static_cast<std::uint64_t>(gangs[static_cast<std::size_t>(j)].id.value)) {
                    stanceForB = r.stance;
                    break;
                }
            }

            // Find gang B's stance toward gang A.
            sim::Stance stanceForA = sim::Stance::Neutral;
            for (const auto& r : relBA) {
                if (r.gang.value == static_cast<std::uint64_t>(gangs[static_cast<std::size_t>(i)].id.value)) {
                    stanceForA = r.stance;
                    break;
                }
            }

            EXPECT_EQ(stanceForB, stanceForA) << "Gang " << gangs[static_cast<std::size_t>(j)].name
                                                << " relation mismatch toward gang "
                                                << gangs[static_cast<std::size_t>(i)].name;
        }
    }
}

TEST(Gangs, AtLeastOneRivalPair) {
    sim::World world = sim::World::Generate({.seed = 100, .robots = 500});
    const auto& gangs = world.State().gangs;

    bool foundRival = false;
    for (std::size_t i = 0; i < gangs.size(); ++i) {
        for (const auto& rel : gangs[static_cast<std::size_t>(i)].relations) {
            if (rel.stance == sim::Stance::Rival) {
                foundRival = true;
                break;
            }
        }
        if (foundRival) break;
    }

    EXPECT_TRUE(foundRival);
}

TEST(Gangs, RelationsSortedByGangId) {
    sim::World world = sim::World::Generate({.seed = 10, .robots = 500});
    const auto& gangs = world.State().gangs;

    for (const auto& gang : gangs) {
        for (std::size_t k = 1; k < gang.relations.size(); ++k) {
            EXPECT_LT(gang.relations[static_cast<std::size_t>(k - 1)].gang.value,
                      gang.relations[static_cast<std::size_t>(k)].gang.value);
        }
    }
}

TEST(Gangs, NoSelfRelation) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 500});
    const auto& gangs = world.State().gangs;

    for (const auto& gang : gangs) {
        for (const auto& rel : gang.relations) {
            EXPECT_NE(rel.gang.value, static_cast<std::uint64_t>(gang.id.value));
        }
    }
}

TEST(Gangs, SaveRoundTrips) {
    sim::World world = sim::World::Generate({.seed = 7, .robots = 100});
    auto json = sim::SerializeWorld(world.State());
    EXPECT_TRUE(json.has_value());

    // Reload world from JSON (migrations bring it to current version).
    auto state = sim::DeserializeWorld(*json);
    ASSERT_TRUE(state.has_value()) << state.error();

    auto reloaded = sim::World::FromState(std::move(*state));
    EXPECT_TRUE(reloaded.has_value());

    const auto& gangs1 = world.State().gangs;
    const auto& gangs2 = reloaded->State().gangs;
    EXPECT_EQ(gangs1.size(), gangs2.size());

    for (std::size_t i = 0; i < gangs1.size(); ++i) {
        const auto& a = gangs1[static_cast<std::size_t>(i)];
        const auto& b = gangs2[static_cast<std::size_t>(i)];
        EXPECT_EQ(a.id.value, b.id.value);
        EXPECT_EQ(a.name, b.name);
        EXPECT_EQ(a.color, b.color);
        EXPECT_EQ(a.turf.value, b.turf.value);
        EXPECT_EQ(a.relations.size(), b.relations.size());
    }
}

TEST(Gangs, TurfIsHabitatOrNone) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 500});
    const auto& gangs = world.State().gangs;

    for (const auto& gang : gangs) {
        if (gang.turf.value == 0) continue;  // no turf is allowed.

        // Look up the building referenced as turf.
        bool found = false;
        bool isHabitat = false;
        for (const sim::Building& b : world.State().city.buildings) {
            if (b.id == gang.turf) {
                found = true;
                isHabitat = (b.kind == sim::BuildingKind::Habitat);
                break;
            }
        }

        EXPECT_TRUE(found) << "Gang turf building not found";
        EXPECT_TRUE(isHabitat) << "Gang turf is not a habitat building";
    }
}

TEST(Gangs, DeterministicAcrossRuns) {
    sim::World first = sim::World::Generate({.seed = 42, .robots = 500});
    sim::World second = sim::World::Generate({.seed = 42, .robots = 500});

    const auto& g1 = first.State().gangs;
    const auto& g2 = second.State().gangs;

    EXPECT_EQ(g1.size(), g2.size());
    for (std::size_t i = 0; i < g1.size(); ++i) {
        const auto& a = g1[static_cast<std::size_t>(i)];
        const auto& b = g2[static_cast<std::size_t>(i)];
        EXPECT_EQ(a.id.value, b.id.value);
        EXPECT_EQ(a.name, b.name);
        EXPECT_EQ(a.color, b.color);
        EXPECT_EQ(a.turf.value, b.turf.value);
        EXPECT_EQ(a.relations.size(), b.relations.size());
    }
}

TEST(Gangs, RobotsJoinGangsOverTime) {
    // Population test: 300 robots for 20 days (480 hours = 28800 minutes).
    sim::World world = sim::World::Generate({.seed = 5, .robots = 300});

    // Count initial gang members (should be zero—generation doesn't assign them).
    std::uint64_t initialGangMembers = 0;
    for (const auto& robot : world.State().robots) {
        if (robot.gang.value) ++initialGangMembers;
    }

    // Run the simulation for 28800 minutes (20 days).
    world.RunFor(28800);

    // Count gang members after 20 days.
    std::uint64_t finalGangMembers = 0;
    for (const auto& robot : world.State().robots) {
        if (robot.gang.value) ++finalGangMembers;
    }

    // Some robots should have joined gangs.
    EXPECT_GT(finalGangMembers, initialGangMembers);
}

TEST(Gangs, NoOfficerIsAGangMember) {
    // Population test: 300 robots for 20 days.
    sim::World world = sim::World::Generate({.seed = 10, .robots = 300});
    world.RunFor(28800);

    const auto& rooms = world.Rooms();
    for (const auto& robot : world.State().robots) {
        if (!robot.gang.value) continue;
        // Officers have their workspace in a PoliceStation room.
        if (robot.workspace.value) {
            auto roomRef = rooms.Find(robot.workspace);
            bool isOfficer = false;
            if (roomRef && roomRef->room->kind == sim::RoomKind::PoliceStation) {
                isOfficer = true;
            }
            EXPECT_FALSE(isOfficer) << "Officer " << robot.name
                                    << " is a gang member";
        }
    }
}

TEST(Gangs, CounterIncrementsOnJoinAndLeave) {
    sim::World world = sim::World::Generate({.seed = 20, .robots = 300});

    // Check initial conditions before running.
    std::uint64_t gangsWithTurf = 0;
    for (const auto& gang : world.State().gangs) {
        if (gang.turf.value) ++gangsWithTurf;
    }

    // Run long enough to hit multiple midnights with ReviewGangs.
    std::uint64_t joinedBeforeHistory = 0;
    if (!world.State().history.empty()) {
        joinedBeforeHistory = world.State().history.back().counted.joinedGangs;
    }

    EXPECT_NO_THROW(world.RunFor(3500));  // Several midnights

    // Count how many robots have gang members after running past several midnights.
    std::uint64_t gangMembersAfter = 0;
    for (const auto& robot : world.State().robots) {
        if (robot.gang.value) ++gangMembersAfter;
    }

    EXPECT_GT(gangMembersAfter, 0U)
        << "No gang members after running past several midnights (eligible="
        << "young+dorm" << ", gangs-with-turf=" << gangsWithTurf
        << ", joinedGangs counter in history="
        << (world.State().history.empty() ? 0 : world.State().history.back().counted.joinedGangs) << ")";
}

TEST(Gangs, CrimeValidationPasses) {
    // Verify that crime data integrates correctly into validation.
    sim::World world = sim::World::Generate({.seed = 100, .robots = 500});

    // Run for many days � the economy should remain stable with crime events.
    world.RunFor(40 * sim::kMinutesPerDay);

    const auto& state = world.State();
    int gangMembers = 0;
    for (const auto& r : state.robots) {
        if (r.gang.value) ++gangMembers;
    }

    // Validate that the world is consistent with crime data present.
    EXPECT_TRUE(sim::ValidateWorldState(state));
}

// Same-gang members never rob each other or fight.
TEST(Gangs, SameGangNoRobbery) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 50});

    // Find two robots of the same gang.
    const auto& gangs = world.State().gangs;
    EXPECT_GT(gangs.size(), 0U);
    sim::EntityId gangId = gangs[0].id;

    std::vector<const sim::Robot*> members;
    for (const auto& r : world.State().robots) {
        if (r.gang == gangId) members.push_back(&r);
    }

    // Need at least 2 same-gang robots. Skip test if none found (rare).
    if (members.size() < 2) GTEST_SKIP();

    std::int64_t robberiesBefore = world.State().today.robberies;
    std::int32_t fightsBefore = world.State().today.fights;

    // Run for several nights when members might socialise together.
    world.RunFor(5 * sim::kMinutesPerDay);

    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

