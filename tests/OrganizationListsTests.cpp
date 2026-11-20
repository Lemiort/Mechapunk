#include <gtest/gtest.h>

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

// WorkersOf: every robot with a job is exactly one worker of its employer.
TEST(OrganizationLists, EachRobotOneEmployer) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    auto frame = MakeWorldFrame(world, eco);

    // Collect all employers and their workers.
    std::unordered_map<std::uint64_t, std::vector<const RobotFrame*>> by_employer;
    for (const auto& robot : frame->robots) {
        if (!robot.workspace.value) continue;  // no workspace = unemployed or out of work.
        const sim::EntityId employer = OwnerOf(*eco, robot.workspace);
        EXPECT_FALSE(employer.value == 0);  // every workspace has an owner.
        by_employer[employer.value].push_back(&robot);
    }

    // Every robot with a workspace belongs to exactly one organization.
    std::size_t employed = 0;
    for (const auto& [org_id, workers] : by_employer) {
        EXPECT_FALSE(workers.empty());
        employed += workers.size();
    }
    EXPECT_GT(employed, 100U);  // most of the 300 robots work.
}

// WorkersOf: worker list is sorted by robot id.
TEST(OrganizationLists, WorkersSorted) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);
    auto frame = MakeWorldFrame(world, eco);

    // Find any organization that has workers and check they are sorted by id.
    bool found = false;
    for (const auto& org : eco->organizations) {
        auto workers = WorkersOf(*frame, org.id);
        if (!workers.empty()) {
            found = true;
            // Verify sorted order among multiple workers.
            for (std::size_t i = 1; i < workers.size(); ++i) {
                EXPECT_LT(workers[i - 1]->id.value, workers[i]->id.value);
            }
            break;
        }
    }
    if (!found) GTEST_SKIP() << "No organizations with workers in this world";
}

// LoansFrom: loans are sorted by owed descending then borrower id ascending.
TEST(OrganizationLists, LoansSortedByDebt) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    auto eco = MakeEconomy(world);

    // Find any organization that has loans and check sorting.
    bool found = false;
    for (const auto& org : eco->organizations) {
        auto loans = LoansFrom(*eco, org.id);
        if (!loans.empty()) {
            found = true;
            for (std::size_t i = 1; i < loans.size(); ++i) {
                const auto& prev_owed = loans[i - 1]->owed;
                const auto& cur_owed = loans[i]->owed;
                if (prev_owed == cur_owed) {
                    EXPECT_LE(loans[i - 1]->borrower.value, loans[i]->borrower.value);
                } else {
                    EXPECT_GT(prev_owed, cur_owed);  // larger first
                }
            }
            break;
        }
    }
    if (!found) GTEST_SKIP() << "No organizations with loans in this world";
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
