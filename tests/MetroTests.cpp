#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Metro.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::World City() { return sim::World::Generate({.seed = 3, .robots = 2000}); }
constexpr double kNoon = 12.0 * 60.0;
}  // namespace

TEST(Metro, EveryStationIsOnItsRowAndColumnLine) {
    const sim::World world = City();
    const sim::Metro metro(world.State().city);
    ASSERT_FALSE(metro.Lines().empty());
    for (std::uint32_t station = 0; station < world.State().city.stations.size(); ++station) {
        for (const auto axis : {sim::LineAxis::Row, sim::LineAxis::Column}) {
            const auto line = metro.LineOf(station, axis);
            ASSERT_TRUE(line.has_value()) << "station " << station;
            EXPECT_EQ(metro.Lines()[*line].axis, axis);
            EXPECT_TRUE(metro.IndexOn(*line, station).has_value());
        }
    }
    for (const sim::MetroLine& line : metro.Lines()) {
        EXPECT_GE(line.stations.size(), 2U);
        EXPECT_EQ(line.meters.front(), 0.0);
        for (std::size_t index = 1; index < line.meters.size(); ++index) {
            EXPECT_GT(line.meters[index], line.meters[index - 1]);
        }
    }
}

TEST(Metro, TimetableRunsUpTurnsAndRunsDown) {
    const sim::Metro metro(City().State().city);
    for (const sim::MetroLine& line : metro.Lines()) {
        const std::size_t count = line.stations.size();
        for (std::size_t index = 0; index < count; ++index) {
            EXPECT_NEAR(line.upDeparts[index] - line.upArrives[index], sim::kDwellMinutes, 1e-9);
            EXPECT_NEAR(line.downDeparts[index] - line.downArrives[index], sim::kDwellMinutes, 1e-9);
            if (index > 0) {
                EXPECT_GT(line.upArrives[index], line.upDeparts[index - 1]);
                EXPECT_GT(line.downArrives[index - 1], line.downDeparts[index]);
            }
        }
        // Down starts after turning in the far tail; the round trip ends
        // after turning in the near one.
        EXPECT_GT(line.downArrives[count - 1], line.upDeparts[count - 1] + sim::kReverseMinutes);
        EXPECT_GT(line.roundTripMinutes, line.downDeparts[0] + sim::kReverseMinutes);
        EXPECT_LE(line.headwayMinutes, sim::kTargetHeadwayMinutes + 1e-9);
        EXPECT_NEAR(line.headwayMinutes * line.trains, line.roundTripMinutes, 1e-9);
    }
}

TEST(Metro, RidesWaitForTheNextTrainEveryHeadway) {
    const sim::Metro metro(City().State().city);
    const sim::MetroLine& line = metro.Lines().front();
    const std::uint32_t from = line.stations.front();
    const std::uint32_t to = line.stations.back();
    const auto first = metro.Ride(0, from, to, kNoon);
    ASSERT_TRUE(first.has_value());
    // Its doors open once the rider is there.
    EXPECT_GE(first->departs - sim::kDwellMinutes, kNoon - 1e-9);
    EXPECT_LT(first->departs - sim::kDwellMinutes, kNoon + line.headwayMinutes + 1e-9);
    EXPECT_EQ(first->direction, sim::Direction::Up);
    EXPECT_NEAR(first->arrives - first->departs,
                line.upArrives.back() - line.upDeparts.front(), 1e-9);
    const auto next = metro.Ride(0, from, to, first->departs + 1e-6);
    ASSERT_TRUE(next.has_value());
    EXPECT_NEAR(next->departs - first->departs, line.headwayMinutes, 1e-9);
    EXPECT_NE(next->train, first->train);
    // The other way runs down.
    const auto back = metro.Ride(0, to, from, kNoon);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->direction, sim::Direction::Down);
}

TEST(Metro, NoTrainsRunAtNight) {
    const sim::Metro metro(City().State().city);
    const sim::MetroLine& line = metro.Lines().front();
    // At 02:00 the next train is after 05:00.
    const auto ride = metro.Ride(0, line.stations.front(), line.stations.back(), 2.0 * 60.0);
    ASSERT_TRUE(ride.has_value());
    EXPECT_GE(ride->departs, static_cast<double>(sim::kServiceStartMinute));
    // At 03:00 every train is in its depot; at noon some run.
    std::size_t total = 0;
    for (const sim::MetroLine& each : metro.Lines()) {
        total += each.trains;
    }
    const auto night = metro.TrainsAt(3.0 * 60.0);
    EXPECT_EQ(night.size(), total);
    for (const sim::TrainState& train : night) {
        EXPECT_EQ(train.phase, sim::TrainState::Phase::InDepot);
    }
    std::size_t running = 0;
    for (const sim::TrainState& train : metro.TrainsAt(kNoon)) {
        running += train.phase == sim::TrainState::Phase::Running ? 1 : 0;
    }
    EXPECT_EQ(running, total);
    // Late at night trains still running were in service before 01:00.
    for (const sim::TrainState& train : metro.TrainsAt(24.0 * 60.0 + 30.0)) {
        EXPECT_NE(train.phase, sim::TrainState::Phase::LeavingDepot);
    }
}

