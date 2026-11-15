#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
std::string Serialize(const sim::World& world) {
    auto json = sim::SerializeWorld(world.State());
    EXPECT_TRUE(json.has_value());
    return json.value_or("");
}

sim::World Copy(const sim::World& world) {
    auto copy = sim::World::FromState(world.State());
    EXPECT_TRUE(copy.has_value()) << copy.error();
    return std::move(*copy);
}
}  // namespace

TEST(World, GeneratesPopulationAtHome) {
    const sim::World world = sim::World::Generate({.seed = 4, .robots = 60});
    ASSERT_EQ(world.State().robots.size(), 60U);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(robot.activity, sim::RobotActivity::Resting);
        EXPECT_EQ(robot.location, robot.apartment);
        EXPECT_GT(robot.nextActionAt, 0U);
        EXPECT_LT(robot.nextActionAt, robot.shiftStartsAt);
        EXPECT_GT(robot.credits, 0);
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

TEST(World, RobotsWorkEveryDayOnTime) {
    sim::World world = sim::World::Generate({.seed = 8, .robots = 150});
    std::map<std::uint64_t, std::uint32_t> shifts;
    std::uint64_t late = 0;
    std::uint64_t wrongLengths = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::ClockedIn) {
            late += event.minutes > 0 ? 1 : 0;
        }
        if (event.kind == sim::EventKind::ShiftEnded) {
            ++shifts[event.robot.value];
            const sim::Robot* robot = world.FindRobot(event.robot);
            const auto workspace = world.Rooms().Find(robot->workspace);
            if (event.tick != robot->shiftStartsAt +
                                  workspace->building->shift.lengthMinutes) {
                ++wrongLengths;
            }
        }
    });
    world.RunFor(4 * sim::kMinutesPerDay);
    EXPECT_EQ(late, 0U);
    EXPECT_EQ(wrongLengths, 0U);
    ASSERT_EQ(shifts.size(), 150U);
    for (const auto& [robot, count] : shifts) {
        // Three full shifts in four days, whatever the shift hours.
        EXPECT_GE(count, 3U) << "robot " << robot;
    }
}

TEST(World, RobotsRestBeforeEveryShift) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 150});
    std::map<std::uint64_t, sim::Tick> cameHome;
    std::uint64_t checked = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::ArrivedHome) {
            cameHome[event.robot.value] = event.tick;
        }
        const sim::Robot* robot = world.FindRobot(event.robot);
        if (event.kind == sim::EventKind::Departed &&
            event.otherRoom == robot->workspace &&
            cameHome.contains(event.robot.value)) {
            ++checked;
            EXPECT_GE(event.tick - cameHome[event.robot.value],
                      sim::World::kRestMinutes)
                << robot->name << " at " << sim::FormatTime(event.tick);
        }
    });
    world.RunFor(5 * sim::kMinutesPerDay);
    EXPECT_GT(checked, 300U);
}

TEST(World, EveningsAreSpentInOpenVenuesWithinCapacity) {
    sim::World world = sim::World::Generate({.seed = 10, .robots = 300});
    std::map<sim::RoomKind, std::uint64_t> visits;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::ArrivedAtVenue) {
            return;
        }
        const auto venue = world.Rooms().Find(event.room);
        ASSERT_TRUE(venue.has_value());
        const sim::VenueRules* rules = sim::FindVenueRules(venue->room->kind);
        ASSERT_NE(rules, nullptr);
        EXPECT_TRUE(sim::IsOpen(*rules, sim::MinuteOfDay(event.tick)))
            << world.Describe(event);
        EXPECT_EQ(event.credits, rules->price);
        ++visits[venue->room->kind];
    });
    for (sim::Tick minute = 0; minute < 3 * sim::kMinutesPerDay; ++minute) {
        world.Step();
        for (const sim::RoomKind kind : sim::VenueKinds()) {
            for (const sim::RoomRef& venue : world.Rooms().RoomsOfKind(kind)) {
                ASSERT_LE(world.GuestsIn(venue.room->id), venue.room->capacity)
                    << venue.room->name;
            }
        }
    }
    // Robots use most kinds of venue, not just one.
    EXPECT_GE(visits.size(), 4U);
}

TEST(World, CreditsAreWagesMinusPrices) {
    sim::World world = sim::World::Generate({.seed = 11, .robots = 100});
    std::map<std::uint64_t, std::int64_t> expected;
    for (const sim::Robot& robot : world.State().robots) {
        expected[robot.id.value] = robot.credits;
    }
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::ShiftEnded) {
            expected[event.robot.value] += event.credits;
        } else if (event.kind == sim::EventKind::ArrivedAtVenue) {
            expected[event.robot.value] -= event.credits;
        }
    });
    world.RunFor(3 * sim::kMinutesPerDay);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(robot.credits, expected[robot.id.value]) << robot.name;
        EXPECT_GE(robot.credits, 0) << robot.name;
    }
}

