#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk::sim {

enum class RobotActivity : std::uint8_t {
    Resting,   // In its apartment.
    Walking,   // On the way from `location` to `destination`.
    Working,   // In its workspace.
    Visiting,  // In a venue.
};

struct Robot {
    EntityId id;
    std::string name;
    EntityId apartment;  // Room the robot lives in.
    EntityId workspace;  // Room the robot works in; none while out of work.
    RobotActivity activity{RobotActivity::Resting};
    // Room the robot is in, or has just left while walking.
    EntityId location;
    // Room the robot is walking to; none unless walking.
    EntityId destination;
    Tick departedAt{};
    // Tick of the next scheduled change: leaving, arriving, ending a shift
    // or ending a visit.
    Tick nextActionAt{};
    // Start of the shift the robot is heading to, working, or last worked.
    Tick shiftStartsAt{};
    // Personal habit: minutes the robot aims to arrive before its shift.
    std::uint32_t punctualityMinutes{};
    // Bit d set: no shift starts on day of the week d (0 = Monday).
    std::uint8_t daysOff{};
    // When the robot leaves home for its next shift; planned on getting home.
    Tick commuteAt{};
    Needs needs;
    std::int64_t credits{};
    // Rent not yet paid; collected from later credits first.
    std::int64_t rentOwed{};
    // Bit n set: likes Interest n.
    std::uint8_t interests{};
    // Robots it has talked to, sorted by their id.
    std::vector<Bond> bonds;
    // Moments it remembers, oldest first.
    std::vector<Memory> memories;
    // 0 (junior) to kMaxJobLevel (lead); higher levels earn more.
    std::uint8_t jobLevel{};
    // How the robot has done at work since it was hired or last promoted.
    std::int32_t performance{};
};

// Everything that defines a running world. This is exactly what a save
// contains; caches such as routes and room lookups are rebuilt after
// loading. Robots are kept sorted by id.
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

// A walk in progress, between two rooms.
struct Walk {
    EntityId from;
    EntityId to;
    Tick departedAt{};
    Tick arrivesAt{};
};

enum class EventKind : std::uint8_t {
    Departed,        // room: left; otherRoom: heading to; minutes: walk.
    ClockedIn,       // room: workspace; minutes: late by.
    ShiftEnded,      // room: workspace; credits: wage earned.
    ArrivedAtVenue,  // room: venue; credits: price; minutes: planned stay.
    ArrivedHome,     // room: apartment.
    RentPaid,        // room: apartment; credits: paid.
    RentMissed,      // room: apartment; credits: now owed in total.
    // room: where; otherRobot: who with; topic; change: affinity, an
    // argument when negative. Each of the two robots gets one.
    Talked,
    // memory: what; otherRobot and room: who with and where, if anyone.
    Remembered,
};

// Something that happened during a step, for logs and observers. Events are
// not part of the saved state.
struct SimEvent {
    Tick tick{};
    EventKind kind{};
    EntityId robot;
    EntityId room;
    EntityId otherRoom;
    std::uint32_t minutes{};
    std::int64_t credits{};
    // Other robots already in the room on arrival.
    std::uint32_t others{};
    EntityId otherRobot;
    Interest topic{};
    std::int32_t change{};
    MemoryKind memory{};
};

// Deterministic simulation of the city. The same WorldState always produces
// the same sequence of states and events, on every platform: all state is
// integral and randomness comes from the saved Pcg32.
//
// Each step is one game minute. Robots work fixed shifts; after work they
// weigh their needs, credits, walking time, opening hours and who is there
// to pick a venue or go home, and they head home in time to rest before the
// next shift. Robots sharing a venue or workspace talk, which makes friends
// and rivals over time.
class World {
public:
    using EventSink = std::function<void(const SimEvent&)>;

    // Minutes at home a robot keeps free before leaving for its next shift.
    static constexpr Tick kRestMinutes = 6 * kMinutesPerHour;
    // Every night ends at home by this minute of the day, 03:30.
    static constexpr Tick kBedtimeMinute = (3 * kMinutesPerHour) + 30;

    // Builds a new city and population from a seed.
    [[nodiscard]] static World Generate(const WorldParams& params);
    // Validates a loaded or hand-made state and rebuilds derived data.
    [[nodiscard]] static std::expected<World, std::string> FromState(
        WorldState state);

