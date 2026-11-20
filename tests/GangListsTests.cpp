#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "GangLists.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/World.hpp"

#include <gtest/gtest.h>

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Gangs.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Social.hpp"

namespace mechapunk {

namespace {

// A frame with these robots (id, gang) and, for each robot in the same
// order as `robots`, the memories the frame publishes in its social list.
WorldFrame MakeFrame(
    const std::vector<std::pair<sim::EntityId, sim::EntityId>>& robotGangs,
    const std::vector<std::vector<sim::Memory>>& memories) {
    WorldFrame frame;
    std::vector<RobotSocial> social;
    for (std::size_t i = 0; i < robotGangs.size(); ++i) {
        RobotFrame robot;
        robot.id = robotGangs[i].first;
        robot.gang = robotGangs[i].second;
        frame.robots.push_back(robot);
        RobotSocial person;
        if (i < memories.size()) {
            person.memories = memories[i];
        }
        social.push_back(std::move(person));
    }
    frame.social =
        std::make_shared<const std::vector<RobotSocial>>(std::move(social));
    return frame;
}

sim::Memory MakeMemory(sim::Tick tick, sim::MemoryKind kind,
    sim::EntityId other, sim::EntityId room) {
    return sim::Memory{
        .tick = tick,
        .kind = kind,
        .robot = other,
        .room = room,
    };
}

// Gangs A (turf building 100), B and C; B rivals A, C is friendly to A.
void AddThreeGangs(std::vector<sim::Gang>& gangs) {
    gangs.push_back(sim::Gang{
        .id = sim::EntityId{10},
        .name = "Alpha",
        .color = 0xFF0000,
        .turf = sim::EntityId{100},
        .relations = {
            sim::GangRelation{sim::EntityId{20}, sim::Stance::Rival},
            sim::GangRelation{sim::EntityId{30}, sim::Stance::Friendly},
        },
    });
    gangs.push_back(sim::Gang{
        .id = sim::EntityId{20},
        .name = "Bravo",
        .relations = {sim::GangRelation{sim::EntityId{10}, sim::Stance::Rival}},
    });
    gangs.push_back(sim::Gang{
        .id = sim::EntityId{30},
        .name = "Charlie",
        .relations = {sim::GangRelation{sim::EntityId{10}, sim::Stance::Friendly}},
    });
}

// Test MembersOf returns the right robots sorted by id.
TEST(GangListsTests, MembersOfReturnsSorted) {
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{10}},
            {sim::EntityId{2}, sim::EntityId{10}},
            {sim::EntityId{5}, sim::EntityId{10}},
            {sim::EntityId{8}, sim::EntityId{20}}},
        {});

    std::vector<const RobotFrame*> members = MembersOf(frame, sim::EntityId{10});

    ASSERT_EQ(members.size(), 3U);
    EXPECT_EQ(members[0]->id, sim::EntityId{1});
    EXPECT_EQ(members[1]->id, sim::EntityId{2});
    EXPECT_EQ(members[2]->id, sim::EntityId{5});
}

// Test MembersOf returns empty when no robots are in the gang.
TEST(GangListsTests, MembersOfEmpty) {
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{20}},
            {sim::EntityId{2}, sim::EntityId{20}}},
        {});

    EXPECT_TRUE(MembersOf(frame, sim::EntityId{10}).empty());
}

// GangCrimes shows only the memories of the gang's own members: a rival
// robot's identical memory must not appear.
TEST(GangListsTests, GangCrimesCollectsOnlyMembers) {
    std::vector<std::vector<sim::Memory>> memories = {
        {MakeMemory(sim::Tick{100}, sim::MemoryKind::RobbedSomeone,
            sim::EntityId{77}, sim::EntityId{555})},
        {MakeMemory(sim::Tick{120}, sim::MemoryKind::RobbedSomeone,
            sim::EntityId{88}, sim::EntityId{666})},
    };
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{10}},
            {sim::EntityId{2}, sim::EntityId{20}}},
        memories);

    std::vector<GangCrime> crimes = GangCrimes(frame, sim::EntityId{10});

    ASSERT_EQ(crimes.size(), 1U);
    EXPECT_EQ(crimes[0].kind, CrimeKind::Robbery);
    EXPECT_EQ(crimes[0].text, "robbed");
    EXPECT_EQ(crimes[0].member, sim::EntityId{1});
    EXPECT_EQ(crimes[0].perpetrator, sim::EntityId{1});
    EXPECT_EQ(crimes[0].victim, sim::EntityId{77});
    EXPECT_EQ(crimes[0].location, sim::EntityId{555});
}

// Was-robbed rows put the robber in `perpetrator` and the member in `victim`;
// arrest rows have no victim; rows come newest first.
TEST(GangListsTests, GangCrimesFieldsAndOrder) {
    std::vector<std::vector<sim::Memory>> memories = {
        {MakeMemory(sim::Tick{90}, sim::MemoryKind::WasRobbed,
            sim::EntityId{44}, sim::EntityId{700}),
            MakeMemory(sim::Tick{95}, sim::MemoryKind::Arrested,
                sim::EntityId{}, sim::EntityId{701})},
    };
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{10}}}, memories);

    std::vector<GangCrime> crimes = GangCrimes(frame, sim::EntityId{10});

    ASSERT_EQ(crimes.size(), 2U);
    EXPECT_EQ(crimes[0].kind, CrimeKind::Arrest);
    EXPECT_EQ(crimes[0].text, "arrested");
    EXPECT_FALSE(crimes[0].victim.IsValid());
    EXPECT_EQ(crimes[0].location, sim::EntityId{701});
    EXPECT_EQ(crimes[1].kind, CrimeKind::WasRobbed);
    EXPECT_EQ(crimes[1].text, "was robbed");
    EXPECT_EQ(crimes[1].perpetrator, sim::EntityId{44});
    EXPECT_EQ(crimes[1].victim, sim::EntityId{1});
    EXPECT_EQ(crimes[1].location, sim::EntityId{700});
}

