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
                // A station this size has three job slots; whoever already
                // worked in this room plus the officer this test adds have to
                // fit them, so a room that held a bigger crew is left alone.
                const auto previousKind = candidate.kind;
                const auto previousCapacity = candidate.capacity;
                const auto previousPrice = candidate.pricePercent;
                const auto previousVisits = candidate.visits;
                std::string previousName = candidate.name;

                candidate.kind = sim::RoomKind::PoliceStation;
                candidate.name = "Police Station 1";
                candidate.capacity = 2;
                candidate.pricePercent = 100;
                candidate.visits = 0;

                std::uint32_t workers = 0;
                for (const sim::Robot& robot : state.robots) {
                    workers += robot.workspace == candidate.id;
                }
                if (workers + 1 > sim::JobsAt(candidate)) {
                    candidate.kind = previousKind;
                    candidate.capacity = previousCapacity;
                    candidate.pricePercent = previousPrice;
                    candidate.visits = previousVisits;
                    candidate.name = std::move(previousName);
                    continue;
                }
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

// Robbery is rare, not the default: it only happens on its 3% pair roll
// (B4). Half the crowd is in a gang and half is gangless, all in one venue
// all night, so most talking pairs there are eligible for a robbery. The
// run has about 18 talks (one every 10 minutes over the night hours left)
// and at most crowd.size() / 2 pairs in each, so the roll puts the count
// near three percent of that upper bound; a rule that ignored the roll
// would rob nearly every one of those pairs instead.
TEST(Gangs, RobberyNeedsThePairRoll) {
    Stage stage = MakeStage(7, 300, 100, false, /*maxWear=*/300);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_EQ(stage.crowd.size(), 100U);
    for (std::size_t seat = 0; seat < stage.crowd.size(); seat += 2) {
        stage.state.robots[stage.crowd[seat]].gang = stage.state.gangs[0].id;
    }

    auto loaded = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    sim::World world = std::move(*loaded);
    RunThroughTheNight(world);

    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    const std::size_t talks = 180 / 10;
    const std::size_t pairsAtMost = talks * stage.crowd.size() / 2;
    // The roll still bites sometimes, and only a small share of the pairs.
    EXPECT_GT(world.State().today.robberies, 0);
    EXPECT_LT(static_cast<std::size_t>(world.State().today.robberies),
              pairsAtMost / 6);
    EXPECT_TRUE(sim::ValidateEconomy(world.State()).has_value());
}

// A robbery moves credits, and the money still adds up. The crowd is big
// because the robbery only happens on its 3% pair roll: a handful of robots
// might never roll it in one night, a crowd of sixty surely does.
TEST(Gangs, RobberyMovesCreditsAndMoneyAddsUp) {
    Stage stage = MakeStage(7, 300, 60, false, /*maxWear=*/300);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 20U);
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

    // B15: an arrest names its officer on both cards. The room has exactly
    // one officer, so every arrested robot's memory names it, and it has
    // memories of every arrest it made.
    const sim::EntityId officerId = after.robots[*stage.officer].id;
    std::size_t arrested = 0;
    for (const sim::Robot& robot : after.robots) {
        if (robot.id == officerId) {
            // The officer's card: "arrested <robber>" for every arrest.
            EXPECT_TRUE(std::ranges::any_of(
                robot.memories, [](const sim::Memory& memory) {
                    return memory.kind == sim::MemoryKind::ArrestedSomeone &&
                           memory.robot.IsValid();
                }));
            continue;
        }
        if (robot.arrests == 0) {
            continue;
        }
        ++arrested;
        EXPECT_TRUE(std::ranges::any_of(
            robot.memories, [&](const sim::Memory& memory) {
                return memory.kind == sim::MemoryKind::Arrested &&
                       memory.robot == officerId;
            })) << robot.name;
    }
    EXPECT_GT(arrested, 0U);
}
