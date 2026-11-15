#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
std::string Serialize(const sim::World& world) {
    auto json = sim::SerializeWorld(world.State());
    EXPECT_TRUE(json.has_value());
    return json.value_or("");
}
}  // namespace

TEST(World, GeneratesPopulationAtHome) {
    const sim::World world = sim::World::Generate({.seed = 4, .robots = 60});
    ASSERT_EQ(world.State().robots.size(), 60U);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(robot.activity, sim::RobotActivity::AtHome);
        EXPECT_GT(robot.nextActionAt, 0U);
        EXPECT_LT(robot.nextActionAt, robot.shiftStartsAt);
    }
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

TEST(World, SameSeedRunsIdentically) {
    sim::World first = sim::World::Generate({.seed = 42, .robots = 80});
    sim::World second = sim::World::Generate({.seed = 42, .robots = 80});
    first.RunFor(2 * sim::kMinutesPerDay);
    second.RunFor(2 * sim::kMinutesPerDay);
    EXPECT_EQ(Serialize(first), Serialize(second));

    sim::World other = sim::World::Generate({.seed = 43, .robots = 80});
    other.RunFor(2 * sim::kMinutesPerDay);
    EXPECT_NE(Serialize(first), Serialize(other));
}

TEST(World, RobotsKeepTheirDailyRoutine) {
    sim::World world = sim::World::Generate({.seed = 8, .robots = 120});
    std::map<std::uint64_t, std::vector<sim::EventKind>> history;
    std::uint64_t lateArrivals = 0;
    std::uint64_t wrongShiftLengths = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        history[event.robot.value].push_back(event.kind);
        if (event.kind == sim::EventKind::ArrivedAtWork && event.minutes > 0) {
            ++lateArrivals;
        }
        if (event.kind == sim::EventKind::LeftWork) {
            const sim::Robot* robot = world.FindRobot(event.robot);
            const sim::Building* work =
                world.State().city.FindBuilding(robot->workplace);
            if (event.tick !=
                robot->shiftStartsAt + work->shift.lengthMinutes) {
                ++wrongShiftLengths;
            }
        }
    });
    world.RunFor(3 * sim::kMinutesPerDay);

    EXPECT_EQ(lateArrivals, 0U);
    EXPECT_EQ(wrongShiftLengths, 0U);
    ASSERT_EQ(history.size(), 120U);
    const std::vector<sim::EventKind> cycle{
        sim::EventKind::LeftHome, sim::EventKind::ArrivedAtWork,
        sim::EventKind::LeftWork, sim::EventKind::ArrivedHome};
    for (const auto& [robot, events] : history) {
        // At least two complete workdays in three days, whatever the shift.
        EXPECT_GE(events.size(), 8U) << "robot " << robot;
        for (std::size_t index = 0; index < events.size(); ++index) {
            EXPECT_EQ(events[index], cycle[index % cycle.size()])
                << "robot " << robot << " event " << index;
        }
    }
}

TEST(World, FromStateContinuesIdentically) {
    sim::World original = sim::World::Generate({.seed = 6, .robots = 50});
    original.RunFor(sim::kMinutesPerDay + 333);
    auto copy = sim::World::FromState(original.State());
    ASSERT_TRUE(copy.has_value()) << copy.error();
    original.RunFor(sim::kMinutesPerDay);
    copy->RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(Serialize(original), Serialize(*copy));
}

TEST(World, ValidationRejectsBrokenReferences) {
    const sim::World world = sim::World::Generate({.seed = 2, .robots = 30});

    sim::WorldState missingHome = world.State();
    missingHome.robots.front().home = sim::EntityId{999999};
    auto result = sim::World::FromState(missingHome);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("home"), std::string::npos);

    sim::WorldState workAtHome = world.State();
    workAtHome.robots.front().workplace = workAtHome.robots.front().home;
    EXPECT_FALSE(sim::World::FromState(workAtHome).has_value());

    sim::WorldState unordered = world.State();
    std::swap(unordered.robots[0], unordered.robots[1]);
    EXPECT_FALSE(sim::World::FromState(unordered).has_value());

    sim::WorldState badStreet = world.State();
    badStreet.city.edges.front().to = sim::NodeId{100000};
    EXPECT_FALSE(sim::World::FromState(badStreet).has_value());

    sim::WorldState overfull = world.State();
    overfull.city.buildings.front().capacity = 0;
    EXPECT_FALSE(sim::World::FromState(overfull).has_value());
}

TEST(World, CommuteMatchesTheLastDeparture) {
    sim::World world = sim::World::Generate({.seed = 8, .robots = 120});
    std::map<std::uint64_t, sim::Tick> lastDeparture;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::LeftHome ||
            event.kind == sim::EventKind::LeftWork) {
            lastDeparture[event.robot.value] = event.tick;
        }
    });
    std::size_t commuting = 0;
    // Check after every minute of a whole day to catch robots mid-walk.
    for (sim::Tick minute = 0; minute < sim::kMinutesPerDay; ++minute) {
        world.Step();
        for (const sim::Robot& robot : world.State().robots) {
            const auto commute = world.CommuteOf(robot);
            const bool walking =
                robot.activity == sim::RobotActivity::CommutingToWork ||
                robot.activity == sim::RobotActivity::CommutingHome;
            ASSERT_EQ(commute.has_value(), walking);
            if (!commute) {
                continue;
            }
            ++commuting;
            EXPECT_EQ(commute->departedAt, lastDeparture[robot.id.value]);
            EXPECT_EQ(commute->arrivesAt, robot.nextActionAt);
            EXPECT_GT(commute->arrivesAt, world.CurrentTick() - 1);
            const bool toWork =
                robot.activity == sim::RobotActivity::CommutingToWork;
            EXPECT_EQ(commute->from, toWork ? robot.home : robot.workplace);
            EXPECT_EQ(commute->to, toWork ? robot.workplace : robot.home);
        }
    }
    EXPECT_GT(commuting, 0U);
}

TEST(World, DescribesEvents) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 5});
    std::vector<std::string> lines;
    world.SetEventSink([&](const sim::SimEvent& event) {
        lines.push_back(world.Describe(event));
    });
    world.RunFor(sim::kMinutesPerDay);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.front().rfind("Day 1 ", 0), 0U) << lines.front();
    EXPECT_NE(lines.front().find(" left Hab Block "), std::string::npos)
        << lines.front();
}
