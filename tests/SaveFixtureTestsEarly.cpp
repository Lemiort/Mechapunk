// The fixtures of the frozen save versions 1 to 8: each one loads through
// the migrations of MigrationsEarly.cpp and goes on living, which is how
// players of an old save know nothing broke. The fixtures from version 9
// on are in SaveFixtureTests.cpp, and the current version's fixture and
// the test for the previous version stay in SaveTests.cpp (guide, rule 3).

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

// A save from the first release, before rooms existed, must load through the
// migration and go on living: robots keep their homes and jobs, and the
// stores added to their habitats fill up in the evening.
TEST(Save, Version1FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v1-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    sim::SaveDocument original;
    ASSERT_FALSE(glz::read_json(original, fixture));
    auto& v1Robots = original["world"]["robots"].get_array();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    ASSERT_EQ(state->robots.size(), v1Robots.size());
    const sim::RoomIndex rooms(state->city);
    for (std::size_t index = 0; index < v1Robots.size(); ++index) {
        const sim::Robot& robot = state->robots[index];
        const auto home = rooms.Find(robot.apartment);
        const auto work = rooms.Find(robot.workspace);
        ASSERT_TRUE(home && work) << robot.name;
        EXPECT_EQ(home->building->id.value,
                  v1Robots[index]["home"].as<std::uint64_t>());
        EXPECT_EQ(work->building->id.value,
                  v1Robots[index]["workplace"].as<std::uint64_t>());
    }

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint64_t visits = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        visits += event.kind == sim::EventKind::ArrivedAtVenue ? 1 : 0;
    });
    world->RunFor(2 * sim::kMinutesPerDay);
    EXPECT_GT(visits, 0U);
    // Saving the migrated world writes the current version.
    EXPECT_NE(Serialize(world->State())
                  .find(std::format("\"version\": {}", sim::kCurrentSaveVersion)),
              std::string::npos);
}

// A version 2 save (rooms, no rent or days off) loads through one migration:
// every apartment gets rent, every robot two days off, and the city keeps
// collecting rent and taking days off.
TEST(Save, Version2FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v2-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Building& building : state->city.buildings) {
        for (const sim::Room& room : building.rooms) {
            EXPECT_EQ(room.rent > 0, room.kind == sim::RoomKind::Apartment)
                << room.name;
        }
    }
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(std::popcount(static_cast<unsigned>(robot.daysOff)), 2)
            << robot.name;
        if (robot.activity == sim::RobotActivity::Resting) {
            EXPECT_EQ(robot.commuteAt, robot.nextActionAt) << robot.name;
        }
    }

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint64_t rentPaid = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        rentPaid += event.kind == sim::EventKind::RentPaid ? 1 : 0;
    });
    world->RunFor(3 * sim::kMinutesPerDay);
    EXPECT_GT(rentPaid, 0U);
}

// A version 3 save (rent and days off, no relationships) loads through one
// migration: every robot gets two interests and starts without bonds or
// memories, and robots make friends once the city runs.
TEST(Save, Version3FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v3-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    std::set<std::uint8_t> tastes;
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(std::popcount(static_cast<unsigned>(robot.interests)), 2)
            << robot.name;
        EXPECT_TRUE(robot.bonds.empty());
        EXPECT_TRUE(robot.memories.empty());
        tastes.insert(robot.interests);
    }
    EXPECT_GT(tastes.size(), 10U);

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint64_t friendships = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        friendships += event.kind == sim::EventKind::Remembered &&
                               event.memory == sim::MemoryKind::BecameFriends
                           ? 1
                           : 0;
    });
    world->RunFor(5 * sim::kMinutesPerDay);
    EXPECT_GT(friendships, 0U);
}

// A version 4 save (relationships, no careers) loads through one migration:
// every robot starts as a junior with a clean record, and keeps working.
TEST(Save, Version4FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v4-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(robot.jobLevel, 0) << robot.name;
        EXPECT_EQ(robot.performance, 0) << robot.name;
        EXPECT_TRUE(robot.workspace.IsValid()) << robot.name;
    }

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    std::uint64_t shifts = 0;
    world->SetEventSink([&](const sim::SimEvent& event) {
        shifts += event.kind == sim::EventKind::ShiftEnded ? 1 : 0;
    });
    world->RunFor(3 * sim::kMinutesPerDay);
    EXPECT_GT(shifts, 0U);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 5 save (careers, no aging) loads through one migration: robots
// get an age and the wear that goes with it, nobody has been scrapped yet,
// and the city goes on aging.
TEST(Save, Version5FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v5-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(state->departed.empty());
    for (const sim::Robot& robot : state->robots) {
        EXPECT_GE(robot.age, 30U) << robot.name;
        EXPECT_LT(robot.age, 330U) << robot.name;
        EXPECT_EQ(robot.wear, sim::WearAtAge(robot.age)) << robot.name;
    }
    // The youngest robot is the least likely to be scrapped in three days.
    const sim::Robot& youngest = *std::ranges::min_element(
        state->robots, std::less{}, &sim::Robot::age);
    const sim::EntityId id = youngest.id;
    const std::uint32_t age = youngest.age;

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(3 * sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
    const sim::Robot* older = world->FindRobot(id);
    ASSERT_NE(older, nullptr);
    EXPECT_EQ(older->age, age + 3);
}

// A version 6 save (aging, no metro) loads through one migration: the city
// gets a station at every third intersection, nobody is riding yet, and
// robots start taking the metro.
TEST(Save, Version6FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v6-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    // An 8 by 8 grid: rows and columns 1, 4 and 7.
    ASSERT_EQ(state->city.stations.size(), 9U);
    EXPECT_EQ(state->city.stations.front().node.value, 9U);
    EXPECT_EQ(state->city.stations.back().node.value, 63U);
    EXPECT_EQ(state->city.stations.front().name, "Neon Square");
    for (const sim::Robot& robot : state->robots) {
        EXPECT_EQ(robot.alightsAt, 0U) << robot.name;
    }

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

