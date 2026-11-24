#pragma once

#include <cstddef>
#include <cstdint>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Social.hpp"

namespace mechapunk::sim {

// What kind of thing an entry records. `World` defines it in World.hpp, below
// WorldState, which needs it here; a scoped enum with a fixed underlying type
// can be declared before its enumerators, so this says only how big it is.
enum class EventKind : std::uint8_t;

// How many entries the chronicle keeps. Oldest first, so "drop the oldest"
// means erase from the front; `Validation` refuses a longer list (rule 5).
inline constexpr std::size_t kChronicleCap = 5000;

// Something worth keeping after the event log has scrolled away: the city's
// story, and each robot's history. Ids rather than pointers, because the
// robots and rooms an entry names may be gone by the time it is read (a
// scrapped robot is still remembered), and `World::Describe` reads an entry
// the way it reads a `SimEvent`. `memory` is the memory the event left, if
// any, the same kind a robot's own memories use. `account` is whose account
// the event's money moved into or out of: the central bank or a saver for
// an emission, the owner for a unit that opened or closed; none for events
// that carry no account. It is what lets an organization's history page
// show the money it was given.
struct ChronicleEntry {
    Tick tick{};
    EventKind kind{};
    EntityId robot;
    EntityId otherRobot;
    EntityId room;
    std::int64_t credits{};
    MemoryKind memory{};
    EntityId account;
};

}  // namespace mechapunk::sim
