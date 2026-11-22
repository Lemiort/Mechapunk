#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}
}  // namespace

TEST(Social, BondLevelsRiseAndFallWithSlack) {
    using enum sim::BondLevel;
    EXPECT_EQ(sim::NextBondLevel(Acquaintance, 24), Acquaintance);
    EXPECT_EQ(sim::NextBondLevel(Acquaintance, 25), Friend);
    // A friend stays a friend below the point where it became one.
    EXPECT_EQ(sim::NextBondLevel(Friend, 15), Friend);
    EXPECT_EQ(sim::NextBondLevel(Friend, 9), Acquaintance);
    EXPECT_EQ(sim::NextBondLevel(Friend, 70), CloseFriend);
    EXPECT_EQ(sim::NextBondLevel(CloseFriend, 60), CloseFriend);
    EXPECT_EQ(sim::NextBondLevel(CloseFriend, 50), Friend);
    EXPECT_EQ(sim::NextBondLevel(Acquaintance, -25), Rival);
    EXPECT_EQ(sim::NextBondLevel(Rival, -15), Rival);
    EXPECT_EQ(sim::NextBondLevel(Rival, -5), Acquaintance);
    // One level at a time.
    EXPECT_EQ(sim::NextBondLevel(Acquaintance, 100), Friend);
    EXPECT_EQ(sim::NextBondLevel(Friend, -100), Acquaintance);

    EXPECT_EQ(sim::MemoryOfChange(Acquaintance, Friend),
              sim::MemoryKind::BecameFriends);
    EXPECT_EQ(sim::MemoryOfChange(Friend, CloseFriend),
              sim::MemoryKind::BecameCloseFriends);
    EXPECT_EQ(sim::MemoryOfChange(Friend, Acquaintance),
              sim::MemoryKind::DriftedApart);
    EXPECT_EQ(sim::MemoryOfChange(Acquaintance, Rival),
              sim::MemoryKind::BecameRivals);
    EXPECT_EQ(sim::MemoryOfChange(Rival, Acquaintance),
              sim::MemoryKind::MadePeace);
    EXPECT_FALSE(sim::MemoryOfChange(CloseFriend, Friend).has_value());
}

TEST(Social, DescribesInterestsAndMemories) {
    EXPECT_EQ(sim::InterestList(0), "nothing in particular");
    EXPECT_EQ(sim::InterestList(0b1001), "drone racing and poetry");
    EXPECT_EQ(sim::InterestList(0b111),
              "drone racing, circuit bending and old films");
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::BecameFriends, "KX-417 Vera"),
              "became friends with KX-417 Vera");
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::FellBehindOnRent, ""),
              "fell behind on rent");
}

TEST(Social, ArrestMemoryLinesNameTheOfficer) {
    // B15c: both sides of an arrest on the personal cards; memories from
    // version 19 saves never filled the other robot, so they keep the old,
    // vaguer line.
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::Arrested, "RB-07 Vex"),
              "arrested by RB-07 Vex");
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::Arrested, ""),
              "arrested for robbing");
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::ArrestedSomeone, "HK-632 Pike"),
              "arrested HK-632 Pike");
    EXPECT_EQ(sim::MemoryText(sim::MemoryKind::ArrestedSomeone, ""),
              "arrested someone");
}

TEST(Social, RobotsAreBornWithTwoInterestsAndNoBonds) {
    const sim::World world = sim::World::Generate({.seed = 4, .robots = 80});
    std::set<std::uint8_t> kinds;
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_EQ(std::popcount(robot.interests), 2) << robot.name;
        EXPECT_TRUE(robot.bonds.empty());
        EXPECT_TRUE(robot.memories.empty());
        kinds.insert(robot.interests);
    }
    EXPECT_GT(kinds.size(), 10U);  // Tastes differ.
}

