#include "SimulationHost.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "FixedStepClock.hpp"
#include "SimulationThread.hpp"
#include "mechapunk/sim/Save.hpp"

namespace mechapunk {

namespace {
// High enough that fast-forward is never limited by how often the worker
// wakes (Windows timers can oversleep by ~15 ms).
constexpr std::uint32_t kMaxStepsPerWake = 4000;
constexpr auto kPauseTimeout = std::chrono::seconds(2);
constexpr std::size_t kEventsPerRobot = 40;
}  // namespace

SimulationHost::SimulationHost(sim::World world)
    : world_(std::move(world)),
      city_(world_.State().city),
      seed_(world_.State().seed),
      baseTick_(world_.CurrentTick()) {
    // Runs on the worker inside World::Step, where reading the world is safe.
    world_.SetEventSink([this](const sim::SimEvent& event) { Record(event); });
    Publish();
    thread_ = std::make_unique<SimulationThread>(
        FixedStepClockSettings{.stepSeconds = kSecondsPerMinute,
                               .maxStepsPerAdvance = kMaxStepsPerWake},
        [this](const SimulationStep&) { Step(); });
}

SimulationHost::~SimulationHost() { Stop(); }

const RobotInfo* SimulationHost::FindRobotInfo(const WorldFrame& frame,
                                               sim::EntityId robot) {
    if (frame.directory == nullptr) {
        return nullptr;
    }
    const auto found = frame.directory->find(robot.value);
    return found != frame.directory->end() ? &found->second : nullptr;
}

const RobotFrame* SimulationHost::FindRobot(const WorldFrame& frame,
                                            sim::EntityId robot) {
    const auto found = std::ranges::lower_bound(frame.robots, robot,
                                                std::less{}, &RobotFrame::id);
    return found != frame.robots.end() && found->id == robot ? &*found
                                                             : nullptr;
}

const RobotSocial* SimulationHost::FindSocial(const WorldFrame& frame,
                                             sim::EntityId robot) {
    const RobotFrame* found = FindRobot(frame, robot);
    if (found == nullptr || frame.social == nullptr) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(found - frame.robots.data());
    return index < frame.social->size() ? &(*frame.social)[index] : nullptr;
}

std::vector<std::string> SimulationHost::RecentEvents(sim::EntityId robot,
                                                      std::size_t count) const {
    const std::scoped_lock lock(logMutex_);
    const auto found = log_.find(robot.value);
    if (found == log_.end()) {
        return {};
    }
    const std::deque<std::string>& events = found->second;
    const std::size_t first = events.size() > count ? events.size() - count : 0;
    return {events.begin() + static_cast<std::ptrdiff_t>(first), events.end()};
}

void SimulationHost::Record(const sim::SimEvent& event) {
    std::string text = world_.Describe(event);
    // The inspector already shows whose history this is.
    const std::string_view who = world_.NameOf(event.robot);
    if (const auto name = text.find(who); name != std::string::npos) {
        text.erase(name, who.size() + 1);
    }
    const std::scoped_lock lock(logMutex_);
    std::deque<std::string>& events = log_[event.robot.value];
    events.push_back(std::move(text));
    if (events.size() > kEventsPerRobot) {
        events.pop_front();
    }
}

std::shared_ptr<const WorldFrame> SimulationHost::LatestFrame() const {
    const auto sample = frames_.SampleAt(0.0);
    if (!sample) {
        return nullptr;
    }
    // Aliasing constructor: share ownership of the entry, point at its value.
    return {sample->current, &sample->current->value};
}

double SimulationHost::RenderTick() const {
    if (thread_ == nullptr) {
        return static_cast<double>(world_.CurrentTick());
    }
    const SimulationTiming timing = thread_->GetTiming();
    return static_cast<double>(baseTick_) +
           (timing.estimatedSeconds / timing.stepSeconds);
}

void SimulationHost::SetPaused(bool paused) {
    if (thread_ != nullptr) {
        thread_->SetPaused(paused);
    }
}

bool SimulationHost::IsPaused() const {
    return thread_ == nullptr || thread_->IsPaused();
}

void SimulationHost::RequestStep() {
    if (thread_ != nullptr) {
        thread_->RequestSingleStep();
    }
}

void SimulationHost::SetSpeed(double timeScale) {
    speed_ = timeScale;
    if (thread_ != nullptr) {
        thread_->SetTimeScale(timeScale);
    }
}

std::expected<void, std::string> SimulationHost::Save(
    const std::filesystem::path& path) {
    if (thread_ == nullptr) {
        return sim::SaveWorldToFile(world_.State(), path);
    }
    const bool wasPaused = thread_->IsPaused();
    thread_->SetPaused(true);
    // Once the worker reports the pause it waits for the next command, so
    // the world is not touched while it is serialized here.
    const auto deadline = std::chrono::steady_clock::now() + kPauseTimeout;
    while (!thread_->GetTiming().paused) {
        if (thread_->GetFailure() != nullptr ||
            std::chrono::steady_clock::now() > deadline) {
            thread_->SetPaused(wasPaused);
            return std::unexpected("The simulation did not pause for saving");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto saved = sim::SaveWorldToFile(world_.State(), path);
    thread_->SetPaused(wasPaused);
    return saved;
}

std::exception_ptr SimulationHost::Failure() const {
    return thread_ != nullptr ? thread_->GetFailure() : nullptr;
}

void SimulationHost::Stop() {
    if (thread_ != nullptr) {
        thread_->Stop();
        thread_.reset();
    }
}

void SimulationHost::Step() {
    world_.Step();
    Publish();
}

void SimulationHost::Publish() {
    constexpr sim::Tick kSocialPeriod = 10;
    WorldFrame frame;
    frame.tick = world_.CurrentTick();
    frame.rain = world_.State().weather.rain;
    if (social_ == nullptr || frame.tick % kSocialPeriod == 0) {
        auto social = std::make_shared<std::vector<RobotSocial>>();
        social->reserve(world_.State().robots.size());
        for (const sim::Robot& robot : world_.State().robots) {
            social->push_back({.bonds = robot.bonds, .memories = robot.memories});
        }
        social_ = std::move(social);
    }
    frame.social = social_;
    if (directory_ == nullptr ||
        directoryVersion_ != world_.PopulationVersion()) {
        auto directory = std::make_shared<RobotDirectory>();
        for (const sim::Robot& robot : world_.State().robots) {
            directory->emplace(robot.id.value,
                               RobotInfo{.name = robot.name,
                                         .daysOff = robot.daysOff,
                                         .interests = robot.interests});
        }
        for (const sim::DepartedRobot& robot : world_.State().departed) {
            directory->emplace(robot.id.value,
                               RobotInfo{.name = robot.name, .departed = true});
        }
        directory_ = std::move(directory);
        directoryVersion_ = world_.PopulationVersion();
    }
    frame.directory = directory_;
    frame.robots.reserve(world_.State().robots.size());
    for (const sim::Robot& robot : world_.State().robots) {
        RobotFrame robotFrame{.id = robot.id,
                              .activity = robot.activity,
                              .location = robot.location,
                              .destination = robot.destination,
                              .nextActionAt = robot.nextActionAt,
                              .shiftStartsAt = robot.shiftStartsAt,
                              .commuteAt = robot.commuteAt,
                              .needs = robot.needs,
                              .credits = robot.credits,
                              .rentOwed = robot.rentOwed,
                              .apartment = robot.apartment,
                              .workspace = robot.workspace,
                              .jobLevel = robot.jobLevel,
                              .performance = robot.performance,
                              .age = robot.age,
                              .wear = robot.wear,
                              .rideFrom = robot.rideFrom,
                              .rideTo = robot.rideTo,
                              .boardsAt = robot.boardsAt,
                              .alightsAt = robot.alightsAt};
        switch (robot.activity) {
            case sim::RobotActivity::Resting:
                ++frame.resting;
                break;
            case sim::RobotActivity::Working:
                ++frame.working;
                break;
            case sim::RobotActivity::Visiting:
                ++frame.visiting;
                break;
            case sim::RobotActivity::Walking:
                ++frame.walking;
                break;
        }
        if (const auto walk = sim::World::WalkOf(robot)) {
            const auto from = world_.Rooms().Find(walk->from);
            const auto to = world_.Rooms().Find(walk->to);
            if (from && to) {
                robotFrame.from = from->building->id;
                robotFrame.to = to->building->id;
                robotFrame.departedAt = walk->departedAt;
                robotFrame.arrivesAt = walk->arrivesAt;
            }
        }
        frame.robots.push_back(robotFrame);
    }
    frames_.Publish(static_cast<double>(frame.tick), std::move(frame));
}

}  // namespace mechapunk
