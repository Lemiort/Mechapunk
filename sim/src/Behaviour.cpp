// A robot's day: acting when its time comes, arriving, deciding where to go
// next, walking, commuting, and officers' patrols.

#include "mechapunk/sim/World.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Names.hpp"
#include "mechapunk/sim/Venues.hpp"

#include "WorldInternal.hpp"

namespace mechapunk::sim {
namespace {

// How much a need wants attention: grows with the square of what is
// missing, from 0 when full to 1000 when empty.
std::int64_t Urgency(std::int32_t need) {
    const std::int64_t missing = Needs::kMax - std::clamp(need, 0, Needs::kMax);
    return (missing * missing) / Needs::kMax;
}

// How much `periods` of ten minutes with `effects` would relieve a robot's
// needs. Gains stop counting once a need is full, so a fast charger is not
// worth more than a full battery.
std::int64_t Relief(const Needs& needs, const NeedEffects& effects,
                    std::int32_t periods) {
    const auto relief = [&](std::int32_t need, std::int32_t effect) {
        const std::int32_t after =
            std::clamp(need + (std::max(effect, 0) * periods), 0, Needs::kMax);
        return Urgency(need) - Urgency(after);
    };
    return relief(needs.charge, effects.charge) +
           relief(needs.condition, effects.condition) +
           relief(needs.social, effects.social) +
           relief(needs.fun, effects.fun);
}

}  // namespace

void World::Act(Robot& robot) {
    switch (robot.activity) {
        case RobotActivity::Resting:
            if (state_.tick >= robot.commuteAt) {
                StartWalk(robot, robot.workspace);
            } else {
                Decide(robot);  // Time off: go out, or stay in a while.
            }
            break;
        case RobotActivity::Walking:
            Arrive(robot);
            break;
        case RobotActivity::Working: {
            const Tick shiftEnd = robot.shiftStartsAt + ShiftOf(robot).lengthMinutes;
            if (state_.tick >= shiftEnd) {
                robot.patrol = EntityId{};
                PayWage(robot);
                EndShift(robot);
                Decide(robot);
                break;
            }
            if (robot.patrol.IsValid()) {
                // Done looking round the venue: back to the station on foot.
                robot.patrol = robot.workspace;
                StartWalk(robot, robot.workspace);
                break;
            }
            Patrol(robot, shiftEnd);
            break;
        }
        case RobotActivity::Visiting:
            --reserved_[robot.location.value];
            --guests_[robot.location.value];
            Decide(robot);
            break;
    }
}

Tick World::NextPatrolAt(const Robot& robot) const {
    // On the next whole hour, but not before the shift starts.
    const Tick hour = state_.tick + (60 - (MinuteOfDay(state_.tick) % 60));
    return std::max(hour, robot.shiftStartsAt);
}

void World::Patrol(Robot& robot, Tick shiftEnd) {
    // Only officers at their station patrol, on the hour once the shift has
    // started, and only to a venue close by that they can walk to, look
    // round and get back from before the shift ends.
    const auto station = rooms_.Find(robot.workspace);
    if (!station || station->room->kind != RoomKind::PoliceStation ||
        robot.location != robot.workspace) {
        robot.nextActionAt = shiftEnd;
        return;
    }
    if (state_.tick >= robot.shiftStartsAt && MinuteOfDay(state_.tick) % 60 == 0) {
        std::vector<EntityId> candidates;
        for (const RoomKind kind : VenueKinds()) {
            for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
                const EntityId id = venue.room->id;
                const Tick there = TravelMinutes(robot.location, id);
                const Tick back = TravelMinutes(id, robot.location);
                if (there <= static_cast<Tick>(state_.settings.crime.patrolReachMinutes) &&
                    state_.tick + there + static_cast<Tick>(state_.settings.crime.patrolStayMinutes) + back <= shiftEnd) {
                    candidates.push_back(id);
                }
            }
        }
        // Sorted, so the pick below is deterministic.
        std::ranges::sort(candidates);
        // The chronicle keeps every `Robbed` event oldest first (task B
        // made the crimes notable, task C keeps them), so walking it back
        // from the newest entry stops at the first robbery older than the
        // lookback. The officer then goes to a venue on that list when one
        // is in reach: its robbers still stand in its room, which is where
        // `PatrolArrests` (B13) arrests a member that robbed in that hour.
        // Without this the arrest rule needs an officer to visit the crime
        // venue by luck out of ~484 venues, which gave 9 arrests in 40 days.
        std::vector<EntityId> crimeRooms;
        for (auto it = state_.chronicle.rbegin();
             it != state_.chronicle.rend() &&
             it->tick + static_cast<Tick>(state_.settings.crime.crimeLookbackMinutes) >= state_.tick;
             ++it) {
            if (it->kind == EventKind::Robbed && it->room.IsValid()) {
                crimeRooms.push_back(it->room);
            }
        }
        std::ranges::sort(crimeRooms);
        crimeRooms.erase(std::unique(crimeRooms.begin(), crimeRooms.end()),
                         crimeRooms.end());
        std::vector<EntityId> crimeVenues;
        std::ranges::set_intersection(candidates, crimeRooms,
                                      std::back_inserter(crimeVenues));
        const std::vector<EntityId>& pick =
            crimeVenues.empty() ? candidates : crimeVenues;
        if (!pick.empty()) {
            robot.patrol = pick[state_.random.UniformInt(
                0, static_cast<std::uint32_t>(pick.size() - 1))];
            StartWalk(robot, robot.patrol);
            return;
        }
    }
    robot.nextActionAt = std::min(NextPatrolAt(robot), shiftEnd);
}

