// The parts of a robot's body: how to name one, the cap that ties the head
// and the torso to the robot's lasting damage, and the mend that follows the
// wear a clinic takes away.
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
    // A robot worn out completely has a destroyed head and torso: step L3b
    // scraps on those parts instead of on `wear`, which is the same moment as
    // long as the cap and the mend below keep the parts on the wear.
    const std::int32_t cap = std::max<std::int32_t>(0, kMaxWear - robot.wear);
    for (const BodyPart part : kCappedParts) {
        PartState& state = Part(robot.body, part);
        // Only ever a cap: a part repaired to a better condition than the
        // cap is held down, and a part already worse than it stays worse.
        state.condition = std::min(state.condition, cap);
    }
}

void MendBody(Robot& robot, std::int32_t wearTaken) noexcept {
    // The cap the wear now allows, then as much of the taken wear back as
    // reaches it: a part damaged beyond what the wear explains is mended only
    // by the amount the clinic actually worked.
    const std::int32_t cap = std::max<std::int32_t>(0, kMaxWear - robot.wear);
    for (const BodyPart part : kCappedParts) {
        PartState& state = Part(robot.body, part);
        state.condition = std::min(cap, state.condition + std::max(0, wearTaken));
    }
}

}  // namespace mechapunk::sim
