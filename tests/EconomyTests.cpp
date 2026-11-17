#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
std::int64_t MoneyIn(const sim::WorldState& state) {
    std::int64_t money = state.economy.supplyMarket;
    for (const sim::Robot& robot : state.robots) {
        money += robot.credits;
    }
    for (const sim::Organization& organization : state.economy.organizations) {
        money += organization.credits;
    }
    return money;
}

const sim::Organization* Find(const sim::WorldState& state, sim::Sector sector) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sector && organization.kind == sim::OrganizationKind::Corporation) {
            return &organization;
        }
    }
    return nullptr;
}
}  // namespace

// Corporations own every workspace and venue, robots the apartments they
// live in, and the housing trust the rest; every corporation and bank has
// owners and a bank to borrow from.
TEST(Economy, EverythingHasAnOwnerFromTheStart) {
    const sim::World world = sim::World::Generate({.seed = 4, .robots = 400});
    const sim::WorldState& state = world.State();
    std::unordered_set<std::uint64_t> homes;
    for (const sim::Robot& robot : state.robots) {
        homes.insert(robot.apartment.value);
        EXPECT_NE(world.FindOrganization(robot.bank), nullptr);
    }
    const sim::Organization* trust = Find(state, sim::Sector::Housing);
    ASSERT_NE(trust, nullptr);
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                if (homes.contains(room.id.value)) {
                    EXPECT_NE(world.FindRobot(room.owner), nullptr) << room.name;
                } else {
                    EXPECT_EQ(room.owner, trust->id) << room.name;
                }
                continue;
            }
            const sim::Organization* owner = world.FindOrganization(room.owner);
            ASSERT_NE(owner, nullptr) << room.name;
            EXPECT_EQ(owner->sector, room.kind == sim::RoomKind::Workspace ? sim::Sector::Industry
                                                                           : sim::Sector::Venues);
        }
    }
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.kind != sim::OrganizationKind::CentralBank) {
            EXPECT_FALSE(organization.owners.empty()) << organization.name;
            EXPECT_NE(world.FindOrganization(organization.bank), nullptr) << organization.name;
        }
    }
    EXPECT_TRUE(sim::ValidateWorldState(state));
}

// Money only moves between accounts: every day, all the money in the city
// is exactly what the central bank created less what it destroyed, and
// every creation is in its log.
TEST(Economy, MoneyAddsUpEveryDay) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 300});
    std::int64_t logged = 0;
    for (const sim::Emission& emission : world.State().economy.emissions) {
        logged += emission.amount;
    }
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::MoneySupplyChanged) {
            logged += event.credits;
        }
    });
    for (int day = 0; day < 10; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        const sim::WorldState& state = world.State();
        ASSERT_EQ(MoneyIn(state), state.economy.created - state.economy.destroyed) << "day " << day;
        ASSERT_EQ(logged, state.economy.created - state.economy.destroyed) << "day " << day;
        ASSERT_TRUE(sim::ValidateWorldState(state)) << "day " << day;
    }
}

// Wages come out of the employer's account and venue prices go into the
// venue owner's, which buys supplies from industry with part of them.
TEST(Economy, WagesAndSalesMoveBetweenAccounts) {
    sim::World world = sim::World::Generate({.seed = 6, .robots = 300});
    world.RunFor(sim::kMinutesPerDay);
    std::int64_t wages = 0;
    std::int64_t sales = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        wages += event.kind == sim::EventKind::ShiftEnded ? event.credits : 0;
        sales += event.kind == sim::EventKind::ArrivedAtVenue ? event.credits : 0;
    });
    world.RunFor(sim::kMinutesPerDay);
    const sim::DailyStats& day = world.State().history.back();
    EXPECT_GT(wages, 0);
    EXPECT_EQ(day.counted.wages, wages);
    EXPECT_GT(day.counted.venueSales, 0);
    EXPECT_GT(day.counted.supplies, 0);
    std::int64_t industryRevenue = 0;
    std::int64_t industryWages = 0;
    for (const sim::Organization& organization : world.State().economy.organizations) {
        if (organization.sector == sim::Sector::Industry) {
            industryRevenue += organization.yesterday.revenue;
            industryWages += organization.yesterday.wages;
        }
    }
    EXPECT_EQ(industryWages, wages);
    EXPECT_GT(industryRevenue, 0);
}

