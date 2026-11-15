#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Random.hpp"

namespace mechapunk::sim {

enum class RobotActivity : std::uint8_t {
    AtHome,
    CommutingToWork,
    Working,
    CommutingHome,
};

struct Robot {
    EntityId id;
    std::string name;
    EntityId home;
    EntityId workplace;
    RobotActivity activity{RobotActivity::AtHome};
    // Tick of the next scheduled change: leaving, arriving or ending a shift.
    Tick nextActionAt{};
    // Start of the shift the robot is heading to or working.
    Tick shiftStartsAt{};
    // Personal habit: minutes the robot aims to arrive before its shift.
    std::uint32_t punctualityMinutes{};
};

// Everything that defines a running world. This is exactly what a save
// contains; caches such as routes are rebuilt after loading. Robots are kept
// sorted by id.
struct WorldState {
    Tick tick{};
    std::uint64_t seed{};
    EntityIdAllocator ids;
    Pcg32 random;
    City city;
    std::vector<Robot> robots;
};

struct WorldParams {
    std::uint64_t seed{1};
    std::uint32_t robots{100};
};

// A walk in progress, derived from a robot's state; not saved separately.
struct Commute {
    EntityId from;
    EntityId to;
    Tick departedAt{};
    Tick arrivesAt{};
};

enum class EventKind : std::uint8_t {
    LeftHome,
    ArrivedAtWork,
    LeftWork,
    ArrivedHome,
};

// Something that happened during a step, for logs and observers. Events are
// not part of the saved state.
struct SimEvent {
    Tick tick{};
    EventKind kind{};
    EntityId robot;
    // Building the robot left or arrived at.
    EntityId place;
    // Destination when leaving; the place it came from when arriving.
    EntityId otherPlace;
    // Walking time when leaving; minutes after shift start when arriving
    // at work late (0 when on time).
    std::uint32_t minutes{};
};

// Deterministic simulation of the city. The same WorldState always produces
// the same sequence of states and events, on every platform: all state is
// integral and randomness comes from the saved Pcg32.
class World {
public:
    using EventSink = std::function<void(const SimEvent&)>;

    // Builds a new city and population from a seed.
    [[nodiscard]] static World Generate(const WorldParams& params);
    // Validates a loaded or hand-made state and rebuilds derived data.
    [[nodiscard]] static std::expected<World, std::string> FromState(
        WorldState state);

    [[nodiscard]] const WorldState& State() const noexcept { return state_; }
    [[nodiscard]] Tick CurrentTick() const noexcept { return state_.tick; }
    [[nodiscard]] const Robot* FindRobot(EntityId id) const noexcept;

    // The walk a commuting robot is on; nothing for robots at home or work.
    [[nodiscard]] std::optional<Commute> CommuteOf(const Robot& robot);
    // Walking time between two buildings' entrances, in whole minutes.
    [[nodiscard]] std::uint32_t WalkingMinutes(EntityId from, EntityId to);

    // Receives every event produced by later steps.
    void SetEventSink(EventSink sink) { sink_ = std::move(sink); }

    // Advances the simulation by one minute.
    void Step();
    void RunFor(Tick minutes);

    // "Day 2 07:41  KX-417 Vera left Hab Block 3 for Foundry 1 (19 min walk)"
    [[nodiscard]] std::string Describe(const SimEvent& event) const;

private:
    explicit World(WorldState state);

    void UpdateRobot(Robot& robot);
    void ScheduleNextCommute(Robot& robot);
    void Emit(const SimEvent& event) const;

    WorldState state_;
    RoutePlanner routes_;
    EventSink sink_;
};

// Checks references and invariants of a state; describes the first problem.
[[nodiscard]] std::expected<void, std::string> ValidateWorldState(
    const WorldState& state);

}  // namespace mechapunk::sim
