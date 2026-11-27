// Relationships and memories: robots that share a venue or a workspace talk,
// grow fond of each other or fall out, remember the moments that mattered,
// and seek out their friends.
#include "mechapunk/sim/Social.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::size_t kMaxCrimeMemories = 12;

// What a robot remembers, in two shares that cannot push each other out:
// twelve memories of ordinary life, and twelve memories of crime - its rap
// sheet, what it did to the city and what the city did to it. One share of
// twelve held everything, so the crime lines faded within days: a member
// fined for robbing a week earlier still carries "Arrests: 1" (the counter
// is durable) and had no arrest in its memories, the card the user read on
// 2026-11-22 as "robber has no memory of arrest".
constexpr std::size_t kMaxOrdinaryMemories = 12;

// A robot keeps this many bonds; to meet someone new it forgets its weakest
// acquaintance, if that one is barely known.
constexpr std::size_t kMaxBonds = 12;

constexpr std::int32_t kStayRivals = -10;

constexpr std::int32_t kBecomeRivals = -25;

constexpr std::int32_t kStayClose = 55;

constexpr std::int32_t kBecomeClose = 70;

constexpr std::int32_t kStayFriends = 10;

// Bond levels: a bond rises at one affinity and falls back at a lower one.
constexpr std::int32_t kBecomeFriends = 25;

// The whole store, which is what validation and a save file allow.
constexpr std::size_t kMaxMemories = kMaxOrdinaryMemories + kMaxCrimeMemories;

// The rap sheet kinds: a robbery the robot did or suffered, an arrest it
// made or took. `Remember` evicts within a share, and validation counts the
// same two shares, so this is the one list both sides agree on.
[[nodiscard]] constexpr bool IsCrimeMemory(MemoryKind kind) {
    return kind == MemoryKind::RobbedSomeone || kind == MemoryKind::WasRobbed ||
           kind == MemoryKind::Arrested || kind == MemoryKind::ArrestedSomeone;
}
constexpr std::array<std::string_view, kInterestCount> kInterestNames{
    "drone racing", "circuit bending", "old films", "poetry",
    "chess",        "scrap art",       "synth music", "gambling"};

// The `index`th set bit of `bits`, counting from the lowest.
std::uint32_t NthBit(std::uint8_t bits, std::uint32_t index) {
    for (std::uint32_t bit = 0; bit < kInterestCount; ++bit) {
        if (((bits >> bit) & 1U) != 0 && index-- == 0) {
            return bit;
        }
    }
    return 0;
}

std::int32_t AffinityOf(const Robot& robot, EntityId other) {
    const auto found =
        std::ranges::lower_bound(robot.bonds, other, std::less{}, &Bond::robot);
    return found != robot.bonds.end() && found->robot == other
               ? found->affinity
               : 0;
}
}  // namespace

std::string_view InterestName(Interest interest) noexcept {
    const auto index = static_cast<std::size_t>(interest);
    return index < kInterestNames.size() ? kInterestNames.at(index) : "?";
}

std::string InterestList(std::uint8_t interests) {
    std::string list;
    const auto count = static_cast<std::uint32_t>(std::popcount(interests));
    for (std::uint32_t index = 0; index < count; ++index) {
        if (index > 0) {
            list += index + 1 == count ? " and " : ", ";
        }
        list += InterestName(static_cast<Interest>(NthBit(interests, index)));
    }
    return list.empty() ? "nothing in particular" : list;
}

std::string_view BondLevelName(BondLevel level) noexcept {
    switch (level) {
        case BondLevel::Acquaintance:
            return "acquaintance";
        case BondLevel::Friend:
            return "friend";
        case BondLevel::CloseFriend:
            return "close friend";
        case BondLevel::Rival:
            return "rival";
    }
    return "?";
}