// Rows of the same minute fall back to member id, whatever order the
// frame's robot list has.
TEST(GangListsTests, GangCrimesOrdersTiesByMemberId) {
    std::vector<std::vector<sim::Memory>> memories = {
        {MakeMemory(sim::Tick{100}, sim::MemoryKind::RobbedSomeone,
            sim::EntityId{5}, sim::EntityId{})},
        {MakeMemory(sim::Tick{100}, sim::MemoryKind::RobbedSomeone,
            sim::EntityId{6}, sim::EntityId{})},
    };
    // Robots deliberately unsorted: id 2 first.
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{2}, sim::EntityId{10}},
            {sim::EntityId{1}, sim::EntityId{10}}},
        memories);

    std::vector<GangCrime> crimes = GangCrimes(frame, sim::EntityId{10});

    ASSERT_EQ(crimes.size(), 2U);
    EXPECT_EQ(crimes[0].member, sim::EntityId{1});
    EXPECT_EQ(crimes[1].member, sim::EntityId{2});
}

// A frame whose social list is not published yields no rows.
TEST(GangListsTests, GangCrimesEmptyWithoutSocial) {
    WorldFrame frame;
    RobotFrame robot;
    robot.id = sim::EntityId{1};
    robot.gang = sim::EntityId{10};
    frame.robots.push_back(robot);
    frame.social = nullptr;

    EXPECT_TRUE(GangCrimes(frame, sim::EntityId{10}).empty());
}

// RivalsInTurf lists a rival gang's robots inside the turf building only:
// not rivals standing elsewhere, not friendly or own members standing on the
// turf.
TEST(GangListsTests, RivalsInTurfOnlyRivalsInside) {
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{20}}, // Bravo (rival) on turf room 1001.
            {sim::EntityId{5}, sim::EntityId{20}}, // Bravo on turf room 1000.
            {sim::EntityId{3}, sim::EntityId{30}}, // Charlie (friendly) on turf.
            {sim::EntityId{7}, sim::EntityId{10}}, // Alpha (own) on turf.
            {sim::EntityId{9}, sim::EntityId{20}}}, // Bravo outside the turf.
        {});
    frame.robots[0].location = sim::EntityId{1001};
    frame.robots[1].location = sim::EntityId{1000};
    frame.robots[2].location = sim::EntityId{1000};
    frame.robots[3].location = sim::EntityId{1000};
    frame.robots[4].location = sim::EntityId{2000};

    std::vector<sim::Gang> gangs;
    AddThreeGangs(gangs);
    frame.gangs =
        std::make_shared<const std::vector<sim::Gang>>(std::move(gangs));

    sim::City city;
    sim::Building turf;
    turf.id = sim::EntityId{100};
    turf.name = "Turf Hall";
    turf.rooms = {sim::Room{.id = sim::EntityId{1000}},
        sim::Room{.id = sim::EntityId{1001}}};
    city.buildings.push_back(std::move(turf));
    sim::Building elsewhere;
    elsewhere.id = sim::EntityId{200};
    elsewhere.name = "Somewhere Else";
    elsewhere.rooms = {sim::Room{.id = sim::EntityId{2000}}};
    city.buildings.push_back(std::move(elsewhere));
    frame.city = std::make_shared<const sim::City>(std::move(city));

    std::vector<const RobotFrame*> rivals = RivalsInTurf(frame, sim::EntityId{10});

    ASSERT_EQ(rivals.size(), 2U);
    EXPECT_EQ(rivals[0]->id, sim::EntityId{1});
    EXPECT_EQ(rivals[1]->id, sim::EntityId{5});
}

// RivalsInTurf is empty when the gang has no turf or the frame has no gangs.
TEST(GangListsTests, RivalsInTurfNeedsTurf) {
    WorldFrame frame = MakeFrame(
        {{sim::EntityId{1}, sim::EntityId{20}}}, {});
    frame.robots[0].location = sim::EntityId{1000};

    std::vector<sim::Gang> gangs;
    AddThreeGangs(gangs);
    gangs[0].turf = sim::EntityId{};
    frame.gangs =
        std::make_shared<const std::vector<sim::Gang>>(std::move(gangs));
    EXPECT_TRUE(RivalsInTurf(frame, sim::EntityId{10}).empty());

    frame.gangs = nullptr;
    EXPECT_TRUE(RivalsInTurf(frame, sim::EntityId{10}).empty());
}

// Test TurfOf returns the gang whose turf a building is.
TEST(GangListsTests, TurfOfReturnsGang) {
    std::vector<sim::Gang> gangs = {
        sim::Gang{.id = sim::EntityId{10}, .name = "Test Gang", .turf = sim::EntityId{100}},
    };

    WorldFrame frame;
    frame.tick = sim::Tick{100};
    frame.gangs = std::make_shared<const std::vector<sim::Gang>>(gangs);

    std::optional<sim::EntityId> result = TurfOf(frame, sim::EntityId{100});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, sim::EntityId{10});

    result = TurfOf(frame, sim::EntityId{200});
    EXPECT_FALSE(result.has_value());
}

// Test TurfOf returns nullopt when the gangs frame is null.
TEST(GangListsTests, TurfOfNullGangs) {
    WorldFrame frame;
    frame.tick = sim::Tick{100};
    frame.gangs = nullptr;

    std::optional<sim::EntityId> result = TurfOf(frame, sim::EntityId{100});
    EXPECT_FALSE(result.has_value());
}

}  // namespace

}  // namespace mechapunk
