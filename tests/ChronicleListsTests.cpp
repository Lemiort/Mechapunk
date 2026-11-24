// Tests for step C4a of docs/tasks/C-chronicle.md: the chronicle lists the
// game's history pages draw (app/ChronicleLists.cpp). A generated city gives
// the population-level tests real entries of many kinds; the filter and group
// tests write their chronicle by hand, so exact kinds, days and names do not
// move when the random stream changes.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ChronicleLists.hpp"
#include "OrganizationLists.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

// Bring in types from the game layer (WorldFrame, RobotFrame, etc.).
using mechapunk::ChronicleFilter;
using mechapunk::ChronicleGroup;
using mechapunk::EconomyFrame;
using mechapunk::RobotDirectory;
using mechapunk::RobotInfo;
using mechapunk::RobotFrame;
using mechapunk::WorldFrame;
using mechapunk::EntriesAboutBuilding;
using mechapunk::EntriesAboutOrganization;
using mechapunk::EntriesAboutRobot;
using mechapunk::EntriesAboutRoom;
using mechapunk::FilterChronicle;
using mechapunk::GroupOf;
using mechapunk::WorkersOf;

namespace {

// A city with a chronicle worth reading, made once: 30 days at 1000 robots
// gives entries of nine kinds (tests/ChronicleTests.cpp checks that), so
// every list below has real rows to check.
const sim::World& City() {
    static const sim::World world = [] {
        sim::World generated = sim::World::Generate({.seed = 5, .robots = 1000});
        generated.RunFor(30 * sim::kMinutesPerDay);
        return generated;
    }();
    return world;
}

// A frame over that city, with just what the list functions read: robots
// and their workspaces, the economy's organizations and owner map, the city,
// the directory and a copy of the world's chronicle.
std::shared_ptr<WorldFrame> MakeFrame(const sim::World& world) {
    auto frame = std::make_shared<WorldFrame>();
    const auto& state = world.State();

    auto economy = std::make_shared<EconomyFrame>();
    economy->organizations = state.economy.organizations;
    auto owners = std::make_shared<std::unordered_map<std::uint64_t, sim::EntityId>>();
    for (const auto& building : state.city.buildings) {
        for (const auto& room : building.rooms) {
            owners->emplace(room.id.value, room.owner);
        }
    }
    economy->owners = owners;

    auto directory = std::make_shared<RobotDirectory>();
    for (const auto& robot : state.robots) {
        directory->emplace(robot.id.value, RobotInfo{.name = robot.name});
    }
    for (const auto& departed : state.departed) {
        directory->emplace(departed.id.value,
                           RobotInfo{.name = departed.name, .departed = true});
    }

    frame->directory = directory;
    frame->economy = economy;
    frame->city = std::make_shared<const sim::City>(state.city);
    frame->chronicle = std::make_shared<std::vector<sim::ChronicleEntry>>(state.chronicle);
    for (const auto& robot : state.robots) {
        frame->robots.push_back(RobotFrame{.id = robot.id, .workspace = robot.workspace});
    }
    return frame;
}

// Newest first means ticks never increase.
void ExpectNewestFirst(const std::vector<const sim::ChronicleEntry*>& entries) {
    ASSERT_FALSE(entries.empty());
    for (std::size_t index = 1; index < entries.size(); ++index) {
        EXPECT_LE(entries[index]->tick, entries[index - 1]->tick)
            << "entry " << index << " is older than the one before it";
    }
}

// The id a chronicle names most often, and how often: the history page of
// such an id is the one whose rows can be checked against the whole list.
std::pair<std::uint64_t, std::size_t> MostFrequent(
    const std::map<std::uint64_t, std::size_t>& counts) {
    const auto best = std::ranges::max_element(counts, {}, [](const auto& pair) {
        return pair.second;
    });
    if (best == counts.end()) {
        return {};
    }
    return std::pair{best->first, best->second};
}

// A chronicle written by hand, with the names its robots have: the filters
// need exact kinds, days and names, which a generated city cannot promise.
std::shared_ptr<WorldFrame> MakeHandFrame(
    const std::vector<sim::ChronicleEntry>& entries,
    const std::map<std::uint64_t, std::string>& names) {
    auto frame = std::make_shared<WorldFrame>();
    auto directory = std::make_shared<RobotDirectory>();
    for (const auto& [id, name] : names) {
        directory->emplace(id, RobotInfo{.name = name});
    }
    frame->directory = directory;
    frame->chronicle = std::make_shared<std::vector<sim::ChronicleEntry>>(entries);
    return frame;
}

// Entries about a robot: newest first, and only entries that name it - as
// either of the two robots, so a robot robbed by another one finds the row.
TEST(ChronicleLists, RobotHistoryIsNewestFirst) {
    const auto frame = MakeFrame(City());

    std::map<std::uint64_t, std::size_t> counts;
    for (const auto& entry : *frame->chronicle) {
        if (entry.robot.IsValid()) ++counts[entry.robot.value];
        if (entry.otherRobot.IsValid()) ++counts[entry.otherRobot.value];
    }
    const auto [best_id, best_count] = MostFrequent(counts);
    ASSERT_GE(best_count, 2U) << "the chronicle names no robot twice";
    const auto robot = sim::EntityId{best_id};

    const auto entries = EntriesAboutRobot(*frame, robot);
    ExpectNewestFirst(entries);
    EXPECT_EQ(entries.size(), best_count);
    for (const auto* entry : entries) {
        EXPECT_TRUE(entry->robot == robot || entry->otherRobot == robot);
    }
}

// Entries about a room: newest first, and only entries whose room it is.
TEST(ChronicleLists, RoomHistoryNamesItsRoom) {
    const auto frame = MakeFrame(City());

    std::map<std::uint64_t, std::size_t> counts;
    for (const auto& entry : *frame->chronicle) {
        if (entry.room.IsValid()) ++counts[entry.room.value];
    }
    const auto [best_id, best_count] = MostFrequent(counts);
    ASSERT_GE(best_count, 2U) << "the chronicle names no room twice";
    const auto room = sim::EntityId{best_id};

    const auto entries = EntriesAboutRoom(*frame, room);
    ExpectNewestFirst(entries);
    EXPECT_EQ(entries.size(), best_count);
    for (const auto* entry : entries) {
        EXPECT_EQ(entry->room, room);
    }
}

// Entries about a building: every entry in one of its rooms, newest first,
// and never one from another building.
TEST(ChronicleLists, BuildingHistoryCoversItsRooms) {
    const auto frame = MakeFrame(City());

    // Which building each room belongs to.
    std::unordered_map<std::uint64_t, std::uint64_t> building_of_room;
    for (const auto& building : frame->city->buildings) {
        for (const auto& room : building.rooms) {
            building_of_room.emplace(room.id.value, building.id.value);
        }
    }

    std::map<std::uint64_t, std::size_t> per_building;
    std::map<std::uint64_t, std::size_t> per_room;
    for (const auto& entry : *frame->chronicle) {
        const auto found = building_of_room.find(entry.room.value);
        if (found != building_of_room.end()) ++per_building[found->second];
        if (entry.room.IsValid()) ++per_room[entry.room.value];
    }
    const auto [best_building, best_count] = MostFrequent(per_building);
    ASSERT_GE(best_count, 2U) << "no building has two entries to list";
    const auto building = sim::EntityId{best_building};

    const auto entries = EntriesAboutBuilding(*frame, building);
    ExpectNewestFirst(entries);
    EXPECT_EQ(entries.size(), best_count);
    for (const auto* entry : entries) {
        EXPECT_EQ(building_of_room.at(entry->room.value), building.value);
    }
    // A building's history is at least as long as its busiest room's.
    const auto [busiest_room, busiest_count] = MostFrequent(per_room);
    EXPECT_GE(entries.size(), busiest_count)
        << "the busiest room's " << busiest_count << " entries are missing";
    EXPECT_EQ(EntriesAboutRoom(*frame, sim::EntityId{busiest_room}).size(), busiest_count);
}

// Entries about an organization: its rooms, the robots it employs now and the
// money moved for its own account, and nothing else.
TEST(ChronicleLists, OrganizationHistoryIsItsRoomsWorkersAndAccount) {
    const auto frame = MakeFrame(City());

    // The organization owning the most rooms the chronicle names.
    std::map<std::uint64_t, std::size_t> per_owner;
    for (const auto& entry : *frame->chronicle) {
        if (!entry.room.IsValid()) continue;
        const auto owner = frame->economy->owners->find(entry.room.value);
        if (owner != frame->economy->owners->end() && owner->second.IsValid()) {
            ++per_owner[owner->second.value];
        }
    }
    const auto [best_owner, best_count] = MostFrequent(per_owner);
    ASSERT_GE(best_count, 2U) << "no organization owns two rooms with entries";
    const auto organization = sim::EntityId{best_owner};

    const auto entries = EntriesAboutOrganization(*frame, organization);
    ExpectNewestFirst(entries);
    EXPECT_GE(entries.size(), best_count) << "its own rooms are missing from its page";

    // Every row is about a room it owns, a robot it employs now, or money
    // moved for its own account.
    std::unordered_set<std::uint64_t> workers;
    for (const auto* robot : WorkersOf(*frame, organization)) {
        workers.insert(robot->id.value);
    }
    for (const auto* entry : entries) {
        const auto owner = frame->economy->owners->find(entry->room.value);
        const bool owns_room =
            owner != frame->economy->owners->end() && owner->second == organization;
        const bool employs = workers.contains(entry->robot.value) ||
                             workers.contains(entry->otherRobot.value);
        const bool funded = entry->account == organization;
        EXPECT_TRUE(owns_room || employs || funded)
            << "an entry about neither its room, its worker nor its account";
    }
}

// The third rule on its own: an emission that funded an organization names no
// room and no robot, so it reaches a page only through the entry's account.
TEST(ChronicleLists, OrganizationHistoryFindsItsOwnEmissions) {
    const auto frame = MakeFrame(City());

    // Which ids are organizations at all: an entry's account can also be a
    // robot's savings account, and those are nobody's history page.
    std::unordered_set<std::uint64_t> organization_ids;
    for (const auto& organization : frame->economy->organizations) {
        organization_ids.insert(organization.id.value);
    }
    std::map<std::uint64_t, std::size_t> per_account;
    for (const auto& entry : *frame->chronicle) {
        if (organization_ids.contains(entry.account.value)) ++per_account[entry.account.value];
    }
    const auto [best_id, best_count] = MostFrequent(per_account);
    ASSERT_GE(best_count, 2U) << "the chronicle names no organization as an account";
    const auto organization = sim::EntityId{best_id};

    const auto entries = EntriesAboutOrganization(*frame, organization);
    ExpectNewestFirst(entries);
    std::size_t funded = 0;
    for (const auto* entry : entries) {
        if (entry->account == organization) ++funded;
    }
    EXPECT_EQ(funded, best_count) << "the money it was given is missing from its page";
}

// The third rule exactly, written by hand so the kinds and accounts cannot
// move with the random stream: an emission that funded the organization is a
// row of its own, a unit whose owner was the organization is a row even after
// the room changed hands, and an emission for a robot's savings account or
// for another organization is not.
TEST(ChronicleLists, OrganizationHistoryIsItsAccountEmissions) {
    constexpr sim::Tick kDay = sim::kMinutesPerDay;
    const auto employer = sim::EntityId{400};
    const auto saver = sim::EntityId{401};
    const auto other = sim::EntityId{402};
    const std::vector<sim::ChronicleEntry> entries = {
        // Money created for the organization: no room, no robot, only its
        // account, so only the third rule can find it.
        {.tick = kDay,
         .kind = sim::EventKind::MoneySupplyChanged,
         .credits = 50000,
         .account = employer},
        // Money destroyed for it, later.
        {.tick = 2 * kDay,
         .kind = sim::EventKind::MoneySupplyChanged,
         .credits = -1200,
         .account = employer},
        // A unit that closed while the organization owned it; the room is
        // nobody's rule's here, the account still names the organization.
        {.tick = 3 * kDay,
         .kind = sim::EventKind::UnitClosed,
         .room = sim::EntityId{90},
         .account = employer},
        // A robot's own savings emission, and an emission for another
        // organization: neither belongs on the first organization's page.
        {.tick = 4 * kDay,
         .kind = sim::EventKind::MoneySupplyChanged,
         .credits = 30000,
         .account = saver},
        {.tick = 5 * kDay,
         .kind = sim::EventKind::MoneySupplyChanged,
         .credits = 40000,
         .account = other},
    };
    // A frame with no economy at all: the account rule reads the entry alone.
    const auto frame = MakeHandFrame(entries, {});

    const auto rows = EntriesAboutOrganization(*frame, employer);
    ExpectNewestFirst(rows);
    ASSERT_EQ(rows.size(), 3U) << "the account rule found no row of its own";
    EXPECT_EQ(rows.front()->tick, 3 * kDay);
    EXPECT_EQ(rows.back()->tick, kDay);
    for (const auto* row : rows) {
        EXPECT_EQ(row->account, employer);
    }
    // A robot's own savings emission joins no organization's page: ids come
    // from one counter per world, so a robot's id is never an organization's
    // id, and the rule matches the entry's account alone. The other
    // organization gets its own emission and not the robot's.
    const auto other_rows = EntriesAboutOrganization(*frame, other);
    ASSERT_EQ(other_rows.size(), 1U);
    EXPECT_EQ(other_rows.front()->tick, 5 * kDay);
}

// Nothing to draw, and no crash: an id that exists nowhere, an id that is
// none, and a frame published before the first chronicle entry.
TEST(ChronicleLists, NoChronicleOrNoIdGivesEmptyLists) {
    const auto frame = MakeFrame(City());
    const auto unknown = sim::EntityId{std::numeric_limits<std::uint64_t>::max()};
    const auto none = sim::EntityId{};
    for (const auto id : {unknown, none}) {
        EXPECT_TRUE(EntriesAboutRobot(*frame, id).empty());
        EXPECT_TRUE(EntriesAboutRoom(*frame, id).empty());
        EXPECT_TRUE(EntriesAboutBuilding(*frame, id).empty());
        EXPECT_TRUE(EntriesAboutOrganization(*frame, id).empty());
    }

    frame->chronicle = nullptr;
    EXPECT_TRUE(EntriesAboutRobot(*frame, sim::EntityId{7}).empty());
    EXPECT_TRUE(EntriesAboutRoom(*frame, sim::EntityId{7}).empty());
    EXPECT_TRUE(EntriesAboutBuilding(*frame, sim::EntityId{7}).empty());
    EXPECT_TRUE(EntriesAboutOrganization(*frame, sim::EntityId{7}).empty());
    EXPECT_TRUE(FilterChronicle(*frame, {}).empty());
}

// The six filter buttons: each kind the chronicle keeps answers to one of
// them, and a kind it never keeps answers to none, so a button hides it.
TEST(ChronicleLists, GroupOfNamesTheButtons) {
    const auto entry = [](sim::EventKind kind, sim::MemoryKind memory = sim::MemoryKind{}) {
        return sim::ChronicleEntry{.tick = 0, .kind = kind, .memory = memory};
    };
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Scrapped)), ChronicleGroup::People);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Arrived)), ChronicleGroup::People);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::BecameCloseFriends)),
              ChronicleGroup::People);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::BecameRivals)),
              ChronicleGroup::People);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::LostJob)),
              ChronicleGroup::Work);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::Promoted)),
              ChronicleGroup::Work);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::BoughtProperty)), ChronicleGroup::Homes);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::Evicted)),
              ChronicleGroup::Homes);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::UnitOpened)), ChronicleGroup::Venues);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::UnitClosed)), ChronicleGroup::Venues);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Robbed)), ChronicleGroup::Crime);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Arrested)), ChronicleGroup::Crime);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Fought)), ChronicleGroup::Crime);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::JoinedGang)), ChronicleGroup::Crime);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::LeftGang)), ChronicleGroup::Crime);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::MoneySupplyChanged)), ChronicleGroup::Money);
    // Daily life, and the memories the chronicle drops: no button names them.
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Departed)), std::nullopt);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::ArrivedHome)), std::nullopt);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::WeatherChanged)), std::nullopt);
    EXPECT_EQ(GroupOf(entry(sim::EventKind::Remembered, sim::MemoryKind::BecameFriends)),
              std::nullopt);
}

