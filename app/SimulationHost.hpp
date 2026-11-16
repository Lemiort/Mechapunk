#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "SimulationThread.hpp"
#include "SnapshotBuffer.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk {

// What the renderer needs to know about one robot after a step.
struct RobotFrame {
    sim::EntityId id;
    sim::RobotActivity activity{};
    // Room the robot is in, or has just left while walking.
    sim::EntityId location;
    // Room the robot is walking to; none unless walking.
    sim::EntityId destination;
    // Buildings walked between; set while walking.
    sim::EntityId from;
    sim::EntityId to;
    sim::Tick departedAt{};
    sim::Tick arrivesAt{};
    sim::Tick nextActionAt{};
    sim::Tick shiftStartsAt{};
    sim::Tick commuteAt{};
    sim::Needs needs;
    std::int64_t credits{};
    std::int64_t rentOwed{};
};

// What does not change while the game runs.
struct RobotInfo {
    std::string name;
    sim::EntityId apartment;
    sim::EntityId workspace;
    std::uint8_t daysOff{};
    std::uint8_t interests{};
};

// Who a robot knows and what it remembers.
struct RobotSocial {
    std::vector<sim::Bond> bonds;
    std::vector<sim::Memory> memories;
};

// Immutable copy of the world's visible state, published after every step.
struct WorldFrame {
    sim::Tick tick{};
    std::vector<RobotFrame> robots;
    std::uint32_t resting{};
    std::uint32_t working{};
    std::uint32_t visiting{};
    std::uint32_t walking{};
    // In the order of `robots`. Bonds and memories only change every ten
    // minutes, so frames in between share one copy.
    std::shared_ptr<const std::vector<RobotSocial>> social;
};

// Runs a World on a SimulationThread. The world belongs to the worker while
// the thread runs; the render thread only sees published WorldFrames.
class SimulationHost {
public:
    // Real seconds per simulated minute at speed 1.
    static constexpr double kSecondsPerMinute = 1.0;

    explicit SimulationHost(sim::World world);
    ~SimulationHost();
    SimulationHost(const SimulationHost&) = delete;
    SimulationHost& operator=(const SimulationHost&) = delete;
    SimulationHost(SimulationHost&&) = delete;
    SimulationHost& operator=(SimulationHost&&) = delete;

    // The city never changes after generation, so the renderer keeps a copy.
    [[nodiscard]] const sim::City& City() const noexcept { return city_; }
    // Seed the world was generated from; stable across saves.
    [[nodiscard]] std::uint64_t Seed() const noexcept { return seed_; }

    [[nodiscard]] const RobotInfo* FindRobotInfo(sim::EntityId robot) const;
    // Robots sorted by id, for lookups into WorldFrame::robots.
    [[nodiscard]] static const RobotFrame* FindRobot(const WorldFrame& frame,
                                                     sim::EntityId robot);
    [[nodiscard]] static const RobotSocial* FindSocial(const WorldFrame& frame,
                                                       sim::EntityId robot);
    // The robot's latest events, newest last, as "Day 2 19:41  text".
    [[nodiscard]] std::vector<std::string> RecentEvents(sim::EntityId robot,
                                                        std::size_t count) const;

    [[nodiscard]] std::shared_ptr<const WorldFrame> LatestFrame() const;
    // Current simulation time in minutes, with a fractional part, for smooth
    // movement between steps.
    [[nodiscard]] double RenderTick() const;

    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const;
    void RequestStep();
    // Simulated minutes per real second.
    void SetSpeed(double minutesPerSecond);
    [[nodiscard]] double Speed() const noexcept { return speed_; }

    // Saves the world between two steps; the simulation pauses for the
    // moment it takes, then continues as before.
    [[nodiscard]] std::expected<void, std::string> Save(
        const std::filesystem::path& path);

    // Non-null once a simulation step has thrown.
    [[nodiscard]] std::exception_ptr Failure() const;

    // Stops the worker for good; the world can be saved afterwards.
    void Stop();

private:
    void Step();
    void Publish();
    void Record(const sim::SimEvent& event);

    sim::World world_;
    sim::City city_;
    std::uint64_t seed_;
    sim::Tick baseTick_;
    double speed_{1.0};
    SnapshotBuffer<WorldFrame> frames_;
    std::unordered_map<std::uint64_t, RobotInfo> robotInfo_;
    std::shared_ptr<const std::vector<RobotSocial>> social_;
    // Written by the worker in its event sink, read by the render thread.
    mutable std::mutex logMutex_;
    std::unordered_map<std::uint64_t, std::deque<std::string>> log_;
    // Declared last: joined before the world it steps is destroyed.
    std::unique_ptr<SimulationThread> thread_;
};

}  // namespace mechapunk
