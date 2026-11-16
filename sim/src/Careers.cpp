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
constexpr std::int64_t kEvictionNights = 5;
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
    return WageAtLevel(WorkplaceOf(robot).shift.wage, robot.jobLevel);
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
    std::unordered_map<std::uint64_t, std::uint32_t> staff;
    for (const Robot& robot : state_.robots) {
        if (robot.workspace.IsValid()) {
            ++staff[robot.workspace.value];
        }
    }
    std::vector<EntityId> openings;
    for (Robot& robot : state_.robots) {
        if (robot.workspace.IsValid() ||
            state_.random.UniformInt(1, 100) > kHirePercent) {
            continue;
        }
        openings.clear();
        for (const RoomRef& workspace : rooms_.RoomsOfKind(RoomKind::Workspace)) {
            if (staff[workspace.room->id.value] < workspace.room->capacity) {
                openings.push_back(workspace.room->id);
            }
        }
        if (openings.empty()) {
            return;  // The city is fully staffed.
        }
        const EntityId job = openings.at(state_.random.UniformInt(
            0, static_cast<std::uint32_t>(openings.size() - 1)));
        ++staff[job.value];
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
        if (rent > 0 && robot.rentOwed >= kEvictionNights * rent) {
            // Out to the cheapest place there is; the debt is written off.
            const auto cheapest = pick(
                [](const Room& a, const Room& b) { return a.rent < b.rent; },
                [](const Room&) { return true; });
            if (cheapest) {
                const EntityId old = robot.apartment;
                robot.rentOwed = 0;
                move(robot, *cheapest);
                Remember(robot, MemoryKind::Evicted, {}, old);
            }
            continue;
        }
        if (robot.credits < kMoveUpSavings ||
            state_.random.UniformInt(1, 100) > kMoveUpPercent) {
            continue;
        }
        // The priciest apartment it can afford for a while.
        const auto nicer = pick(
            [](const Room& a, const Room& b) { return a.rent > b.rent; },
            [&](const Room& room) {
                return room.rent > rent &&
                       static_cast<std::int64_t>(room.rent) * kAffordNights <=
                           robot.credits;
            });
        if (nicer) {
            move(robot, *nicer);
            Remember(robot, MemoryKind::MovedUp, {}, *nicer);
        }
    }
}

}  // namespace mechapunk::sim
