#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
sim::EntityId HousingTrust(const sim::WorldState& state) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sim::Sector::Housing) {
            return organization.id;
        }
    }
    return {};
}

void SetOwner(sim::WorldState& state, sim::EntityId room, sim::EntityId owner) {
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& each : building.rooms) {
            if (each.id == room) {
                each.owner = owner;
            }
        }
    }
}

// A studio nobody lives in.
sim::EntityId VacantStudio(const sim::World& world) {
    std::set<std::uint64_t> homes;
    for (const sim::Robot& robot : world.State().robots) {
        homes.insert(robot.apartment.value);
    }
    for (const sim::Building& building : world.State().city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.apartmentClass == sim::ApartmentClass::Studio && !homes.contains(room.id.value)) {
                return room.id;
            }
        }
    }
    return {};
}

sim::World Load(sim::WorldState state) {
    auto world = sim::World::FromState(std::move(state));
    EXPECT_TRUE(world.has_value()) << world.error();
    return std::move(*world);
}

// Index of the first robot matching `wanted`.
std::optional<std::size_t> FindIndex(const sim::WorldState& state,
                                     const std::function<bool(const sim::Robot&)>& wanted) {
    for (std::size_t index = 0; index < state.robots.size(); ++index) {
        if (wanted(state.robots[index])) {
            return index;
        }
    }
    return std::nullopt;
}

bool Remembers(const sim::Robot& robot, sim::MemoryKind kind) {
    for (const sim::Memory& memory : robot.memories) {
        if (memory.kind == kind) {
            return true;
        }
    }
    return false;
}

std::uint32_t RentOf(const sim::World& world, sim::EntityId apartment) {
    const auto room = world.Rooms().Find(apartment);
    return room ? room->room->rent : 0;
}

// True when `room` is one of the rooms of `building`; an invalid room id,
// such as a robot with no job, answers false.
bool InBuilding(const sim::WorldState& state, sim::EntityId building, sim::EntityId room) {
    for (const sim::Building& each : state.city.buildings) {
        if (each.id != building) {
            continue;
        }
        return std::ranges::any_of(
            each.rooms, [room](const sim::Room& held) { return held.id == room; });
    }
    return false;
}

// A workplace building that has workspaces, and the employer that owns them:
// the ids a test needs, since a state moved into `Load` invalidates pointers
// into it.
std::pair<sim::EntityId, sim::EntityId> WorkplaceAndOwner(const sim::WorldState& state) {
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Workspace) {
                return {building.id, room.owner};
            }
        }
    }
    return {};
}

// A habitat building with a home nobody lives in, and that home.
std::pair<sim::EntityId, sim::EntityId> HabitatAndVacantHome(const sim::WorldState& state) {
    std::set<std::uint64_t> homes;
    for (const sim::Robot& robot : state.robots) {
        homes.insert(robot.apartment.value);
    }
    for (const sim::Building& building : state.city.buildings) {
        if (building.kind != sim::BuildingKind::Habitat) {
            continue;
        }
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Apartment && !homes.contains(room.id.value)) {
                return {building.id, room.id};
            }
        }
    }
    return {};
}

// Puts a building on a site: a project raises it for `nights` more nights,
// owned by `owner`. The cities these tests write have no project yet - the
// shortage that starts one is read off seven saved days - so the list stays
// sorted by building id, as validation asks.
void Raise(sim::WorldState& state, sim::EntityId building, sim::EntityId owner,
           std::uint32_t nights) {
    state.projects.push_back({
        .building = building,
        .owner = owner,
        .daysLeft = nights,
        .floorsDone = 1,
    });
}

