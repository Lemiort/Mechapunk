// Tests for step D2b of docs/tasks/D-construction.md: `World::ReviewConstruction`
// (sim/src/Construction.cpp) pays a site its supplies, raises one floor a
// night, ends the project when its nights are up, and starts a habitat or a
// workplace when homes or jobs have run short for a week.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {

// The economy's organization of a sector, in the order `Economy` keeps them.
[[nodiscard]] const sim::Organization* SectorOrganization(
    const sim::WorldState& state, sim::Sector sector) {
    for (const sim::Organization& organization : state.economy.organizations) {
        if (organization.sector == sector) {
            return &organization;
        }
    }
    return nullptr;
}

// The city's building with this id.
[[nodiscard]] const sim::Building* BuildingWithId(const sim::WorldState& state,
                                                  sim::EntityId id) {
    for (const sim::Building& building : state.city.buildings) {
        if (building.id == id) {
            return &building;
        }
    }
    return nullptr;
}

// The building a project is raising.
[[nodiscard]] const sim::Building* ProjectBuilding(
    const sim::WorldState& state, const sim::Project& project) {
    return BuildingWithId(state, project.building);
}

// A city whose homes are all taken: the apartments nobody lives in are gone,
// so `vacantApartments` reads zero every night and homes have run short for
// as long as the city has been running. Validation accepts it: no robot lives
// in, works in or is on its way to a room that was removed.
[[nodiscard]] sim::WorldState HomesTakenState(std::uint64_t seed,
                                              std::uint32_t robots) {
    sim::WorldState state = sim::World::Generate({.seed = seed, .robots = robots}).State();
    std::unordered_set<std::uint64_t> lived;
    for (const sim::Robot& robot : state.robots) {
        lived.insert(robot.apartment.value);
    }
    for (sim::Building& building : state.city.buildings) {
        std::erase_if(building.rooms,
                      [&lived](const sim::Room& room) {
                          return room.kind == sim::RoomKind::Apartment &&
                                 !lived.contains(room.id.value);
                      });
    }
    return state;
}

// Steps the city minute by minute until a site is going up, and says whether
// one ever started. The shortage is read off seven saved days, so nothing
// starts before the seventh midnight.
bool RunUntilASiteStarts(sim::World& world, sim::Tick days) {
    for (sim::Tick minute = 0; minute != days * sim::kMinutesPerDay; ++minute) {
        world.Step();
        if (!world.State().projects.empty()) {
            return true;
        }
    }
    return false;
}

// The project raising a habitat, if the city has one: the workplace sites
// start on the same night, so the first project is not always a home.
[[nodiscard]] const sim::Project* HabitatSite(const sim::WorldState& state) {
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        if (building != nullptr && building->kind == sim::BuildingKind::Habitat) {
            return &project;
        }
    }
    return nullptr;
}

// The plot nearest the middle of the grid, the way Construction.cpp picks
// one: the mean of the node coordinates, and the saved order of the plots
// breaks a tie.
[[nodiscard]] sim::NodeId MiddlePlot(const sim::WorldState& state) {
    std::int64_t centreX = 0;
    std::int64_t centreZ = 0;
    for (const sim::CityNode& node : state.city.nodes) {
        centreX += node.x;
        centreZ += node.z;
    }
    const std::int64_t count = static_cast<std::int64_t>(state.city.nodes.size());
    centreX /= count;
    centreZ /= count;
    std::optional<sim::NodeId> best;
    std::int64_t bestDistance = 0;
    for (const sim::CityLot& lot : state.city.lots) {
        const sim::CityNode& node = state.city.nodes[lot.node.value];
        const std::int64_t dx = node.x - centreX;
        const std::int64_t dz = node.z - centreZ;
        const std::int64_t distance = (dx * dx) + (dz * dz);
        if (!best.has_value() || distance < bestDistance) {
            best = lot.node;
            bestDistance = distance;
        }
    }
    return best.value_or(sim::NodeId{0});
}

}  // namespace

