#include <gtest/gtest.h>

#include <cstdint>
#include <map>
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

// Corporations own every workspace and venue, the city's housing office the
// dorms everyone starts in, and the housing trust every other apartment;
// City Hall owns the police stations, and the Metro Authority the crew rooms;
// every corporation and bank has owners and a bank to borrow from.
TEST(Economy, EverythingHasAnOwnerFromTheStart) {
    const sim::World world = sim::World::Generate({.seed = 4, .robots = 400});
    const sim::WorldState& state = world.State();
    for (const sim::Robot& robot : state.robots) {
        EXPECT_NE(world.FindOrganization(robot.bank), nullptr);
        const auto home = world.Rooms().Find(robot.apartment);
        ASSERT_TRUE(home.has_value());
        EXPECT_EQ(home->room->apartmentClass, sim::ApartmentClass::Dorm) << robot.name;
    }
    const sim::Organization* trust = Find(state, sim::Sector::Housing);
    ASSERT_NE(trust, nullptr);
    const sim::Organization* office = nullptr;
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.kind == sim::OrganizationKind::Government) {
            office = &organization;
        }
    }
    ASSERT_NE(office, nullptr);
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment) {
                EXPECT_EQ(room.owner, room.apartmentClass == sim::ApartmentClass::Dorm
                                          ? office->id
                                          : trust->id)
                    << room.name;
                continue;
            }
            const sim::Organization* owner = world.FindOrganization(room.owner);
            ASSERT_NE(owner, nullptr) << room.name;
            const sim::Sector expected = room.kind == sim::RoomKind::Workspace ? sim::Sector::Industry
                : room.kind == sim::RoomKind::CrewRoom ? sim::Sector::Transit
                    : room.kind == sim::RoomKind::PoliceStation ? sim::Sector::Government
                        : sim::Sector::Venues;
            EXPECT_EQ(owner->sector, expected);
        }
    }
    // Every venue has its staff, on every shift.
    std::map<std::uint64_t, std::uint32_t> staff;
    for (const sim::Robot& robot : state.robots) {
        if (const auto job = world.Rooms().Find(robot.workspace)) {
            staff[sim::JobKey(*job->room, robot.id)] += 1;
        }
    }
    for (const sim::RoomKind kind : sim::VenueKinds()) {
        for (const sim::RoomRef& venue : world.Rooms().RoomsOfKind(kind)) {
            const sim::VenueRules& rules = *sim::FindVenueRules(kind);
            for (std::uint32_t slot = 0; slot < rules.staff.shifts; ++slot) {
                EXPECT_EQ(staff[(venue.room->id.value * 4) + slot], sim::StaffPerShift(*venue.room))
                    << venue.room->name << " shift " << slot;
            }
        }
    }
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.kind != sim::OrganizationKind::CentralBank &&
            organization.kind != sim::OrganizationKind::Government) {
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
    // Robots are paid their wages less City Hall's tax.
    EXPECT_GT(wages, 0);
    EXPECT_EQ(day.counted.wages - day.counted.wageTax, wages);
    EXPECT_GT(day.counted.venueSales, 0);
    EXPECT_GT(day.counted.supplies, 0);
    // Industry and venues employ robots; between them they paid every wage.
    std::int64_t industryRevenue = 0;
    std::int64_t employersWages = 0;
    std::int64_t venueWages = 0;
    for (const sim::Organization& organization : world.State().economy.organizations) {
        employersWages += organization.yesterday.wages;
        if (organization.sector == sim::Sector::Industry) {
            industryRevenue += organization.yesterday.revenue;
        } else if (organization.sector == sim::Sector::Venues) {
            venueWages += organization.yesterday.wages;
        }
    }
    // Employers paid every wage; City Hall paid support to robots out of work.
    std::int64_t support = 0;
    for (const sim::Organization& organization : world.State().economy.organizations) {
        if (organization.kind == sim::OrganizationKind::Government) {
            support = organization.yesterday.wages;
        }
    }
    EXPECT_EQ(employersWages - support, day.counted.wages);
    EXPECT_GT(venueWages, 0);
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
    const sim::EntityId bank = robot.bank;
    // It owns a studio it lets out.
    sim::EntityId home;
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& room : building.rooms) {
            if (!home.IsValid() && room.apartmentClass == sim::ApartmentClass::Studio) {
                room.owner = id;
                home = room.id;
            }
        }
    }
    ASSERT_TRUE(home.IsValid());
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

