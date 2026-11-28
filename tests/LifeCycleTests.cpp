#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "mechapunk/sim/Body.hpp"
#include "mechapunk/sim/Construction.hpp"
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

bool Remembers(const sim::Robot& robot, sim::MemoryKind kind,
               sim::EntityId other = {}) {
    return std::ranges::any_of(robot.memories, [&](const sim::Memory& memory) {
        return memory.kind == kind && (!other.IsValid() || memory.robot == other);
    });
}

// The housing trust: the organization that owns a habitat's homes and pays
// what a habitat site costs a night.
sim::EntityId HousingTrust(const sim::WorldState& state) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sim::Sector::Housing) {
            return organization.id;
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

// A habitat building with a home nobody lives in: one a test can put on a
// site and still expect free homes in it.
sim::EntityId HabitatWithFreeHome(const sim::WorldState& state) {
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
                return building.id;
            }
        }
    }
    return {};
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
}  // namespace

TEST(LifeCycle, WearGrowsWithAgeAndCapsCondition) {
    EXPECT_EQ(sim::WearAtAge(0), 0);
    EXPECT_LT(sim::WearAtAge(100), sim::WearAtAge(200));
    // Older robots wear faster.
    EXPECT_LT(sim::WearAtAge(100) - sim::WearAtAge(50),
              sim::WearAtAge(350) - sim::WearAtAge(300));
    EXPECT_EQ(sim::WearAtAge(5000), sim::kMaxWear);

    sim::World world = sim::World::Generate({.seed = 4, .robots = 120});
    world.RunFor(3 * sim::kMinutesPerDay);
    for (const sim::Robot& robot : world.State().robots) {
        EXPECT_LE(robot.needs.condition, sim::kMaxWear - robot.wear) << robot.name;
    }
}

// A robot staying at a clinic loses wear while it is there.
TEST(LifeCycle, ClinicsTakeWearAway) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    world.RunFor(sim::kMinutesPerDay + (8 * 60));  // Day 2, 08:00.
    std::optional<std::size_t> patient;
    for (int search = 0; search < 24 && !patient; ++search) {
        world.RunFor(30);
        const auto& robots = world.State().robots;
        for (std::size_t index = 0; index < robots.size(); ++index) {
            const auto room = world.Rooms().Find(robots[index].location);
            if (robots[index].activity == sim::RobotActivity::Visiting &&
                room && room->room->kind == sim::RoomKind::Clinic) {
                patient = index;
                break;
            }
        }
    }
    ASSERT_TRUE(patient) << "nobody visited a clinic";
    sim::WorldState state = world.State();
    sim::Robot& robot = state.robots[*patient];
    robot.wear = 500;
    // Both parts are damaged well below what the wear allows, so the clinic's
    // mend can be told apart from the cap rising: the head follows the wear
    // away, the legs do not (step L12 is what fits a leg).
    sim::Part(robot.body, sim::BodyPart::Head).condition = 100;
    sim::Part(robot.body, sim::BodyPart::Legs).condition = 300;
    robot.nextActionAt = state.tick + 120;  // Stays two more hours.
    const sim::EntityId id = robot.id;
    sim::World clinic = Load(std::move(state));
    clinic.RunFor(60);
    const sim::Robot* patientRobot = clinic.FindRobot(id);
    ASSERT_TRUE(patientRobot != nullptr);
    const std::int32_t wearTaken = 500 - patientRobot->wear;
    EXPECT_GT(wearTaken, 0) << "the clinic took no wear";
    EXPECT_LE(sim::Part(patientRobot->body, sim::BodyPart::Head).condition,
              sim::kMaxWear - patientRobot->wear)
        << "the mend went above the cap";
    EXPECT_EQ(sim::Part(patientRobot->body, sim::BodyPart::Head).condition, 100 + wearTaken)
        << "the head did not follow the wear the clinic took away";
    EXPECT_EQ(sim::Part(patientRobot->body, sim::BodyPart::Legs).condition, 300)
        << "a clinic mended a part it does not mend";
}

