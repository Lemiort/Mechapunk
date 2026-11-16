#include <gtest/gtest.h>

#include <bit>
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
    world.RunFor(7 * sim::kMinutesPerDay);
    EXPECT_EQ(late, 0U);
    EXPECT_EQ(wrongLengths, 0U);
    ASSERT_EQ(shifts.size(), 150U);
    for (const auto& [robot, count] : shifts) {
        // Five working days a week; the last night shift may still be
        // running when the week ends.
        EXPECT_GE(count, 4U) << "robot " << robot;
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

TEST(World, CreditsAreWagesMinusPricesAndRent) {
    sim::World world = sim::World::Generate({.seed = 11, .robots = 100});
    std::map<std::uint64_t, std::int64_t> expected;
    std::map<std::uint64_t, std::int64_t> rentDue;
    for (const sim::Robot& robot : world.State().robots) {
        expected[robot.id.value] = robot.credits;
        rentDue[robot.id.value] =
            world.Rooms().Find(robot.apartment)->room->rent;
    }
    std::map<std::uint64_t, std::int64_t> rentPaid;
    std::uint64_t nights = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::ShiftEnded) {
            expected[event.robot.value] += event.credits;
        } else if (event.kind == sim::EventKind::ArrivedAtVenue) {
            expected[event.robot.value] -= event.credits;
        } else if (event.kind == sim::EventKind::RentPaid) {
            EXPECT_EQ(sim::MinuteOfDay(event.tick), 0U);
            expected[event.robot.value] -= event.credits;
            rentPaid[event.robot.value] += event.credits;
        }
    });
    for (sim::Tick day = 0; day < 6; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        ++nights;
    }
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(robot.credits, expected[robot.id.value]) << robot.name;
        EXPECT_GE(robot.credits, 0) << robot.name;
        // Every night's rent is either paid or still owed.
        EXPECT_EQ(rentPaid[robot.id.value] + robot.rentOwed,
                  rentDue[robot.id.value] * static_cast<std::int64_t>(nights))
            << robot.name;
    }
}

TEST(World, RobotsKeepABudgetForRent) {
    sim::World world = sim::World::Generate({.seed = 14, .robots = 150});
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::ArrivedAtVenue) {
            return;
        }
        // The price is already paid on arrival; what is left must cover
        // tonight's rent and anything owed.
        const sim::Robot* robot = world.FindRobot(event.robot);
        const std::int64_t rent =
            world.Rooms().Find(robot->apartment)->room->rent;
        EXPECT_GE(robot->credits, rent + robot->rentOwed) << robot->name;
    });
    world.RunFor(7 * sim::kMinutesPerDay);
}

TEST(World, NobodyWorksOnTheirDaysOff) {
    sim::World world = sim::World::Generate({.seed = 15, .robots = 150});
    std::map<std::uint64_t, std::uint32_t> shifts;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::ClockedIn) {
            return;
        }
        const sim::Robot* robot = world.FindRobot(event.robot);
        const std::uint32_t day = sim::DayOfWeek(robot->shiftStartsAt);
        EXPECT_EQ((robot->daysOff >> day) & 1U, 0U)
            << robot->name << " worked on " << sim::WeekdayName(day);
        ++shifts[event.robot.value];
    });
    world.RunFor(14 * sim::kMinutesPerDay);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(std::popcount(static_cast<unsigned>(robot.daysOff)), 2)
            << robot.name;
        // Five working days a week, give or take the first and last shift.
        EXPECT_GE(shifts[robot.id.value], 9U) << robot.name;
        EXPECT_LE(shifts[robot.id.value], 11U) << robot.name;
    }
}

TEST(World, DaysOffAreSpentOutAndNightsAtHome) {
    sim::World world = sim::World::Generate({.seed = 16, .robots = 150});
    std::uint64_t dayOffVisits = 0;
    std::map<std::uint64_t, bool> out;
    world.SetEventSink([&](const sim::SimEvent& event) {
        const sim::Robot* robot = world.FindRobot(event.robot);
        if (event.kind == sim::EventKind::ArrivedAtVenue) {
            out[event.robot.value] = true;
            const std::uint32_t minute = sim::MinuteOfDay(event.tick);
            const bool dayOff =
                ((robot->daysOff >> sim::DayOfWeek(event.tick)) & 1U) != 0;
            if (dayOff && minute >= 9 * 60 && minute < 18 * 60) {
                ++dayOffVisits;
            }
        } else if (event.kind == sim::EventKind::ArrivedHome) {
            out[event.robot.value] = false;
        }
    });
    for (sim::Tick minute = 0; minute < 14 * sim::kMinutesPerDay; ++minute) {
        world.Step();
        // By 04:00 everyone who went out has come home.
        if (sim::MinuteOfDay(world.CurrentTick()) == 4 * 60) {
            for (const sim::Robot& robot : world.State().robots) {
                EXPECT_NE(robot.activity, sim::RobotActivity::Visiting)
                    << robot.name << " at "
                    << sim::FormatTime(world.CurrentTick());
            }
        }
    }
    EXPECT_GT(dayOffVisits, 300U);
}

// A robot home from work early in the evening may still go out: after a
// break it weighs its needs again instead of waiting for the next shift.
TEST(World, RobotsGoOutAgainAfterGettingHomeFromWork) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 200});
    // Robots home on a working day before 21:00, by id.
    std::map<std::uint64_t, sim::Tick> homeEarly;
    std::uint64_t outAgain = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        const sim::Robot* robot = world.FindRobot(event.robot);
        const bool workday =
            ((robot->daysOff >> sim::DayOfWeek(event.tick)) & 1U) == 0;
        const std::uint32_t minute = sim::MinuteOfDay(event.tick);
        if (event.kind == sim::EventKind::ArrivedHome) {
            if (workday && minute >= 12 * 60 && minute < 21 * 60) {
                homeEarly[event.robot.value] = event.tick;
            } else {
                homeEarly.erase(event.robot.value);
            }
        } else if (event.kind == sim::EventKind::Departed &&
                   event.otherRoom != robot->workspace) {
            const auto found = homeEarly.find(event.robot.value);
            if (found != homeEarly.end() &&
                sim::StartOfDay(found->second) == sim::StartOfDay(event.tick)) {
                ++outAgain;
            }
            homeEarly.erase(event.robot.value);
        }
    });
    world.RunFor(7 * sim::kMinutesPerDay);
    EXPECT_GT(outAgain, 100U);
}

// Venues and rest keep needs up: after two weeks only a few robots have run
// out of anything.
TEST(World, FewRobotsRunOutOfAnyNeed) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    world.RunFor((14 * sim::kMinutesPerDay) + (20 * 60));  // 20:00.
    std::uint32_t empty = 0;
    for (const sim::Robot& robot : world.State().robots) {
        const sim::Needs& needs = robot.needs;
        empty += needs.charge == 0 || needs.condition == 0 ||
                         needs.social == 0 || needs.fun == 0
                     ? 1
                     : 0;
    }
    EXPECT_LT(empty, 300U / 10) << empty << " robots have a need at zero";
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