BondLevel NextBondLevel(BondLevel level, std::int32_t affinity) noexcept {
    switch (level) {
        case BondLevel::Acquaintance:
            if (affinity >= kBecomeFriends) {
                return BondLevel::Friend;
            }
            return affinity <= kBecomeRivals ? BondLevel::Rival
                                             : BondLevel::Acquaintance;
        case BondLevel::Friend:
            if (affinity >= kBecomeClose) {
                return BondLevel::CloseFriend;
            }
            return affinity < kStayFriends ? BondLevel::Acquaintance
                                           : BondLevel::Friend;
        case BondLevel::CloseFriend:
            return affinity < kStayClose ? BondLevel::Friend
                                         : BondLevel::CloseFriend;
        case BondLevel::Rival:
            return affinity > kStayRivals ? BondLevel::Acquaintance
                                          : BondLevel::Rival;
    }
    return level;
}

std::optional<MemoryKind> MemoryOfChange(BondLevel from, BondLevel to) noexcept {
    using enum BondLevel;
    if (from == Acquaintance && to == Friend) {
        return MemoryKind::BecameFriends;
    }
    if (from == Friend && to == CloseFriend) {
        return MemoryKind::BecameCloseFriends;
    }
    if (from == Friend && to == Acquaintance) {
        return MemoryKind::DriftedApart;
    }
    if (from == Acquaintance && to == Rival) {
        return MemoryKind::BecameRivals;
    }
    if (from == Rival && to == Acquaintance) {
        return MemoryKind::MadePeace;
    }
    return std::nullopt;  // A close friend cooling to a friend is no event.
}

std::string MemoryText(MemoryKind kind, std::string_view other,
                       std::string_view place) {
    const auto at = [&](std::string text, std::string_view preposition) {
        if (!place.empty()) {
            text += std::format(" {} {}", preposition, place);
        }
        return text;
    };
    switch (kind) {
        case MemoryKind::BecameFriends:
            return at(std::format("became friends with {}", other), "at");
        case MemoryKind::BecameCloseFriends:
            return at(std::format("grew close to {}", other), "at");
        case MemoryKind::DriftedApart:
            return std::format("drifted apart from {}", other);
        case MemoryKind::BecameRivals:
            return at(std::format("started a rivalry with {}", other), "at");
        case MemoryKind::MadePeace:
            return std::format("made peace with {}", other);
        case MemoryKind::FellBehindOnRent:
            return "fell behind on rent";
        case MemoryKind::PaidOffDebt:
            return "paid off the rent owed";
        case MemoryKind::Promoted:
            return at("got promoted", "at");
        case MemoryKind::LostJob:
            return at("lost the job", "at");
        case MemoryKind::FoundJob:
            return at("found a job", "at");
        case MemoryKind::MovedUp:
            return at("moved up", "to");
        case MemoryKind::Evicted:
            return at("was evicted", "from");
        case MemoryKind::LostFriend:
            return std::format("lost {}, who was scrapped", other);
        case MemoryKind::ArrivedInCity:
            return at("moved to the city", "into");
        case MemoryKind::GotBadPart:
            return at("got a dodgy part", "at");
        case MemoryKind::BoughtProperty:
            return place.empty() ? std::string{"bought an apartment"}
                                 : std::format("bought {}", place);
        case MemoryKind::SoldHome:
            return place.empty() ? std::string{"sold its home"}
                                 : std::format("sold {} and rents it now", place);
        case MemoryKind::TookLoan:
            return "took a loan";
        case MemoryKind::Inherited:
            return std::format("inherited everything {} had", other);
        case MemoryKind::MovedInTogether:
            return at(std::format("moved in with {}", other), "to");
        case MemoryKind::JoinedGang:
            return std::format("joined a gang");
        case MemoryKind::LeftGang:
            return "left the gang";
        case MemoryKind::WasRobbed:
            return std::format("was robbed by {}", other);
        case MemoryKind::RobbedSomeone:
            return place.empty() ? std::string{"robbed someone"}
                                 : std::format("robbed someone at {}", place);
        case MemoryKind::Arrested:
            // B15: the robber's card names the officer that took it; memories
            // from version 19 saves never filled the other robot, so those
            // keep the old, vaguer line.
            return at(other.empty() ? std::string{"arrested for robbing"}
                                    : std::format("arrested by {}", other),
                      "at");
        case MemoryKind::ArrestedSomeone:
            // The officer's card: it was the officer that made the arrest.
            return at(other.empty() ? std::string{"arrested someone"}
                                    : std::format("arrested {}", other),
                      "at");
    }
    return "remembered something";
}

