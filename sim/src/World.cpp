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

// Walking pace of 1.25 m/s.
constexpr std::uint32_t kWalkingMetersPerMinute = 75;

// Floors climbed per minute of stairs or elevator.
constexpr std::uint32_t kFloorsPerMinute = 3;

constexpr NeedEffects kWalking{.charge = -8, .condition = -1, .social = -5,
                               .fun = -3};

constexpr NeedEffects kWorking{.charge = -12, .condition = -2, .social = -4,
                               .fun = -4};

constexpr NeedEffects kVisiting{.charge = -3, .condition = -1, .social = -2,
                                .fun = -1};

void Apply(Needs& needs, const NeedEffects& effects) {
    const auto add = [](std::int32_t& need, std::int32_t delta) {
        need = std::clamp(need + delta, 0, Needs::kMax);
    };
    add(needs.charge, effects.charge);
    add(needs.condition, effects.condition);
    add(needs.social, effects.social);
    add(needs.fun, effects.fun);
}

}  // namespace

World::World(WorldState state)
    : state_(std::move(state)),
      routes_(state_.city),
      rooms_(state_.city),
      metro_(state_.city) {
    IndexEconomy();
    CountResidents();
    for (const Robot& robot : state_.robots) {
        if (robot.activity == RobotActivity::Visiting) {
            ++reserved_[robot.location.value];
            ++guests_[robot.location.value];
        } else if (robot.activity == RobotActivity::Walking) {
            if (IsGuestTrip(robot, robot.destination)) {
                ++reserved_[robot.destination.value];
            }
        }
    }
}

std::expected<World, std::string> World::FromState(WorldState state) {
    if (auto valid = ValidateWorldState(state); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    return World(std::move(state));
}

bool World::IsGuestTrip(const Robot& robot, EntityId destination) const {
    const auto room = rooms_.Find(destination);
    return room && IsVenue(room->room->kind) && destination != robot.workspace &&
           destination != robot.patrol;
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

void World::Step() {
    ++state_.tick;
    if (state_.tick >= state_.weather.changesAt) {
        state_.weather = NextWeather(state_.weather, state_.seed, state_.tick);
        Emit({.tick = state_.tick,
              .kind = EventKind::WeatherChanged,
              .minutes = static_cast<std::uint32_t>(state_.weather.changesAt -
                                                    state_.tick),
              .change = static_cast<std::int32_t>(state_.weather.rain)});
    }
    if (MinuteOfDay(state_.tick) == 0) {
        CollectRent();
        AgeRobots();
        ReviewHomes();
        ReviewJobs();
        ReviewGangs();
        FadeBonds();
    }
    if (state_.tick % kNeedPeriod == 0) {
        UpdateNeeds();
        Socialize();
    }
    // Last, once tonight's new memories have pushed out old ones.
    if (MinuteOfDay(state_.tick) == 0) {
        ForgetDeparted();
        TradeProperty();
        SettleEconomy();
        ReviewBanks();
        // After the settlement: the city raises a building when homes or
        // jobs have run short, and its sites pay supplies and grow
        // (Construction.cpp).
        ReviewStreets();
        ReviewTransit();
        ReviewMetroAuthority();
        ReviewConstruction();
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
                        robot.wear = std::max(robot.wear - static_cast<std::int32_t>(state_.settings.behaviour.clinicWearRepair), 0);
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

void World::Emit(const SimEvent& event) {
    if (sink_) {
        sink_(event);
    }
    Chronicle(event);
}

}  // namespace mechapunk::sim
