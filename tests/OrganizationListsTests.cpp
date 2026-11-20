#include <gtest/gtest.h>

#include <limits>
#include <unordered_set>

#include "OrganizationLists.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;
using namespace sim;  // EntityId, RoomKind, ApartmentClass, etc.

// Bring in types from the game layer (EconomyFrame, RobotFrame, etc.).
using mechapunk::EconomyFrame;
using mechapunk::WorldFrame;
using mechapunk::RobotDirectory;
using mechapunk::RobotInfo;
using mechapunk::RobotFrame;
using mechapunk::OwnedRoom;
using mechapunk::RoomsOwnedBy;
using mechapunk::WorkersOf;
using mechapunk::LoansFrom;

namespace {

// EconomyFrame and RobotFrame helpers to build a frame from a World.
static std::shared_ptr<EconomyFrame> MakeEconomy(const sim::World& world) {
    auto economy = std::make_shared<EconomyFrame>();
    const auto& state = world.State();
    economy->organizations = state.economy.organizations;
    economy->loans = state.economy.loans;
    // Build owners map: room id -> owner id.
    auto owners = std::make_shared<std::unordered_map<std::uint64_t, sim::EntityId>>();
    for (const auto& building : state.city.buildings) {
        for (const auto& room : building.rooms) {
            owners->emplace(room.id.value, room.owner);
        }
    }
    economy->owners = owners;
    return economy;
}

static std::shared_ptr<WorldFrame> MakeWorldFrame(const sim::World& world,
                                                  const std::shared_ptr<EconomyFrame>& eco) {
    auto frame = std::make_shared<WorldFrame>();
    const auto& state = world.State();
    // Build robot directory.
    auto directory = std::make_shared<RobotDirectory>();
    for (const auto& robot : state.robots) {
        directory->emplace(robot.id.value, RobotInfo{
            .name = robot.name,
            .daysOff = robot.daysOff,
            .interests = robot.interests});
    }
    for (const auto& departed : state.departed) {
        directory->emplace(departed.id.value, RobotInfo{.name = departed.name, .departed = true});
    }

    frame->directory = directory;
    frame->economy = eco;
    frame->city = std::make_shared<const sim::City>(state.city);
    // Build robot frames.
    for (const auto& robot : state.robots) {
        RobotFrame rf{
            .id = robot.id,
            .activity = robot.activity,
            .location = robot.location,
            .credits = robot.credits,
            .workspace = robot.workspace,
            .jobLevel = robot.jobLevel};
        switch (robot.activity) {
            case sim::RobotActivity::Resting: ++frame->resting; break;
            case sim::RobotActivity::Working: ++frame->working; break;
            case sim::RobotActivity::Visiting: ++frame->visiting; break;
            case sim::RobotActivity::Walking: ++frame->walking; break;
        }
        frame->robots.push_back(rf);
    }
    return frame;
}

// RoomsOwnedBy: Habitat Trust owns apartments and they are sorted.
TEST(OrganizationLists, RoomsOwnedBySorted) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);

    // Find a housing organization (Habitat Trust or similar).
    EntityId housing_id{};
    for (const auto& org : eco->organizations) {
        if (org.kind == OrganizationKind::Government || org.sector == Sector::Housing) {
            housing_id = org.id;
            break;
        }
    }

    const sim::RoomIndex rooms(world.State().city);
    auto owned = RoomsOwnedBy(*eco, rooms, housing_id);

    ASSERT_FALSE(owned.empty());
    // Check sorted: by building, then floor, then room.
    for (std::size_t i = 1; i < owned.size(); ++i) {
        const auto& prev = owned[i - 1];
        const auto& curr = owned[i];
        if (prev.building == curr.building) {
            ASSERT_LE(prev.floor, curr.floor);
            if (prev.floor == curr.floor) {
                ASSERT_LE(prev.room, curr.room);
            }
        } else {
            ASSERT_LT(prev.building, curr.building);
        }
    }
}

// RoomsOwnedBy: City Hall owns dorm rooms and police stations.
TEST(OrganizationLists, CityHallOwnsDorms) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);

    // Find City Hall (Government organization).
    EntityId city_hall_id{};
    for (const auto& org : eco->organizations) {
        if (org.kind == OrganizationKind::Government) {
            city_hall_id = org.id;
            break;
        }
    }

    const sim::RoomIndex rooms(world.State().city);
    auto owned = RoomsOwnedBy(*eco, rooms, city_hall_id);

    ASSERT_FALSE(owned.empty());
    // City Hall owns dorms (in Habitat buildings) and police stations.
    std::uint32_t dorm_count = 0;
    for (const auto& o : owned) {
        const auto ref = rooms.Find(o.room);
        if (!ref) continue;
        // Dorm is an apartment class, not a RoomKind.
        if (ref->room->apartmentClass == ApartmentClass::Dorm) ++dorm_count;
    }
    // With 300 robots there are many dorms.
    EXPECT_GT(dorm_count, 50U);
}