TEST(Construction, GrowsOneFloorPerNightAndEndsWhenDone) {
    // A city whose homes are all taken: the habitat rule needs seven nights
    // of vacancy under 5%, which a young city does not reach (its market
    // homes stand empty), so the homes nobody lives in are removed first.
    auto world = sim::World::FromState(HomesTakenState(33, 100));
    ASSERT_TRUE(world.has_value()) << world.error();
    ASSERT_TRUE(RunUntilASiteStarts(*world, 20));

    const sim::Project* site = HabitatSite(world->State());
    ASSERT_TRUE(site != nullptr) << "no habitat site in 20 days";
    const sim::EntityId home = site->building;
    EXPECT_EQ(site->daysLeft, 6U) << "six floors above the ground floor";
    EXPECT_EQ(site->floorsDone, 1U) << "the ground floor stands from the first night";
    const sim::Building* building = BuildingWithId(world->State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 1U);
    EXPECT_TRUE(building->rooms.empty());

    // One night: a floor more, one night less, and the homes of that floor.
    world->RunFor(sim::kMinutesPerDay);
    building = BuildingWithId(world->State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 2U);
    EXPECT_EQ(building->rooms.size(), 6U) << "six studios on a low floor";
    for (const sim::Room& room : building->rooms) {
        EXPECT_EQ(room.floor, 1U) << "the new floor, and no room on the ground";
        EXPECT_TRUE(room.owner.IsValid());
        EXPECT_NE(room.apartmentClass, sim::ApartmentClass::None);
        EXPECT_GT(room.rent, 0U);
    }
    const auto going = std::ranges::find_if(
        world->State().projects, [home](const sim::Project& each) {
            return each.building == home;
        });
    ASSERT_TRUE(going != world->State().projects.end());
    EXPECT_EQ(going->daysLeft, 5U);
    EXPECT_EQ(going->floorsDone, 2U);

    // The rest of its nights: the site ends, and the building is ordinary.
    const auto stillUp = [home](const sim::WorldState& state) {
        return std::ranges::find_if(state.projects, [home](const sim::Project& each) {
                   return each.building == home;
               }) != state.projects.end();
    };
    for (int guard = 0; guard != 10; ++guard) {
        world->RunFor(sim::kMinutesPerDay);
        if (!stillUp(world->State())) {
            break;
        }
    }
    EXPECT_FALSE(stillUp(world->State()));
    building = BuildingWithId(world->State(), home);
    ASSERT_TRUE(building != nullptr);
    EXPECT_EQ(building->floors, 7U) << "the ground floor and six of homes";
    std::vector<std::uint32_t> homesPerFloor(building->floors, 0);
    for (const sim::Room& room : building->rooms) {
        ASSERT_LT(room.floor, building->floors);
        ++homesPerFloor[room.floor];
    }
    EXPECT_EQ(homesPerFloor[0], 0U) << "the site office comes with step D3";
    for (std::size_t floor = 1; floor != homesPerFloor.size(); ++floor) {
        EXPECT_GT(homesPerFloor[floor], 0U) << "floor " << floor;
    }
}

TEST(Construction, TheOwnerPaysTheSuppliesTheSiteCosts) {
    auto world = sim::World::FromState(HomesTakenState(33, 100));
    ASSERT_TRUE(world.has_value()) << world.error();
    ASSERT_TRUE(RunUntilASiteStarts(*world, 20));
    // One night more, so the site has paid its supplies and raised a floor.
    world->RunFor(sim::kMinutesPerDay);

    const sim::WorldState& state = world->State();
    // Only a site that was already going up pays tonight: the review advances
    // the sites first and starts new ones after, so a site started this very
    // midnight has not paid yet. `floorsDone` above the ground floor says it
    // has raised a floor, so it was there to pay.
    int sitesPaying = 0;
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        if (building != nullptr && building->kind == sim::BuildingKind::Habitat &&
            project.floorsDone > 1) {
            ++sitesPaying;
        }
    }
    ASSERT_GT(sitesPaying, 0);

    // The books rolled over before the review, so tonight's supplies are
    // only what the sites paid: the housing trust's sector share of the
    // site's cost, and nothing for an industry corporation, whose supplies
    // are what the other sectors buy from it (PaySupplies, Money.cpp).
    const sim::Organization* trust = SectorOrganization(state, sim::Sector::Housing);
    const sim::Organization* industry = SectorOrganization(state, sim::Sector::Industry);
    ASSERT_TRUE(trust != nullptr);
    ASSERT_TRUE(industry != nullptr);
    EXPECT_EQ(trust->today.supplies, 100 * sitesPaying);
    EXPECT_EQ(industry->today.supplies, 0);

    // Money still adds up, with the supply market taking the site's share.
    EXPECT_TRUE(sim::ValidateEconomy(state).has_value())
        << sim::ValidateEconomy(state).error();
}

