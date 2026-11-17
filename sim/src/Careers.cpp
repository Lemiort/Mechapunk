// Work and money stories: robots are promoted or fired by how they do at
// work, find new jobs when out of work, move up when they have saved, and
// are evicted when they fall too far behind on rent.
#include "mechapunk/sim/Careers.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::array<std::string_view, kMaxJobLevel + 1> kJobTitles{
    "junior", "regular", "senior", "lead"};
constexpr std::uint32_t kRaisePercentPerLevel = 15;
constexpr std::int32_t kFirstPromotion = 15;
constexpr std::int32_t kPromotionStep = 10;

// A shift earns 0 to 2 points of performance, less for a robot that turned
// up worn down or miserable.
constexpr std::uint32_t kBestShift = 2;
constexpr std::int32_t kWornDownPenalty = 3;
constexpr std::int32_t kMiserablePenalty = 1;
constexpr std::int32_t kFitCondition = 300;
constexpr std::int32_t kFitCharge = 150;
constexpr std::int32_t kContentNeed = 200;

// Chance in percent that a robot out of work is hired on a given night, if
// there is an opening.
constexpr std::uint32_t kHirePercent = 50;

// Owing this many nights of rent gets a robot evicted.
// Robots with this much saved sometimes move to a nicer, pricier apartment
// they can pay for this many nights from savings.
constexpr std::int64_t kMoveUpSavings = 500;
constexpr std::uint32_t kMoveUpPercent = 10;
constexpr std::int64_t kAffordNights = 12;
}  // namespace

std::string_view JobTitle(std::uint8_t level) noexcept {
    return kJobTitles.at(std::min<std::size_t>(level, kMaxJobLevel));
}

std::uint32_t WageAtLevel(std::uint32_t baseWage, std::uint8_t level) noexcept {
    return baseWage * (100 + (kRaisePercentPerLevel * level)) / 100;
}

std::int32_t PromotionTarget(std::uint8_t level) noexcept {
    return kFirstPromotion + (kPromotionStep * level);
}

std::uint32_t World::WageOf(const Robot& robot) const {
    return WageAtLevel(ShiftOf(robot).wage, robot.jobLevel);
}

Shift JobShift(const RoomRef& job, EntityId robot) noexcept {
    const VenueRules* rules = FindVenueRules(job.room->kind);
    if (rules == nullptr) {
        return job.building->shift;
    }
    const std::uint32_t slot = ShiftSlot(robot, *rules);
    return {.startMinute = rules->staff.shiftStarts.at(slot),
            .lengthMinutes = rules->staff.lengthMinutes,
            .wage = rules->staff.wage};
}

std::uint64_t JobKey(const Room& job, EntityId robot) noexcept {
    const VenueRules* rules = FindVenueRules(job.kind);
    return (job.id.value * 4) + (rules != nullptr ? ShiftSlot(robot, *rules) : 0);
}

Shift World::ShiftOf(const Robot& robot) const {
    const auto job = rooms_.Find(robot.workspace);
    if (!job) {
        throw std::logic_error("Robot has no job");
    }
    return JobShift(*job, robot.id);
}

std::unordered_map<std::uint64_t, std::uint32_t> World::StaffCounts() const {
    std::unordered_map<std::uint64_t, std::uint32_t> staff;
    for (const Robot& robot : state_.robots) {
        if (const auto job = rooms_.Find(robot.workspace)) {
            ++staff[JobKey(*job->room, robot.id)];
        }
    }
    return staff;
}

std::vector<EntityId> World::OpeningsFor(
    EntityId robot, const std::unordered_map<std::uint64_t, std::uint32_t>& staff) const {
    std::vector<EntityId> openings;
    const auto taken = [&](const Room& room) {
        const auto found = staff.find(JobKey(room, robot));
        return found != staff.end() ? found->second : 0U;
    };
    for (const RoomRef& workspace : rooms_.RoomsOfKind(RoomKind::Workspace)) {
        if (taken(*workspace.room) < workspace.room->capacity) {
            openings.push_back(workspace.room->id);
        }
    }
    for (const RoomKind kind : VenueKinds()) {
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            if (taken(*venue.room) < StaffPerShift(*venue.room)) {
                openings.push_back(venue.room->id);
            }
        }
    }
    return openings;
}

void World::ScoreArrivalAtWork(Robot& robot) {
    const Needs& needs = robot.needs;
    // An old robot cannot be in better condition than its wear allows; it
    // is worn down only when below half of that, as well as below the bar.
    const std::int32_t reachable = kMaxWear - robot.wear;
    const bool neglected =
        needs.condition < kFitCondition && needs.condition < reachable / 2;
    if (neglected || needs.charge < kFitCharge) {
        robot.performance -= kWornDownPenalty;
    }
    if (needs.fun < kContentNeed || needs.social < kContentNeed) {
        robot.performance -= kMiserablePenalty;
    }
}