void World::Socialize() {
    // The rooms that hold an officer on shift right now, through World::Officer
    // -OnShift: the same definition HandleOfficerAffinity uses, so a watched
    // room and the officer in it cannot be decided two different ways. Built
    // once per call so ConsiderCrime can look a room up instead of walking
    // every robot for every pair (B7's speed item); the value is the room's
    // officer, the smaller id when two stand in it, so an arrest can name
    // one and names the same one on every machine (rule 1).
    std::map<std::uint64_t, std::size_t> officerInRoom;
    for (std::size_t index = 0; index < state_.robots.size(); ++index) {
        const Robot& robot = state_.robots[index];
        if (!OfficerOnShift(robot)) {
            continue;
        }
        const auto [found, inserted] =
            officerInRoom.try_emplace(robot.location.value, index);
        if (!inserted && state_.robots[found->second].id.value > robot.id.value) {
            found->second = index;
        }
    }

    // Robots sharing a room, in id order within each room and rooms in id
    // order, so random draws happen in a fixed order.
    std::map<std::uint64_t, std::vector<std::size_t>> rooms;
    for (std::size_t index = 0; index < state_.robots.size(); ++index) {
        const Robot& robot = state_.robots[index];
        if (robot.activity == RobotActivity::Visiting ||
            robot.activity == RobotActivity::Working) {
            rooms[robot.location.value].push_back(index);
        }
    }
    std::vector<std::uint32_t> weights;
    // A robot takes part in one talk at a time.
    std::vector<bool> busy;
    for (const auto& [room, present] : rooms) {
        if (present.size() < 2) {
            continue;
        }
        const bool venue =
            state_.robots[present.front()].activity == RobotActivity::Visiting;
        const std::uint32_t chance =
            venue ? static_cast<std::uint32_t>(state_.settings.social.venueTalkPercent) : static_cast<std::uint32_t>(state_.settings.social.workTalkPercent);
        busy.assign(present.size(), false);
        for (std::size_t self = 0; self < present.size(); ++self) {
            if (state_.random.UniformInt(1, 100) > chance || busy[self]) {
                continue;
            }
            // Robots seek out those they like.
            Robot& robot = state_.robots[present[self]];
            weights.assign(present.size(), 0);
            std::uint32_t total = 0;
            for (std::size_t slot = 0; slot < present.size(); ++slot) {
                if (slot == self || busy[slot]) {
                    continue;
                }
                const std::int32_t affinity =
                    AffinityOf(robot, state_.robots[present[slot]].id);
                weights[slot] =
                    static_cast<std::uint32_t>(state_.settings.social.partnerWeight) + static_cast<std::uint32_t>(
                                         std::max(affinity, 0) /
                                         static_cast<std::int32_t>(state_.settings.social.affinityPerWeight));
                total += weights[slot];
            }
            if (total == 0) {
                continue;  // Everyone else is already talking.
            }
            std::uint32_t pick = state_.random.UniformInt(0, total - 1);
            std::size_t slot = 0;
            while (pick >= weights[slot]) {
                pick -= weights[slot];
                ++slot;
            }
            busy[self] = true;
            busy[slot] = true;
            Talk(robot, state_.robots[present[slot]]);
            ConsiderCrime(robot, state_.robots[present[slot]], officerInRoom);
        }
        // The officer's other half (B4): a member standing in a room an officer
        // stands in, that robbed in the last hour, is arrested by that officer
        // (in Gangs.cpp). The room the robbery happened in is not required:
        // officers see about one venue in fifty, so matching rooms left arrests
        // almost unseen.
        const auto watched = officerInRoom.find(room);
        if (watched != officerInRoom.end()) {
            PatrolArrests(present, watched->second);
        }
    }
}

