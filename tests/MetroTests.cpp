#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <set>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Metro.hpp"
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
