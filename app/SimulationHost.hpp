#pragma once

#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
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
    // Set while commuting.
    sim::EntityId from;
    sim::EntityId to;
    sim::Tick departedAt{};
    sim::Tick arrivesAt{};
};

// Immutable copy of the world's visible state, published after every step.
struct WorldFrame {
    sim::Tick tick{};
    std::vector<RobotFrame> robots;
    std::uint32_t atHome{};
    std::uint32_t working{};
    std::uint32_t commuting{};
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

    sim::World world_;
    sim::City city_;
    std::uint64_t seed_;
    sim::Tick baseTick_;
    double speed_{1.0};
    SnapshotBuffer<WorldFrame> frames_;
    // Declared last: joined before the world it steps is destroyed.
    std::unique_ptr<SimulationThread> thread_;
};

}  // namespace mechapunk