// The day range is inclusive at both ends, and a bound of 0 means no bound.
TEST(ChronicleLists, FilterChronicleByGroupAndDay) {
    constexpr sim::Tick kDay = sim::kMinutesPerDay;
    const std::vector<sim::ChronicleEntry> entries = {
        {.tick = 0, .kind = sim::EventKind::Scrapped},        // day 1
        {.tick = kDay, .kind = sim::EventKind::UnitOpened},   // day 2
        {.tick = 2 * kDay, .kind = sim::EventKind::Robbed},   // day 3
        {.tick = 3 * kDay, .kind = sim::EventKind::Departed}, // day 4
    };
    const auto frame = MakeHandFrame(entries, {});

    // No filter: everything, newest first.
    const auto all = FilterChronicle(*frame, {});
    ASSERT_EQ(all.size(), 4U);
    EXPECT_EQ(all.front()->tick, 3 * kDay);
    EXPECT_EQ(all.back()->tick, 0U);

    // A group button: only its kind.
    const auto crime = FilterChronicle(*frame, {.group = ChronicleGroup::Crime});
    ASSERT_EQ(crime.size(), 1U);
    EXPECT_EQ(crime.front()->kind, sim::EventKind::Robbed);

    // A day range, inclusive: days 2 to 3.
    const auto middle = FilterChronicle(*frame, {.fromDay = 2, .toDay = 3});
    ASSERT_EQ(middle.size(), 2U);
    EXPECT_EQ(middle.front()->tick, 2 * kDay);
    EXPECT_EQ(middle.back()->tick, kDay);

    // A group and a range together.
    EXPECT_TRUE(FilterChronicle(*frame, {.group = ChronicleGroup::Venues, .fromDay = 3}).empty());
    EXPECT_EQ(FilterChronicle(*frame, {.group = ChronicleGroup::Venues, .toDay = 2}).size(), 1U);
}