// The site office of a building put on a site by hand, as `startSite`
// (Construction.cpp) makes it: the builders' room on the ground floor, owned
// by the site's owner, with room for the crew of six a site asks for. The
// shortage that starts a real site is read off seven saved nights, so a careers
// test that wants builders writes the room instead of waiting for one.
sim::EntityId AddOffice(sim::WorldState& state, sim::EntityId building,
                        sim::EntityId owner) {
    for (sim::Building& each : state.city.buildings) {
        if (each.id != building) {
            continue;
        }
        // The new id is the highest there is, so the building's rooms stay
        // sorted by id, as validation asks.
        each.rooms.push_back({
            .id = state.ids.Allocate(),
            .name = "Site Office",
            .kind = sim::RoomKind::SiteOffice,
            .floor = 0,
            .capacity = 6,
            .owner = owner,
        });
        return each.rooms.back().id;
    }
    return {};
}

// Lays a robot off as `CloseUnit` (Units.cpp) does it: no workplace, junior,
// no performance, no commute planned, and at home when it stood at work.
void LayOff(sim::Robot& robot) {
    robot.workspace = sim::EntityId{};
    robot.jobLevel = 0;
    robot.performance = 0;
    robot.commuteAt = sim::kNoCommute;
    if (robot.activity == sim::RobotActivity::Working) {
        robot.activity = sim::RobotActivity::Resting;
        robot.location = robot.apartment;
    }
}

// A city a day and a half in, with robots at work.
sim::World WorkingCity() {
    sim::World world = sim::World::Generate({.seed = 7, .robots = 200});
    world.RunFor(sim::kMinutesPerDay + (10 * 60));  // Day 2, 10:00.
    return world;
}
}  // namespace

TEST(Careers, LevelsPayMoreAndTakeLongerToClimb) {
    EXPECT_EQ(sim::WageAtLevel(100, 0), 100U);
    EXPECT_EQ(sim::WageAtLevel(100, 1), 115U);
    EXPECT_EQ(sim::WageAtLevel(100, 3), 145U);
    EXPECT_LT(sim::PromotionTarget(0), sim::PromotionTarget(1));
    EXPECT_EQ(sim::JobTitle(0), "junior");
    EXPECT_EQ(sim::JobTitle(sim::kMaxJobLevel), "lead");
}

TEST(Careers, WagesFollowTheJobLevel) {
    sim::World world = sim::World::Generate({.seed = 3, .robots = 120});
    std::uint64_t shifts = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind != sim::EventKind::ShiftEnded) {
            return;
        }
        const sim::Robot* robot = world.FindRobot(event.robot);
        const auto room = world.Rooms().Find(event.room);
        ASSERT_TRUE(robot != nullptr && room.has_value());
        // The level's wage, at the share of it the employer pays now.
        const sim::Organization* employer = world.FindOrganization(room->room->owner);
        ASSERT_NE(employer, nullptr);
        // The level's wage at the employer's share of it, less the wage tax.
        const std::int64_t wage =
            static_cast<std::int64_t>(
                sim::WageAtLevel(sim::JobShift(*room, robot->id).wage, robot->jobLevel)) *
            employer->payPercent / 100;
        EXPECT_EQ(event.credits, wage - (wage * sim::kWageTaxPercent / 100));
        ++shifts;
    });
    world.RunFor(3 * sim::kMinutesPerDay);
    EXPECT_GT(shifts, 200U);
}

// A robot that has done well enough is promoted at the end of its shift.
TEST(Careers, GoodWorkIsPromoted) {
    const sim::World city = WorkingCity();
    sim::WorldState state = city.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Working &&
               robot.jobLevel < sim::kMaxJobLevel;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const std::uint8_t level = robot.jobLevel;
    robot.performance = sim::PromotionTarget(level);
    robot.needs = {.charge = 1000, .condition = 1000, .social = 1000, .fun = 1000};
    const sim::Tick shiftEnd = robot.nextActionAt;
    const sim::EntityId id = robot.id;

    sim::World world = Load(std::move(state));
    world.RunFor(shiftEnd - world.CurrentTick() + 1);
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_EQ(after.jobLevel, level + 1);
    EXPECT_EQ(after.performance, 0);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::Promoted));
}

