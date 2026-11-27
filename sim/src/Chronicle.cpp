// The saved story: `World::Chronicle` decides which events `World::Emit`
// keeps in `state_.chronicle`, so the city's tale outlives the event log.

#include <cstdint>
#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {
namespace {
// Money created by the central bank is small day by day (the guide's watch
// list says so), so only an emission of more than this changes the city
// enough to be worth reading about years later.
constexpr std::int64_t kMoneyNoteCredits = 10000;


// The events worth the chronicle's space: what shaped a robot, a building
// or the whole city. Errands, talks, rent and prices are daily life and stay
// in the log. A `Remembered` event is judged by the memory it carries:
// friendships and rivalries forming, being fired, promoted to lead and
// evicted; finding a job, making peace or drifting apart repeats too much
// to keep.
bool IsNotable(const SimEvent& event) noexcept {
    switch (event.kind) {
        case EventKind::Scrapped:
        case EventKind::Arrived:
        case EventKind::BoughtProperty:
        case EventKind::UnitOpened:
        case EventKind::UnitClosed:
        case EventKind::Robbed:
        case EventKind::Arrested:
        case EventKind::Fought:
        case EventKind::JoinedGang:
        case EventKind::LeftGang:
        case EventKind::StationOpened:
        case EventKind::BankFounded:
        case EventKind::CorporationFounded:
        case EventKind::GangFormed:
            return true;
        case EventKind::MoneySupplyChanged:
            return event.credits > kMoneyNoteCredits;
        case EventKind::Remembered:
            switch (event.memory) {
                case MemoryKind::BecameCloseFriends:
                case MemoryKind::BecameRivals:
                case MemoryKind::LostJob:
                case MemoryKind::Promoted:
                case MemoryKind::Evicted:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

}  // namespace

// The list stays oldest first: an entry is appended the minute its event
// happens, and past `kChronicleCap` the oldest is dropped, which is the
// front. No random draws, so recording cannot disturb the simulation.
void World::Chronicle(const SimEvent& event) {
    if (!IsNotable(event)) {
        return;
    }
    state_.chronicle.push_back({
        .tick = event.tick,
        .kind = event.kind,
        .robot = event.robot,
        .otherRobot = event.otherRobot,
        .room = event.room,
        .credits = event.credits,
        .memory = event.memory,
        .account = event.account,
    });
    if (state_.chronicle.size() > kChronicleCap) {
        state_.chronicle.erase(state_.chronicle.begin());
    }
}

}  // namespace mechapunk::sim
