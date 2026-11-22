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

// Officer and rival rules over the stage: the bond a talking officer and member make,
// friendly gangs never robbing each other, rival members fighting, and the officer

// catching the robbers of the hour before. The stage fixture is the one from
// GangCrimeTests.cpp; the copy lives in an anonymous namespace.

namespace {
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

}  // namespace


// B4's "always rivals": an officer on shift and a gang member who talk lose
// 20 affinity each way. Before B12b this rule could not fire at all: the
// officer test began with `first.gang.value &&`, and ReviewGangs forbids an
// officer from having a gang (Gangs.NoOfficerIsAGangMember pins that).
//
// The stage for it is one gang member and one other robot in a venue room,
// with the rest of the city frozen, so no wanderer joins the room and the
// pair is the only thing those minutes can do. Those two robots have no bonds
// and no interests of their own, so one talk between them moves feelings by
// the stranger's band alone, -5 to +3, and the test can read the officer's
// -20 out of the bond. The pair is scripted to talk exactly once, where
// ChangeBond's "deeper than rivals" halving cannot interfere, and the run
// stays inside one day and out of the 20:00 to 04:00 hours, where a robbery
// would draw the pair roll, move credits and drop a bond by 60.
struct Pair {
    std::int32_t memberSide = 0;  // What the member feels about the other.
    std::int32_t otherSide = 0;   // What the other feels about the member.
    std::uint32_t talks = 0;      // How often the two of them talked.
};

// The bond one robot keeps about another, or none if they never met.
const sim::Bond* BondAbout(const sim::Robot& robot, std::uint64_t otherId) {
    for (const sim::Bond& bond : robot.bonds) {
        if (bond.robot.value == otherId) return &bond;
    }
    return nullptr;
}

// The other robot always works in the stage's police station; `atPost` has it
// standing that post - working, or on a patrol leg through the room - instead
// of visiting the room off shift. Both variants stand in the stage's empty
// venue, where the pair is alone because nothing can walk in while the city
// is frozen.
sim::WorldState PairVariant(const Stage& stage, std::size_t memberSeat,
    bool atPost, std::uint32_t hour) {
    sim::WorldState state = stage.state;
    // Daytime: the run goes from this hour to the next one.
    state.tick = sim::StartOfDay(state.tick) + hour * sim::kMinutesPerHour;

    sim::Robot& member = state.robots[memberSeat];
    sim::Robot& other = state.robots[*stage.officer];

    member.gang = state.gangs[0].id;
    other.gang = sim::EntityId{};
    // No history, and nothing they could enjoy talking about: one talk is
    // then worth a known amount, the stranger's -5 plus the spread up to +3.
    member.bonds.clear();
    other.bonds.clear();
    member.interests = 0;
    other.interests = 0;

    member.location = stage.room;
    other.location = stage.room;
    other.destination = sim::EntityId{};
    other.boardsAt = 0;
    other.alightsAt = 0;
    // A patrol leg is how an officer standing a venue looks it over; only the
    // on-shift variant may carry one, or the state is invalid.
    other.patrol = atPost ? stage.room : sim::EntityId{};
    other.activity = atPost ? sim::RobotActivity::Working
                           : sim::RobotActivity::Visiting;
    return state;
}

// Runs a variant over `minutes` of daytime and reads the pair's bonds back.
Pair RunPair(sim::WorldState state, std::size_t memberSeat,
    std::size_t otherSeat, std::uint32_t minutes) {
    // Nobody acts during the run.
    const auto stay = state.tick + sim::kMinutesPerDay;
    for (sim::Robot& robot : state.robots) {
        robot.nextActionAt = stay;
    }

    auto loaded = sim::World::FromState(std::move(state));
    if (!loaded.has_value()) {
        ADD_FAILURE() << loaded.error();
        return {};
    }
    sim::World world = std::move(*loaded);
    world.RunFor(minutes - 1);

    const sim::WorldState& after = world.State();
    EXPECT_TRUE(sim::ValidateWorldState(after));
    EXPECT_TRUE(sim::ValidateEconomy(after).has_value());
    const sim::Robot& member = after.robots[memberSeat];
    const sim::Robot& other = after.robots[otherSeat];
    const sim::Bond* toOther = BondAbout(member, other.id.value);
    const sim::Bond* toMember = BondAbout(other, member.id.value);
    if (toOther == nullptr || toMember == nullptr) {
        ADD_FAILURE() << "the pair never formed a bond";
        return {};
    }
    return {toOther->affinity, toMember->affinity, toOther->talks};
}

// An officer on shift in the room is a rival to the gang member: on top of
// what the one talk itself was worth, both feelings drop by 20. These minutes
// give exactly one talk at this seed, which is what makes the band below
// exact: two robots with no interests of their own talk like strangers, so
// one talk moves feelings from -5 to +3, and only a talk plus the rule's -20
// lands in -25 to -17. Any other talk count shows up in the bond's own
// counter, which the test pins as well.
TEST(Gangs, OfficerCoolsTheBondToAMember) {
    const std::uint32_t seed = 38;
    Stage stage = MakeStage(seed, 300, 1, true);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_EQ(stage.crowd.size(), 1U);
    ASSERT_TRUE(stage.officer.has_value());
    const std::size_t member = stage.crowd[0];
    const std::size_t other = *stage.officer;

    const Pair onShift =
        RunPair(PairVariant(stage, member, true, 4), member, other, 200);
    EXPECT_EQ(onShift.talks, 2U);  // The talk, then what the rule did.
    EXPECT_LE(onShift.memberSide, -17);
    EXPECT_GE(onShift.memberSide, -25);
    EXPECT_LE(onShift.otherSide, -17);
    EXPECT_GE(onShift.otherSide, -25);
}

