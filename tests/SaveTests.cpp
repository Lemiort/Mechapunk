#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

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

// Applies `edit` to the parsed save and returns the edited JSON.
template <typename Edit>
std::string EditSave(const std::string& json, Edit edit) {
    sim::SaveDocument document;
    EXPECT_FALSE(glz::read_json(document, json));
    edit(document);
    return glz::write_json(document).value_or("");
}

const std::filesystem::path kFixtureDirectory{MECHAPUNK_FIXTURE_DIR};
}  // namespace

TEST(Save, RoundTripPreservesTheWorld) {
    sim::World world = sim::World::Generate({.seed = 12, .robots = 40});
    world.RunFor(sim::kMinutesPerDay + 600);
    const std::string json = Serialize(world.State());
    auto loaded = sim::DeserializeWorld(json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_EQ(Serialize(*loaded), json);
}

TEST(Save, LoadedWorldContinuesExactlyLikeTheOriginal) {
    sim::World straight = sim::World::Generate({.seed = 21, .robots = 70});
    straight.RunFor(3 * sim::kMinutesPerDay);

    sim::World first = sim::World::Generate({.seed = 21, .robots = 70});
    first.RunFor((3 * sim::kMinutesPerDay) / 2);
    auto state = sim::DeserializeWorld(Serialize(first.State()));
    ASSERT_TRUE(state.has_value()) << state.error();
    auto resumed = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(resumed.has_value()) << resumed.error();
    resumed->RunFor((3 * sim::kMinutesPerDay) / 2);

    EXPECT_EQ(Serialize(resumed->State()), Serialize(straight.State()));
}

TEST(Save, FileRoundTripLeavesNoTemporaryFile) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "mechapunk-save-test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "world.json";
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});

    ASSERT_TRUE(sim::SaveWorldToFile(world.State(), path));
    // Saving again replaces the existing file.
    ASSERT_TRUE(sim::SaveWorldToFile(world.State(), path));
    EXPECT_FALSE(std::filesystem::exists(directory / "world.json.tmp"));

    auto loaded = sim::LoadWorldFromFile(path);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_EQ(Serialize(*loaded), Serialize(world.State()));
    std::filesystem::remove_all(directory);

    EXPECT_FALSE(sim::LoadWorldFromFile(directory / "missing.json"));
}

TEST(Save, RejectsForeignNewerAndBrokenSaves) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});
    const std::string json = Serialize(world.State());

    EXPECT_FALSE(sim::DeserializeWorld("not json"));
    EXPECT_FALSE(sim::DeserializeWorld(R"({"format":"other","version":1})"));

    const auto newer = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            document["version"] = sim::kCurrentSaveVersion + 1;
        }));
    ASSERT_FALSE(newer);
    EXPECT_NE(newer.error().find("not supported"), std::string::npos)
        << newer.error();

    // A missing field means a migration was forgotten.
    const auto missing = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            document["world"].get_object().erase("tick");
        }));
    EXPECT_FALSE(missing);

    const auto unknown = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            document["world"]["weather"] = "rain";
        }));
    EXPECT_FALSE(unknown);
}

TEST(Save, ShippedMigrationsCoverEveryVersion) {
    for (std::uint32_t version = 1; version < sim::kCurrentSaveVersion;
         ++version) {
        EXPECT_TRUE(std::ranges::any_of(
            sim::SaveMigrations(),
            [&](const sim::Migration& migration) {
                return migration.fromVersion == version;
            }))
            << "no migration from version " << version;
    }
}

TEST(Save, MigrationsRunInOrderAndReportGaps) {
    constexpr std::array kMigrations{
        sim::Migration{
            .fromVersion = 1,
            .summary = "add battery",
            .apply = [](sim::SaveDocument& world)
                -> std::expected<void, std::string> {
                world["battery"] = 100;
                return {};
            }},
        sim::Migration{
            .fromVersion = 2,
            .summary = "rename battery to charge",
            .apply = [](sim::SaveDocument& world)
                -> std::expected<void, std::string> {
                if (!world.contains("battery")) {
                    return std::unexpected("battery missing");
                }
                world["charge"] = world["battery"];
                world.get_object().erase("battery");
                return {};
            }},
    };

    sim::SaveDocument world = sim::SaveDocument::object_t{};
    ASSERT_TRUE(sim::MigrateSave(world, 1, 3, kMigrations));
    EXPECT_FALSE(world.contains("battery"));
    EXPECT_EQ(world["charge"].as<std::uint64_t>(), 100U);

    sim::SaveDocument fresh = sim::SaveDocument::object_t{};
    const auto failed = sim::MigrateSave(fresh, 2, 3, kMigrations);
    ASSERT_FALSE(failed);
    EXPECT_NE(failed.error().find("battery missing"), std::string::npos);

    EXPECT_FALSE(sim::MigrateSave(fresh, 1, 4, kMigrations));  // no 3 -> 4
    EXPECT_FALSE(sim::MigrateSave(fresh, 3, 1, kMigrations));  // downgrade
    EXPECT_TRUE(sim::MigrateSave(fresh, 3, 3, kMigrations));   // nothing to do
}

// The committed fixture was written by the current save version. Saving it
// again must reproduce it byte for byte; when the format changes on purpose,
// bump kCurrentSaveVersion, add a migration and a new fixture, and keep this
// one loading through the migration (see tests/fixtures/saves/README.md).
TEST(Save, CurrentVersionFixtureLoadsAndRoundTrips) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v10-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(Serialize(*state), fixture);
    // Saved with tenants, savers, robots and a corporation in debt, scrapped
    // robots still remembered, and days of statistics.
    EXPECT_TRUE(std::ranges::any_of(state->robots, [&](const sim::Robot& robot) {
        for (const sim::Building& building : state->city.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.id == robot.apartment && room.owner != robot.id) {
                    return true;
                }
            }
        }
        return false;
    }));
    EXPECT_TRUE(std::ranges::any_of(state->robots,
                                    [](const sim::Robot& robot) { return robot.savings > 0; }));
    EXPECT_GT(state->economy.loans.size(), 1U);
    EXPECT_FALSE(state->departed.empty());
    EXPECT_GT(state->history.size(), 10U);
    EXPECT_FALSE(state->economy.emissions.empty());
    // The fixture carries friendships and memories.
    EXPECT_TRUE(std::ranges::any_of(state->robots, [](const sim::Robot& robot) {
        return !robot.bonds.empty() && !robot.memories.empty();
    }));
    EXPECT_GT(state->weather.spells, 10U);

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

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
