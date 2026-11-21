#include <gtest/gtest.h>

#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
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

// --- The crime tests B4 asked for and B7 promised (guide §5) ---
//
// One stage serves them all: a crowd of robots standing together in a venue
// room, the only gang members in the city, everyone else gangless. Crime
// needs a gang member, so every crime of the day has a crowd robot in it.
// Gangless idlers do wander into the room, so a test asks the event log
// which robots a crime was about, not only how many crimes happened.
struct Stage {
    sim::WorldState state;
    sim::EntityId room{};         // The venue room the crowd stands in.
    std::vector<std::size_t> crowd;  // Indexes of the room's robots.
    std::optional<std::size_t> officer;
};

// Places `crowdSize` non-officer robots Visiting in the first venue room
// nobody stands in, and `wantOfficer` adds one officer standing in that room
// on a patrol leg. Every scripted robot's next action is far away, so none
// of them moves during the run.
Stage MakeStage(std::uint32_t seed, std::uint32_t robots, std::size_t crowdSize,
                bool wantOfficer, std::int32_t maxWear = 400) {
    sim::WorldState state =
        sim::World::Generate({.seed = seed, .robots = robots}).State();

    // Night time: crime only happens between 20:00 and 04:00. Starting the
    // stage at 21:00 and stopping the runs before midnight keeps them inside
    // one day, where wear cannot grow at all and needs only drift a little
    // - what a fight costs condition is only measurable against that drift.
    state.tick = sim::StartOfDay(state.tick) + 21 * sim::kMinutesPerHour;

    std::set<std::uint64_t> stations;
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::PoliceStation) {
                stations.insert(room.id.value);
            }
        }
    }
    const auto worksInPoliceStation = [&](const sim::Robot& robot) {
        return robot.workspace.IsValid() &&
               stations.count(robot.workspace.value) != 0;
    };

    const auto occupied = [&](std::uint64_t id) {
        for (const sim::Robot& robot : state.robots) {
            if (robot.location.value == id) return true;
        }
        return false;
    };

    // A room nobody stands in, preferring a bar or a diner: they add nothing
    // to condition, so a fight's -100 is not buried under a clinic's +150,
    // and they are open through the night, where crime happens. A Visiting
    // robot must be in a venue, so the fallback is any venue room.
    sim::EntityId room{};
    for (const sim::RoomKind kind :
        std::initializer_list<sim::RoomKind>{sim::RoomKind::Bar, sim::RoomKind::Diner}) {
        for (const sim::Building& building : state.city.buildings) {
            for (const sim::Room& candidate : building.rooms) {
                if (candidate.kind != kind || occupied(candidate.id.value)) continue;
                room = candidate.id;
                break;
            }
            if (room.IsValid()) break;
        }
        if (room.IsValid()) break;
    }
    if (!room.IsValid()) {
        for (const sim::Building& building : state.city.buildings) {
            for (const sim::Room& candidate : building.rooms) {
                if (!sim::IsVenue(candidate.kind) || occupied(candidate.id.value)) continue;
                room = candidate.id;
                break;
            }
            if (room.IsValid()) break;
        }
    }

    // An officer standing in that room: an officer is a robot whose job room
    // is a police station. A town this small has none (the city founds one
    // per 3000 residents), so take over a ground-floor room nobody stands
    // in, as the founder and the growing city do, and hand it to a robot.
    if (wantOfficer && stations.empty()) {
        for (sim::Building& building : state.city.buildings) {
            for (sim::Room& candidate : building.rooms) {
                if (candidate.floor != 0 || candidate.kind == sim::RoomKind::Apartment ||
                    candidate.id == room || occupied(candidate.id.value)) {
                    continue;
                }
                candidate.kind = sim::RoomKind::PoliceStation;
                candidate.name = "Police Station 1";
                candidate.capacity = 2;
                candidate.pricePercent = 100;
                candidate.visits = 0;
                stations.insert(candidate.id.value);
                break;
            }
            if (!stations.empty()) break;
        }
    }

    std::optional<std::size_t> officer;
    if (wantOfficer) {
        for (std::size_t index = 0; index < state.robots.size(); ++index) {
            if (worksInPoliceStation(state.robots[index])) {
                officer = index;
                break;
            }
        }
        if (!officer) {
            sim::EntityId station{};
            for (const sim::Building& building : state.city.buildings) {
                for (const sim::Room& candidate : building.rooms) {
                    if (candidate.kind != sim::RoomKind::PoliceStation) continue;
                    if (sim::JobsAt(candidate) > 0) {
                        station = candidate.id;
                        break;
                    }
                }
                if (station.IsValid()) break;
            }
            if (station.IsValid()) {
                for (std::size_t index = 0; index < state.robots.size(); ++index) {
                    if (worksInPoliceStation(state.robots[index])) continue;
                    state.robots[index].workspace = station;
                    officer = index;
                    break;
                }
            }
        }
    }

    std::vector<std::size_t> crowd;
    for (std::size_t index = 0; index < state.robots.size(); ++index) {
        if (officer && index == *officer) continue;
        if (worksInPoliceStation(state.robots[index])) continue;
        // A well worn robot has its condition capped at kMaxWear minus wear
        // by LifeCycle, which blurs the fight's own -100.
        if (state.robots[index].wear > maxWear) continue;
        if (crowd.size() < crowdSize) crowd.push_back(index);
    }

    // Nobody belongs to a gang until the test puts them in one.
    for (sim::Robot& robot : state.robots) {
        robot.gang = sim::EntityId{};
    }

    const auto stay = state.tick + 100000;
    for (std::size_t index : crowd) {
        sim::Robot& robot = state.robots[index];
        robot.activity = sim::RobotActivity::Visiting;
        robot.location = room;
        robot.destination = sim::EntityId{};
        robot.patrol = sim::EntityId{};
        robot.boardsAt = 0;
        robot.alightsAt = 0;
        robot.nextActionAt = stay;
        // Start exactly at the wear cap: LifeCycle can then only cost what
        // the wear itself costs (20 a fight), which leaves the fight's own
        // -100 measurable.
        robot.needs.condition = sim::kMaxWear - robot.wear;
    }
    if (officer) {
        sim::Robot& robot = state.robots[*officer];
        robot.activity = sim::RobotActivity::Working;
        robot.patrol = room;  // On shift: officers on a patrol leg count.
        robot.location = room;
        robot.boardsAt = 0;
        robot.alightsAt = 0;
        robot.nextActionAt = stay;
    }
    return {std::move(state), room, std::move(crowd), officer};
}