void World::Talk(Robot& first, Robot& second) {
    const auto shared = static_cast<std::uint8_t>(first.interests &
                                                  second.interests);
    const auto spread = static_cast<std::int32_t>(
        state_.random.UniformInt(0, static_cast<std::uint32_t>(state_.settings.social.talkSpread)));
    const std::int32_t change =
        shared != 0 ? spread + static_cast<std::int32_t>(state_.settings.social.sharedTalkBase) +
                          (static_cast<std::int32_t>(state_.settings.social.perSharedInterest) * std::popcount(shared))
                    : spread + static_cast<std::int32_t>(state_.settings.social.strangerTalkBase);
    ++state_.today.talks;
    state_.today.arguments += change < 0 ? 1 : 0;
    // About something they share, or failing that one of them likes.
    std::uint8_t topics = shared != 0
                              ? shared
                              : static_cast<std::uint8_t>(first.interests |
                                                          second.interests);
    if (topics == 0) {
        topics = 0xFF;
    }
    const auto topic = static_cast<Interest>(NthBit(
        topics, state_.random.UniformInt(
                    0, static_cast<std::uint32_t>(std::popcount(topics)) - 1)));

    for (Robot* robot : {&first, &second}) {
        Robot& other = robot == &first ? second : first;
        if (change > 0) {
            const std::int32_t extra =
                std::max(AffinityOf(*robot, other.id), 0) /
                static_cast<std::int32_t>(state_.settings.social.affinityPerExtraSocial);
            robot->needs.social =
                std::min(robot->needs.social + static_cast<std::int32_t>(state_.settings.social.talkSocial) + extra, Needs::kMax);
        } else if (change < 0) {
            robot->needs.fun = std::max(robot->needs.fun - static_cast<std::int32_t>(state_.settings.social.argumentFun), 0);
        }
        Emit({.tick = state_.tick,
              .kind = EventKind::Talked,
              .robot = robot->id,
              .room = robot->location,
              .otherRobot = other.id,
              .topic = topic,
              .change = change});
    }
    ChangeBond(first, second.id, change);
    ChangeBond(second, first.id, change);
}

void World::ChangeBond(Robot& robot, EntityId other, std::int32_t change) {
    auto found =
        std::ranges::lower_bound(robot.bonds, other, std::less{}, &Bond::robot);
    if (found == robot.bonds.end() || found->robot != other) {
        if (robot.bonds.size() >= kMaxBonds) {
            // Make room by forgetting the least known acquaintance.
            const auto weakest = std::ranges::min_element(
                robot.bonds, std::less{}, [](const Bond& bond) {
                    return bond.level == BondLevel::Acquaintance
                               ? std::abs(bond.affinity)
                               : Bond::kMaxAffinity + 1;
                });
            if (weakest->level != BondLevel::Acquaintance ||
                std::abs(weakest->affinity) > static_cast<std::int32_t>(state_.settings.social.forgettable)) {
                return;  // Everyone it knows matters more.
            }
            robot.bonds.erase(weakest);
            found = std::ranges::lower_bound(robot.bonds, other, std::less{},
                                             &Bond::robot);
        }
        found = robot.bonds.insert(found, Bond{.robot = other});
    }
    Bond& bond = *found;
    const bool deeper = (change > 0 && bond.affinity >= kBecomeFriends) ||
                        (change < 0 && bond.affinity <= kBecomeRivals);
    const std::int32_t step = deeper ? change / 2 : change;
    bond.affinity = std::clamp(bond.affinity + step, -Bond::kMaxAffinity,
                               Bond::kMaxAffinity);
    ++bond.talks;
    bond.lastTalk = state_.tick;
    UpdateBondLevel(robot, bond, robot.location);
}