TEST(Metro, ChangesLinesWhereTheyCross) {
    const sim::World world = City();
    const sim::Metro metro(world.State().city);
    // A station's row line and another row's column line cross elsewhere.
    const sim::MetroLine& row = metro.Lines().front();
    const std::uint32_t from = row.stations.front();
    std::uint32_t to = from;
    for (std::uint32_t station = 0; station < world.State().city.stations.size(); ++station) {
        if (metro.LineOf(station, sim::LineAxis::Row) != metro.LineOf(from, sim::LineAxis::Row) &&
            metro.LineOf(station, sim::LineAxis::Column) != metro.LineOf(from, sim::LineAxis::Column)) {
            to = station;
            break;
        }
    }
    ASSERT_NE(to, from);
    const auto legs = metro.Route(from, to, kNoon);
    ASSERT_EQ(legs.size(), 2U);
    EXPECT_EQ(legs[0].fromStation, from);
    EXPECT_EQ(legs[1].toStation, to);
    EXPECT_EQ(legs[0].toStation, legs[1].fromStation);
    EXPECT_GE(legs[1].departs - sim::kDwellMinutes, legs[0].arrives + sim::kTransferMinutes - 1e-9);
    // One line: one leg.
    const auto direct = metro.Route(from, row.stations.back(), kNoon);
    EXPECT_EQ(direct.size(), 1U);
}

TEST(Metro, TrainsStandAtThePlatformTheyLeaveFrom) {
    const sim::Metro metro(City().State().city);
    for (std::uint32_t lineIndex = 0; lineIndex < metro.Lines().size(); ++lineIndex) {
        const sim::MetroLine& line = metro.Lines()[lineIndex];
        for (const bool up : {true, false}) {
            const std::uint32_t from = up ? line.stations[0] : line.stations.back();
            const std::uint32_t to = up ? line.stations.back() : line.stations[0];
            const auto leg = metro.Ride(lineIndex, from, to, kNoon);
            ASSERT_TRUE(leg.has_value());
            bool found = false;
            for (const sim::TrainState& train : metro.TrainsAt(leg->departs - 0.1)) {
                if (train.line == lineIndex && train.train == leg->train) {
                    found = true;
                    EXPECT_EQ(train.phase, sim::TrainState::Phase::Running);
                    EXPECT_NEAR(train.meters, up ? 0.0 : line.meters.back(), 1e-6);
                    EXPECT_EQ(train.side, up ? -1.0 : 1.0);
                }
            }
            EXPECT_TRUE(found);
        }
    }
}

// A driver takes its own train at the line's first station and hands it on
// there, at the first stop from the handover, to the next driver ready then.
TEST(Metro, DriversHandTheirTrainOnAtTheSamePlatform) {
    const sim::Metro metro(City().State().city);
    const sim::MetroLine& line = metro.Lines().front();
    const double handover = kNoon + 60.0;
    const double latestOff = kNoon + (7.0 * 60.0);
    for (std::uint32_t train = 0; train < line.trains; ++train) {
        const auto run = metro.PlanDriverRun(0, train, kNoon, handover, latestOff);
        ASSERT_TRUE(run.has_value()) << train;
        EXPECT_EQ(run->train, train);
        EXPECT_GE(run->boards, kNoon);
        EXPECT_GT(run->alights, run->departs);
        EXPECT_GE(run->alights, handover);
        EXPECT_LT(run->alights, handover + line.roundTripMinutes);
        // The train stands at the first station's up platform then.
        const sim::TrainState at = metro.TrainAt(0, train, run->alights + 0.01);
        EXPECT_EQ(at.phase, sim::TrainState::Phase::Running);
        EXPECT_NEAR(at.meters, 0.0, 0.5);
        EXPECT_LT(at.side, 0.0);
        const auto next = metro.PlanDriverRun(0, train, handover, latestOff, latestOff + 60.0);
        ASSERT_TRUE(next.has_value()) << train;
        EXPECT_EQ(next->boards, run->alights) << "the next driver boards where this one gets off";
    }
    // No trains between 01:00 and 05:00: a driver ready at 02:00 cannot hand a
    // train on by 03:00.
    EXPECT_FALSE(metro.PlanDriverRun(0, 0, 2.0 * 60.0, 3.0 * 60.0, 3.0 * 60.0).has_value());
}