// After two weeks robots have friends, a few have rivals, and every bond and
// memory stays within its limits.
TEST(Social, FriendshipsAndRivalriesFormOverWeeks) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 200});
    world.RunFor(14 * sim::kMinutesPerDay);

    std::uint32_t withFriend = 0;
    std::uint32_t withRival = 0;
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_LE(robot.bonds.size(), 12U);
        EXPECT_LE(robot.memories.size(), 12U);
        EXPECT_TRUE(std::ranges::is_sorted(robot.bonds, std::less{},
                                           &sim::Bond::robot));
        bool friendly = false;
        bool rival = false;
        for (const sim::Bond& bond : robot.bonds) {
            EXPECT_NE(bond.robot, robot.id);
            EXPECT_LE(std::abs(bond.affinity), sim::Bond::kMaxAffinity);
            EXPECT_NE(world.FindRobot(bond.robot), nullptr);
            friendly = friendly || bond.level == sim::BondLevel::Friend ||
                       bond.level == sim::BondLevel::CloseFriend;
            rival = rival || bond.level == sim::BondLevel::Rival;
        }
        withFriend += friendly ? 1 : 0;
        withRival += rival ? 1 : 0;
        EXPECT_TRUE(std::ranges::is_sorted(robot.memories, std::less{},
                                           &sim::Memory::tick));
    }
    EXPECT_GT(withFriend, 200U / 2) << "most robots have a friend";
    EXPECT_GT(withRival, 0U);
    EXPECT_LT(withRival, 160U) << "rivals are the exception";
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Robots who share interests like each other more than robots who share
// none.
TEST(Social, SharedInterestsMakeFriends) {
    sim::World world = sim::World::Generate({.seed = 12, .robots = 200});
    world.RunFor(14 * sim::kMinutesPerDay);
    std::int64_t sharedSum = 0;
    std::int64_t sharedCount = 0;
    std::int64_t apartSum = 0;
    std::int64_t apartCount = 0;
    for (const sim::Robot& robot : world.State().robots) {
        for (const sim::Bond& bond : robot.bonds) {
            const sim::Robot* other = world.FindRobot(bond.robot);
            ASSERT_NE(other, nullptr);
            if ((robot.interests & other->interests) != 0) {
                sharedSum += bond.affinity;
                ++sharedCount;
            } else {
                apartSum += bond.affinity;
                ++apartCount;
            }
        }
    }
    ASSERT_GT(sharedCount, 0);
    ASSERT_GT(apartCount, 0);
    EXPECT_GT(sharedSum / sharedCount, 8);
    EXPECT_LT(apartSum / apartCount, 0);
}

// Both robots of a talk hear about it, and nobody is in two talks at once.
TEST(Social, TalksComeInPairsOneAtATime) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 120});
    std::vector<sim::SimEvent> talks;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::Talked) {
            talks.push_back(event);
        }
    });
    world.RunFor(3 * sim::kMinutesPerDay);
    ASSERT_FALSE(talks.empty());
    ASSERT_EQ(talks.size() % 2, 0U);
    std::map<sim::Tick, std::set<std::uint64_t>> talking;
    for (std::size_t index = 0; index < talks.size(); index += 2) {
        const sim::SimEvent& first = talks[index];
        const sim::SimEvent& second = talks[index + 1];
        EXPECT_EQ(first.robot, second.otherRobot);
        EXPECT_EQ(first.otherRobot, second.robot);
        EXPECT_EQ(first.topic, second.topic);
        EXPECT_EQ(first.change, second.change);
        EXPECT_EQ(first.room, second.room);
        EXPECT_EQ(first.tick % 10, 0U);
        EXPECT_TRUE(talking[first.tick].insert(first.robot.value).second);
        EXPECT_TRUE(talking[first.tick].insert(first.otherRobot.value).second);
    }
    const std::string line = world.Describe(talks.front());
    EXPECT_TRUE(line.find(" chatted with ") != std::string::npos ||
                line.find(" argued with ") != std::string::npos)
        << line;
    EXPECT_NE(line.find(" about "), std::string::npos) << line;
}