void World::EndShift(Robot& robot) {
    const auto score =
        static_cast<std::int32_t>(state_.random.UniformInt(0, kBestShift));
    robot.performance = std::clamp(robot.performance + score, kFiringPerformance,
                                   PromotionTarget(robot.jobLevel));
    if (robot.performance >= PromotionTarget(robot.jobLevel) &&
        robot.jobLevel < kMaxJobLevel) {
        ++robot.jobLevel;
        robot.performance = 0;
        Remember(robot, MemoryKind::Promoted, {}, robot.workspace);
    } else if (robot.performance <= kFiringPerformance) {
        const EntityId lost = robot.workspace;
        robot.workspace = {};
        robot.jobLevel = 0;
        robot.performance = 0;
        robot.commuteAt = kNoCommute;
        Remember(robot, MemoryKind::LostJob, {}, lost);
    }
}

void World::ReviewJobs() {
    std::unordered_map<std::uint64_t, std::uint32_t> staff = StaffCounts();
    for (Robot& robot : state_.robots) {
        if (robot.workspace.IsValid() ||
            state_.random.UniformInt(1, 100) > kHirePercent) {
            continue;
        }
        const std::vector<EntityId> openings = OpeningsFor(robot.id, staff);
        if (openings.empty()) {
            continue;  // Nothing on the shifts it could work.
        }
        const EntityId job = openings.at(state_.random.UniformInt(
            0, static_cast<std::uint32_t>(openings.size() - 1)));
        ++staff[JobKey(*rooms_.Find(job)->room, robot.id)];
        robot.workspace = job;
        robot.jobLevel = 0;
        robot.performance = 0;
        Remember(robot, MemoryKind::FoundJob, {}, job);
        if (robot.activity == RobotActivity::Resting) {
            ScheduleNextCommute(robot);
        }
    }
}

void World::ReviewHomes() {
    std::unordered_set<std::uint64_t> taken;
    for (const Robot& robot : state_.robots) {
        taken.insert(robot.apartment.value);
    }
    const std::vector<RoomRef>& apartments = rooms_.RoomsOfKind(RoomKind::Apartment);
    // The free apartment that `better` prefers, if any.
    const auto pick = [&](const std::function<bool(const Room&, const Room&)>& better,
                          const std::function<bool(const Room&)>& allowed)
        -> std::optional<EntityId> {
        const Room* best = nullptr;
        for (const RoomRef& apartment : apartments) {
            const Room& room = *apartment.room;
            if (taken.contains(room.id.value) || !allowed(room)) {
                continue;
            }
            if (best == nullptr || better(room, *best)) {
                best = &room;
            }
        }
        return best != nullptr ? std::optional{best->id} : std::nullopt;
    };
    const auto move = [&](Robot& robot, EntityId to) {
        taken.erase(robot.apartment.value);
        taken.insert(to.value);
        robot.apartment = to;
        if (robot.activity == RobotActivity::Resting) {
            StartWalk(robot, to);  // Moving out tonight.
        }
    };

    for (Robot& robot : state_.robots) {
        // A robot on its way somewhere moves another night.
        if (robot.activity == RobotActivity::Walking) {
            continue;
        }
        const std::int64_t rent = RentOf(robot);
        const auto home = rooms_.Find(robot.apartment);
        // Owners behind on upkeep sell instead; see TradeProperty.
        const bool owns = home && home->room->owner == robot.id;
        if (!owns && rent > 0 && robot.rentOwed >= kEvictionNights * rent) {
            // Out to the cheapest place there is; the debt is written off.
            const auto cheapest = pick(
                [](const Room& a, const Room& b) { return a.rent < b.rent; },
                [](const Room&) { return true; });
            if (cheapest) {
                const EntityId old = robot.apartment;
                state_.today.writtenOff += robot.rentOwed;
                robot.rentOwed = 0;
                move(robot, *cheapest);
                Remember(robot, MemoryKind::Evicted, {}, old);
            }
            continue;
        }
        const std::int64_t means = robot.credits + robot.savings;
        if (means < kMoveUpSavings ||
            state_.random.UniformInt(1, 100) > kMoveUpPercent) {
            continue;
        }
        // The priciest apartment it can afford for a while. An owner sells
        // its home to the trust and buys the new one.
        const std::int64_t homeRent = home ? home->room->rent : 0;
        const std::int64_t sale = owns ? PriceOf(*home->room) * kSellPercent / 100 : 0;
        const auto nicer = pick(
            [](const Room& a, const Room& b) { return a.rent > b.rent; },
            [&](const Room& room) {
                if (room.rent <= homeRent) {
                    return false;
                }
                return owns ? PriceOf(room) + (kAffordNights * room.rent) <= means + sale
                            : static_cast<std::int64_t>(room.rent) * kAffordNights <= means;
            });
        if (nicer) {
            if (owns) {
                const EntityId old = robot.apartment;
                Transfer(old, housing_, sale);
                const auto room = rooms_.Find(*nicer);
                if (room->room->owner != robot.id) {
                    Withdraw(robot, PriceOf(*room->room) - robot.credits);
                    Transfer(*nicer, robot.id, PriceOf(*room->room));
                }
            }
            move(robot, *nicer);
            Remember(robot, MemoryKind::MovedUp, {}, *nicer);
        }
    }
}

}  // namespace mechapunk::sim