void World::UpdateBondLevel(Robot& robot, Bond& bond, EntityId room) {
    const BondLevel next = NextBondLevel(bond.level, bond.affinity);
    if (next == bond.level) {
        return;
    }
    const std::optional<MemoryKind> memory = MemoryOfChange(bond.level, next);
    bond.level = next;
    if (memory) {
        Remember(robot, *memory, bond.robot, room);
    }
}

void World::Remember(Robot& robot, MemoryKind kind, EntityId other,
                     EntityId room) {
    DayCounters& today = state_.today;
    switch (kind) {
        case MemoryKind::Promoted: ++today.promotions; break;
        case MemoryKind::LostJob: ++today.jobsLost; break;
        case MemoryKind::FoundJob: ++today.jobsFound; break;
        case MemoryKind::MovedUp: ++today.movedUp; break;
        case MemoryKind::Evicted: ++today.evictions; break;
        case MemoryKind::MovedInTogether: ++today.movedInTogether; break;
        case MemoryKind::BecameFriends: ++today.newFriendships; break;
        case MemoryKind::BecameRivals: ++today.newRivalries; break;
        case MemoryKind::GotBadPart: ++today.badParts; break;
        case MemoryKind::WasRobbed: break;   // counted in ConsiderCrime when robbery happens.
        case MemoryKind::RobbedSomeone: break;  // counted when robbery happens.
        case MemoryKind::Arrested: break;    // counted in ConsiderCrime when arrest happens.
        default: break;
    }
    robot.memories.push_back(
        {.tick = state_.tick, .kind = kind, .robot = other, .room = room});
    // Make way within the share the new memory belongs to: the oldest entry
    // of that share goes, and it is the first entry of the share because the
    // store is kept in tick order. Friendships cannot push out a rap sheet,
    // and a busy rap sheet cannot push out the friendships, so neither a
    // criminal's card nor an officer's card loses its crimes (rule 1: the
    // order is the store's own, not a container's).
    // Make way within the share the new memory belongs to: the oldest entry
    // of that share goes, and it is the first entry of the share because the
    // store is kept in tick order. Friendships cannot push out a rap sheet,
    // and a busy rap sheet cannot push out the friendships, so neither a
    // criminal's card nor an officer's card loses its crimes (rule 1: the
    // order is the store's own, not a container's).
    const bool crime = IsCrimeMemory(kind);
    const std::size_t share =
        crime ? kMaxCrimeMemories : kMaxOrdinaryMemories;
    const auto inShare = [crime](const Memory& memory) {
        return IsCrimeMemory(memory.kind) == crime;
    };
    if (static_cast<std::size_t>(std::ranges::count_if(robot.memories, inShare)) >
        share) {
        robot.memories.erase(std::ranges::find_if(robot.memories, inShare));
    }
    Emit({.tick = state_.tick,
          .kind = EventKind::Remembered,
          .robot = robot.id,
          .room = room,
          .otherRobot = other,
          .memory = kind});
}

void World::FadeBonds() {
    for (Robot& robot : state_.robots) {
        for (Bond& bond : robot.bonds) {
            if (state_.tick - bond.lastTalk < static_cast<Tick>(state_.settings.social.fadeAfter)) {
                continue;
            }
            // Feelings cool toward indifference.
            if (bond.affinity > 0) {
                bond.affinity = std::max(bond.affinity - static_cast<std::int32_t>(state_.settings.social.fadePerDay), 0);
            } else {
                bond.affinity = std::min(bond.affinity + static_cast<std::int32_t>(state_.settings.social.fadePerDay), 0);
            }
            UpdateBondLevel(robot, bond, EntityId{});
        }
        std::erase_if(robot.bonds, [](const Bond& bond) {
            return bond.affinity == 0 && bond.level == BondLevel::Acquaintance;
        });
    }
}