void World::Arrive(Robot& robot) {
    const Tick now = state_.tick;
    robot.location = robot.destination;
    robot.destination = EntityId{};
    robot.rideFrom = 0;
    robot.rideVia = 0;
    robot.rideTo = 0;
    robot.boardsAt = 0;
    robot.alightsAt = 0;
    if (robot.patrol.IsValid() && robot.location == robot.patrol) {
        // A patrol leg is over; the officer stays on the clock throughout.
        robot.activity = RobotActivity::Working;
        const Tick shiftEnd = robot.shiftStartsAt + ShiftOf(robot).lengthMinutes;
        if (robot.location == robot.workspace) {
            // Back at the station, until the next hour's patrol.
            robot.patrol = EntityId{};
            robot.nextActionAt = std::min(NextPatrolAt(robot), shiftEnd);
        } else {
            // At the venue: look round for a while, then head back.
            robot.nextActionAt = std::min(now + static_cast<Tick>(state_.settings.crime.patrolStayMinutes), shiftEnd);
        }
        return;
    }
    if (robot.workspace.IsValid() && robot.location == robot.workspace) {
        robot.activity = RobotActivity::Working;
        const auto workplace = rooms_.Find(robot.workspace);
        if (workplace && workplace->room->kind == RoomKind::PoliceStation) {
            // An officer wakes every hour to patrol, not only at shift end.
            robot.nextActionAt = std::min(
                NextPatrolAt(robot), robot.shiftStartsAt + ShiftOf(robot).lengthMinutes);
        } else {
            // A late robot still leaves at the regular end of its shift.
            robot.nextActionAt = std::max(
                robot.shiftStartsAt + ShiftOf(robot).lengthMinutes, now);
        }
        const auto late = static_cast<std::uint32_t>(
            now > robot.shiftStartsAt ? now - robot.shiftStartsAt : 0);
        if (late > 0) {
            robot.performance -= static_cast<std::int32_t>(state_.settings.behaviour.latePerformance);
        }
        ScoreArrivalAtWork(robot);
        state_.today.lateArrivals += late > 0 ? 1 : 0;
        Emit({.tick = now,
              .kind = EventKind::ClockedIn,
              .robot = robot.id,
              .room = robot.workspace,
              .minutes = late});
        return;
    }
    if (robot.location == robot.apartment) {
        robot.activity = RobotActivity::Resting;
        Emit({.tick = now,
              .kind = EventKind::ArrivedHome,
              .robot = robot.id,
              .room = robot.apartment});
        ScheduleNextCommute(robot);
        return;
    }

    const auto venue = rooms_.Find(robot.location);
    const VenueRules* rules =
        venue ? FindVenueRules(venue->room->kind) : nullptr;
    if (rules == nullptr) {
        throw std::logic_error("Robot arrived at a room it has no business in");
    }
    const std::uint32_t others = GuestsIn(robot.location);
    robot.activity = RobotActivity::Visiting;
    ++guests_[robot.location.value];
    const std::int64_t price = PriceAt(*venue->room);
    ++MutableRoom(venue->room->id).visits;
    const std::int64_t luxury = LuxuryOf(robot, price);
    if (robot.credits < price) {
        Withdraw(robot, price - robot.credits);  // Repairs from savings.
    }
    Sell(robot, venue->room->owner, price);
    robot.spentToday += price;
    state_.today.venueSales += price;
    if (luxury > 0) {
        Withdraw(robot, luxury);
        Sell(robot, venue->room->owner, luxury);
        robot.spentToday += luxury;
        state_.today.venueSales += luxury;
    }
    ++state_.today.visits;
    if (venue->room->kind == RoomKind::BlackMarket &&
        state_.random.UniformInt(1, 100) <= static_cast<std::uint32_t>(state_.settings.behaviour.badPartPercent)) {
        robot.wear = std::min(robot.wear + static_cast<std::int32_t>(state_.settings.behaviour.badPartWear), kMaxWear);
        Remember(robot, MemoryKind::GotBadPart, {}, robot.location);
    }

    // Stay a while, but leave by closing time and in time to rest.
    const Tick latestLeave = LatestHomeTick(robot) -
                             TravelMinutes(robot.location, robot.apartment);
    Tick stay = state_.random.UniformInt(rules->minVisitMinutes,
                                         rules->maxVisitMinutes);
    stay = std::min<Tick>(stay, MinutesUntilClose(*rules, MinuteOfDay(now)));
    stay = latestLeave > now ? std::min(stay, latestLeave - now) : 1;
    stay = std::max<Tick>(stay, 1);
    robot.nextActionAt = now + stay;
    Emit({.tick = now,
          .kind = EventKind::ArrivedAtVenue,
          .robot = robot.id,
          .room = robot.location,
          .minutes = static_cast<std::uint32_t>(stay),
          .credits = price,
          .others = others});
}