    // Derived lookups point into the state, so a World can be moved but not
    // copied.
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World() = default;

    [[nodiscard]] const WorldState& State() const noexcept { return state_; }
    [[nodiscard]] Tick CurrentTick() const noexcept { return state_.tick; }
    [[nodiscard]] const Robot* FindRobot(EntityId id) const noexcept;
    [[nodiscard]] const RoomIndex& Rooms() const noexcept { return rooms_; }
    // Robots in a venue right now, not counting those still on their way.
    [[nodiscard]] std::uint32_t GuestsIn(EntityId room) const;

    // The walk a robot is on; nothing unless it is walking.
    [[nodiscard]] static std::optional<Walk> WalkOf(const Robot& robot);
    // Walking time between two rooms, in whole minutes, including stairs.
    [[nodiscard]] std::uint32_t WalkingMinutes(EntityId fromRoom,
                                               EntityId toRoom);

    // Receives every event produced by later steps.
    void SetEventSink(EventSink sink) { sink_ = std::move(sink); }

    // Advances the simulation by one minute.
    void Step();
    void RunFor(Tick minutes);

    // "Day 2 19:41  KX-417 Vera arrived at Neon Wok (noodle diner, Hab Block
    // 3), paid 10 cr, 4 others there"
    [[nodiscard]] std::string Describe(const SimEvent& event) const;
    // "Neon Wok (noodle diner, Hab Block 3)" or "Apartment 4-02, Hab Block 3"
    [[nodiscard]] std::string DescribeRoom(EntityId room) const;

private:
    explicit World(WorldState state);

    void UpdateNeeds();
    void Act(Robot& robot);
    void Arrive(Robot& robot);
    void Decide(Robot& robot);
    void StartWalk(Robot& robot, EntityId destination);
    void ScheduleNextCommute(Robot& robot);
    [[nodiscard]] Tick LatestHomeTick(const Robot& robot);
    void CollectRent();
    // Work and money, in Careers.cpp.
    [[nodiscard]] std::uint32_t WageOf(const Robot& robot) const;
    // Scores the shift just worked; promotes or fires.
    void EndShift(Robot& robot);
    // At night: robots out of work may be hired.
    void ReviewJobs();
    // At night: evictions, and robots with savings moving to nicer places.
    void ReviewHomes();
    // Relationships, in Social.cpp.
    void Socialize();
    void Talk(Robot& first, Robot& second);
    void ChangeBond(Robot& robot, EntityId other, std::int32_t change);
    void UpdateBondLevel(Robot& robot, Bond& bond, EntityId room);
    void Remember(Robot& robot, MemoryKind kind, EntityId other, EntityId room);
    void FadeBonds();
    // How much friends at a venue draw the robot there, rivals keep it away.
    [[nodiscard]] std::int64_t CompanyAt(const Robot& robot,
                                         EntityId venue) const;
    // Sets when a robot at home next acts: an outing on a free day, or
    // leaving for work.
    void PlanAtHome(Robot& robot);
    [[nodiscard]] static bool IsDayOff(const Robot& robot, Tick tick) noexcept;
    // First shift start after `after` that is not on a day off.
    [[nodiscard]] Tick NextShiftStart(const Robot& robot, Tick after) const;
    // Longest time the robot may need between leaving home and its shift.
    [[nodiscard]] Tick MaxCommuteLead(const Robot& robot);
    [[nodiscard]] std::int64_t RentOf(const Robot& robot) const;
    [[nodiscard]] const Building& WorkplaceOf(const Robot& robot) const;
    void Emit(const SimEvent& event) const;

    WorldState state_;
    RoutePlanner routes_;
    RoomIndex rooms_;
    // Robots in or heading to each venue room, for capacity checks.
    std::unordered_map<std::uint64_t, std::uint32_t> reserved_;
    // Robots inside each venue room.
    std::unordered_map<std::uint64_t, std::uint32_t> guests_;
    EventSink sink_;
};

// Checks references and invariants of a state; describes the first problem.
[[nodiscard]] std::expected<void, std::string> ValidateWorldState(
    const WorldState& state);

}  // namespace mechapunk::sim