TEST(World, NeedsStayInRangeOverWeeks) {
    sim::World world = sim::World::Generate({.seed = 12, .robots = 200});
    world.RunFor(21 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    std::int64_t social = 0;
    for (const sim::Robot& robot : world.State().robots) {
        social += robot.needs.social;
    }
    // Evenings out keep the population from drifting to zero.
    EXPECT_GT(social / 200, 200);
}

TEST(World, WalkMatchesTheLastDeparture) {
    sim::World world = sim::World::Generate({.seed = 8, .robots = 120});
    std::map<std::uint64_t, sim::SimEvent> lastDeparture;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::Departed) {
            lastDeparture[event.robot.value] = event;
        }
    });
    std::size_t walking = 0;
    for (sim::Tick minute = 0; minute < sim::kMinutesPerDay; ++minute) {
        world.Step();
        for (const sim::Robot& robot : world.State().robots) {
            const auto walk = sim::World::WalkOf(robot);
            ASSERT_EQ(walk.has_value(),
                      robot.activity == sim::RobotActivity::Walking);
            if (!walk) {
                continue;
            }
            ++walking;
            const sim::SimEvent& departure = lastDeparture[robot.id.value];
            EXPECT_EQ(walk->departedAt, departure.tick);
            EXPECT_EQ(walk->from, departure.room);
            EXPECT_EQ(walk->to, departure.otherRoom);
            EXPECT_EQ(walk->arrivesAt - walk->departedAt, departure.minutes);
        }
    }
    EXPECT_GT(walking, 0U);
}

TEST(World, FromStateContinuesIdentically) {
    sim::World original = sim::World::Generate({.seed = 6, .robots = 50});
    original.RunFor(sim::kMinutesPerDay + 1150);  // Mid-evening.
    sim::World copy = Copy(original);
    original.RunFor(sim::kMinutesPerDay);
    copy.RunFor(sim::kMinutesPerDay);
    EXPECT_EQ(Serialize(original), Serialize(copy));
}

TEST(World, ValidationRejectsInconsistentStates) {
    const sim::World world = sim::World::Generate({.seed = 2, .robots = 30});
    const auto rejects = [](sim::WorldState state, const char* expected) {
        auto result = sim::World::FromState(std::move(state));
        ASSERT_FALSE(result.has_value()) << expected;
        EXPECT_NE(result.error().find(expected), std::string::npos)
            << result.error();
    };

    sim::WorldState missingHome = world.State();
    missingHome.robots.front().apartment = sim::EntityId{999999};
    rejects(missingHome, "apartment");

    sim::WorldState workAtHome = world.State();
    workAtHome.robots.front().workspace = workAtHome.robots.front().apartment;
    rejects(workAtHome, "workspace");

    sim::WorldState sharedFlat = world.State();
    sharedFlat.robots[1].apartment = sharedFlat.robots[0].apartment;
    sharedFlat.robots[1].location = sharedFlat.robots[0].apartment;
    rejects(sharedFlat, "capacity");

    sim::WorldState wrongPlace = world.State();
    wrongPlace.robots.front().location = wrongPlace.robots.front().workspace;
    rejects(wrongPlace, "match");

    sim::WorldState unordered = world.State();
    std::swap(unordered.robots[0], unordered.robots[1]);
    rejects(unordered, "id");

    sim::WorldState badStreet = world.State();
    badStreet.city.edges.front().to = sim::NodeId{100000};
    rejects(badStreet, "street");

    sim::WorldState badNeed = world.State();
    badNeed.robots.front().needs.fun = 1001;
    rejects(badNeed, "range");

    sim::WorldState floating = world.State();
    floating.city.buildings.front().rooms.back().floor = 999;
    rejects(floating, "floor");
}

TEST(World, DescribesEventsWithRoomsAndBuildings) {
    sim::World world = sim::World::Generate({.seed = 1, .robots = 40});
    std::vector<std::string> lines;
    world.SetEventSink([&](const sim::SimEvent& event) {
        lines.push_back(world.Describe(event));
    });
    world.RunFor(sim::kMinutesPerDay);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.front().rfind("Day 1 ", 0), 0U) << lines.front();
    EXPECT_NE(lines.front().find(" left Apartment "), std::string::npos)
        << lines.front();
    EXPECT_NE(lines.front().find(", Hab Block "), std::string::npos)
        << lines.front();
}