// The same robot visiting the room, its police job behind it, is off shift
// and nobody's rival: feelings move by the one talk alone.
TEST(Gangs, OfficerOffShiftCoolsNothing) {
    const std::uint32_t seed = 4;
    Stage stage = MakeStage(seed, 300, 1, true);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_EQ(stage.crowd.size(), 1U);
    ASSERT_TRUE(stage.officer.has_value());
    const std::size_t member = stage.crowd[0];
    const std::size_t other = *stage.officer;

    const Pair offShift =
        RunPair(PairVariant(stage, member, false, 18), member, other, 40);
    EXPECT_EQ(offShift.talks, 1U);
    EXPECT_GE(offShift.memberSide, -5);
    EXPECT_LE(offShift.memberSide, 3);
    EXPECT_GE(offShift.otherSide, -5);
    EXPECT_LE(offShift.otherSide, 3);
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

// B4 asked for two ways an officer takes a robber, and the code had one: the
// pair roll decides a robbery in the same ten minutes the watched room is
// built, so the officer had to be standing there at that exact moment, which
// over 40 days at 10000 robots came out at 4 arrests to 1473 robberies. The
// other way is the one this test pins: an officer on patrol arrests the members
// it meets in its room who robbed in the last hour. The stage runs in two
// halves - the night half with no officer in the room, so the robberies can
// happen at all, then the officer arrives after 04:00, where `ConsiderCrime`
// does not act at all and an arrest can only come from the hour it walked into.
TEST(Gangs, OfficerCatchesRobbersOfTheHourBefore) {
    Stage stage = MakeStage(7, 300, 100, /*wantOfficer=*/true);
    ASSERT_TRUE(stage.room.IsValid());
    ASSERT_GE(stage.crowd.size(), 2U);
    ASSERT_TRUE(stage.officer.has_value());
    const sim::EntityId gang = stage.state.gangs[0].id;
    for (std::size_t seat = 0; seat < stage.crowd.size(); seat += 2) {
        stage.state.robots[stage.crowd[seat]].gang = gang;
    }
    const std::set<std::uint64_t> members = IdsOf(stage.state, stage.crowd);
    const sim::EntityId officerId = stage.state.robots[*stage.officer].id;

    // 03:00 to 04:00 is the night half; everything after 04:00 is day.
    stage.state.tick = sim::StartOfDay(stage.state.tick) +
                       3 * sim::kMinutesPerHour;
    // The officer waits at its own station until the second half: standing in
    // the room from the start would turn every attempt there into an arrest.
    sim::Robot& watchman = stage.state.robots[*stage.officer];
    watchman.location = watchman.workspace;
    watchman.patrol = sim::EntityId{};

    auto started = sim::World::FromState(std::move(stage.state));
    ASSERT_TRUE(started.has_value()) << started.error();
    sim::World world = std::move(*started);
    world.RunFor(60);
    sim::WorldState mid = world.State();
    ASSERT_GT(mid.today.robberies, 0U);
    ASSERT_EQ(mid.today.arrests, 0U);

    // The officer arrives on patrol in the room the crowd is standing in, and
    // the robbers' memories keep their old rooms - their apartments - so a
    // rule that asked whether the crime happened in this room would arrest
    // nobody: the officer cannot see where a crime was, only who stands in
    // the room with it now.
    for (sim::Robot& robot : mid.robots) {
        if (robot.id == officerId) {
            robot.activity = sim::RobotActivity::Working;
            robot.location = stage.room;
            robot.patrol = stage.room;
            robot.nextActionAt = mid.tick + 100000;
            continue;
        }
        for (sim::Memory& memory : robot.memories) {
            if (memory.kind == sim::MemoryKind::RobbedSomeone) memory.room = robot.apartment;
        }
    }
    auto resumed = sim::World::FromState(std::move(mid));
    ASSERT_TRUE(resumed.has_value()) << resumed.error();
    sim::World patrolled = std::move(*resumed);
    std::vector<sim::SimEvent> log;
    Watch(log, patrolled);
    patrolled.RunFor(30);

    const sim::WorldState& after = patrolled.State();
    EXPECT_TRUE(sim::ValidateWorldState(after));
    EXPECT_TRUE(sim::ValidateEconomy(after).has_value());
    EXPECT_GT(after.today.arrests, 0U);
    // Nobody could rob in the second half: the room is watched, so an attempt
    // there becomes an arrest, and the crowd's members are its only members.
    EXPECT_EQ(after.today.robberies, mid.today.robberies);
    // The arrests are about the members who robbed in that room, and they
    // happen in the second half: `mid.today.arrests` was 0.
    std::size_t arrests = 0;
    for (const sim::SimEvent& event : log) {
        if (event.kind == sim::EventKind::Arrested) ++arrests;
    }
    EXPECT_EQ(arrests, after.today.arrests);
    EXPECT_GT(CountOf(log, sim::EventKind::Arrested, members), 0U);
    // B15: the chronicle line has a who and a whom: every arrest of the
    // second half names the officer that walked in (the only one there) as
    // `robot` and the arrested member as `otherRobot`, and the officer
    // remembers each of them.
    for (const sim::SimEvent& event : log) {
        if (event.kind != sim::EventKind::Arrested) continue;
        EXPECT_EQ(event.robot, officerId);
        EXPECT_TRUE(event.otherRobot.IsValid());
    }
    {
        const sim::Robot& watchman = after.robots[*stage.officer];
        EXPECT_TRUE(std::ranges::any_of(
            watchman.memories, [](const sim::Memory& memory) {
                return memory.kind == sim::MemoryKind::ArrestedSomeone;
            }));
    }
}

