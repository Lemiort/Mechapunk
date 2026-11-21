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
#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/Gangs.hpp"
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
    std::int64_t savings{};
    sim::EntityId bank;
    // Robots move house and change jobs, so these come with every frame.
    sim::EntityId apartment;
    sim::EntityId workspace;  // None while out of work.
    std::uint8_t jobLevel{};
    std::int32_t performance{};
    std::uint32_t age{};
    std::int32_t wear{};
    // A metro ride on this walk between stations (indices into
    // City::stations): on the platform from boardsAt, on the train until
    // alightsAt. Zero when on foot.
    std::uint32_t rideFrom{};
    std::uint32_t rideTo{};
    sim::Tick boardsAt{};
    sim::Tick alightsAt{};
    std::uint32_t gangColor{};  // 0 if no gang; shared by frames until population changes.
    sim::EntityId gang;  // Gang the robot belongs to, none if zero.
    std::uint32_t arrests{};
};
struct RobotInfo {
    std::string name;
    std::uint8_t daysOff{};
    std::uint8_t interests{};
    // Scrapped, but still remembered by someone.
    bool departed{false};
};
using RobotDirectory = std::unordered_map<std::uint64_t, RobotInfo>;

// Who a robot knows and what it remembers.
struct RobotSocial {
    std::vector<sim::Bond> bonds;
    std::vector<sim::Memory> memories;
};

// Money and ownership. Organizations, loans and the central bank's log are
// copied every ten minutes; statistics and owners only change at midnight,
// so frames of a day share one copy.
struct EconomyFrame {
    std::vector<sim::Organization> organizations;  // Sorted by id.
    std::vector<sim::Loan> loans;
    std::vector<sim::Emission> emissions;  // Oldest first.
    std::int64_t created{};
    std::int64_t destroyed{};
    std::int64_t supplyMarket{};
    sim::DayCounters today;
    std::shared_ptr<const std::vector<sim::DailyStats>> history;
    // Owner of every room, by room id.
    std::shared_ptr<const std::unordered_map<std::uint64_t, sim::EntityId>> owners;

    [[nodiscard]] const sim::Organization* FindOrganization(sim::EntityId id) const;
    [[nodiscard]] const sim::Loan* LoanOf(sim::EntityId borrower) const;
    [[nodiscard]] sim::EntityId OwnerOf(sim::EntityId room) const;
};

// Immutable copy of the world's visible state, published after every step.
struct WorldFrame {
    sim::Tick tick{};
    // The weather's rain, 0 (dry) to sim::kMaxRain.
    std::uint32_t rain{};
    std::vector<RobotFrame> robots;
    std::uint32_t resting{};
    std::uint32_t working{};
    std::uint32_t visiting{};
    std::uint32_t walking{};
    // In the order of `robots`. Bonds and memories only change every ten
    // minutes, so frames in between share one copy.
    std::shared_ptr<const std::vector<RobotSocial>> social;
    // Every living robot and every remembered scrapped one; shared by frames
    // until the population changes.
    std::shared_ptr<const RobotDirectory> directory;
    std::shared_ptr<const EconomyFrame> economy;
    // The city as it is now, shared by frames until a room opens, closes or
    // changes; cityVersion counts those changes since the world was loaded.
    std::uint64_t cityVersion{};
    std::shared_ptr<const sim::City> city;
    // Gangs, published alongside economy data every ten minutes.
    std::shared_ptr<const std::vector<sim::Gang>> gangs;
    // The city's saved story, oldest first (see sim::ChronicleEntry). Frames
    // share one copy until a notable event adds an entry; Publish recopies
    // only when the list changed.
    std::shared_ptr<const std::vector<sim::ChronicleEntry>> chronicle;
};

// Runs a World on a SimulationThread. The world belongs to the worker while
// the thread runs; the render thread only sees published WorldFrames.
class SimulationHost {
public:
    // Real seconds per simulated minute at speed 1: real time.
    static constexpr double kSecondsPerMinute = 60.0;

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

    [[nodiscard]] static const RobotInfo* FindRobotInfo(const WorldFrame& frame,
                                                         sim::EntityId robot);
    // Robots sorted by id, for lookups into WorldFrame::robots.
    [[nodiscard]] static const RobotFrame* FindRobot(const WorldFrame& frame,
                                                     sim::EntityId robot);
    [[nodiscard]] static const RobotSocial* FindSocial(const WorldFrame& frame,
                                                       sim::EntityId robot);
    // A robot's or an organization's name.
    [[nodiscard]] static std::string AccountName(const WorldFrame& frame, sim::EntityId account);
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
    // How many times faster than real time the city runs.
    void SetSpeed(double timeScale);
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
    std::shared_ptr<const std::vector<RobotSocial>> social_;
    std::shared_ptr<const RobotDirectory> directory_;
    std::uint64_t directoryVersion_{};
    std::shared_ptr<const EconomyFrame> economy_;
    std::shared_ptr<const std::vector<sim::DailyStats>> history_;
    std::shared_ptr<const std::vector<sim::Gang>> gangs_;
    std::shared_ptr<const std::vector<sim::ChronicleEntry>> chronicle_;
    // Size and newest tick of the chronicle chronicle_ was copied from: an
    // entry is appended only by a notable event, and the oldest leaves only
    // past kChronicleCap, so that pair changes exactly when the list does.
    std::size_t chronicleSize_{};
    sim::Tick chronicleLastTick_{};
    std::shared_ptr<const std::unordered_map<std::uint64_t, sim::EntityId>> owners_;
    sim::Tick economyDay_{};
    std::shared_ptr<const sim::City> cityFrame_;
    std::uint64_t cityVersion_{};
    // Written by the worker in its event sink, read by the render thread.
    mutable std::mutex logMutex_;
    std::unordered_map<std::uint64_t, std::deque<std::string>> log_;
    // Declared last: joined before the world it steps is destroyed.
    std::unique_ptr<SimulationThread> thread_;
};

}  // namespace mechapunk