std::int64_t World::CompanyAt(const Robot& robot, EntityId venue) const {
    std::int64_t pull = 0;
    for (const Bond& bond : robot.bonds) {
        if (bond.level == BondLevel::Acquaintance) {
            continue;
        }
        const Robot* other = FindRobot(bond.robot);
        if (other == nullptr) {
            continue;
        }
        // There now, or on the way.
        const bool there =
            (other->activity == RobotActivity::Visiting &&
             other->location == venue) ||
            (other->activity == RobotActivity::Walking &&
             other->destination == venue);
        if (!there) {
            continue;
        }
        switch (bond.level) {
            case BondLevel::Friend:
                pull += static_cast<std::int64_t>(state_.settings.social.friendPull);
                break;
            case BondLevel::CloseFriend:
                pull += static_cast<std::int64_t>(state_.settings.social.closeFriendPull);
                break;
            case BondLevel::Rival:
                pull -= static_cast<std::int64_t>(state_.settings.social.rivalPush);
                break;
            case BondLevel::Acquaintance:
                break;
        }
    }
    return pull;
}

std::expected<void, std::string> ValidateSocial(const WorldState& state) {
    const auto exists = [&](EntityId id) {
        const auto found = std::ranges::lower_bound(state.robots, id,
                                                    std::less{}, &Robot::id);
        return found != state.robots.end() && found->id == id;
    };
    // Memories may name robots that have since been scrapped.
    const auto remembered = [&](EntityId id) {
        const auto found = std::ranges::lower_bound(
            state.departed, id, std::less{}, &DepartedRobot::id);
        return exists(id) ||
               (found != state.departed.end() && found->id == id);
    };
    EntityId previousDeparted;
    for (const DepartedRobot& departed : state.departed) {
        if (departed.id <= previousDeparted || exists(departed.id) ||
            departed.id.value >= state.ids.next) {
            return std::unexpected(std::format(
                "Scrapped robot '{}' has an invalid, repeated or out-of-order "
                "id",
                departed.name));
        }
        previousDeparted = departed.id;
    }
    const RoomIndex rooms(state.city);
    for (const Robot& robot : state.robots) {
        if (robot.bonds.size() > kMaxBonds ||
            robot.memories.size() > kMaxMemories) {
            return std::unexpected(std::format(
                "Robot '{}' has too many bonds or memories", robot.name));
        }
        EntityId previous;
        for (const Bond& bond : robot.bonds) {
            if (bond.robot <= previous || bond.robot == robot.id ||
                !exists(bond.robot) ||
                std::abs(bond.affinity) > Bond::kMaxAffinity ||
                bond.lastTalk > state.tick) {
                return std::unexpected(std::format(
                    "Robot '{}' has an invalid, repeated or out-of-order bond "
                    "with robot {}",
                    robot.name, bond.robot.value));
            }
            previous = bond.robot;
        }
        std::size_t crimes = 0;
        for (const Memory& memory : robot.memories) {
            if (memory.tick > state.tick ||
                (memory.robot.IsValid() && !remembered(memory.robot)) ||
                (memory.room.IsValid() && !rooms.Find(memory.room))) {
                return std::unexpected(std::format(
                    "Robot '{}' has a memory of a missing robot or room",
                    robot.name));
            }
            crimes += IsCrimeMemory(memory.kind) ? 1 : 0;
        }
        // The two shares, not only the whole store: the card prints an
        // arrest count that never fades, so a store that lost its crime
        // share silently would contradict it (B16).
        if (crimes > kMaxCrimeMemories || robot.memories.size() - crimes >
            kMaxOrdinaryMemories) {
            return std::unexpected(std::format(
                "Robot '{}' has more memories of crime or of ordinary life "
                "than it can keep",
                robot.name));
        }
    }
    return {};
}

}  // namespace mechapunk::sim