// Every midnight adds a day of statistics.
TEST(Economy, StatisticsAreKeptDaily) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 300});
    world.RunFor(3 * sim::kMinutesPerDay);
    const auto& history = world.State().history;
    ASSERT_EQ(history.size(), 3U);
    for (std::size_t index = 0; index < history.size(); ++index) {
        const sim::DailyStats& day = history[index];
        EXPECT_EQ(day.day, index + 1);
        EXPECT_EQ(day.population, 300);
        EXPECT_EQ(day.employed + day.unemployed, day.population);
        EXPECT_EQ(day.homeowners + day.tenants, day.population);
        EXPECT_GT(day.counted.walks, 0);
        EXPECT_EQ(day.moneySupply, day.createdTotal - day.destroyedTotal);
        EXPECT_GE(day.giniWealth, 0);
        EXPECT_LE(day.giniWealth, 1000);
    }
}

TEST(Economy, GiniMeasuresInequality) {
    EXPECT_EQ(sim::GiniPerMille({100, 100, 100, 100}), 0);
    EXPECT_EQ(sim::GiniPerMille({0, 0, 0, 400}), 750);
    EXPECT_EQ(sim::GiniPerMille({}), 0);
    // Debts count as nothing.
    EXPECT_EQ(sim::GiniPerMille({-50, 0, 0, 400}), 750);
}

// A robot with a job and nothing left borrows tonight's bill from its bank,
// and remembers taking its first loan.
TEST(Economy, WorkersBorrowToPayTheirBills) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));
    sim::WorldState state = start.State();
    sim::Robot* borrower = nullptr;
    for (sim::Robot& robot : state.robots) {
        if (robot.workspace.IsValid() && robot.activity == sim::RobotActivity::Resting) {
            borrower = &robot;
            break;
        }
    }
    ASSERT_NE(borrower, nullptr);
    const sim::EntityId id = borrower->id;
    borrower->savings = 0;
    sim::SetCredits(state, *borrower, 0);
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(61);
    const sim::Loan* loan = world->LoanOf(id);
    ASSERT_NE(loan, nullptr);
    EXPECT_GT(loan->owed, 0);
    EXPECT_EQ(world->FindRobot(id)->rentOwed, 0);
    bool remembers = false;
    for (const sim::Memory& memory : world->FindRobot(id)->memories) {
        remembers = remembers || memory.kind == sim::MemoryKind::TookLoan;
    }
    EXPECT_TRUE(remembers);
}

// A scrapped robot's apartments, savings and shares go to its bank when it
// has no friend to leave them to.
TEST(Economy, ScrappedRobotsLeaveTheirThingsBehind) {
    // Worn out a minute before midnight, too late for a clinic to help.
    sim::World start = sim::World::Generate({.seed = 8, .robots = 200});
    start.RunFor(sim::kMinutesPerDay - 1);
    sim::WorldState state = start.State();
    sim::Robot& robot = state.robots.front();
    robot.wear = sim::kMaxWear - 1;
    robot.bonds.clear();
    const sim::EntityId id = robot.id;
    const sim::EntityId home = robot.apartment;
    const sim::EntityId bank = robot.bank;
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(1);
    EXPECT_EQ(world->FindRobot(id), nullptr);
    const auto room = world->Rooms().Find(home);
    ASSERT_TRUE(room.has_value());
    EXPECT_EQ(room->room->owner, bank);
    for (const sim::Organization& organization : world->State().economy.organizations) {
        for (const sim::Share& share : organization.owners) {
            EXPECT_NE(share.owner, id);
        }
    }
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}