// Robots finishing a shift are drawn to the venue where a close friend is
// spending the evening, even when they would have gone elsewhere alone.
TEST(Social, CloseFriendsDrawEachOtherToVenues) {
    sim::World base = sim::World::Generate({.seed = 31, .robots = 300});
    base.RunFor(sim::kMinutesPerDay);
    constexpr sim::Tick kWindow = 3 * 60;
    constexpr std::size_t kWantedWorkers = 15;
    constexpr std::size_t kWantedGuests = 20;

    // The first half hour of Day 2 with enough robots about to leave work
    // and enough robots out.
    std::vector<std::size_t> workers;
    std::size_t guests = 0;
    for (int search = 0; search < 48 && (workers.size() < kWantedWorkers ||
                                         guests < kWantedGuests);
         ++search) {
        base.RunFor(30);
        workers.clear();
        guests = 0;
        const auto& robots = base.State().robots;
        for (std::size_t index = 0; index < robots.size(); ++index) {
            const sim::Robot& robot = robots[index];
            if (robot.activity == sim::RobotActivity::Working &&
                robot.nextActionAt <= base.CurrentTick() + kWindow) {
                workers.push_back(index);
            }
            guests += robot.activity == sim::RobotActivity::Visiting ? 1 : 0;
        }
    }
    ASSERT_GE(workers.size(), kWantedWorkers);
    ASSERT_GE(guests, kWantedGuests);
    const sim::Tick now = base.CurrentTick();

    // Runs the next hours and returns the first venue each worker goes to.
    const auto firstVenues = [&](const sim::WorldState& start) {
        sim::World world = Load(start);
        std::map<std::uint64_t, sim::EntityId> first;
        world.SetEventSink([&](const sim::SimEvent& event) {
            if (event.kind == sim::EventKind::ArrivedAtVenue) {
                first.try_emplace(event.robot.value, event.room);
            }
        });
        world.RunFor(kWindow + 60);
        return first;
    };
    const auto alone = firstVenues(base.State());

    // Give workers a close friend who is out at another venue and stays
    // there for the evening.
    sim::WorldState withFriends = base.State();
    std::map<std::uint64_t, sim::EntityId> friendVenue;
    // Only friends at venues with room for everyone joining them.
    std::set<std::size_t> used;
    std::map<std::uint64_t, std::uint32_t> joining;
    auto& robots = withFriends.robots;
    for (const std::size_t worker : workers) {
        sim::Robot& a = robots[worker];
        const auto own = alone.find(a.id.value);
        for (std::size_t guest = 0; guest < robots.size(); ++guest) {
            sim::Robot& b = robots[guest];
            if (b.activity != sim::RobotActivity::Visiting ||
                used.contains(guest) ||
                (own != alone.end() && own->second == b.location)) {
                continue;
            }
            const auto venue = base.Rooms().Find(b.location);
            // At most three per venue, with seats to spare for robots
            // already on their way there.
            constexpr std::uint32_t kPerVenue = 3;
            std::uint32_t& count = joining[b.location.value];
            if (count >= kPerVenue ||
                base.GuestsIn(b.location) + count + kPerVenue >
                    venue->room->capacity) {
                continue;
            }
            ++count;
            used.insert(guest);
            b.nextActionAt = std::max(b.nextActionAt, a.nextActionAt + 120);
            friendVenue[a.id.value] = b.location;
            a.bonds = {{.robot = b.id,
                        .affinity = 90,
                        .level = sim::BondLevel::CloseFriend,
                        .talks = 30,
                        .lastTalk = now}};
            b.bonds = {{.robot = a.id,
                        .affinity = 90,
                        .level = sim::BondLevel::CloseFriend,
                        .talks = 30,
                        .lastTalk = now}};
            break;
        }
    }
    ASSERT_GE(friendVenue.size(), 6U);

    std::size_t joined = 0;
    for (const auto& [robot, venue] : firstVenues(withFriends)) {
        const auto wanted = friendVenue.find(robot);
        joined += wanted != friendVenue.end() && wanted->second == venue ? 1 : 0;
    }
    EXPECT_GE(joined * 2, friendVenue.size())
        << joined << " of " << friendVenue.size()
        << " went to their close friend's venue instead of their own choice";
}

TEST(Social, ValidationRejectsBrokenBonds) {
    sim::World world = sim::World::Generate({.seed = 2, .robots = 30});
    const auto rejects = [](sim::WorldState state, const char* expected) {
        auto result = sim::World::FromState(std::move(state));
        ASSERT_FALSE(result.has_value()) << expected;
        EXPECT_NE(result.error().find(expected), std::string::npos)
            << result.error();
    };
    const sim::EntityId self = world.State().robots[0].id;
    const sim::EntityId first = world.State().robots[1].id;
    const sim::EntityId second = world.State().robots[2].id;

    sim::WorldState selfLove = world.State();
    selfLove.robots[0].bonds = {{.robot = self}};
    rejects(selfLove, "bond");

    sim::WorldState stranger = world.State();
    stranger.robots[0].bonds = {{.robot = sim::EntityId{999999}}};
    rejects(stranger, "bond");

    sim::WorldState unsorted = world.State();
    unsorted.robots[0].bonds = {{.robot = second}, {.robot = first}};
    rejects(unsorted, "bond");

    sim::WorldState obsessed = world.State();
    obsessed.robots[0].bonds = {{.robot = first, .affinity = 101}};
    rejects(obsessed, "bond");

    sim::WorldState ghost = world.State();
    ghost.robots[0].memories = {{.kind = sim::MemoryKind::BecameFriends,
                                 .robot = sim::EntityId{999999}}};
    rejects(ghost, "memory");

    sim::WorldState fine = world.State();
    fine.robots[0].bonds = {{.robot = first, .affinity = 30,
                             .level = sim::BondLevel::Friend}};
    fine.robots[0].memories = {{.kind = sim::MemoryKind::BecameFriends,
                                .robot = first}};
    EXPECT_TRUE(sim::World::FromState(std::move(fine)).has_value());
}
