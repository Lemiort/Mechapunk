// The parts of a robot's body: how to name one and the cap that ties the
// head and the torso to the robot's lasting damage.
#include "mechapunk/sim/Body.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {

// The parts a robot can be capped in: the ones that cannot be replaced, so
// their condition follows its wear.
constexpr std::array<BodyPart, 2> kCappedParts{BodyPart::Head, BodyPart::Torso};

}  // namespace

PartState& Part(Body& body, BodyPart part) noexcept {
    // The enum has exactly the five parts, in the order of `parts`.
    return body.parts[static_cast<std::size_t>(part)];
}

const PartState& Part(const Body& body, BodyPart part) noexcept {
    return body.parts[static_cast<std::size_t>(part)];
}

void CapBody(Robot& robot) noexcept {
    // A robot worn out completely has a destroyed head and torso, and is
    // scrapped on the same day it always was (step L3 keeps that rule).
    const std::int32_t cap = std::max<std::int32_t>(0, kMaxWear - robot.wear);
    for (const BodyPart part : kCappedParts) {
        PartState& state = Part(robot.body, part);
        // Only ever a cap: a part repaired to a better condition than the
        // cap is held down, and a part already worse than it stays worse.
        state.condition = std::min(state.condition, cap);
    }
}

}  // namespace mechapunk::sim