void World::Decide(Robot& robot) {
    const Tick now = state_.tick;
    const Tick latestHome = LatestHomeTick(robot);
    const std::uint32_t walkHome = TravelMinutes(robot.location,
                                                  robot.apartment);

    std::int64_t bestScore =
        static_cast<std::int64_t>(state_.settings.behaviour.homeComfort) + Relief(robot.needs, kResting, static_cast<std::int32_t>(state_.settings.behaviour.homePeriods));
    if (now + walkHome + static_cast<Tick>(state_.settings.behaviour.bedtimeWindow) >= latestHome) {
        bestScore += static_cast<std::int64_t>(state_.settings.behaviour.bedtimeUrge);
    }
    EntityId best = robot.apartment;

    // Keep enough for tonight's rent, anything already owed, a fare home,
    // and something toward paying back a loan.
    const Loan* loan = LoanOf(robot.id);
    const std::int64_t reserve =
        RentOf(robot) + robot.rentOwed + static_cast<std::int64_t>(kMetroFare) +
        (loan != nullptr ? std::min(loan->owed, PocketOf(robot)) : 0);
    const std::int64_t budget = BudgetOf(robot);
    const auto here = rooms_.Find(robot.location);
    const RoomKind hereKind = here ? here->room->kind : RoomKind::Apartment;
    // Where this outing may go at all: on a gang outing, the venues round one
    // gang's turf (`GangOuting`, in Gangs.cpp); empty means the whole city,
    // as it always was.
    const std::vector<EntityId> gangOuting = GangOuting(robot);
    for (const RoomKind kind : VenueKinds()) {
        const VenueRules& rules = *FindVenueRules(kind);
        // Within what it can pay, and what it means to spend today. A worn
        // robot pays for repairs out of its savings, budget or not.
        const bool needsRepair = (kind == RoomKind::Clinic || kind == RoomKind::RepairShop) &&
                                 robot.needs.condition < static_cast<std::int32_t>(state_.settings.behaviour.repairUrgentCondition);
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            const Room& room = *venue.room;
            // A gang outing only goes to the neighbourhood it was drawn to.
            if (!gangOuting.empty() &&
                !std::binary_search(gangOuting.cbegin(), gangOuting.cend(),
                                    room.id)) {
                continue;
            }
            const std::int64_t price = PriceAt(room);
            if (needsRepair ? robot.credits + robot.savings - price < reserve
                            : (robot.credits - price < reserve ||
                               robot.spentToday + price > budget)) {
                continue;
            }
            // Leaving a venue means going somewhere else, and nobody goes
            // out to the venue it works at.
            if (room.id == robot.location || room.id == robot.workspace ||
                reserved_[room.id.value] >= room.capacity) {
                continue;
            }
            const std::uint32_t walk = TravelMinutes(robot.location, room.id);
            const Tick arrival = now + walk;
            const std::uint32_t arrivalMinute = MinuteOfDay(arrival);
            const Tick walkBack = TravelMinutes(room.id, robot.apartment);
            if (!IsOpen(rules, arrivalMinute) ||
                MinutesUntilClose(rules, arrivalMinute) <
                    rules.minVisitMinutes ||
                arrival + rules.minVisitMinutes + walkBack > latestHome) {
                continue;
            }
            NeedEffects effects = rules.effects;
            effects.social += std::min(
                static_cast<std::int32_t>(GuestsIn(room.id)) *
                    rules.socialPerGuest,
                rules.socialGuestCap);
            const auto periods = static_cast<std::int32_t>(
                (rules.minVisitMinutes + rules.maxVisitMinutes) / 2 /
                kNeedPeriod);
            const std::int64_t score =
                Relief(robot.needs, effects, periods) -
                (static_cast<std::int64_t>(state_.settings.behaviour.walkCostPerMinute) * walk) -
                (static_cast<std::int64_t>(state_.settings.behaviour.priceCostPerCredit) * price) -
                (kind == hereKind ? static_cast<std::int64_t>(state_.settings.behaviour.sameKindPenalty) : 0) +
                (kind == RoomKind::Clinic ? robot.wear / static_cast<std::int64_t>(state_.settings.behaviour.wearPerClinicPoint) : 0) +
                CompanyAt(robot, room.id) +
                state_.random.UniformInt(0, static_cast<std::uint32_t>(state_.settings.behaviour.maxWhim));
            if (score > bestScore) {
                bestScore = score;
                best = room.id;
            }
        }
    }
    if (best == robot.location) {
        PlanAtHome(robot);  // Already home and staying in.
        return;
    }
    StartWalk(robot, best);
}

