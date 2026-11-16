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

namespace mechapunk::sim {

namespace {
// Walking pace of 1.25 m/s.
constexpr std::uint32_t kWalkingMetersPerMinute = 75;
// Floors climbed per minute of stairs or elevator.
constexpr std::uint32_t kFloorsPerMinute = 3;
constexpr std::uint32_t kMinPunctualityMinutes = 5;
constexpr std::uint32_t kMaxPunctualityMinutes = 20;
// Daily variation added to a robot's punctuality.
constexpr std::uint32_t kMaxDailyExtraMinutes = 10;
constexpr std::uint32_t kMinSerialNumber = 100;
constexpr std::uint32_t kMaxSerialNumber = 999;
constexpr std::int32_t kMinStartingNeed = 550;
constexpr std::int32_t kMaxStartingNeed = 950;
// A day off starts no earlier than this and may start up to two hours later.
constexpr Tick kOutingEarliestMinute = 9 * kMinutesPerHour;
constexpr std::uint32_t kOutingSpreadMinutes = 120;
// No new outings from home after this minute of the day.
constexpr Tick kLastOutingMinute = 22 * kMinutesPerHour;
// A break at home between outings on a free day.
constexpr std::uint32_t kMinHomeBreak = 90;
constexpr std::uint32_t kMaxHomeBreak = 240;
// A shorter break at home after work before thinking about going out.
constexpr std::uint32_t kMinEveningBreak = 30;
constexpr std::uint32_t kMaxEveningBreak = 90;
// An outing needs at least this long before the robot must be back.
constexpr Tick kMinOutingMinutes = 60;
constexpr std::uint32_t kMinStartingCredits = 40;
constexpr std::uint32_t kMaxStartingCredits = 200;
// Needs change every this many minutes.
constexpr Tick kNeedPeriod = 10;
// Independent random streams per purpose, so for example a change in robot
// generation does not reshape the city.
constexpr std::uint64_t kCityStream = 1;
constexpr std::uint64_t kPopulationStream = 2;
constexpr std::uint64_t kRuntimeStream = 3;
constexpr std::uint64_t kInterestStream = 4;
constexpr std::uint64_t kCareerStream = 5;
constexpr std::uint64_t kAgeStream = 6;
// Robots in a new city are up to about a year old.
constexpr std::uint32_t kMaxStartingAge = 360;
// A robot at a clinic loses this much wear every ten minutes, and values a
// clinic visit by a third of its wear.
constexpr std::int32_t kClinicWearRepair = 2;
constexpr std::int64_t kWearPerClinicPoint = 3;
// Chance in percent that a black market part turns out dodgy, and the wear
// it causes.
constexpr std::uint32_t kBadPartPercent = 10;
constexpr std::int32_t kBadPartWear = 60;
// Robots start somewhere between junior and senior.
constexpr std::uint32_t kHighestStartingLevel = 2;
// Arriving late costs this much performance.
constexpr std::int32_t kLatePerformance = 3;
constexpr std::uint32_t kInterestsPerRobot = 2;

// Need changes per ten minutes of each activity; visits add venue effects.
// Resting at home does not wear a robot down.
constexpr NeedEffects kResting{.charge = 25, .condition = 0, .social = -3,
                               .fun = -2};
constexpr NeedEffects kWalking{.charge = -8, .condition = -1, .social = -5,
                               .fun = -3};
constexpr NeedEffects kWorking{.charge = -12, .condition = -2, .social = -4,
                               .fun = -4};
constexpr NeedEffects kVisiting{.charge = -3, .condition = -1, .social = -2,
                                .fun = -1};

// Weights of the evening decision. Going home is judged by an hour of rest.
constexpr std::int32_t kHomePeriods = 6;
constexpr std::int64_t kHomeComfort = 120;
constexpr std::int64_t kBedtimeUrge = 400;
constexpr Tick kBedtimeWindow = 2 * kMinutesPerHour;
constexpr std::int64_t kWalkCostPerMinute = 6;
constexpr std::int64_t kPriceCostPerCredit = 3;
constexpr std::uint32_t kMaxWhim = 80;
// Going straight from one venue to another of the same kind is less
// appealing than trying something different.
constexpr std::int64_t kSameKindPenalty = 150;

constexpr std::array<std::string_view, 8> kModelPrefixes{
    "KX", "VR", "NE", "TS", "OM", "ZR", "HK", "LU"};
constexpr std::array<std::string_view, 24> kGivenNames{
    "Vera",  "Ash",  "Juno", "Rook",  "Mira",  "Dex",  "Echo", "Nova",
    "Iris",  "Bolt", "Kai",  "Lumen", "Sable", "Tarn", "Vex",  "Wren",
    "Quill", "Onyx", "Pike", "Rhea",  "Sol",   "Tess", "Ulla", "Zed"};

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

void Apply(Needs& needs, const NeedEffects& effects) {
    const auto add = [](std::int32_t& need, std::int32_t delta) {
        need = std::clamp(need + delta, 0, Needs::kMax);
    };
    add(needs.charge, effects.charge);
    add(needs.condition, effects.condition);
    add(needs.social, effects.social);
    add(needs.fun, effects.fun);
}

// Picks a room of `kind` with space left, starting from a random one.
EntityId AssignRoom(const RoomIndex& rooms, RoomKind kind,
                    std::unordered_map<std::uint64_t, std::uint32_t>& used,
                    Pcg32& random) {
    const std::vector<RoomRef>& candidates = rooms.RoomsOfKind(kind);
    if (candidates.empty()) {
        throw std::logic_error("Generated city has no room of a needed kind");
    }
    const auto start = random.UniformInt(
        0, static_cast<std::uint32_t>(candidates.size() - 1));
    for (std::size_t offset = 0; offset < candidates.size(); ++offset) {
        const Room& room = *candidates[(start + offset) % candidates.size()].room;
        if (used[room.id.value] < room.capacity) {
            ++used[room.id.value];
            return room.id;
        }
    }
    throw std::logic_error("Generated city has too little capacity");
}
}  // namespace

std::string RandomRobotName(Pcg32& random) {
    const auto prefix = kModelPrefixes.at(random.UniformInt(
        0, static_cast<std::uint32_t>(kModelPrefixes.size() - 1)));
    const auto serial = random.UniformInt(kMinSerialNumber, kMaxSerialNumber);
    const auto given = kGivenNames.at(random.UniformInt(
        0, static_cast<std::uint32_t>(kGivenNames.size() - 1)));
    return std::format("{}-{} {}", prefix, serial, given);
}

World::World(WorldState state)
    : state_(std::move(state)), routes_(state_.city), rooms_(state_.city) {
    for (const Robot& robot : state_.robots) {
        if (robot.activity == RobotActivity::Visiting) {
            ++reserved_[robot.location.value];
            ++guests_[robot.location.value];
        } else if (robot.activity == RobotActivity::Walking) {
            const auto destination = rooms_.Find(robot.destination);
            if (destination && IsVenue(destination->room->kind)) {
                ++reserved_[robot.destination.value];
            }
        }
    }
}

World World::Generate(const WorldParams& params) {
    WorldState state;
    state.seed = params.seed;
    Pcg32 cityRandom = Pcg32::FromSeed(params.seed, kCityStream);
    state.city = GenerateCity(CityParams{.residents = params.robots},
                              cityRandom, state.ids);

    const RoomIndex rooms(state.city);
    Pcg32 population = Pcg32::FromSeed(params.seed, kPopulationStream);
    std::unordered_map<std::uint64_t, std::uint32_t> used;
    const auto need = [&] {
        return static_cast<std::int32_t>(population.UniformInt(
            kMinStartingNeed, kMaxStartingNeed));
    };
    for (std::uint32_t index = 0; index < params.robots; ++index) {
        Robot robot;
        robot.name = RandomRobotName(population);
        robot.id = state.ids.Allocate();
        robot.apartment =
            AssignRoom(rooms, RoomKind::Apartment, used, population);
        robot.workspace =
            AssignRoom(rooms, RoomKind::Workspace, used, population);
        robot.location = robot.apartment;
        robot.punctualityMinutes = population.UniformInt(
            kMinPunctualityMinutes, kMaxPunctualityMinutes);
        robot.needs = {.charge = need(), .condition = need(),
                       .social = need(), .fun = need()};
        robot.credits = population.UniformInt(kMinStartingCredits,
                                              kMaxStartingCredits);
        // Two consecutive days off a week, different for each robot, so
        // workplaces stay staffed every day.
        const std::uint32_t firstDayOff =
            population.UniformInt(0, kDaysPerWeek - 1);
        robot.daysOff = static_cast<std::uint8_t>(
            (1U << firstDayOff) | (1U << ((firstDayOff + 1) % kDaysPerWeek)));
        state.robots.push_back(std::move(robot));
    }

    Pcg32 tastes = Pcg32::FromSeed(params.seed, kInterestStream);
    for (Robot& robot : state.robots) {
        while (std::popcount(robot.interests) <
               static_cast<int>(kInterestsPerRobot)) {
            robot.interests = static_cast<std::uint8_t>(
                robot.interests | (1U << tastes.UniformInt(0, kInterestCount - 1)));
        }
    }

    Pcg32 ages = Pcg32::FromSeed(params.seed, kAgeStream);
    for (Robot& robot : state.robots) {
        robot.age = ages.UniformInt(0, kMaxStartingAge);
        robot.wear = WearAtAge(robot.age);
        robot.needs.condition =
            std::min(robot.needs.condition, kMaxWear - robot.wear);
    }

    Pcg32 careers = Pcg32::FromSeed(params.seed, kCareerStream);
    for (Robot& robot : state.robots) {
        robot.jobLevel =
            static_cast<std::uint8_t>(careers.UniformInt(0, kHighestStartingLevel));
    }

    state.random = Pcg32::FromSeed(params.seed, kRuntimeStream);
    World world(std::move(state));
    for (Robot& robot : world.state_.robots) {
        world.ScheduleNextCommute(robot);
    }
    return world;
}

std::expected<World, std::string> World::FromState(WorldState state) {
    if (auto valid = ValidateWorldState(state); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    return World(std::move(state));
}

const Robot* World::FindRobot(EntityId id) const noexcept {
    const auto found =
        std::ranges::lower_bound(state_.robots, id, std::less{}, &Robot::id);
    return found != state_.robots.end() && found->id == id ? &*found
                                                           : nullptr;
}

std::uint32_t World::GuestsIn(EntityId room) const {
    const auto found = guests_.find(room.value);
    return found != guests_.end() ? found->second : 0;
}

std::optional<Walk> World::WalkOf(const Robot& robot) {
    if (robot.activity != RobotActivity::Walking) {
        return std::nullopt;
    }
    return Walk{.from = robot.location,
                .to = robot.destination,
                .departedAt = robot.departedAt,
                .arrivesAt = robot.nextActionAt};
}

std::uint32_t World::WalkingMinutes(EntityId fromRoom, EntityId toRoom) {
    const auto from = rooms_.Find(fromRoom);
    const auto to = rooms_.Find(toRoom);
    if (!from || !to) {
        throw std::logic_error("Walk between unknown rooms");
    }
    std::uint32_t minutes = 0;
    std::uint32_t floors = 0;
    if (from->building == to->building) {
        floors = from->room->floor > to->room->floor
                     ? from->room->floor - to->room->floor
                     : to->room->floor - from->room->floor;
    } else {
        const auto meters = routes_.DistanceMeters(from->building->entrance,
                                                   to->building->entrance);
        if (!meters) {
            throw std::logic_error("No route between buildings");
        }
        minutes = (*meters + kWalkingMetersPerMinute - 1) /
                  kWalkingMetersPerMinute;
        floors = from->room->floor + to->room->floor;
    }
    minutes += (floors + kFloorsPerMinute - 1) / kFloorsPerMinute;
    // Even next door takes a minute.
    return std::max<std::uint32_t>(1, minutes);
}

void World::Step() {
    ++state_.tick;
    if (MinuteOfDay(state_.tick) == 0) {
        CollectRent();
        AgeRobots();
        ReviewHomes();
        ReviewJobs();
        FadeBonds();
    }
    if (state_.tick % kNeedPeriod == 0) {
        UpdateNeeds();
        Socialize();
    }
    // Robots act in id order, so random draws happen in a fixed order.
    for (Robot& robot : state_.robots) {
        if (robot.nextActionAt <= state_.tick) {
            Act(robot);
        }
    }
}

void World::RunFor(Tick minutes) {
    for (Tick minute = 0; minute < minutes; ++minute) {
        Step();
    }
}

void World::UpdateNeeds() {
    for (Robot& robot : state_.robots) {
        switch (robot.activity) {
            case RobotActivity::Resting:
                Apply(robot.needs, kResting);
                break;
            case RobotActivity::Walking:
                Apply(robot.needs, kWalking);
                break;
            case RobotActivity::Working:
                Apply(robot.needs, kWorking);
                break;
            case RobotActivity::Visiting: {
                Apply(robot.needs, kVisiting);
                const auto venue = rooms_.Find(robot.location);
                if (const VenueRules* rules =
                        venue ? FindVenueRules(venue->room->kind) : nullptr) {
                    NeedEffects effects = rules->effects;
                    const std::uint32_t others =
                        GuestsIn(robot.location) > 0
                            ? GuestsIn(robot.location) - 1
                            : 0;
                    effects.social += std::min(
                        static_cast<std::int32_t>(others) *
                            rules->socialPerGuest,
                        rules->socialGuestCap);
                    Apply(robot.needs, effects);
                    if (venue->room->kind == RoomKind::Clinic) {
                        robot.wear = std::max(robot.wear - kClinicWearRepair, 0);
                    }
                }
                break;
            }
        }
        // Wear limits how good a robot's condition can get.
        robot.needs.condition =
            std::min(robot.needs.condition, kMaxWear - robot.wear);
    }
}

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
            const std::uint32_t wage = WageOf(robot);
            robot.credits += wage;
            Emit({.tick = state_.tick,
                  .kind = EventKind::ShiftEnded,
                  .robot = robot.id,
                  .room = robot.workspace,
                  .credits = wage});
            EndShift(robot);
            Decide(robot);
            break;
        }
        case RobotActivity::Visiting:
            --reserved_[robot.location.value];
            --guests_[robot.location.value];
            Decide(robot);
            break;
    }
}

