#include "mechapunk/sim/Metro.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mechapunk::sim {

namespace {
constexpr double kDay = static_cast<double>(kMinutesPerDay);
constexpr double kTailMinutes = kTailMeters / kTrainMetersPerMinute;
// From leaving the last platform one way to reaching it the other way:
// into the tail, reverse and cross over, back to the platform.
constexpr double kTurnMinutes = (2.0 * kTailMinutes) + kReverseMinutes;

// The first round trip of a service day starts when the first train reaches
// the first station after leaving the depot.
double FirstRoundTrip(double day) {
    return day + static_cast<double>(kServiceStartMinute) + kDepotMinutes;
}
double ServiceEnd(double day) { return day + static_cast<double>(kServiceEndMinute); }
}  // namespace

Metro::Metro(const City& city)
    : rowLine_(city.stations.size()),
      columnLine_(city.stations.size()) {
    std::map<std::int32_t, std::vector<std::uint32_t>> rows;
    std::map<std::int32_t, std::vector<std::uint32_t>> columns;
    for (std::uint32_t station = 0; station < city.stations.size(); ++station) {
        const CityNode& node = city.nodes.at(city.stations[station].node.value);
        rows[node.z].push_back(station);
        columns[node.x].push_back(station);
    }
    const auto add = [&](LineAxis axis, std::vector<std::uint32_t> stations) {
        if (stations.size() < 2) {
            return;
        }
        const auto along = [&](std::uint32_t station) {
            const CityNode& node = city.nodes.at(city.stations[station].node.value);
            return axis == LineAxis::Row ? node.x : node.z;
        };
        std::ranges::sort(stations, {}, along);
        MetroLine line{.axis = axis, .stations = std::move(stations)};
        const std::int32_t origin = along(line.stations.front());
        for (const std::uint32_t station : line.stations) {
            line.meters.push_back(static_cast<double>(along(station) - origin));
        }
        // Up: stop at each station in turn; then turn in the far tail, and
        // stop at each again on the way down; then turn in the near tail.
        const std::size_t count = line.stations.size();
        double time = 0.0;
        for (std::size_t index = 0; index < count; ++index) {
            if (index > 0) {
                time += (line.meters[index] - line.meters[index - 1]) / kTrainMetersPerMinute;
            }
            line.upArrives.push_back(time);
            time += kDwellMinutes;
            line.upDeparts.push_back(time);
        }
        time += kTurnMinutes;
        line.downArrives.assign(count, 0.0);
        line.downDeparts.assign(count, 0.0);
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t index = count - 1 - step;
            if (step > 0) {
                time += (line.meters[index + 1] - line.meters[index]) / kTrainMetersPerMinute;
            }
            line.downArrives[index] = time;
            time += kDwellMinutes;
            line.downDeparts[index] = time;
        }
        line.roundTripMinutes = time + kTurnMinutes;
        line.trains = std::max<std::uint32_t>(
            1, static_cast<std::uint32_t>(std::ceil(line.roundTripMinutes / kTargetHeadwayMinutes)));
        line.headwayMinutes = line.roundTripMinutes / static_cast<double>(line.trains);
        const auto number = static_cast<std::uint32_t>(lines_.size());
        for (const std::uint32_t station : line.stations) {
            (axis == LineAxis::Row ? rowLine_ : columnLine_).at(station) = number;
        }
        lines_.push_back(std::move(line));
    };
    for (auto& [z, stations] : rows) {
        add(LineAxis::Row, std::move(stations));
    }
    for (auto& [x, stations] : columns) {
        add(LineAxis::Column, std::move(stations));
    }
}

std::optional<std::uint32_t> Metro::LineOf(std::uint32_t station, LineAxis axis) const {
    if (station >= rowLine_.size()) {
        return std::nullopt;
    }
    return axis == LineAxis::Row ? rowLine_[station] : columnLine_[station];
}

std::optional<std::size_t> Metro::IndexOn(std::uint32_t line, std::uint32_t station) const {
    if (line >= lines_.size()) {
        return std::nullopt;
    }
    const auto& stations = lines_[line].stations;
    const auto found = std::ranges::find(stations, station);
    if (found == stations.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - stations.begin());
}