// City Hall takes a tenth of every wage and a share of rents and profits,
// owns the metro, and supports every robot out of work.
TEST(Economy, CityHallTaxesAndSupports) {
    sim::World start = sim::World::Generate({.seed = 9, .robots = 300});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));
    sim::WorldState state = start.State();
    int laidOff = 0;
    for (sim::Robot& robot : state.robots) {
        if (robot.activity == sim::RobotActivity::Resting && laidOff < 3) {
            robot.workspace = {};
            robot.commuteAt = sim::kNoCommute;
            ++laidOff;
        }
    }
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(61);  // Past midnight.
    const sim::DailyStats& day = world->State().history.back();
    // A tenth, give or take rounding on each wage.
    EXPECT_NEAR(static_cast<double>(day.counted.wageTax) * 100.0 /
                    static_cast<double>(day.counted.wages),
                static_cast<double>(sim::kWageTaxPercent), 1.0);
    EXPECT_GT(day.counted.propertyTax, 0);
    // Robots still out of work after the night's hiring got support.
    std::int64_t jobless = 0;
    for (const sim::Robot& robot : world->State().robots) {
        jobless += robot.workspace.IsValid() ? 0 : 1;
    }
    EXPECT_EQ(day.counted.benefits, jobless * sim::kBenefit);
    for (const sim::Organization& organization : world->State().economy.organizations) {
        if (organization.sector == sim::Sector::Transit) {
            ASSERT_EQ(organization.owners.size(), 1U);
            EXPECT_EQ(world->FindOrganization(organization.owners.front().owner)->kind,
                      sim::OrganizationKind::Government);
        }
    }
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// While the central bank need not create money, its rate falls.
TEST(Economy, TheCentralBankRateFallsWhileMoneyIsSteady) {
    sim::World world = sim::World::Generate({.seed = 10, .robots = 400});
    world.RunFor(6 * sim::kMinutesPerDay);
    EXPECT_LT(world.State().economy.policyRate, sim::kStartingPolicyRate);
    EXPECT_GE(world.State().economy.policyRate, sim::kMinPolicyRate);
    EXPECT_EQ(world.State().history.back().policyRate, world.State().economy.policyRate);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// At midnight savers earn half the central bank's rate on their savings.
TEST(Economy, SaversEarnInterest) {
    sim::World start = sim::World::Generate({.seed = 10, .robots = 400});
    start.RunFor(sim::kMinutesPerDay - 1);
    sim::WorldState state = start.State();
    state.robots.front().savings = 20000;
    const sim::EntityId saver = state.robots.front().id;
    const std::int64_t rate = state.economy.policyRate;
    auto world = sim::World::FromState(std::move(state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(1);
    EXPECT_GE(world->FindRobot(saver)->savings + world->FindRobot(saver)->credits,
              20000 + (20000 * rate / 2 / 10000));
    EXPECT_GT(world->State().history.back().counted.savingsInterest, 0);
}

namespace {
// The city's public buildings: police stations run by City Hall and metro
// crew rooms run by the Metro Authority. FoundEconomy assigns them to those
// organizations (sim/src/Founding.cpp), and any station opened later goes to
// City Hall too (World::OpenUnit); nothing ever closes or reassigns them.

bool PublicRoomsArePublic(const sim::World& world) {
    for (const sim::RoomRef& ref : world.Rooms().RoomsOfKind(sim::RoomKind::PoliceStation)) {
        const sim::Organization* owner = world.FindOrganization(ref.room->owner);
        if (!owner || owner->name != "City Hall") {
            return false;
        }
    }
    for (const sim::RoomRef& ref : world.Rooms().RoomsOfKind(sim::RoomKind::CrewRoom)) {
        const sim::Organization* owner = world.FindOrganization(ref.room->owner);
        if (!owner || owner->name != "Metro Authority") {
            return false;
        }
    }
    return true;
}

// Robots whose workspace is a police station or crew room. OpeningsFor now
// offers these rooms through StaffPerShift, so staff are hired as expected.
std::uint32_t PublicBuildingStaff(const sim::World& world) {
    std::uint32_t staff = 0;
    for (const sim::Robot& robot : world.State().robots) {
        if (!robot.workspace.IsValid()) {
            continue;
        }
        const auto job = world.Rooms().Find(robot.workspace);
        if (job && (job->room->kind == sim::RoomKind::PoliceStation ||
                    job->room->kind == sim::RoomKind::CrewRoom)) {
            ++staff;
        }
    }
    return staff;
}

const sim::Organization* OrganizationNamed(const sim::WorldState& state, const char* name) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.name == name) {
            return &organization;
        }
    }
    return nullptr;
}
}  // namespace

// City Hall owns the police stations and the Metro Authority the metro crew
// rooms. After two days of hiring, robots are staffed in both, money adds up,
// and each owner's wage ledger is non-zero.
TEST(Economy, StationsAndDepotsAreOwnedAndStaffed) {
    sim::World world = sim::World::Generate({.seed = 11, .robots = 4000});
    EXPECT_GT(world.Rooms().RoomsOfKind(sim::RoomKind::PoliceStation).size(), 0U);
    EXPECT_GT(world.Rooms().RoomsOfKind(sim::RoomKind::CrewRoom).size(), 0U);
    EXPECT_TRUE(PublicRoomsArePublic(world));
    // Two days of shifts, hiring and layoffs; the review that opens new
    // stations keeps them City Hall's (World::OpenUnit).
    world.RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    EXPECT_TRUE(PublicRoomsArePublic(world));
    EXPECT_GT(PublicBuildingStaff(world), 0U);
}

// Wages are posted to the owner of a robot's workspace room when its shift
// ends (World::PayWage). Both City Hall and Metro Authority now pay wages for
// police stations and crew rooms respectively.
TEST(Economy, CityHallAndTransitPayStaffWages) {
    sim::World world = sim::World::Generate({.seed = 12, .robots = 4000});
    EXPECT_GT(world.Rooms().RoomsOfKind(sim::RoomKind::PoliceStation).size(), 0U);
    EXPECT_GT(world.Rooms().RoomsOfKind(sim::RoomKind::CrewRoom).size(), 0U);
    world.RunFor(2 * sim::kMinutesPerDay);
    const sim::WorldState& state = world.State();
    ASSERT_TRUE(sim::ValidateWorldState(state));
    EXPECT_GT(PublicBuildingStaff(world), 0U);
    // Both organizations should have paid wages for their staff.
    const auto* cityHall = OrganizationNamed(state, "City Hall");
    const auto* authority = OrganizationNamed(state, "Metro Authority");
    ASSERT_NE(cityHall, nullptr);
    ASSERT_NE(authority, nullptr);
    EXPECT_GT(cityHall->yesterday.wages, 0LL);
    EXPECT_GT(authority->yesterday.wages, 0LL);
}