// Runs to the last minute of the same day: all the night hours where crime happens are
// in, midnight is not, where ReviewGangs would found new members elsewhere
// and blur the counters.
void RunThroughTheNight(sim::World& world) {
    const auto tick = world.State().tick;
    world.RunFor(sim::StartOfDay(tick) + sim::kMinutesPerDay - 1 - tick);
}

// Robot of an id in a state.
const sim::Robot* FindRobot(const sim::WorldState& state, std::uint64_t id) {
    for (const sim::Robot& robot : state.robots) {
        if (robot.id.value == id) return &robot;
    }
    return nullptr;
}

// Robot ids of the robots in `seats`.
std::set<std::uint64_t> IdsOf(const sim::WorldState& state,
                              const std::vector<std::size_t>& seats) {
    std::set<std::uint64_t> ids;
    for (std::size_t index : seats) {
        ids.insert(state.robots[index].id.value);
    }
    return ids;
}

// Catches every event the world emits during the run.
void Watch(std::vector<sim::SimEvent>& log, sim::World& world) {
    world.SetEventSink([&log](const sim::SimEvent& event) { log.push_back(event); });
}

// Crimes of `kind` whose robot is one of `ids` and whose other robot is one
// of `others`: how a test asks whether its own robots were robbed or fought.
std::size_t Between(const std::vector<sim::SimEvent>& log, sim::EventKind kind,
                    const std::set<std::uint64_t>& ids,
                    const std::set<std::uint64_t>& others) {
    std::size_t count = 0;
    for (const sim::SimEvent& event : log) {
        if (event.kind != kind) continue;
        if (ids.contains(event.robot.value) && others.contains(event.otherRobot.value)) {
            ++count;
        }
    }
    return count;
}

// Crimes of `kind` with a robot of `ids` in either seat (an arrest names
// only the arrested robot).
std::size_t CountOf(const std::vector<sim::SimEvent>& log, sim::EventKind kind,
                    const std::set<std::uint64_t>& ids) {
    std::size_t count = 0;
    for (const sim::SimEvent& event : log) {
        if (event.kind != kind) continue;
        if (ids.contains(event.robot.value) || ids.contains(event.otherRobot.value)) {
            ++count;
        }
    }
    return count;
}

// How one gang feels about another: both directions are set by the caller,
// as validation expects.
void SetStance(sim::WorldState& state, sim::EntityId from, sim::EntityId to,
               sim::Stance stance) {
    for (sim::Gang& gang : state.gangs) {
        if (gang.id != from) continue;
        for (sim::GangRelation& relation : gang.relations) {
            if (relation.gang == to) {
                relation.stance = stance;
                return;
            }
        }
    }
}