// The search box finds a robot's name in any case, in either robot field,
// and finds nothing for a name no entry has.
TEST(ChronicleLists, FilterChronicleSearchesNames) {
    const std::vector<sim::ChronicleEntry> entries = {
        {.tick = 5, .kind = sim::EventKind::Scrapped, .robot = sim::EntityId{11}},
        {.tick = 6,
         .kind = sim::EventKind::Robbed,
         .robot = sim::EntityId{12},
         .otherRobot = sim::EntityId{11}},
        {.tick = 7, .kind = sim::EventKind::Arrived, .robot = sim::EntityId{13}},
    };
    const auto frame =
        MakeHandFrame(entries, {{11, "Kato-9"}, {12, "Vex-1"}, {13, "Nobody Else"}});

    const auto found = FilterChronicle(*frame, {.text = "kato"});
    ASSERT_EQ(found.size(), 2U);
    EXPECT_EQ(found.front()->tick, 6U);  // the robbery naming it as the victim
    EXPECT_EQ(found.back()->tick, 5U);

    EXPECT_EQ(FilterChronicle(*frame, {.text = "VEX"}).size(), 1U);
    EXPECT_TRUE(FilterChronicle(*frame, {.text = "zzz"}).empty());
    // A name only one entry's robot has finds that one row.
    EXPECT_EQ(FilterChronicle(*frame, {.text = "nobody"}).size(), 1U);
}

}  // namespace
