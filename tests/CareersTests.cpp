#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}

// Index of the first robot matching `wanted`.
std::optional<std::size_t> FindIndex(const sim::WorldState& state,
                                     const std::function<bool(const sim::Robot&)>& wanted) {
    for (std::size_t index = 0; index < state.robots.size(); ++index) {
        if (wanted(state.robots[index])) {
            return index;
        }
    }
    return std::nullopt;
}

bool Remembers(const sim::Robot& robot, sim::MemoryKind kind) {
    for (const sim::Memory& memory : robot.memories) {
        if (memory.kind == kind) {
            return true;
        }
    }
    return false;
}

std::uint32_t RentOf(const sim::World& world, sim::EntityId apartment) {
    const auto room = world.Rooms().Find(apartment);
    return room ? room->room->rent : 0;
}

// A city a day and a half in, with robots at work.
sim::World WorkingCity() {
    sim::World world = sim::World::Generate({.seed = 7, .robots = 200});
    world.RunFor(sim::kMinutesPerDay + (10 * 60));  // Day 2, 10:00.
    return world;
}
}  // namespace

TEST(Careers, LevelsPayMoreAndTakeLongerToClimb) {
    EXPECT_EQ(sim::WageAtLevel(100, 0), 100U);
    EXPECT_EQ(sim::WageAtLevel(100, 1), 115U);
    EXPECT_EQ(sim::WageAtLevel(100, 3), 145U);
    EXPECT_LT(sim::PromotionTarget(0), sim::PromotionTarget(1));
    EXPECT_EQ(sim::JobTitle(0), "junior");
    EXPECT_EQ(sim::JobTitle(sim::kMaxJobLevel), "lead");
}

TEST(Careers, WagesFollowTheJobLevel) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 120});
    std::uint64_t shifts = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::ShiftEnded) {
            return;
        }
        const sim::Robot* robot = world.FindRobot(event.robot);
        const auto room = world.Rooms().Find(event.room);
        ASSERT_TRUE(robot != nullptr && room.has_value());
        EXPECT_EQ(event.credits,
                  sim::WageAtLevel(room->building->shift.wage, robot->jobLevel));
        ++shifts;
    });
    world.RunFor(3 * sim::kMinutesPerDay);
    EXPECT_GT(shifts, 200U);
}

// A robot that has done well enough is promoted at the end of its shift.
TEST(Careers, GoodWorkIsPromoted) {
    const sim::World city = WorkingCity();
    sim::WorldState state = city.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Working &&
               robot.jobLevel < sim::kMaxJobLevel;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const std::uint8_t level = robot.jobLevel;
    robot.performance = sim::PromotionTarget(level);
    robot.needs = {.charge = 1000, .condition = 1000, .social = 1000, .fun = 1000};
    const sim::Tick shiftEnd = robot.nextActionAt;
    const sim::EntityId id = robot.id;

    sim::World world = Load(std::move(state));
    world.RunFor(shiftEnd - world.CurrentTick() + 1);
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_EQ(after.jobLevel, level + 1);
    EXPECT_EQ(after.performance, 0);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::Promoted));
}

// A worn-down robot on its last chance loses its job, lives off work for a
// while, and is hired again; it never works while out of work.
TEST(Careers, WornDownWorkersAreFiredAndHiredAgain) {
    const sim::World city = WorkingCity();
    sim::WorldState state = city.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Working;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    robot.performance = sim::kFiringPerformance + 1;
    robot.needs.condition = 0;
    const sim::Tick shiftEnd = robot.nextActionAt;
    const sim::EntityId id = robot.id;

    sim::World world = Load(std::move(state));
    bool fired = false;
    bool hired = false;
    bool workedWhileOut = false;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.robot != id) {
            return;
        }
        if (event.kind == sim::EventKind::Remembered) {
            fired = fired || event.memory == sim::MemoryKind::LostJob;
            hired = hired || (fired && event.memory == sim::MemoryKind::FoundJob);
        }
        if (event.kind == sim::EventKind::ClockedIn && fired && !hired) {
            workedWhileOut = true;
        }
    });
    world.RunFor(shiftEnd - world.CurrentTick() + 1);
    ASSERT_TRUE(fired);
    {
        const sim::Robot& out = *world.FindRobot(id);
        EXPECT_FALSE(out.workspace.IsValid());
        EXPECT_EQ(out.commuteAt, sim::kNoCommute);
        EXPECT_NE(out.activity, sim::RobotActivity::Working);
    }
    for (int day = 0; day < 10 && !hired; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    EXPECT_TRUE(hired);
    EXPECT_FALSE(workedWhileOut);
    const sim::Robot& rehired = *world.FindRobot(id);
    EXPECT_TRUE(rehired.workspace.IsValid());
    EXPECT_EQ(rehired.jobLevel, 0);
}

// Owing too much rent gets a robot evicted to a free apartment that costs no
// more; the debt is written off and nobody shares a flat.
TEST(Careers, DebtorsAreEvicted) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Resting;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const sim::EntityId id = robot.id;
    const sim::EntityId home = robot.apartment;
    robot.credits = 0;
    robot.rentOwed = 10 * static_cast<std::int64_t>(RentOf(start, home));

    sim::World world = Load(std::move(state));
    world.RunFor(90);  // Past midnight and the walk to the new place.
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_NE(after.apartment, home);
    EXPECT_LE(RentOf(world, after.apartment), RentOf(world, home));
    EXPECT_EQ(after.rentOwed, 0);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::Evicted));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// A robot with plenty saved moves to a pricier apartment before long.
TEST(Careers, SaversMoveUp) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay);
    sim::WorldState state = start.State();
    // The robot at home with the cheapest apartment has the most to gain.
    std::optional<std::size_t> index;
    for (std::size_t candidate = 0; candidate < state.robots.size(); ++candidate) {
        const sim::Robot& robot = state.robots[candidate];
        if (robot.activity == sim::RobotActivity::Resting &&
            (!index || RentOf(start, robot.apartment) <
                           RentOf(start, state.robots[*index].apartment))) {
            index = candidate;
        }
    }
    ASSERT_TRUE(index);
    // The only robot with savings, so nobody takes the nicer flats first.
    for (sim::Robot& other : state.robots) {
        other.credits = std::min<std::int64_t>(other.credits, 50);
    }
    sim::Robot& robot = state.robots[*index];
    robot.credits = 5000;
    const sim::EntityId id = robot.id;
    const sim::EntityId home = robot.apartment;

    sim::World world = Load(std::move(state));
    for (int day = 0; day < 40 && world.FindRobot(id)->apartment == home; ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_GT(RentOf(world, after.apartment), RentOf(world, home));
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::MovedUp));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Over two months some robots are promoted, and the population keeps its
// jobs: nearly everyone who loses one finds another.
TEST(Careers, CareersMoveOverMonths) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    std::uint64_t promotions = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        promotions += event.kind == sim::EventKind::Remembered &&
                              event.memory == sim::MemoryKind::Promoted
                          ? 1
                          : 0;
    });
    world.RunFor(56 * sim::kMinutesPerDay);
    std::uint32_t outOfWork = 0;
    for (const sim::Robot& robot : world.State().robots) {
        outOfWork += robot.workspace.IsValid() ? 0 : 1;
    }
    EXPECT_GT(promotions, 30U);
    EXPECT_LT(outOfWork, 300U / 20);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