std::optional<std::uint32_t> Metro::Crossing(std::uint32_t rowLine,
                                             std::uint32_t columnLine) const {
    if (rowLine >= lines_.size() || columnLine >= lines_.size()) {
        return std::nullopt;
    }
    for (const std::uint32_t station : lines_[rowLine].stations) {
        if (columnLine_.at(station) == columnLine) {
            return station;
        }
    }
    return std::nullopt;
}

std::optional<Leg> Metro::RideOnDay(std::uint32_t line, std::size_t from, std::size_t to,
                                    double readyAt, double day) const {
    const MetroLine& metro = lines_[line];
    const Direction direction = to > from ? Direction::Up : Direction::Down;
    const double departs = (direction == Direction::Up ? metro.upDeparts : metro.downDeparts)[from];
    const double arrives = (direction == Direction::Up ? metro.upArrives : metro.downArrives)[to];
    // Round trip j starts at first + j * headway, while it starts before the
    // end of service.
    const double first = FirstRoundTrip(day);
    const double end = ServiceEnd(day);
    const double doors = departs - kDwellMinutes;
    const double earliest = std::ceil((readyAt - first - doors) / metro.headwayMinutes - 1e-9);
    const auto trip = static_cast<std::int64_t>(std::max(0.0, earliest));
    const double start = first + (static_cast<double>(trip) * metro.headwayMinutes);
    if (start >= end) {
        return std::nullopt;
    }
    return Leg{.line = line,
               .direction = direction,
               .fromStation = metro.stations[from],
               .toStation = metro.stations[to],
               .departs = start + departs,
               .arrives = start + arrives,
               .train = static_cast<std::uint32_t>(trip % metro.trains),
               .roundTripStart = start};
}

std::optional<Leg> Metro::Ride(std::uint32_t line, std::uint32_t fromStation,
                               std::uint32_t toStation, double readyAt) const {
    const auto from = IndexOn(line, fromStation);
    const auto to = IndexOn(line, toStation);
    if (!from || !to || *from == *to) {
        return std::nullopt;
    }
    // Trains of the service day that began yesterday may still run after
    // midnight; otherwise today's, then tomorrow's.
    const double today = std::floor(readyAt / kDay) * kDay;
    for (const double day : {today - kDay, today, today + kDay}) {
        if (auto leg = RideOnDay(line, *from, *to, readyAt, day)) {
            return leg;
        }
    }
    return std::nullopt;
}

std::vector<Leg> Metro::Route(std::uint32_t fromStation, std::uint32_t toStation,
                              double readyAt) const {
    std::vector<Leg> best;
    const auto consider = [&](std::vector<Leg> legs) {
        if (!legs.empty() && (best.empty() || legs.back().arrives < best.back().arrives)) {
            best = std::move(legs);
        }
    };
    for (const LineAxis axis : {LineAxis::Row, LineAxis::Column}) {
        const auto first = LineOf(fromStation, axis);
        if (!first) {
            continue;
        }
        if (LineOf(toStation, axis) == first) {
            if (auto leg = Ride(*first, fromStation, toStation, readyAt)) {
                consider({*leg});
            }
            continue;
        }
        // Change where this line crosses the destination's other line.
        const LineAxis other = axis == LineAxis::Row ? LineAxis::Column : LineAxis::Row;
        const auto second = LineOf(toStation, other);
        if (!second) {
            continue;
        }
        const auto via = axis == LineAxis::Row ? Crossing(*first, *second)
                                               : Crossing(*second, *first);
        if (!via || *via == fromStation || *via == toStation) {
            continue;
        }
        const auto one = Ride(*first, fromStation, *via, readyAt);
        if (!one) {
            continue;
        }
        if (const auto two = Ride(*second, *via, toStation, one->arrives + kTransferMinutes)) {
            consider({*one, *two});
        }
    }
    return best;
}