// RoomsOwnedBy: unknown owner returns empty list.
TEST(OrganizationLists, UnknownOwnerEmpty) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    const sim::RoomIndex rooms(world.State().city);

    // A never-existing id (max value).
    EntityId unknown{std::numeric_limits<std::uint64_t>::max()};
    auto owned = RoomsOwnedBy(*eco, rooms, unknown);

    EXPECT_TRUE(owned.empty());
}

// WorkersOf: every robot with a job is listed exactly once, under the
// organization that owns its job room, sorted by id.
TEST(OrganizationLists, WorkersAreListedUnderTheirEmployer) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    auto frame = MakeWorldFrame(world, eco);

    std::size_t employed = 0;
    for (const auto& robot : frame->robots) {
        employed += robot.workspace.IsValid() ? 1 : 0;
    }
    ASSERT_GT(employed, 100U);

    std::size_t listed = 0;
    std::size_t employers = 0;
    std::unordered_set<std::uint64_t> seen;
    for (const auto& org : eco->organizations) {
        const auto workers = WorkersOf(*frame, org.id);
        employers += workers.empty() ? 0 : 1;
        for (std::size_t i = 0; i < workers.size(); ++i) {
            EXPECT_EQ(OwnerOf(*eco, workers[i]->workspace), org.id);
            EXPECT_TRUE(seen.insert(workers[i]->id.value).second) << "listed twice";
            if (i > 0) {
                EXPECT_LT(workers[i - 1]->id.value, workers[i]->id.value);
            }
        }
        listed += workers.size();
    }
    EXPECT_EQ(listed, employed);
    // Industry, venue chains, City Hall (police) and the Metro Authority.
    EXPECT_GT(employers, 3U);
}

// WorkersOf: an id that owns nothing, or none at all, has no workers.
TEST(OrganizationLists, UnknownEmployerHasNoWorkers) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    auto frame = MakeWorldFrame(world, eco);
    EXPECT_TRUE(WorkersOf(*frame, EntityId{std::numeric_limits<std::uint64_t>::max()}).empty());
    EXPECT_TRUE(WorkersOf(*frame, EntityId{}).empty());
}

// LoansFrom: only the bank's loans, largest debt first, then by borrower.
// A new city has no loans yet, so the test makes its own.
TEST(OrganizationLists, LoansSortedByDebt) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    const EntityId bank{1001};
    const EntityId other{1002};
    eco->loans = {
        {.id = EntityId{1}, .lender = bank, .borrower = EntityId{30}, .owed = 50},
        {.id = EntityId{2}, .lender = other, .borrower = EntityId{31}, .owed = 900},
        {.id = EntityId{3}, .lender = bank, .borrower = EntityId{20}, .owed = 70},
        {.id = EntityId{4}, .lender = bank, .borrower = EntityId{10}, .owed = 50},
    };
    const auto loans = LoansFrom(*eco, bank);
    ASSERT_EQ(loans.size(), 3U);
    EXPECT_EQ(loans[0]->borrower, EntityId{20});
    EXPECT_EQ(loans[1]->borrower, EntityId{10});
    EXPECT_EQ(loans[2]->borrower, EntityId{30});
}

// Lists stay empty, not crash, before the first economy frame has owners.
TEST(OrganizationLists, NoOwnersYetGivesEmptyLists) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    eco->owners = nullptr;
    auto frame = MakeWorldFrame(world, eco);
    const sim::RoomIndex rooms(world.State().city);
    EXPECT_TRUE(RoomsOwnedBy(*eco, rooms, eco->organizations.front().id).empty());
    EXPECT_TRUE(WorkersOf(*frame, eco->organizations.front().id).empty());
}

// LoansFrom: unknown bank returns empty list.
TEST(OrganizationLists, UnknownBankEmpty) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);

    EntityId unknown{std::numeric_limits<std::uint64_t>::max()};
    auto loans = LoansFrom(*eco, unknown);

    EXPECT_TRUE(loans.empty());
}

}  // namespace