// A robot on its last chance that turns up worn down loses its job at the
// end of the shift, lives off work for a while, and is hired again; it
// never works while out of work.
TEST(Careers, WornDownWorkersAreFiredAndHiredAgain) {
    const sim::World city = WorkingCity();
    sim::WorldState state = city.State();
    // Its shift must start soon: over days off it would recover before it.
    const sim::Tick now = city.CurrentTick();
    const auto index = FindIndex(state, [&](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Resting &&
               robot.commuteAt != sim::kNoCommute && robot.shiftStartsAt < now + 720;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    // Start exactly on the firing line: the worn-down penalty at clock-in
    // then guarantees the shift ends at or below it, whatever the random
    // shift score is. A single point above it made this test depend on the
    // score draw (and on unrelated RNG use elsewhere in the simulation).
    robot.performance = sim::kFiringPerformance;
    robot.needs.condition = 0;
    robot.wear = 0;
    const auto workspace = city.Rooms().Find(robot.workspace);
    ASSERT_TRUE(workspace.has_value());
    const sim::Tick shiftEnd =
        robot.shiftStartsAt + sim::JobShift(*workspace, robot.id).lengthMinutes;
    const sim::EntityId id = robot.id;

    sim::World world = Load(std::move(state));
    bool fired = false;
    bool hired = false;
    bool workedWhileOut = false;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.robot != id) {
            return;
        }
        if (event.kind == sim::EventKind::Remembered) {
            fired = fired || event.memory == sim::MemoryKind::LostJob;
            hired = hired || (fired && event.memory == sim::MemoryKind::FoundJob);
        }
        if (event.kind == sim::EventKind::ClockedIn && fired && !hired) {
            workedWhileOut = true;
        }
    });
    world.RunFor(shiftEnd - world.CurrentTick() + 1);
    ASSERT_TRUE(fired);
    {
        const sim::Robot& out = *world.FindRobot(id);
        EXPECT_FALSE(out.workspace.IsValid());
        EXPECT_EQ(out.commuteAt, sim::kNoCommute);
        EXPECT_NE(out.activity, sim::RobotActivity::Working);
    }
    for (int day = 0; day < 10 && !hired; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    EXPECT_TRUE(hired);
    EXPECT_FALSE(workedWhileOut);
    const sim::Robot& rehired = *world.FindRobot(id);
    EXPECT_TRUE(rehired.workspace.IsValid());
    EXPECT_EQ(rehired.jobLevel, 0);
}

// Owing too much rent gets a robot evicted to a free apartment that costs no
// more; the debt is written off and nobody shares a flat.
TEST(Careers, DebtorsAreEvicted) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Resting;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const sim::EntityId id = robot.id;
    const sim::EntityId home = robot.apartment;
    // A tenant of the housing trust, out of work so no bank lends to it.
    SetOwner(state, home, HousingTrust(state));
    robot.workspace = {};
    robot.commuteAt = sim::kNoCommute;
    robot.savings = 0;
    sim::SetCredits(state, robot, 0);
    robot.rentOwed = 10 * static_cast<std::int64_t>(RentOf(start, home));

    sim::World world = Load(std::move(state));
    world.RunFor(90);  // Past midnight and the walk to the new place.
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_NE(after.apartment, home);
    EXPECT_LE(RentOf(world, after.apartment), RentOf(world, home));
    EXPECT_EQ(after.rentOwed, 0);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::Evicted));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// An owner who cannot pay its home's upkeep sells it to the housing trust,
// pays its debts from the sale and stays on as a tenant.
TEST(Careers, OwnersBehindOnUpkeepSellAndStay) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));
    sim::WorldState state = start.State();
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Resting;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const sim::EntityId id = robot.id;
    // It owns and lives in a studio nobody else lives in.
    const sim::EntityId home = VacantStudio(start);
    ASSERT_TRUE(home.IsValid());
    SetOwner(state, home, id);
    robot.apartment = home;
    robot.location = home;
    robot.workspace = {};
    robot.commuteAt = sim::kNoCommute;
    robot.savings = 0;
    sim::SetCredits(state, robot, 0);
    robot.rentOwed = 10 * static_cast<std::int64_t>(RentOf(start, home));
    const sim::EntityId trust = HousingTrust(state);

    sim::World world = Load(std::move(state));
    world.RunFor(90);
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_EQ(after.apartment, home);
    EXPECT_EQ(world.Rooms().Find(home)->room->owner, trust);
    EXPECT_EQ(after.rentOwed, 0);
    EXPECT_GT(after.credits + after.savings, 0);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::SoldHome));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// A robot with plenty saved moves to a pricier apartment before long.
