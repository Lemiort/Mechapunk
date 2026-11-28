#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"

namespace mechapunk::sim {

// The elevated metro. Every row and every column of stations with at least
// two stations is a line. A line has two tracks, one each way, and a tail
// track beyond each end: a train runs into the tail, the driver changes
// ends, and it crosses over to the other track. Each line has a depot
// beyond its first station; trains leave it from kServiceStartMinute, run
// whole round trips, and go back once a round trip would start after
// kServiceEndMinute (01:00 the next day), standing kDwellMinutes at the first
// platform a last time to let everyone off.
//
// The timetable is fixed by the city alone, so it is never saved: a train
// about every kTargetHeadwayMinutes each way, evenly spaced. Times are game
// minutes since Day 1 00:00, as fractions, for smooth drawing; the
// simulation rounds them up to whole ticks.

inline constexpr double kTrainMetersPerMinute = 500.0;  // 30 km/h
inline constexpr double kDwellMinutes = 0.5;       // At each platform.
inline constexpr double kReverseMinutes = 1.0;     // In a tail track.
inline constexpr double kTailMeters = 40.0;        // Beyond an end station.
inline constexpr double kDepotMinutes = 1.5;       // Depot to first platform.
inline constexpr double kDoorMinutes = 0.05;       // Doors sliding open or shut.
inline constexpr double kTargetHeadwayMinutes = 4.0;
inline constexpr std::uint32_t kServiceStartMinute = 5 * 60;
inline constexpr std::uint32_t kServiceEndMinute = 25 * 60;
// A driver's walk between its crew room and its line's first platform. The
// crew's shifts (Venues.cpp) overlap by enough for this and a round trip, so
// a driver going off hands its train to the next shift's at that platform.
inline constexpr double kCrewWalkMinutes = 2.0;
// Changing lines at a station: down or up a level to the other platform.
inline constexpr double kTransferMinutes = 1.0;

enum class LineAxis : std::uint8_t { Row, Column };
// Up runs from a line's first station to its last, Down back.
enum class Direction : std::uint8_t { Up, Down };

struct MetroLine {
    LineAxis axis{LineAxis::Row};
    // Indices into City::stations, in order along the line (west to east
    // for rows, north to south for columns).
    std::vector<std::uint32_t> stations;
    // Meters along the line from its first station.
    std::vector<double> meters;
    // The line's timetable: minutes from a round trip's start, when a train
    // reaches its first station on the up track.
    std::vector<double> upArrives, upDeparts, downArrives, downDeparts;
    double roundTripMinutes{};
    std::uint32_t trains{};
    double headwayMinutes{};
};

// One ride on one line.
struct Leg {
    std::uint32_t line{};
    Direction direction{Direction::Up};
    std::uint32_t fromStation{};  // Indices into City::stations.
    std::uint32_t toStation{};
    double departs{};
    double arrives{};
    // Which train, and the start of its round trip.
    std::uint32_t train{};
    double roundTripStart{};
};

// A metro driver's run in one shift: the train it takes over at its line's
// first station, and the stop there where it hands the train on.
struct DriverRun {
    std::uint32_t train{};
    double boards{};   // Doors open at the first station, up track.
    double departs{};
    double alights{};  // The train stands at the same platform again.
};

// Where a train is, for drawing.
struct TrainState {
    enum class Phase : std::uint8_t { InDepot, LeavingDepot, Running, EnteringDepot };
    std::uint32_t line{};
    std::uint32_t train{};
    Phase phase{Phase::InDepot};
    // Running: meters along the line (negative and beyond its length in the
    // tail tracks), the track's side from -1 (up track) to +1 (down track);
    // trains keep right, so -1 is the right of a train running up,
    // and whether it is driven toward the line's end.
    double meters{};
    double side{-1.0};
    bool towardEnd{true};
    // The end the driver drives from, along the line: +1 toward its end, -1
    // toward its start. While a train reverses in a tail track the driver
    // walks to the other end, and this passes smoothly between the two.
    double cab{1.0};
    // LeavingDepot and EnteringDepot: how far, 0 to 1, from the depot.
    double progress{};
    // Running and standing at a platform: how far its doors are open, from 0
    // (shut) to 1. They slide open over kDoorMinutes after it stops and shut
    // over the same time before it leaves.
    double doors{};
};

class Metro {
public:
    explicit Metro(const City& city);