void World::Arrive(Robot& robot) {
    const Tick now = state_.tick;
    robot.location = robot.destination;
    robot.destination = EntityId{};
    if (robot.workspace.IsValid() && robot.location == robot.workspace) {
        robot.activity = RobotActivity::Working;
        // A late robot still leaves at the regular end of its shift.
        robot.nextActionAt = std::max(
            robot.shiftStartsAt + WorkplaceOf(robot).shift.lengthMinutes, now);
        const auto late = static_cast<std::uint32_t>(
            now > robot.shiftStartsAt ? now - robot.shiftStartsAt : 0);
        if (late > 0) {
            robot.performance -= kLatePerformance;
        }
        ScoreArrivalAtWork(robot);
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
    robot.credits -= std::min<std::int64_t>(rules->price, robot.credits);
    if (venue->room->kind == RoomKind::BlackMarket &&
        state_.random.UniformInt(1, 100) <= kBadPartPercent) {
        robot.wear = std::min(robot.wear + kBadPartWear, kMaxWear);
        Remember(robot, MemoryKind::GotBadPart, {}, robot.location);
    }

    // Stay a while, but leave by closing time and in time to rest.
    const Tick latestLeave = LatestHomeTick(robot) -
                             WalkingMinutes(robot.location, robot.apartment);
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
          .credits = rules->price,
          .others = others});
}