// A robot worn out completely is scrapped at night: it leaves the city,
// its friends remember losing it, and a newcomer moves into the city.
TEST(LifeCycle, WornOutRobotsAreScrappedAndReplaced) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 150});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    sim::Robot& doomed = state.robots[10];
    sim::Robot& mourner = state.robots[20];
    doomed.wear = sim::kMaxWear - 1;
    mourner.bonds = {{.robot = doomed.id,
                      .affinity = 50,
                      .level = sim::BondLevel::Friend}};
    const sim::EntityId doomedId = doomed.id;
    const std::string doomedName = doomed.name;
    const sim::EntityId mournerId = mourner.id;
    const std::uint64_t lastId = state.robots.back().id.value;
    const std::size_t population = state.robots.size();

    sim::World world = Load(std::move(state));
    std::uint64_t scrappedEvents = 0;
    std::string farewell;
    world.SetEventSink([&](const sim::SimEvent& event) {
        if (event.kind == sim::EventKind::Scrapped && event.robot == doomedId) {
            ++scrappedEvents;
        }
        if (event.kind == sim::EventKind::Remembered &&
            event.memory == sim::MemoryKind::LostFriend && event.robot == mournerId) {
            farewell = world.Describe(event);
        }
    });
    world.RunFor(90);  // Past midnight.

    EXPECT_EQ(scrappedEvents, 1U);
    EXPECT_EQ(world.FindRobot(doomedId), nullptr);
    EXPECT_EQ(world.NameOf(doomedId), doomedName);
    const sim::Robot& friendLeft = *world.FindRobot(mournerId);
    EXPECT_TRUE(Remembers(friendLeft, sim::MemoryKind::LostFriend, doomedId));
    EXPECT_TRUE(std::ranges::none_of(friendLeft.bonds, [&](const sim::Bond& bond) {
        return bond.robot == doomedId;
    }));
    EXPECT_NE(farewell.find(doomedName), std::string::npos) << farewell;

    // Same population, with a newcomer at the end.
    ASSERT_EQ(world.State().robots.size(), population);
    const sim::Robot& newcomer = world.State().robots.back();
    EXPECT_GT(newcomer.id.value, lastId);
    EXPECT_TRUE(Remembers(newcomer, sim::MemoryKind::ArrivedInCity));
    EXPECT_LT(newcomer.wear, sim::kMaxWear / 2);
    EXPECT_TRUE(sim::ValidateWorldState(world.State()));
}

// Wear grows every night and a clinic takes it away again; the head and the
// torso follow it both ways, so in a running city no robot has a basic part
// whose condition disagrees with `kMaxWear - wear`. Step L3b scraps on those
// parts instead of on the wear number and needs this to land on the same night
// (task L, step L3a).
TEST(LifeCycle, TheBasicPartsFollowTheWearBothWays) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    for (int nights = 0; nights < 4; ++nights) {
        world.RunFor(10 * sim::kMinutesPerDay);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
        for (const sim::Robot& robot : world.State().robots) {
            const std::int32_t cap = std::max(0, sim::kMaxWear - robot.wear);
            for (const sim::BodyPart part : {sim::BodyPart::Head, sim::BodyPart::Torso}) {
                EXPECT_EQ(sim::Part(robot.body, part).condition, cap)
                    << robot.name << " at wear " << robot.wear;
            }
        }
    }
}