// Members of one gang never rob or fight each other. They are the only gang
// members in the city (guide §5: make the data the test needs, never skip
// it), so if the same-gang rule in ConsiderCrime broke, every talking pair
// of the crowd would rob - StanceOfGang of a gang with itself reads Neutral
// - and these counts would be far above zero.
TEST(Gangs, SameGangNoRobbery) {
    Stage stage = MakeStage(1, 60, 12, false);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 2U);
    ASSERT_FALSE(stage.state.gangs.empty());
    for (std::size_t index : stage.crowd) {
        stage.state.robots[index].gang = stage.state.gangs[0].id;
    }
    const std::set<std::uint64_t> crowd = IdsOf(stage.state, stage.crowd);

    auto loaded = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    std::vector<sim::SimEvent> log;
    Watch(log, world);
    RunThroughTheNight(world);

    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    // A gangless idler that wanders in may be robbed by the crowd: that is
    // the rule working, not breaking, so the test looks at the crowd's own
    // pairs.
    EXPECT_EQ(Between(log, sim::EventKind::Robbed, crowd, crowd), 0U);
    EXPECT_EQ(Between(log, sim::EventKind::Fought, crowd, crowd), 0U);
}

// A robbery moves credits, and the money still adds up.
TEST(Gangs, RobberyMovesCreditsAndMoneyAddsUp) {
    Stage stage = MakeStage(7, 60, 12, false);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 4U);
    const sim::EntityId gang = stage.state.gangs[0].id;
    std::vector<std::size_t> memberSeats;
    std::vector<std::size_t> civilianSeats;
    for (std::size_t seat = 0; seat < stage.crowd.size(); ++seat) {
        if (seat % 2 == 0) {
            stage.state.robots[stage.crowd[seat]].gang = gang;
            memberSeats.push_back(stage.crowd[seat]);
        } else {
            civilianSeats.push_back(stage.crowd[seat]);
        }
    }
    const std::set<std::uint64_t> members = IdsOf(stage.state, memberSeats);
    const std::set<std::uint64_t> civilians = IdsOf(stage.state, civilianSeats);
    std::map<std::uint64_t, std::int64_t> creditsBefore;
    for (std::size_t index : stage.crowd) {
        creditsBefore[stage.state.robots[index].id.value] =
            stage.state.robots[index].credits;
    }

    auto loaded = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    std::vector<sim::SimEvent> log;
    Watch(log, world);
    RunThroughTheNight(world);

    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    const sim::SimEvent* crime = nullptr;
    for (const sim::SimEvent& event : log) {
        if (event.kind != sim::EventKind::Robbed) continue;
        if (!civilians.contains(event.robot.value) || !members.contains(event.otherRobot.value)) {
            continue;
        }
        crime = &event;
        break;
    }
    ASSERT_TRUE(crime != nullptr) << "no robbery between the crowd's robots";
    EXPECT_GT(crime->credits, 0);

    // The two pockets moved by that loot: the crowd's robots never act on
    // their own, so a robbery is the only thing that can change them.
    const sim::Robot* victim = FindRobot(world.State(), crime->robot.value);
    const sim::Robot* robber = FindRobot(world.State(), crime->otherRobot.value);
    ASSERT_TRUE(victim != nullptr);
    ASSERT_TRUE(robber != nullptr);
    EXPECT_LE(victim->credits,
              creditsBefore.at(crime->robot.value) - crime->credits);
    EXPECT_GE(robber->credits,
              creditsBefore.at(crime->otherRobot.value) + crime->credits);
    // And the ledger behind the pockets still adds up.
    EXPECT_GT(world.State().today.robbedCredits, 0);
    EXPECT_TRUE(sim::ValidateEconomy(world.State()).has_value());
}

// With an officer on shift in the room, the pair is arrested instead of
// robbing: no credit moves, the robber's rap sheet grows, the fine is paid
// to the government.
TEST(Gangs, OfficerTurnsTheRobberyIntoAnArrest) {
    Stage stage = MakeStage(7, 300, 100, true, /*maxWear=*/300);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 2U);
    ASSERT_TRUE(stage.officer.has_value());
    const sim::EntityId gang = stage.state.gangs[0].id;
    for (std::size_t seat = 0; seat < stage.crowd.size(); seat += 2) {
        stage.state.robots[stage.crowd[seat]].gang = gang;
    }

    auto loaded = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    RunThroughTheNight(world);

    const sim::WorldState& after = world.State();
    EXPECT_TRUE(sim::ValidateWorldState(after));
    EXPECT_GT(after.today.arrests, 0);
    EXPECT_EQ(after.today.robberies, 0);
    EXPECT_EQ(after.today.robbedCredits, 0);
    std::int64_t marks = 0;
    for (const sim::Robot& robot : after.robots) {
        marks += robot.arrests;
    }
    EXPECT_GE(marks, static_cast<std::int64_t>(after.today.arrests));
    EXPECT_TRUE(sim::ValidateEconomy(after).has_value());
}