void World::Decide(Robot& robot) {
    const Tick now = state_.tick;
    const Tick latestHome = LatestHomeTick(robot);
    const std::uint32_t walkHome = WalkingMinutes(robot.location,
                                                  robot.apartment);

    std::int64_t bestScore =
        kHomeComfort + Relief(robot.needs, kResting, kHomePeriods);
    if (now + walkHome + kBedtimeWindow >= latestHome) {
        bestScore += kBedtimeUrge;
    }
    EntityId best = robot.apartment;

    // Keep enough for tonight's rent and anything already owed.
    const std::int64_t reserve = RentOf(robot) + robot.rentOwed;
    const auto here = rooms_.Find(robot.location);
    const RoomKind hereKind = here ? here->room->kind : RoomKind::Apartment;
    for (const RoomKind kind : VenueKinds()) {
        const VenueRules& rules = *FindVenueRules(kind);
        if (robot.credits - static_cast<std::int64_t>(rules.price) < reserve) {
            continue;
        }
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            const Room& room = *venue.room;
            // Leaving a venue means going somewhere else.
            if (room.id == robot.location ||
                reserved_[room.id.value] >= room.capacity) {
                continue;
            }
            const std::uint32_t walk = WalkingMinutes(robot.location, room.id);
            const Tick arrival = now + walk;
            const std::uint32_t arrivalMinute = MinuteOfDay(arrival);
            const Tick walkBack = WalkingMinutes(room.id, robot.apartment);
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
                (kWalkCostPerMinute * walk) -
                (kPriceCostPerCredit * rules.price) -
                (kind == hereKind ? kSameKindPenalty : 0) +
                (kind == RoomKind::Clinic ? robot.wear / kWearPerClinicPoint : 0) +
                CompanyAt(robot, room.id) +
                state_.random.UniformInt(0, kMaxWhim);
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
    const std::uint32_t walk = WalkingMinutes(robot.location, destination);
    robot.activity = RobotActivity::Walking;
    robot.destination = destination;
    robot.departedAt = state_.tick;
    robot.nextActionAt = state_.tick + walk;
    const auto room = rooms_.Find(destination);
    if (room && IsVenue(room->room->kind)) {
        ++reserved_[destination.value];
    }
    Emit({.tick = state_.tick,
          .kind = EventKind::Departed,
          .robot = robot.id,
          .room = robot.location,
          .otherRoom = destination,
          .minutes = walk});
}

void World::ScheduleNextCommute(Robot& robot) {
    if (!robot.workspace.IsValid()) {
        robot.commuteAt = kNoCommute;  // Out of work: every day is free.
        PlanAtHome(robot);
        return;
    }
    const Tick now = state_.tick;
    const Tick lead = WalkingMinutes(robot.apartment, robot.workspace) +
                      robot.punctualityMinutes +
                      state_.random.UniformInt(0, kMaxDailyExtraMinutes);
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
    if (IsDayOff(robot, now) && minute < kLastOutingMinute) {
        // Back home on a free day: out again after a break, but not before
        // the morning.
        const Tick afterBreak =
            now + state_.random.UniformInt(kMinHomeBreak, kMaxHomeBreak);
        const Tick morning = today + kOutingEarliestMinute +
                             state_.random.UniformInt(0, kOutingSpreadMinutes);
        outing = std::max(afterBreak, morning);
    } else if (minute >= kOutingEarliestMinute && minute < kLastOutingMinute) {
        // Home during the day or evening of a working day: after a break,
        // think again about going out.
        outing = now + state_.random.UniformInt(kMinEveningBreak,
                                                kMaxEveningBreak);
    } else {
        for (Tick day = 1; day <= kDaysPerWeek; ++day) {
            const Tick start = today + (day * kMinutesPerDay);
            if (IsDayOff(robot, start)) {
                outing = start + kOutingEarliestMinute +
                         state_.random.UniformInt(0, kOutingSpreadMinutes);
                break;
            }
        }
    }
    if (outing > now && outing + kMinOutingMinutes <= latest &&
        outing < robot.nextActionAt) {
        robot.nextActionAt = outing;
    }
}

bool World::IsDayOff(const Robot& robot, Tick tick) noexcept {
    return !robot.workspace.IsValid() ||
           ((robot.daysOff >> DayOfWeek(tick)) & 1U) != 0;
}

Tick World::NextShiftStart(const Robot& robot, Tick after) const {
    Tick start = StartOfDay(after) + WorkplaceOf(robot).shift.startMinute;
    // At most a week ahead: validation guarantees a working day.
    while (start <= after || IsDayOff(robot, start)) {
        start += kMinutesPerDay;
    }
    return start;
}

Tick World::MaxCommuteLead(const Robot& robot) {
    return WalkingMinutes(robot.apartment, robot.workspace) +
           robot.punctualityMinutes + kMaxDailyExtraMinutes;
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

std::int64_t World::RentOf(const Robot& robot) const {
    const auto apartment = rooms_.Find(robot.apartment);
    return apartment ? apartment->room->rent : 0;
}

void World::CollectRent() {
    for (Robot& robot : state_.robots) {
        const std::int64_t owedBefore = robot.rentOwed;
        const std::int64_t due = RentOf(robot) + robot.rentOwed;
        const std::int64_t paid = std::min(robot.credits, due);
        robot.credits -= paid;
        robot.rentOwed = due - paid;
        if (owedBefore == 0 && robot.rentOwed > 0) {
            Remember(robot, MemoryKind::FellBehindOnRent, {}, robot.apartment);
        } else if (owedBefore > 0 && robot.rentOwed == 0) {
            Remember(robot, MemoryKind::PaidOffDebt, {}, robot.apartment);
        }
        if (paid > 0) {
            Emit({.tick = state_.tick,
                  .kind = EventKind::RentPaid,
                  .robot = robot.id,
                  .room = robot.apartment,
                  .credits = paid});
        }
        if (robot.rentOwed > 0) {
            Emit({.tick = state_.tick,
                  .kind = EventKind::RentMissed,
                  .robot = robot.id,
                  .room = robot.apartment,
                  .credits = robot.rentOwed});
        }
    }
}

const Building& World::WorkplaceOf(const Robot& robot) const {
    const auto workspace = rooms_.Find(robot.workspace);
    if (!workspace) {
        throw std::logic_error("Robot has no workspace");
    }
    return *workspace->building;
}

void World::Emit(const SimEvent& event) const {
    if (sink_) {
        sink_(event);
    }
}

std::string World::DescribeRoom(EntityId room) const {
    const auto found = rooms_.Find(room);
    if (!found) {
        return "somewhere";
    }
    const Room& where = *found->room;
    const Building& building = *found->building;
    if (const VenueRules* rules = FindVenueRules(where.kind)) {
        return std::format("{} ({}, {})", where.name, rules->label,
                           building.name);
    }
    return std::format("{}, {}", where.name, building.name);
}

std::string World::Describe(const SimEvent& event) const {
    const std::string_view who = NameOf(event.robot);
    const std::string when = FormatTime(event.tick);
    const std::string room = DescribeRoom(event.room);
    switch (event.kind) {
        case EventKind::Departed:
            return std::format("{}  {} left {} for {} ({} min walk)", when, who,
                               room, DescribeRoom(event.otherRoom),
                               event.minutes);
        case EventKind::ClockedIn:
            return event.minutes == 0
                       ? std::format("{}  {} clocked in at {}", when, who, room)
                       : std::format("{}  {} clocked in at {}, {} min late",
                                     when, who, room, event.minutes);
        case EventKind::ShiftEnded:
            return std::format("{}  {} finished the shift at {}, earned {} cr",
                               when, who, room, event.credits);
        case EventKind::ArrivedAtVenue:
            return std::format(
                "{}  {} arrived at {}, paid {} cr, {} other{} there, staying "
                "{} min",
                when, who, room, event.credits, event.others,
                event.others == 1 ? "" : "s", event.minutes);
        case EventKind::ArrivedHome:
            return std::format("{}  {} got home to {}", when, who, room);
        case EventKind::RentPaid:
            return std::format("{}  {} paid {} cr rent for {}", when, who,
                               event.credits, room);
        case EventKind::RentMissed:
            return std::format("{}  {} is behind on rent, owes {} cr", when,
                               who, event.credits);
        case EventKind::Talked: {
            return std::format(
                "{}  {} {} {} about {} at {}", when, who,
                event.change < 0 ? "argued with" : "chatted with",
                NameOf(event.otherRobot), InterestName(event.topic), room);
        }
        case EventKind::Remembered: {
            return std::format(
                "{}  {} {}", when, who,
                MemoryText(event.memory, NameOf(event.otherRobot),
                           event.room.IsValid() ? room : std::string{}));
        }
        case EventKind::Scrapped:
            return std::format("{}  {} broke down for good and was scrapped",
                               when, who);
        case EventKind::Arrived:
            return std::format("{}  {} moved to the city, into {}", when, who,
                               room);
    }
    return std::format("{}  {}: unknown event", when, who);
}

std::expected<void, std::string> ValidateWorldState(const WorldState& state) {
    const auto fail = [](std::string message) {
        return std::unexpected(std::move(message));
    };
    const City& city = state.city;
    if (city.nodes.empty()) {
        return fail("The city has no street nodes");
    }
    for (std::size_t index = 0; index < city.nodes.size(); ++index) {
        if (city.nodes[index].id.value != index) {
            return fail(std::format("Street node {} has id {}", index,
                                    city.nodes[index].id.value));
        }
    }
    const auto nodeCount = city.nodes.size();
    for (const CityEdge& edge : city.edges) {
        if (edge.from.value >= nodeCount || edge.to.value >= nodeCount ||
            edge.lengthMeters == 0) {
            return fail(std::format("Invalid street {} -> {}",
                                    edge.from.value, edge.to.value));
        }
    }

    // Every id is unique across buildings, rooms and robots.
    std::unordered_set<std::uint64_t> ids;
    const auto claim = [&](EntityId id) {
        return id.IsValid() && id.value < state.ids.next &&
               ids.insert(id.value).second;
    };
    EntityId previous;
    for (const Building& building : city.buildings) {
        if (!claim(building.id) || building.id <= previous) {
            return fail(std::format("Building '{}' has an invalid, repeated "
                                    "or out-of-order id {}",
                                    building.name, building.id.value));
        }
        previous = building.id;
        if (building.entrance.value >= nodeCount || building.floors == 0) {
            return fail(std::format("Building '{}' has no valid entrance or "
                                    "floors",
                                    building.name));
        }
        if (building.kind == BuildingKind::Workplace &&
            (building.shift.startMinute >= kMinutesPerDay ||
             building.shift.lengthMinutes == 0 ||
             building.shift.lengthMinutes >= kMinutesPerDay)) {
            return fail(std::format("Workplace '{}' has an invalid shift",
                                    building.name));
        }
        EntityId previousRoom;
        for (const Room& room : building.rooms) {
            if (!claim(room.id) || room.id <= previousRoom) {
                return fail(std::format("Room '{}' in '{}' has an invalid, "
                                        "repeated or out-of-order id {}",
                                        room.name, building.name,
                                        room.id.value));
            }
            previousRoom = room.id;
            if (room.floor >= building.floors || room.capacity == 0) {
                return fail(std::format("Room '{}' in '{}' has an invalid "
                                        "floor or capacity",
                                        room.name, building.name));
            }
        }
    }

    RoutePlanner routes(city);
    if (!city.buildings.empty()) {
        const NodeId origin = city.buildings.front().entrance;
        for (const Building& building : city.buildings) {
            if (!routes.DistanceMeters(origin, building.entrance)) {
                return fail(std::format("Building '{}' cannot be reached",
                                        building.name));
            }
        }
    }

    const RoomIndex rooms(city);
    std::unordered_map<std::uint64_t, std::uint32_t> occupants;
    const auto roomOfKind = [&](EntityId id,
                                RoomKind kind) -> const Room* {
        const auto found = rooms.Find(id);
        return found && found->room->kind == kind ? found->room : nullptr;
    };
    previous = EntityId{};
    for (const Robot& robot : state.robots) {
        if (!claim(robot.id) || robot.id <= previous) {
            return fail(std::format("Robot '{}' has an invalid, repeated or "
                                    "out-of-order id {}",
                                    robot.name, robot.id.value));
        }
        previous = robot.id;
        // At least one working day a week.
        constexpr std::uint32_t kWholeWeek = (1U << kDaysPerWeek) - 1;
        if ((robot.daysOff & kWholeWeek) == kWholeWeek ||
            robot.daysOff > kWholeWeek) {
            return fail(std::format("Robot '{}' has invalid days off",
                                    robot.name));
        }
        const Room* apartment = roomOfKind(robot.apartment, RoomKind::Apartment);
        const Room* workspace = roomOfKind(robot.workspace, RoomKind::Workspace);
        if (apartment == nullptr ||
            (robot.workspace.IsValid() && workspace == nullptr) ||
            robot.jobLevel > kMaxJobLevel) {
            return fail(std::format("Robot '{}' has no valid apartment or "
                                    "workspace",
                                    robot.name));
        }
        if (++occupants[apartment->id.value] > apartment->capacity ||
            (workspace != nullptr &&
             ++occupants[workspace->id.value] > workspace->capacity)) {
            return fail(std::format("Robot '{}' exceeds the capacity of its "
                                    "apartment or workspace",
                                    robot.name));
        }
        const auto location = rooms.Find(robot.location);
        if (!location) {
            return fail(std::format("Robot '{}' is in a missing room",
                                    robot.name));
        }
        const bool consistent = [&] {
            switch (robot.activity) {
                case RobotActivity::Resting:
                    return robot.location == robot.apartment;
                case RobotActivity::Working:
                    return robot.workspace.IsValid() &&
                           robot.location == robot.workspace;
                case RobotActivity::Visiting:
                    return IsVenue(location->room->kind);
                case RobotActivity::Walking:
                    return rooms.Find(robot.destination).has_value() &&
                           robot.departedAt <= robot.nextActionAt;
            }
            return false;
        }();
        if (!consistent) {
            return fail(std::format("Robot '{}' is in a room that does not "
                                    "match what it is doing",
                                    robot.name));
        }
        const auto inRange = [](std::int32_t need) {
            return need >= 0 && need <= Needs::kMax;
        };
        if (!inRange(robot.needs.charge) || !inRange(robot.needs.condition) ||
            !inRange(robot.needs.social) || !inRange(robot.needs.fun) ||
            robot.credits < 0 || robot.rentOwed < 0 || robot.wear < 0 ||
            robot.wear > kMaxWear) {
            return fail(std::format("Robot '{}' has needs or credits out of "
                                    "range",
                                    robot.name));
        }
    }
    return ValidateSocial(state);
}

}  // namespace mechapunk::sim
