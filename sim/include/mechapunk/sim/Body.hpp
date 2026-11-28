// A robot's body: five parts, each with a condition and a flag saying it is
// gone. The parts decide what the robot can do: the head talking and being
// punctual, the torso the power it carries, the hands holding and working,
// the legs walking. See docs/tasks/L-body-parts.md for the rules that wear
// them and repair them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mechapunk::sim {

// The robot itself; `CapBody` needs it and World.hpp defines it.
struct Robot;

enum class BodyPart : std::uint8_t { Head, Torso, LeftHand, RightHand, Legs };

// A part's condition: 1000 whole and 0 destroyed. Nothing else: a part is
// neither replaced nor reshaped by the rules that wear it, and only repairs
// give condition back.
struct PartState {
    std::int32_t condition{1000};
    bool lost{false};

    bool operator==(const PartState&) const = default;
};

// The body of one robot: its five parts and the hand it works with, the one
// that wears faster and does the fine work. The head and the torso are never
// lost: at condition 0 the robot is scrapped instead.
struct Body {
    std::array<PartState, 5> parts;
    BodyPart dominantHand{BodyPart::RightHand};

    bool operator==(const Body&) const = default;
};

// The part of a body, by which part it is.
[[nodiscard]] PartState& Part(Body& body, BodyPart part) noexcept;
[[nodiscard]] const PartState& Part(const Body& body, BodyPart part) noexcept;

// The head and the torso of a robot can never be in better condition than
// its lasting damage allows: `kMaxWear` minus its `wear` (World.hpp). Hands
// and legs are not capped, because they can be lost and replaced. Call this
// wherever a robot's wear grows; it only ever lowers a condition, so a part
// already worse than the cap stays worse.
void CapBody(Robot& robot) noexcept;

// A clinic that takes lasting damage away gives the head and the torso back
// the same amount of condition, never more than the cap allows: the basic
// parts follow wear in both directions, while damage the clinic has not reached
// stays. Without this the head would remember the worst wear a robot ever
// reached, and a robot whose dodgy part pushed wear to `kMaxWear` one morning
// would die the night a clinic had already repaired that wear - the day the
// wear rule let it live (task L, step L3a). Repairing a hand or a leg is
// step L12's rule and gives nothing back here.
void MendBody(Robot& robot, std::int32_t wearTaken) noexcept;

}  // namespace mechapunk::sim