void World::StartWalk(Robot& robot, EntityId destination) {
    Trip trip = PlanTrip(robot.location, destination);
    // A robot walks rather than miss tonight's rent for a fare.
    if (trip.metro && robot.credits - static_cast<std::int64_t>(kMetroFare) <
                          RentOf(robot) + robot.rentOwed) {
        trip = PlanTrip(robot.location, destination, false);
    }
    robot.activity = RobotActivity::Walking;
    robot.destination = destination;
    robot.departedAt = state_.tick;
    robot.nextActionAt = state_.tick + trip.minutes;
    std::int64_t fare = 0;
    if (trip.metro) {
        robot.rideFrom = trip.fromStation;
        robot.rideVia = trip.viaStation;
        robot.rideTo = trip.toStation;
        robot.boardsAt = state_.tick + trip.minutesToStation;
        robot.alightsAt = robot.boardsAt + trip.minutesUnderground;
        // A town's metro runs free until its Metro Authority is founded (K12).
        fare = transit_.IsValid() ? std::min<std::int64_t>(kMetroFare, robot.credits) : 0;
        if (fare > 0) {
            Sell(robot, transit_, fare);
        }
        state_.today.fares += fare;
        ++state_.today.metroRides;
    } else {
        robot.rideFrom = 0;
        robot.rideVia = 0;
        robot.rideTo = 0;
        robot.boardsAt = 0;
        robot.alightsAt = 0;
    }
    if (IsGuestTrip(robot, destination)) {
        ++reserved_[destination.value];
    }
    ++state_.today.walks;
    Emit({.tick = state_.tick,
          .kind = EventKind::Departed,
          .robot = robot.id,
          .room = robot.location,
          .otherRoom = destination,
          .minutes = trip.minutes,
          .credits = fare,
          .metro = trip.metro,
          .fromStation = trip.fromStation,
          .viaStation = trip.viaStation,
          .toStation = trip.toStation});
}