TEST(Construction, ASiteTakesTheFreePlotNearestTheMiddle) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const sim::WorldState& before = world.State();
    const sim::NodeId middle = MiddlePlot(before);
    const std::uint64_t highestBuilding = before.city.buildings.back().id.value;
    const std::size_t plots = before.city.lots.size();
    const std::size_t buildings = before.city.buildings.size();

    ASSERT_TRUE(RunUntilASiteStarts(world, 20));
    const sim::WorldState& after = world.State();
    EXPECT_GT(after.city.buildings.size(), buildings);
    EXPECT_EQ(after.city.lots.size(), plots - (after.city.buildings.size() - buildings))
        << "every new building consumes one plot";

    int raised = 0;
    for (const sim::Building& building : after.city.buildings) {
        if (building.id.value <= highestBuilding) {
            continue;
        }
        ++raised;
        EXPECT_EQ(building.floors, 1U) << "the building stands from the first night";
        EXPECT_TRUE(building.rooms.empty());
    }
    EXPECT_GT(raised, 0);

    // The first new building of the middle node's lowest free corner.
    bool onMiddle = false;
    for (const sim::Building& building : after.city.buildings) {
        onMiddle = onMiddle || (building.id.value > highestBuilding && building.entrance == middle);
    }
    EXPECT_TRUE(onMiddle) << "the site nearest the middle of the grid";

    // A plot a building already stands on would be refused on the next load.
    EXPECT_TRUE(sim::ValidateWorldState(after).has_value())
        << sim::ValidateWorldState(after).error();
}

TEST(Construction, KeepsTheCityValidAndSavable) {
    sim::World world = sim::World::Generate({.seed = 5, .robots = 1000});
    const std::size_t buildingsAtStart = world.State().city.buildings.size();
    const std::uint64_t highestAtStart = world.State().city.buildings.back().id.value;
    world.RunFor(30 * sim::kMinutesPerDay);
    const sim::WorldState& state = world.State();

    EXPECT_GT(state.city.buildings.size(), buildingsAtStart)
        << "a 1000 robot city over 30 nights raises something";

    // Homes are not short in a young city: about a third of its apartments
    // stand empty, far above the 5% the rule asks for, so every building
    // this city raised is a workplace.
    int raisedHabitats = 0;
    for (const sim::Building& building : state.city.buildings) {
        if (building.id.value > highestAtStart &&
            building.kind == sim::BuildingKind::Habitat) {
            ++raisedHabitats;
        }
    }
    EXPECT_EQ(raisedHabitats, 0) << "the vacancy rule bites only when homes run short";
    EXPECT_TRUE(sim::ValidateWorldState(state).has_value())
        << sim::ValidateWorldState(state).error();
    EXPECT_TRUE(sim::ValidateEconomy(state).has_value())
        << sim::ValidateEconomy(state).error();

    // Every site names a building of the city, has its owner, and has no
    // finished floor the building does not have; its rooms are on finished
    // floors above the ground floor.
    for (const sim::Project& project : state.projects) {
        const sim::Building* building = ProjectBuilding(state, project);
        ASSERT_TRUE(building != nullptr) << project.building.value;
        EXPECT_GE(building->floors, project.floorsDone);
        EXPECT_GT(project.daysLeft, 0U) << "a finished site is dropped";
        for (const sim::Room& room : building->rooms) {
            EXPECT_GT(room.floor, 0U);
            EXPECT_EQ(room.owner, project.owner) << "the site's owner owns its rooms";
        }
    }

    // The new buildings and rooms survive a save.
    auto json = sim::SerializeWorld(state);
    ASSERT_TRUE(json.has_value());
    auto loaded = sim::DeserializeWorld(*json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    auto resumed = sim::World::FromState(std::move(*loaded));
    ASSERT_TRUE(resumed.has_value()) << resumed.error();
    EXPECT_EQ(resumed->State().projects.size(), state.projects.size());
    EXPECT_EQ(resumed->State().city.buildings.size(), state.city.buildings.size());
}

TEST(Construction, RunsTheSameWayTwice) {
    // Construction takes no random number and walks saved lists in order,
    // so the same city raises the same buildings on every run (rule 1).
    auto run = [](std::uint64_t seed) {
        sim::World world = sim::World::Generate({.seed = seed, .robots = 1000});
        world.RunFor(12 * sim::kMinutesPerDay);
        auto json = sim::SerializeWorld(world.State());
        EXPECT_TRUE(json.has_value());
        return *json;
    };
    const std::string first = run(5);
    const std::string second = run(5);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, run(6)) << "a different seed still gives a different city";
}