// The crew's shifts cover the whole service: whenever a train runs, from
// leaving its first platform in the morning to its last stop there at night,
// one driver or another of the crew of its rank is in its cab.
TEST(Metro, TheCrewsShiftsLeaveNoTrainWithoutADriver) {
    const sim::Metro metro(City().State().city);
    const sim::StaffRules* crew = sim::FindStaffRules(sim::RoomKind::CrewRoom);
    ASSERT_NE(crew, nullptr);
    constexpr double kDay = static_cast<double>(sim::kMinutesPerDay);
    std::vector<double> starts;
    for (const double day : {0.0, kDay, 2.0 * kDay}) {
        for (std::uint32_t slot = 0; slot < crew->shifts; ++slot) {
            starts.push_back(day + crew->shiftStarts.at(slot));
        }
    }
    std::ranges::sort(starts);
    for (std::uint32_t line = 0; line < metro.Lines().size(); ++line) {
        for (std::uint32_t train = 0; train < metro.Lines()[line].trains; ++train) {
            std::vector<sim::DriverRun> runs;
            for (std::size_t shift = 0; shift + 1 < starts.size(); ++shift) {
                const double start = starts[shift];
                const double end = start + crew->lengthMinutes;
                if (const auto run = metro.PlanDriverRun(
                        line, train, start + sim::kCrewWalkMinutes,
                        starts[shift + 1] + sim::kCrewWalkMinutes,
                        end - sim::kCrewWalkMinutes - sim::kDwellMinutes)) {
                    runs.push_back(*run);
                }
            }
            for (double time = kDay; time < 2.0 * kDay; time += 0.1) {
                const sim::TrainState state = metro.TrainAt(line, train, time);
                if (state.phase != sim::TrainState::Phase::Running) {
                    continue;
                }
                EXPECT_TRUE(std::ranges::any_of(runs, [&](const sim::DriverRun& run) {
                    return run.boards <= time && time <= run.alights + sim::kDwellMinutes;
                })) << "line " << line << " train " << train << " at minute " << time;
            }
        }
    }
}

// A driver changes ends while the train reverses, so the cab moves between
// them smoothly rather than flipping, and is at the front otherwise.
TEST(Metro, TheCabChangesEndsSmoothly) {
    const sim::Metro metro(City().State().city);
    const sim::MetroLine& line = metro.Lines().front();
    constexpr double kStep = 0.005;
    double last = metro.TrainAt(0, 0, kNoon).cab;
    bool between = false;
    for (double time = kNoon; time < kNoon + line.roundTripMinutes; time += kStep) {
        const sim::TrainState state = metro.TrainAt(0, 0, time);
        EXPECT_LE(std::abs(state.cab - last), (2.0 * kStep / sim::kReverseMinutes) + 1e-9) << time;
        if (std::abs(state.cab) < 0.99) {
            between = true;
        } else {
            EXPECT_EQ(state.cab > 0.0, state.towardEnd) << time;
        }
        last = state.cab;
    }
    EXPECT_TRUE(between);
}

// Doors slide open after a train stops at a platform, stand open, and are shut
// again before it leaves and while it runs.
TEST(Metro, DoorsOpenOnlyWhileATrainStandsAtAPlatform) {
    const sim::Metro metro(City().State().city);
    const sim::MetroLine& line = metro.Lines().front();
    const auto ride = metro.Ride(0, line.stations.front(), line.stations.back(), kNoon);
    ASSERT_TRUE(ride.has_value());
    const double stops = ride->departs - sim::kDwellMinutes;
    const auto doors = [&](double time) { return metro.TrainAt(0, ride->train, time).doors; };
    EXPECT_NEAR(doors(stops + (sim::kDwellMinutes * 0.5)), 1.0, 1e-9);
    EXPECT_NEAR(doors(stops + (sim::kDoorMinutes * 0.5)), 0.5, 1e-6);
    EXPECT_NEAR(doors(ride->departs - (sim::kDoorMinutes * 0.5)), 0.5, 1e-6);
    EXPECT_EQ(doors(ride->departs + 0.05), 0.0);
    EXPECT_EQ(metro.TrainAt(0, ride->train, ride->departs + 0.05).phase,
              sim::TrainState::Phase::Running);
}