void World::ScheduleNextCommute(Robot& robot) {
    if (!robot.workspace.IsValid()) {
        robot.commuteAt = kNoCommute;  // Out of work: every day is free.
        PlanAtHome(robot);
        return;
    }
    const Tick now = state_.tick;
    const Tick lead = TravelMinutes(robot.apartment, robot.workspace) +
                      robot.punctualityMinutes +
                      state_.random.UniformInt(0, static_cast<std::uint32_t>(state_.settings.behaviour.maxDailyExtraMinutes));
    // The next working day's shift whose departure time is still ahead.
    const Tick start = NextShiftStart(robot, now + lead);
    robot.shiftStartsAt = start;
    robot.commuteAt = start - lead;
    PlanAtHome(robot);
}

void World::PlanAtHome(Robot& robot) {
    const Tick now = state_.tick;
    robot.nextActionAt = robot.commuteAt;
    const Tick latest =
        robot.commuteAt > kRestMinutes ? robot.commuteAt - kRestMinutes : 0;
    const Tick today = StartOfDay(now);
    Tick outing = 0;
    const std::uint32_t minute = MinuteOfDay(now);
    if (IsDayOff(robot, now) && minute < static_cast<Tick>(state_.settings.behaviour.lastOutingMinute)) {
        // Back home on a free day: out again after a break, but not before
        // the morning.
        const Tick afterBreak =
            now + state_.random.UniformInt(static_cast<std::uint32_t>(state_.settings.behaviour.minHomeBreak), static_cast<std::uint32_t>(state_.settings.behaviour.maxHomeBreak));
        const Tick morning = today + static_cast<Tick>(state_.settings.behaviour.outingEarliestMinute) +
                             state_.random.UniformInt(0, static_cast<std::uint32_t>(state_.settings.behaviour.outingSpreadMinutes));
        outing = std::max(afterBreak, morning);
    } else if (minute >= static_cast<Tick>(state_.settings.behaviour.outingEarliestMinute) && minute < static_cast<Tick>(state_.settings.behaviour.lastOutingMinute)) {
        // Home during the day or evening of a working day: after a break,
        // think again about going out.
        outing = now + state_.random.UniformInt(static_cast<std::uint32_t>(state_.settings.behaviour.minEveningBreak),
                                                static_cast<std::uint32_t>(state_.settings.behaviour.maxEveningBreak));
    } else {
        for (Tick day = 1; day <= kDaysPerWeek; ++day) {
            const Tick start = today + (day * kMinutesPerDay);
            if (IsDayOff(robot, start)) {
                outing = start + static_cast<Tick>(state_.settings.behaviour.outingEarliestMinute) +
                         state_.random.UniformInt(0, static_cast<std::uint32_t>(state_.settings.behaviour.outingSpreadMinutes));
                break;
            }
        }
    }
    if (outing > now && outing + static_cast<Tick>(state_.settings.behaviour.minOutingMinutes) <= latest &&
        outing < robot.nextActionAt) {
        robot.nextActionAt = outing;
    }
}

bool World::IsDayOff(const Robot& robot, Tick tick) noexcept {
    return !robot.workspace.IsValid() ||
           ((robot.daysOff >> DayOfWeek(tick)) & 1U) != 0;
}

Tick World::NextShiftStart(const Robot& robot, Tick after) const {
    Tick start = StartOfDay(after) + ShiftOf(robot).startMinute;
    // At most a week ahead: validation guarantees a working day.
    while (start <= after || IsDayOff(robot, start)) {
        start += kMinutesPerDay;
    }
    return start;
}

Tick World::MaxCommuteLead(const Robot& robot) {
    return TravelMinutes(robot.apartment, robot.workspace) +
           robot.punctualityMinutes + static_cast<std::uint32_t>(state_.settings.behaviour.maxDailyExtraMinutes);
}

Tick World::LatestHomeTick(const Robot& robot) {
    const Tick now = state_.tick;
    // Home by the end of every night, workday or not.
    Tick bedtime = StartOfDay(now) + kBedtimeMinute;
    if (bedtime <= now) {
        bedtime += kMinutesPerDay;
    }
    if (!robot.workspace.IsValid()) {
        return bedtime;
    }
    // A full rest before the next working day's shift.
    const Tick lead = MaxCommuteLead(robot);
    const Tick nextShift = NextShiftStart(robot, now + lead);
    const Tick margin = lead + kRestMinutes;
    const Tick beforeWork = nextShift > margin ? nextShift - margin : 0;
    return std::min(beforeWork, bedtime);
}

}  // namespace mechapunk::sim
