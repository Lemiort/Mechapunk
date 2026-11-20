#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"

namespace mechapunk::sim {

// Things robots like to talk about. A robot's interests are a bit set with
// bit n standing for Interest n; robots that share interests get along.
enum class Interest : std::uint8_t {
    DroneRacing,
    CircuitBending,
    OldFilms,
    Poetry,
    Chess,
    ScrapArt,
    SynthMusic,
    Gambling,
};
inline constexpr std::uint32_t kInterestCount = 8;

// "drone racing"
[[nodiscard]] std::string_view InterestName(Interest interest) noexcept;
// "drone racing, poetry and chess"
[[nodiscard]] std::string InterestList(std::uint8_t interests);

// How one robot sees another it has talked to.
enum class BondLevel : std::uint8_t {
    Acquaintance,
    Friend,
    CloseFriend,
    Rival,
};

// "acquaintance", "friend", "close friend", "rival"
[[nodiscard]] std::string_view BondLevelName(BondLevel level) noexcept;

// One robot's feelings about another. Bonds are one-sided: both robots of a
// talk change by the same amount, but each forgets and fades on its own.
struct Bond {
    static constexpr std::int32_t kMaxAffinity = 100;

    EntityId robot;
    // From -100 (can't stand them) to 100 (best friends).
    std::int32_t affinity{};
    // Follows affinity with some slack, so it does not flicker.
    BondLevel level{BondLevel::Acquaintance};
    std::uint32_t talks{};
    Tick lastTalk{};
};

// Moments a robot remembers.
enum class MemoryKind : std::uint8_t {
    BecameFriends,
    BecameCloseFriends,
    DriftedApart,  // A friend became an acquaintance again.
    BecameRivals,
    MadePeace,  // A rival became an acquaintance again.
    FellBehindOnRent,
    PaidOffDebt,
    Promoted,
    LostJob,
    FoundJob,
    MovedUp,   // Moved to a nicer apartment; room: the new one.
    Evicted,   // room: the apartment it had to leave.
    LostFriend,     // A friend was scrapped; robot: the friend.
    ArrivedInCity,  // room: its first apartment.
    GotBadPart,     // room: the black market that sold it.
    BoughtProperty, // room: the apartment.
    SoldHome,       // room: the apartment, still lived in as a tenant.
    TookLoan,
    Inherited,      // robot: the scrapped friend.
    MovedInTogether,  // robot: the friend; room: the flat they share.
    JoinedGang,     // room: its home; otherRobot: the friend who invited it.
    LeftGang,       // room: its home.
    WasRobbed,      // room: where; credits: what was taken; otherRobot: robber.
    RobbedSomeone,  // credits: what it took; otherRobot: victim; room: where.
    Arrested,       // room: the venue; otherRobot: victim (if an officer).
};

struct Memory {
    Tick tick{};
    MemoryKind kind{};
    EntityId robot;  // The other robot, if someone else was involved.
    EntityId room;   // Where it happened, if anywhere in particular.
};

// The level a bond moves to at `affinity`. A bond only moves one level at a
// time, and falls back at a lower affinity than it rose, so small swings do
// not make and break friendships.
[[nodiscard]] BondLevel NextBondLevel(BondLevel level,
                                      std::int32_t affinity) noexcept;
// What a robot remembers about a bond changing from one level to another.
[[nodiscard]] std::optional<MemoryKind> MemoryOfChange(BondLevel from,
                                                       BondLevel to) noexcept;
// "became friends with KX-417 Vera at Neon Wok", "got promoted at Smelting
// Bay 2, Foundry 1". `other` names the other robot, if any, and `place` the
// room, if any.
[[nodiscard]] std::string MemoryText(MemoryKind kind, std::string_view other,
                                     std::string_view place = {});

struct WorldState;
// Checks every robot's bonds and memories; part of ValidateWorldState.
[[nodiscard]] std::expected<void, std::string> ValidateSocial(
    const WorldState& state);

}  // namespace mechapunk::sim