    [[nodiscard]] const std::vector<MetroLine>& Lines() const noexcept {
        return lines_;
    }
    // The line along `axis` through a station; nothing when none does.
    [[nodiscard]] std::optional<std::uint32_t> LineOf(std::uint32_t station,
                                                      LineAxis axis) const;
    // A station's place along a line.
    [[nodiscard]] std::optional<std::size_t> IndexOn(std::uint32_t line,
                                                     std::uint32_t station) const;
    // The earliest ride from one station to another on a line whose doors
    // open (kDwellMinutes before it leaves) at or after `readyAt`. Nothing when they are not both on it or no more
    // trains run before the next day's service.
    [[nodiscard]] std::optional<Leg> Ride(std::uint32_t line,
                                          std::uint32_t fromStation,
                                          std::uint32_t toStation,
                                          double readyAt) const;
    // The quickest way between two stations: one ride along a shared line,
    // or two with a change where the first station's line crosses the
    // second's. Empty when no train gets there before the next day.
    [[nodiscard]] std::vector<Leg> Route(std::uint32_t fromStation,
                                         std::uint32_t toStation,
                                         double readyAt) const;
    // Every train of every line at `time`.
    [[nodiscard]] std::vector<TrainState> TrainsAt(double time) const;
    // One train of a line at `time`.
    [[nodiscard]] TrainState TrainAt(std::uint32_t line, std::uint32_t train,
                                     double time) const;
    // The run of a train's driver ready at the line's first station at
    // `readyAt`: on at the train's first stop there from then, off at its
    // first stop there from `handoverAt` (where the next driver gets on), or
    // at its last stop there by `latestOff` or before the depot if sooner.
    // Nothing when the train runs no round trip in between (at night, or a
    // shift too short).
    [[nodiscard]] std::optional<DriverRun> PlanDriverRun(std::uint32_t line, std::uint32_t train,
                                                         double readyAt, double handoverAt,
                                                         double latestOff) const;
    // The station where a row line and a column line cross.
    [[nodiscard]] std::optional<std::uint32_t> Crossing(std::uint32_t rowLine,
                                                        std::uint32_t columnLine) const;

private:
    // A train's first stop at its line's first platform, up track, at or
    // after `after`: the start of one of its round trips, or its last
    // arrival there before the depot.
    [[nodiscard]] std::optional<double> NextStopAt(std::uint32_t line, std::uint32_t train,
                                                   double after) const;
    [[nodiscard]] std::optional<Leg> RideOnDay(std::uint32_t line,
                                               std::size_t from, std::size_t to,
                                               double readyAt, double day) const;
    [[nodiscard]] std::optional<TrainState> TrainOnDay(std::uint32_t line,
                                                       std::uint32_t train,
                                                       double time,
                                                       double day) const;

    std::vector<MetroLine> lines_;
    // Per station: its row line and column line, if any.
    std::vector<std::optional<std::uint32_t>> rowLine_;
    std::vector<std::optional<std::uint32_t>> columnLine_;
};

// A metro depot: a building whose rooms are all crew rooms, raised by
// generation at a line's first station or by a town's Metro Authority.
[[nodiscard]] bool IsDepot(const Building& building) noexcept;

// Whether an intersection is on a line's street: its row for a row line, its
// column for a column line.
[[nodiscard]] bool OnLineStreet(const City& city, const MetroLine& line, NodeId node);

// Each line's depot, by line: in line order, each line takes the first depot
// in id order on its street that no line before it took. An invalid id: the
// line has none. The Metro Authority and the view both go by this, so they
// agree which line a depot serves after lines open and are numbered anew.
[[nodiscard]] std::vector<EntityId> LineDepots(const City& city, const Metro& metro);

}  // namespace mechapunk::sim