TEST(Careers, SaversMoveUp) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay);
    sim::WorldState state = start.State();
    // The robot at home with the cheapest apartment has the most to gain.
    std::optional<std::size_t> index;
    for (std::size_t candidate = 0; candidate < state.robots.size(); ++candidate) {
        const sim::Robot& robot = state.robots[candidate];
        if (robot.activity == sim::RobotActivity::Resting &&
            (!index || RentOf(start, robot.apartment) <
                           RentOf(start, state.robots[*index].apartment))) {
            index = candidate;
        }
    }
    ASSERT_TRUE(index);
    // The only robot with savings, so nobody takes the nicer flats first.
    for (sim::Robot& other : state.robots) {
        sim::SetCredits(state, other, std::min<std::int64_t>(other.credits, 50));
        other.savings = 0;
    }
    sim::Robot& robot = state.robots[*index];
    sim::SetCredits(state, robot, 5000);
    const sim::EntityId id = robot.id;
    const sim::EntityId home = robot.apartment;

    // Disable gangs so robbery doesn't drain the saver's credits.
    state.gangs.clear();
    for (sim::Robot& r : state.robots) {
        r.gang = sim::EntityId{0};
    }

    sim::World world = Load(std::move(state));
    for (int day = 0; day < 40 && world.FindRobot(id)->apartment == home; ++day) {
        world.RunFor(sim::kMinutesPerDay);
    }
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_GT(RentOf(world, after.apartment), RentOf(world, home));
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::MovedUp));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Over two months some robots are promoted, and the population keeps its
// jobs: nearly everyone who loses one finds another.
TEST(Careers, CareersMoveOverMonths) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    std::uint64_t promotions = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        promotions += event.kind == sim::EventKind::Remembered &&
                              event.memory == sim::MemoryKind::Promoted
                          ? 1
                          : 0;
    });
    world.RunFor(56 * sim::kMinutesPerDay);
    std::uint32_t outOfWork = 0;
    for (const sim::Robot& robot : world.State().robots) {
        outOfWork += robot.workspace.IsValid() ? 0 : 1;
    }
    EXPECT_GT(promotions, 30U);
    EXPECT_LT(outOfWork, 300U / 20);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// A building a project is raising takes no staff: none of its rooms is an
