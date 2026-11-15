#include "SimulationHost.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
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
}  // namespace

SimulationHost::SimulationHost(sim::World world)
    : world_(std::move(world)),
      city_(world_.State().city),
      seed_(world_.State().seed),
      baseTick_(world_.CurrentTick()) {
    Publish();
    thread_ = std::make_unique<SimulationThread>(
        FixedStepClockSettings{.stepSeconds = kSecondsPerMinute,
                               .maxStepsPerAdvance = kMaxStepsPerWake},
        [this](const SimulationStep&) { Step(); });
}

SimulationHost::~SimulationHost() { Stop(); }

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

void SimulationHost::SetSpeed(double minutesPerSecond) {
    speed_ = minutesPerSecond;
    if (thread_ != nullptr) {
        thread_->SetTimeScale(minutesPerSecond);
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
    WorldFrame frame;
    frame.tick = world_.CurrentTick();
    frame.robots.reserve(world_.State().robots.size());
    for (const sim::Robot& robot : world_.State().robots) {
        RobotFrame robotFrame{.id = robot.id, .activity = robot.activity};
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