// Over months robots wear out and newcomers replace them; the city keeps
// its size, black markets now and then sell a dodgy part, and only scrapped
// robots someone remembers are kept.
TEST(LifeCycle, TheCityRenewsItself) {
    sim::World world = sim::World::Generate({.seed = 9, .robots = 300});
    std::uint64_t scrapped = 0;
    std::uint64_t arrived = 0;
    std::uint64_t badParts = 0;
    world.SetEventSink([&](const sim::SimEvent& event) {
        scrapped += event.kind == sim::EventKind::Scrapped ? 1 : 0;
        arrived += event.kind == sim::EventKind::Arrived ? 1 : 0;
        badParts += event.kind == sim::EventKind::Remembered &&
                            event.memory == sim::MemoryKind::GotBadPart
                        ? 1
                        : 0;
    });
    for (int month = 0; month < 3; ++month) {
        world.RunFor(30 * sim::kMinutesPerDay);
        EXPECT_EQ(world.State().robots.size(), 300U);
        EXPECT_TRUE(sim::ValidateWorldState(world.State()));
    }
    // About 20 in this city; the exact count moves with the random stream.
    EXPECT_GT(scrapped, 12U);
    EXPECT_EQ(arrived, scrapped);
    EXPECT_GT(badParts, 0U);

    std::set<std::uint64_t> remembered;
    for (const sim::Robot& robot : world.State().robots) {
        for (const sim::Memory& memory : robot.memories) {
            remembered.insert(memory.robot.value);
        }
    }
    for (const sim::DepartedRobot& departed : world.State().departed) {
        EXPECT_TRUE(remembered.contains(departed.id.value)) << departed.name;
    }
}

// A home in a building a project is still raising is not a free home: the
// newcomer that replaces a scrapped robot moves into a finished one even
// when the site's empty homes are the city's only free dorms, which is what
// a newcomer reaches for first.
TEST(LifeCycle, NewcomersSkipSiteHomes) {
    sim::World start = sim::World::Generate({.seed = 7, .robots = 200});
    start.RunFor(sim::kMinutesPerDay + (23 * 60));  // Day 2, 23:00.
    sim::WorldState state = start.State();
    const sim::EntityId site = HabitatWithFreeHome(state);
    const sim::EntityId trust = HousingTrust(state);
    ASSERT_TRUE(site.IsValid());
    ASSERT_TRUE(trust.IsValid());
    Raise(state, site, trust, 30);  // Still going up all week.
    // The site's empty homes are the city's only free dorms, and every other
    // free home is off that list: without the skip the newcomer therefore
    // moves onto the site for sure, and with it it takes a finished home.
    std::set<std::uint64_t> homes;
    for (const sim::Robot& robot : state.robots) {
        homes.insert(robot.apartment.value);
    }
    std::uint32_t freeFinished = 0;
    for (sim::Building& building : state.city.buildings) {
        for (sim::Room& room : building.rooms) {
            if (room.kind != sim::RoomKind::Apartment || homes.contains(room.id.value)) {
                continue;
            }
            if (building.id == site) {
                room.apartmentClass = sim::ApartmentClass::Dorm;
                continue;
            }
            if (room.apartmentClass == sim::ApartmentClass::Dorm) {
                room.apartmentClass = sim::ApartmentClass::Studio;
            }
            ++freeFinished;
        }
    }
    ASSERT_GT(freeFinished, 0U) << "the city has a finished free home";
    // One robot wears out at the next midnight: its place goes and a
    // newcomer moves into the city.
    sim::Robot& doomed = state.robots[10];
    doomed.wear = sim::kMaxWear - 1;
    const std::uint64_t lastId = state.robots.back().id.value;

    sim::World world = Load(std::move(state));
    world.RunFor(90);  // Past midnight and the walk to the new home.
    const sim::WorldState& after = world.State();
    std::uint32_t arrivals = 0;
    for (const sim::Robot& robot : after.robots) {
        if (robot.id.value <= lastId) {
            continue;
        }
        ++arrivals;
        EXPECT_TRUE(robot.apartment.IsValid()) << "the newcomer got no home";
        EXPECT_FALSE(InBuilding(after, site, robot.apartment)) << "a newcomer moved onto a site";
        EXPECT_TRUE(Remembers(robot, sim::MemoryKind::ArrivedInCity));
    }
    EXPECT_GT(arrivals, 0U) << "the scrapped robot was not replaced";
    EXPECT_FALSE(after.projects.empty()) << "the site is still going up";
    EXPECT_TRUE(sim::ValidateWorldState(after));
}
