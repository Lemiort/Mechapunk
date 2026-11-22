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

// Appends one chronicle entry to a save document: all seven keys the format
// has for an entry, everything but the tick left empty.
void PushChronicleEntry(sim::SaveDocument& chronicle, std::uint64_t tick) {
    sim::SaveDocument entry = sim::SaveDocument::object_t{};
    entry["tick"] = tick;
    entry["kind"] = "scrapped";
    entry["robot"] = std::uint64_t{0};
    entry["otherRobot"] = std::uint64_t{0};
    entry["room"] = std::uint64_t{0};
    entry["credits"] = std::int64_t{0};
    entry["memory"] = "arrivedInCity";
    chronicle.get_array().push_back(std::move(entry));
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
        kFixtureDirectory / "saves" / "v20-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(Serialize(*state), fixture);
    // Saved with City Hall collecting taxes,
    EXPECT_GT(state->history.back().counted.wageTax, 0);
    // Saved with venues asking more and less than usual, rents moved,
    EXPECT_TRUE(std::ranges::any_of(state->city.buildings, [](const sim::Building& building) {
        return std::ranges::any_of(building.rooms, [](const sim::Room& room) {
            return sim::IsVenue(room.kind) && room.pricePercent != 100;
        });
    }));
    EXPECT_TRUE(std::ranges::any_of(state->economy.rentPercent,
                                    [](std::int64_t percent) { return percent != 100; }));
    // robots still in dorms and others moved on,
    std::set<sim::ApartmentClass> lived;
    for (const sim::Robot& robot : state->robots) {
        for (const sim::Building& building : state->city.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.id == robot.apartment) {
                    lived.insert(room.apartmentClass);
                }
            }
        }
    }
    EXPECT_TRUE(lived.contains(sim::ApartmentClass::Dorm));
    EXPECT_GE(lived.size(), 2U);
    // venue staff,
    EXPECT_TRUE(std::ranges::any_of(state->robots, [&](const sim::Robot& robot) {
        for (const sim::Building& building : state->city.buildings) {
            for (const sim::Room& room : building.rooms) {
                if (room.id == robot.workspace && sim::IsVenue(room.kind)) {
                    return true;
                }
            }
        }
        return false;
    }));
    // tenants, savers, robots and a corporation in debt, scrapped
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
    // Police stations and metro depots with crew rooms; officers here can
    // walk a patrol leg, so this asserts nothing about who happens to be
    // mid patrol at the saved minute.
    EXPECT_TRUE(std::ranges::any_of(state->city.buildings, [](const sim::Building& building) {
        return std::ranges::any_of(building.rooms, [](const sim::Room& room) {
            return room.kind == sim::RoomKind::PoliceStation ||
                   room.kind == sim::RoomKind::CrewRoom;
        });
    }));
    // Saved in a city that has gangs, and a chronicle that already tells a
    // story: the recording step exists, so notable events were kept while
    // this fixture ran. The round trip above is what proves the save
    // carries the keys at all.
    EXPECT_FALSE(state->gangs.empty());
    EXPECT_FALSE(state->chronicle.empty());

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 19 save already has every key version 20 reads: the new
// `arrestedSomeone` memory kind can only appear in files version 20 writes,
// so the migration is the version number alone. The fixture is checked for
// the absence of that kind, so what loads is the migration's work and not a
// fixture that quietly grew the new memory.
TEST(Save, Version19FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v19-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    EXPECT_EQ(fixture.find("\"arrestedSomeone\""), std::string::npos);

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(state->chronicle.empty());
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
    // And running through version 20 code writes the new kind only when an
    // officer arrests: the v19 story itself is left untouched.
    for (const sim::ChronicleEntry& entry : world->State().chronicle) {
        EXPECT_NE(entry.tick, 0U);
    }
}

// The chronicle is kept in order and in check: an entry older than the one
// before it is refused, as is a save holding more than the game keeps.
TEST(Save, RejectsChroniclesOutOfOrderOrOverfull) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});
    const std::string json = Serialize(world.State());

    const auto backwards = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            auto& chronicle = document["world"]["chronicle"];
            PushChronicleEntry(chronicle, 500);
            PushChronicleEntry(chronicle, 400);
        }));
    ASSERT_TRUE(backwards.has_value()) << backwards.error();
    auto reordered = sim::World::FromState(std::move(*backwards));
    ASSERT_FALSE(reordered);
    EXPECT_NE(reordered.error().find("chronicle"), std::string::npos)
        << reordered.error();

    const auto overfull = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            auto& chronicle = document["world"]["chronicle"];
            for (std::size_t i = 0; i <= sim::kChronicleCap; ++i) {
                PushChronicleEntry(chronicle, static_cast<std::uint64_t>(i));
            }
        }));
    ASSERT_TRUE(overfull.has_value()) << overfull.error();
    auto rejected = sim::World::FromState(std::move(*overfull));
    ASSERT_FALSE(rejected);
    EXPECT_NE(rejected.error().find("chronicle"), std::string::npos)
        << rejected.error();
}

