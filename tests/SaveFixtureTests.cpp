// The committed fixtures of save versions 7 to 18: each one loads through
// the migrations and goes on living, which is how players of an old save
// know nothing broke. The fixtures before version 7 and from version 19
// on are in SaveFixtureTestsEarly.cpp, and the current version's fixture
// and the test for the previous version stay in SaveTests.cpp (guide, rule 3).

#include <gtest/gtest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>

#include <glaze/glaze.hpp>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace sim = mechapunk::sim;

namespace {
std::string Serialize(const sim::WorldState& state) {
    auto json = sim::SerializeWorld(state);
    EXPECT_TRUE(json.has_value());
    return json.value_or("");
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

const std::filesystem::path kFixtureDirectory{MECHAPUNK_FIXTURE_DIR};
}  // namespace

// A version 7 save (metro, no weather) loads through one migration: the
// world starts dry for six hours, then its weather changes like any other.
TEST(Save, Version7FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v7-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(state->weather.rain, 0U);
    EXPECT_EQ(state->weather.spells, 0U);
    EXPECT_EQ(state->weather.changesAt, state->tick + (6 * sim::kMinutesPerHour));

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint32_t changes = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        changes += event.kind == sim::EventKind::WeatherChanged ? 1 : 0;
    });
    world->RunFor(4 * sim::kMinutesPerDay);
    EXPECT_GT(changes, 0U);
    EXPECT_EQ(world->State().weather.spells, changes);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 8 save (no changes of line) loads through one migration:
// robots on the metro change at their last station, that is, they don't,
// and the city goes on riding the timetable's trains.
TEST(Save, Version8FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v8-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    bool riding = false;
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(robot.rideVia, robot.rideTo) << robot.name;
        riding = riding || robot.alightsAt != 0;
    }
    EXPECT_TRUE(riding);

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint64_t rides = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        rides += event.kind == sim::EventKind::Departed && event.metro ? 1 : 0;
    });
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_GT(rides, 0U);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 9 save gets a founded economy: organizations with capital,
// every room an owner, robots a bank, and money that adds up; then it
// keeps running and records statistics.
TEST(Save, Version9FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v9-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_GT(state->economy.organizations.size(), 6U);
    EXPECT_TRUE(state->history.empty());
    EXPECT_TRUE(sim::ValidateWorldState(*state));

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_EQ(world->State().history.size(), 2U);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 10 save gets daily budgets and a price of supplies, and goes on
// running: out-of-work robots find jobs on venues' staff.
TEST(Save, Version10FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v10-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(state->economy.supplyPercent, 60);
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(robot.spentToday, 0) << robot.name;
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 11 save's apartments all become studios, and the city gets a
// housing office with nothing to its name yet; then it keeps running.
TEST(Save, Version11FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v11-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Building& building : state->city.buildings) {
        for (const sim::Room& room : building.rooms) {
            EXPECT_EQ(room.apartmentClass, room.kind == sim::RoomKind::Apartment
                                               ? sim::ApartmentClass::Studio
                                               : sim::ApartmentClass::None);
        }
    }
    EXPECT_TRUE(std::ranges::any_of(state->economy.organizations, [](const sim::Organization& each) {
        return each.kind == sim::OrganizationKind::Government;
    }));
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 12 save's venues ask their base prices and market rents start
// at their base; then it keeps running.
TEST(Save, Version12FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v12-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const std::int64_t percent : state->economy.rentPercent) {
        EXPECT_EQ(percent, 100);
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 13 save's rooms have not been quiet yet; then it keeps running.
TEST(Save, Version13FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v13-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Building& building : state->city.buildings) {
        for (const sim::Room& room : building.rooms) {
            EXPECT_EQ(room.quietNights, 0U);
        }
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 14 save's housing office becomes City Hall and takes over the
// metro; then it keeps running and collecting taxes.
TEST(Save, Version14FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v14-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(std::ranges::any_of(state->economy.organizations, [](const sim::Organization& each) {
        return each.kind == sim::OrganizationKind::Government && each.name == "City Hall";
    }));
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_GT(world->State().history.back().counted.wageTax, 0);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 15 save's central bank starts at the old fixed rate.
TEST(Save, Version15FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v15-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(state->economy.policyRate, 10);
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 16 save predates patrols: every robot's patrol is cleared, and
// the city keeps running with its police stations and metro depots.
TEST(Save, Version16FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v16-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Robot& robot : state->robots) {
        EXPECT_FALSE(robot.patrol.IsValid()) << robot.name;
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 17 save predates gangs: every robot's gang is zero, arrests are
// zero, and crime counters start at zero; the city keeps running and grows
// gangs at its first midnight.
TEST(Save, Version17FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v17-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(robot.gang.value, 0U) << robot.name;
        EXPECT_EQ(robot.arrests, 0U) << robot.name;
    }
    EXPECT_TRUE(state->gangs.empty());
    EXPECT_EQ(state->today.robberies, 0);
    for (const auto& day : state->history) {
        EXPECT_EQ(day.counted.robberies, 0);
        EXPECT_EQ(day.gangMembers, 0);
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
    // The save had no gangs and grew some at its first midnight, from its own
    // seed; Gangs.OldSaveGrowsGangsAtMidnight checks what they look like.
    EXPECT_FALSE(world->State().gangs.empty());
}

// A version 18 save predates the chronicle: the only thing its migration adds
// is an empty one, and the city keeps running. The fixture is checked for
// the absence of the key, so what loads is the migration's work, not a
// fixture that quietly grew the field.
TEST(Save, Version18FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v18-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    EXPECT_EQ(fixture.find("\"chronicle\""), std::string::npos);

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(state->chronicle.empty());
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}
