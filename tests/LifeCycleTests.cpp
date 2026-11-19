#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}

bool Remembers(const sim::Robot& robot, sim::MemoryKind kind,
               sim::EntityId other = {}) {
    return std::ranges::any_of(robot.memories, [&](const sim::Memory& memory) {
        return memory.kind == kind && (!other.IsValid() || memory.robot == other);
    });
}
}  // namespace

TEST(LifeCycle, WearGrowsWithAgeAndCapsCondition) {
    EXPECT_EQ(sim::WearAtAge(0), 0);
    EXPECT_LT(sim::WearAtAge(100), sim::WearAtAge(200));
    // Older robots wear faster.
    EXPECT_LT(sim::WearAtAge(100) - sim::WearAtAge(50),
              sim::WearAtAge(350) - sim::WearAtAge(300));
    EXPECT_EQ(sim::WearAtAge(5000), sim::kMaxWear);

    sim::World world = sim::World::Generate({.seed = 4, .robots = 120});
    world.RunFor(3 * sim::kMinutesPerDay);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_LE(robot.needs.condition, sim::kMaxWear - robot.wear) << robot.name;
    }
}

// A robot staying at a clinic loses wear while it is there.
TEST(LifeCycle, ClinicsTakeWearAway) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    world.RunFor(sim::kMinutesPerDay + (8 * 60));  // Day 2, 08:00.
    std::optional<std::size_t> patient;
    for (int search = 0; search < 24 && !patient; ++search) {
        world.RunFor(30);
        const auto& robots = world.State().robots;
        for (std::size_t index = 0; index < robots.size(); ++index) {
            const auto room = world.Rooms().Find(robots[index].location);
            if (robots[index].activity == sim::RobotActivity::Visiting &&
                room && room->room->kind == sim::RoomKind::Clinic) {
                patient = index;
                break;
            }
        }
    }
    ASSERT_TRUE(patient) << "nobody visited a clinic";
    sim::WorldState state = world.State();
    sim::Robot& robot = state.robots[*patient];
    robot.wear = 500;
    robot.nextActionAt = state.tick + 120;  // Stays two more hours.
    const sim::EntityId id = robot.id;
    sim::World clinic = Load(std::move(state));
    clinic.RunFor(60);
    EXPECT_LT(clinic.FindRobot(id)->wear, 500);
}

// A robot worn out completely is scrapped at night: it leaves the city,
// its friends remember losing it, and a newcomer moves into the city.
TEST(LifeCycle, WornOutRobotsAreScrappedAndReplaced) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 150});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    sim::Robot& doomed = state.robots[10];
    sim::Robot& mourner = state.robots[20];
    doomed.wear = sim::kMaxWear - 1;
    mourner.bonds = {{.robot = doomed.id,
                      .affinity = 50,
                      .level = sim::BondLevel::Friend}};
    const sim::EntityId doomedId = doomed.id;
    const std::string doomedName = doomed.name;
    const sim::EntityId mournerId = mourner.id;
    const std::uint64_t lastId = state.robots.back().id.value;
    const std::size_t population = state.robots.size();

    sim::World world = Load(std::move(state));
    std::uint64_t scrappedEvents = 0;
    std::string farewell;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::Scrapped && event.robot == doomedId) {
            ++scrappedEvents;
        }
        if (event.kind == sim::EventKind::Remembered &&
            event.memory == sim::MemoryKind::LostFriend && event.robot == mournerId) {
            farewell = world.Describe(event);
        }
    });
    world.RunFor(90);  // Past midnight.

    EXPECT_EQ(scrappedEvents, 1U);
    EXPECT_EQ(world.FindRobot(doomedId), nullptr);
    EXPECT_EQ(world.NameOf(doomedId), doomedName);
    const sim::Robot& friendLeft = *world.FindRobot(mournerId);
    EXPECT_TRUE(Remembers(friendLeft, sim::MemoryKind::LostFriend, doomedId));
    EXPECT_TRUE(std::ranges::none_of(friendLeft.bonds, [&](const sim::Bond& bond) {
        return bond.robot == doomedId;
    }));
    EXPECT_NE(farewell.find(doomedName), std::string::npos) << farewell;

    // Same population, with a newcomer at the end.
    ASSERT_EQ(world.State().robots.size(), population);
    const sim::Robot& newcomer = world.State().robots.back();
    EXPECT_GT(newcomer.id.value, lastId);
    EXPECT_TRUE(Remembers(newcomer, sim::MemoryKind::ArrivedInCity));
    EXPECT_LT(newcomer.wear, sim::kMaxWear / 2);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Over months robots wear out and newcomers replace them; the city keeps
// its size, black markets now and then sell a dodgy part, and only scrapped
// robots someone remembers are kept.
TEST(LifeCycle, TheCityRenewsItself) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    std::uint64_t scrapped = 0;
    std::uint64_t arrived = 0;
    std::uint64_t badParts = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        scrapped += event.kind == sim::EventKind::Scrapped ? 1 : 0;
        arrived += event.kind == sim::EventKind::Arrived ? 1 : 0;
        badParts += event.kind == sim::EventKind::Remembered &&
                            event.memory == sim::MemoryKind::GotBadPart
                        ? 1
                        : 0;
    });
    for (int month = 0; month < 3; ++month) {
        world.RunFor(30 * sim::kMinutesPerDay);
        EXPECT_EQ(world.State().robots.size(), 300U);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    // About 20 in this city; the exact count moves with the random stream.
    EXPECT_GT(scrapped, 12U);
    EXPECT_EQ(arrived, scrapped);
    EXPECT_GT(badParts, 0U);

    std::set<std::uint64_t> remembered;
    for (const sim::Robot& robot : world.State().robots) {
        for (const sim::Memory& memory : robot.memories) {
            remembered.insert(memory.robot.value);
        }
    }
    for (const sim::DepartedRobot& departed : world.State().departed) {
        EXPECT_TRUE(remembered.contains(departed.id.value)) << departed.name;
    }
}