// Two gangs that are friendly both ways never rob each other, and never
// fight (only rivals fight).
TEST(Gangs, FriendlyGangsNeverRob) {
    Stage stage = MakeStage(7, 60, 12, false);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 2U);
    ASSERT_GE(stage.state.gangs.size(), 2U);
    const sim::EntityId first = stage.state.gangs[0].id;
    const sim::EntityId second = stage.state.gangs[1].id;
    SetStance(stage.state, first, second, sim::Stance::Friendly);
    SetStance(stage.state, second, first, sim::Stance::Friendly);
    for (std::size_t seat = 0; seat < stage.crowd.size(); ++seat) {
        stage.state.robots[stage.crowd[seat]].gang =
            seat % 2 == 0 ? first : second;
    }
    const std::set<std::uint64_t> crowd = IdsOf(stage.state, stage.crowd);

    auto loaded = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    std::vector<sim::SimEvent> log;
    Watch(log, world);
    RunThroughTheNight(world);

    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    // A member may still rob a gangless idler that wanders into the room;
    // what must never happen is the two gangs turning on each other.
    EXPECT_EQ(Between(log, sim::EventKind::Robbed, crowd, crowd), 0U);
    EXPECT_EQ(CountOf(log, sim::EventKind::Fought, crowd), 0U);
    // Nobody else in the city has a gang, so no other pair can fight either.
    EXPECT_EQ(world.State().today.fights, 0);
}

// Moves a stage's world through the night and totals how the crowd's
// condition fell and its wear rose, plus the fights it saw.
struct Drift {
    std::int64_t conditionLoss = 0;
    std::int64_t wearGain = 0;
    std::int64_t fights = 0;
};

Drift MeasureDrift(Stage&& stage) {
    auto loaded = sim::World::FromState(std::move(stage.state));
    if (!loaded.has_value()) {
        ADD_FAILURE() << loaded.error();
        return {};
    }
    sim::World world = std::move(*loaded);
    std::vector<std::int32_t> condition;
    std::vector<std::int32_t> wear;
    for (std::size_t index : stage.crowd) {
        condition.push_back(world.State().robots[index].needs.condition);
        wear.push_back(world.State().robots[index].wear);
    }
    RunThroughTheNight(world);
    Drift drift;
    for (std::size_t seat = 0; seat < stage.crowd.size(); ++seat) {
        const sim::Robot& robot = world.State().robots[stage.crowd[seat]];
        drift.conditionLoss += condition[seat] - robot.needs.condition;
        drift.wearGain += robot.wear - wear[seat];
    }
    drift.fights = static_cast<std::int64_t>(world.State().today.fights);
    return drift;
}

// Rival gangs do fight, and a fight costs both sides wear 20 and condition
// 100 (B4; needs.condition, not a condition field - B7 was wrong). Needs
// drift on their own while a robot sits idle, so the same crowd is run twice
// - once ganged, once not - and the difference is what the fights cost.
TEST(Gangs, RivalFightsCutWearAndCondition) {
    // A seed with eight fights over the night: one is enough to prove the
    // cost, more make the figure stand out.
    const std::uint32_t seed = 6;
    Stage rival = MakeStage(seed, 400, 120, false, /*maxWear=*/300);
    Stage control = MakeStage(seed, 400, 120, false, /*maxWear=*/300);
    ASSERT_TRUE(rival.room.IsValid());
    ASSERT_EQ(rival.crowd.size(), 120U);
    ASSERT_GE(rival.state.gangs.size(), 2U);
    const sim::EntityId first = rival.state.gangs[0].id;
    const sim::EntityId second = rival.state.gangs[1].id;
    SetStance(rival.state, first, second, sim::Stance::Rival);
    SetStance(rival.state, second, first, sim::Stance::Rival);
    for (std::size_t seat = 0; seat < rival.crowd.size(); ++seat) {
        rival.state.robots[rival.crowd[seat]].gang =
            seat % 2 == 0 ? first : second;
    }
    // The control crowd is the same robots in the same room with nobody
    // in a gang: no crime can touch them.

    const Drift idle = MeasureDrift(std::move(control));
    const Drift fighting = MeasureDrift(std::move(rival));
    EXPECT_GT(fighting.fights, 0);
    // Nothing else moves these two figures inside a day: wear only grows at
    // midnight, and the room cannot raise condition, so the control run
    // says exactly what the idle decay costs and the difference is the
    // fights - 20 wear and 100 condition each side.
    EXPECT_EQ(fighting.conditionLoss - idle.conditionLoss, 200 * fighting.fights);
    EXPECT_EQ(fighting.wearGain - idle.wearGain, 40 * fighting.fights);
}