std::optional<TrainState> Metro::TrainOnDay(std::uint32_t line, std::uint32_t train,
                                            double time, double day) const {
    const MetroLine& metro = lines_[line];
    const double first = FirstRoundTrip(day) + (static_cast<double>(train) * metro.headwayMinutes);
    const double leaves = first - kDepotMinutes;
    const double end = ServiceEnd(day);
    if (time < leaves) {
        return std::nullopt;
    }
    if (first >= end) {
        return std::nullopt;  // This train sits the day out.
    }
    // Its last round trip is the last to start before the end of service.
    const auto trips = static_cast<std::int64_t>(std::ceil((end - first) / metro.roundTripMinutes));
    const double back = first + (static_cast<double>(trips) * metro.roundTripMinutes);
    TrainState state{.line = line, .train = train};
    if (time < first) {
        state.phase = TrainState::Phase::LeavingDepot;
        state.progress = (time - leaves) / kDepotMinutes;
        return state;
    }
    if (time >= back + kDepotMinutes) {
        return std::nullopt;
    }
    if (time >= back) {
        state.phase = TrainState::Phase::EnteringDepot;
        state.progress = (time - back) / kDepotMinutes;
        return state;
    }
    state.phase = TrainState::Phase::Running;
    const double into = std::fmod(time - first, metro.roundTripMinutes);
    const std::size_t count = metro.stations.size();
    const double length = metro.meters.back();
    // Up the line.
    for (std::size_t index = 0; index < count; ++index) {
        if (into < metro.upArrives[index]) {
            const double from = metro.upDeparts[index - 1];
            state.meters = metro.meters[index - 1] +
                           ((into - from) * kTrainMetersPerMinute);
            state.side = -1.0;
            state.towardEnd = true;
            return state;
        }
        if (into < metro.upDeparts[index]) {
            state.meters = metro.meters[index];
            state.side = -1.0;
            state.towardEnd = true;
            return state;
        }
    }
    // Turning in a tail track: in, stop and cross over diagonally back out.
    const auto turn = [&](double since, double end, double outward) {
        state.towardEnd = outward > 0.0;
        if (since < kTailMinutes) {
            state.meters = end + (outward * since * kTrainMetersPerMinute);
            state.side = outward > 0.0 ? -1.0 : 1.0;
            return;
        }
        since -= kTailMinutes;
        if (since < kReverseMinutes) {
            const double half = kReverseMinutes * 0.5;
            const double cross = since < half ? 0.0 : (since - half) / half;
            state.meters = end + (outward * kTailMeters * (1.0 - (0.5 * cross)));
            const double from = outward > 0.0 ? -1.0 : 1.0;
            state.side = from + ((-2.0 * from) * cross);
            state.towardEnd = since < half ? outward > 0.0 : outward < 0.0;
            return;
        }
        since -= kReverseMinutes;
        state.meters = end + (outward * kTailMeters * 0.5 * (1.0 - (since / kTailMinutes)));
        state.side = outward > 0.0 ? 1.0 : -1.0;
        state.towardEnd = outward < 0.0;
    };
    if (into < metro.downArrives[count - 1]) {
        turn(into - metro.upDeparts[count - 1], length, 1.0);
        return state;
    }
    for (std::size_t step = 0; step < count; ++step) {
        const std::size_t index = count - 1 - step;
        if (into < metro.downArrives[index]) {
            const double from = metro.downDeparts[index + 1];
            state.meters = metro.meters[index + 1] -
                           ((into - from) * kTrainMetersPerMinute);
            state.side = 1.0;
            state.towardEnd = false;
            return state;
        }
        if (into < metro.downDeparts[index]) {
            state.meters = metro.meters[index];
            state.side = 1.0;
            state.towardEnd = false;
            return state;
        }
    }
    turn(into - metro.downDeparts[0], 0.0, -1.0);
    return state;
}

TrainState Metro::TrainAt(std::uint32_t line, std::uint32_t train, double time) const {
    const double today = std::floor(time / kDay) * kDay;
    std::optional<TrainState> state;
    for (const double day : {today - kDay, today}) {
        if (auto found = TrainOnDay(line, train, time, day)) {
            state = found;
        }
    }
    return state.value_or(TrainState{.line = line, .train = train});  // In the depot.
}

std::vector<TrainState> Metro::TrainsAt(double time) const {
    std::vector<TrainState> trains;
    for (std::uint32_t line = 0; line < lines_.size(); ++line) {
        for (std::uint32_t train = 0; train < lines_[line].trains; ++train) {
            trains.push_back(TrainAt(line, train, time));
        }
    }
    return trains;
}

}  // namespace mechapunk::sim