// opening until the project is done, so the robots laid off from it find work
// in the finished buildings instead.
TEST(Careers, SitesTakeNoWorkers) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    sim::WorldState state = start.State();
    const auto [site, employer] = WorkplaceAndOwner(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(employer.IsValid());
    // Everyone working in that building is laid off, so its rooms stand empty
    // and would be openings if a site's rooms counted.
    std::set<std::uint64_t> laidOff;
    for (sim::Robot& robot : state.robots) {
        if (!InBuilding(state, site, robot.workspace)) {
            continue;
        }
        LayOff(robot);
        laidOff.insert(robot.id.value);
    }
    ASSERT_FALSE(laidOff.empty());
    Raise(state, site, employer, 30);  // Still going up all week.

    sim::World world = Load(std::move(state));
    for (int day = 0; day < 7; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    const sim::WorldState& after = world.State();
    EXPECT_FALSE(after.projects.empty()) << "the site is still going up";
    std::uint32_t staffed = 0;
    std::uint32_t rehired = 0;
    for (const sim::Robot& robot : after.robots) {
        staffed += InBuilding(after, site, robot.workspace) ? 1 : 0;
        rehired += laidOff.contains(robot.id.value) && robot.workspace.IsValid() ? 1 : 0;
    }
    EXPECT_EQ(staffed, 0U) << "a site's rooms are not openings";
    EXPECT_GT(rehired, 0U) << "the laid off robots did look for work";
    EXPECT_TRUE(sim::ValidateWorldState(after));
}

// A building going up gets its builders: the site office is an opening while
// its project is up, and the site's other rooms still take no workers.
TEST(Careers, SitesGetBuildersAtTheOffice) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    sim::WorldState state = start.State();
    const auto [site, employer] = WorkplaceAndOwner(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(employer.IsValid());
    Raise(state, site, employer, 60);  // Still going up all fortnight.
    const sim::EntityId office = AddOffice(state, site, employer);
    ASSERT_TRUE(office.IsValid());
    // Everyone whose job is a workspace is laid off, so the city looks hard
    // for work: many robots ask for a job each night, and the office is one
    // of the few openings they choose among.
    std::set<std::uint64_t> workspaces;
    for (const sim::Building& building : state.city.buildings) {
        for (const sim::Room& room : building.rooms) {
            if (room.kind == sim::RoomKind::Workspace) {
                workspaces.insert(room.id.value);
            }
        }
    }
    std::uint32_t jobless = 0;
    for (sim::Robot& robot : state.robots) {
        if (!workspaces.contains(robot.workspace.value)) {
            continue;
        }
        LayOff(robot);
        ++jobless;
    }
    ASSERT_GT(jobless, 0U);

    sim::World world = Load(std::move(state));
    for (int day = 0; day < 14; ++day) {
        world.RunFor(sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    const sim::WorldState& after = world.State();
    EXPECT_FALSE(after.projects.empty()) << "the site is still going up";
    std::uint32_t builders = 0;
    std::uint32_t staffed = 0;
    for (const sim::Robot& robot : after.robots) {
        if (!InBuilding(after, site, robot.workspace)) {
            continue;
        }
        if (robot.workspace == office) {
            ++builders;
        } else {
            ++staffed;
        }
    }
    // The office fills its crew of six within the fortnight; the bound leaves
    // room for the random stream to move in a later step.
    EXPECT_GE(builders, 3U) << "a site's office takes builders";
    EXPECT_EQ(staffed, 0U) << "the site's other rooms are not openings";
    EXPECT_TRUE(sim::ValidateWorldState(after));
}

// A home in a building still going up cannot be moved into: an evicted robot
// takes the cheapest finished home even when the site's is cheaper still.
TEST(Careers, EvicteesSkipSiteHomes) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    const auto [site, cheap] = HabitatAndVacantHome(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(cheap.IsValid());
    Raise(state, site, HousingTrust(state), 30);
    // The site's home is the cheapest in the city: a rent of 1 is under a
    // dorm's 8 (City.cpp), so an evicted robot would reach for it if a site's
    // homes were rentable.
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& room : building.rooms) {
            if (room.id == cheap) {
                room.rent = 1;
            }
        }
    }
    // A tenant of the housing trust, deep in debt, and the only debtor:
    // `CollectRent` has charged the others one night, under the five that
    // evict.
    const auto index = FindIndex(state, [](const sim::Robot& robot) {
        return robot.activity == sim::RobotActivity::Resting;
    });
    ASSERT_TRUE(index);
    sim::Robot& robot = state.robots[*index];
    const sim::EntityId id = robot.id;
    const sim::EntityId flat = robot.apartment;
    for (sim::Robot& other : state.robots) {
        if (other.id != id) {
            other.rentOwed = 0;
        }
    }
    SetOwner(state, flat, HousingTrust(state));
    robot.workspace = {};
    robot.commuteAt = sim::kNoCommute;
    robot.savings = 0;
    sim::SetCredits(state, robot, 0);
    robot.rentOwed = 10 * static_cast<std::int64_t>(RentOf(start, flat));

    sim::World world = Load(std::move(state));
    world.RunFor(90);  // Past midnight and the walk to the new place.
    const sim::Robot& after = *world.FindRobot(id);
    EXPECT_NE(after.apartment, cheap) << "a site's homes are not rentable";
    EXPECT_NE(after.apartment, flat);
    EXPECT_TRUE(Remembers(after, sim::MemoryKind::Evicted));
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}
