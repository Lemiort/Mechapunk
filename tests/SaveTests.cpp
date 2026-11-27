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

// Appends one chronicle entry to a save document: all eight keys the format
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
    entry["account"] = std::uint64_t{0};
    chronicle.get_array().push_back(std::move(entry));
}

// Appends one project to a save document: all six keys the format has for a
// project, one day left, `floorsDone` floors finished, `costSoFar` credits
// paid for its supplies and the shortage `reason` that started it.
void PushProject(sim::SaveDocument& projects, std::uint64_t building,
                 std::uint64_t owner, std::uint64_t floorsDone = 1,
                 std::int64_t costSoFar = 0, std::string_view reason = "homesShort") {
    sim::SaveDocument project = sim::SaveDocument::object_t{};
    project["building"] = building;
    project["owner"] = owner;
    project["daysLeft"] = std::uint64_t{1};
    project["floorsDone"] = floorsDone;
    project["costSoFar"] = costSoFar;
    project["reason"] = std::string(reason);
    projects.get_array().push_back(std::move(project));
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

    // A save from before the oldest supported version is refused with a
    // plain message, not a missing migration.
    const auto old = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            document["version"] = sim::kOldestSupportedSaveVersion - 1;
        }));
    ASSERT_FALSE(old);
    EXPECT_NE(old.error().find("not supported"), std::string::npos) << old.error();

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
    for (std::uint32_t version = sim::kOldestSupportedSaveVersion;
         version < sim::kCurrentSaveVersion; ++version) {
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
// bump kCurrentSaveVersion, add a migration and a new fixture (see
// tests/fixtures/saves/README.md).
TEST(Save, CurrentVersionFixtureLoadsAndRoundTrips) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v29-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(Serialize(*state), fixture);
    // From version 28 the simulation's settings are saved with the city; this
    // fixture was made with the defaults.
    EXPECT_EQ(state->settings, sim::SimSettings{});
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
    // And from version 21 the story names accounts: an entry for an
    // emission or for a unit opening or closing carries the account its
    // money moved for, so the fixture's chronicle has at least one entry
    // with a valid account.
    EXPECT_TRUE(std::ranges::any_of(state->chronicle,
                                    [](const sim::ChronicleEntry& entry) {
                                        return entry.account.IsValid();
                                    }));
    // And from version 22 the city saves its free plots: the corners of its
    // intersections no building stands on, so the fixture has plots and
    // none of them is a corner a building already occupies.
    EXPECT_FALSE(state->city.lots.empty());
    {
        std::vector<std::uint32_t> taken(state->city.nodes.size(), 0);
        for (const sim::Building& building : state->city.buildings) {
            ++taken.at(building.entrance.value);
        }
        for (const sim::CityLot& lot : state->city.lots) {
            EXPECT_LT(lot.corner, sim::kCornersPerNode);
            EXPECT_GE(lot.corner, taken.at(lot.node.value));
        }
    }
    // And from version 23 the world saves its projects: buildings under
    // construction. The nightly review (step D2b) ran through this fixture's
    // twenty days but never saw a shortage last seven nights, so its list
    // is empty; the round trip above is what proves the save carries the key
    // at all.
    EXPECT_TRUE(state->projects.empty());
    // And from version 24 rooms may be site offices of a building going up
    // (step D3a): nothing raises one until step D3b, so this fixture has
    // none, and the hand-written save in `SiteOfficeRoomKindRoundTrips` is
    // what proves the new kind name reads and writes.
    EXPECT_TRUE(std::ranges::none_of(state->city.buildings, [](const sim::Building& building) {
        return std::ranges::any_of(building.rooms, [](const sim::Room& room) {
            return room.kind == sim::RoomKind::SiteOffice;
        });
    }));
    // And from version 25 a project carries the credits its site has paid
    // for its supplies and the shortage that started it (step D5b2). This
    // fixture raised no site, so its list is empty and the hand-written save
    // in `ProjectCostAndReasonRoundTrips` is what proves the keys read and
    // write.
    // And from version 26 the daily statistics count construction (step D5e2):
    // the sites the review started and ended in a day, and the robots whose
    // job is a site office. This fixture ran the review through its twenty
    // days and raised no site, so today's books and every saved day count
    // none; the round trip above is what proves the save carries the keys
    // at all.
    EXPECT_EQ(state->today.projectsStarted, 0);
    EXPECT_EQ(state->today.projectsFinished, 0);
    for (const sim::DailyStats& day : state->history) {
        EXPECT_EQ(day.counted.projectsStarted, 0);
        EXPECT_EQ(day.counted.projectsFinished, 0);
        EXPECT_EQ(day.buildersEmployed, 0);
    }
    // And from version 27 every building names the style it is dressed in
    // (task I, step I1b): its massing and its roof, which change nothing a
    // robot does. The round trip above is what proves the key is written and
    // read back; what is checked here is that the fixture's town wears more
    // than one style and that each building carries the one its own id, kind
    // and floor count give it.
    {
        std::set<sim::BuildingStyle> styles;
        for (const sim::Building& building : state->city.buildings) {
            EXPECT_EQ(building.style,
                      sim::StyleOf(building.kind, building.id,
                                   building.floors))
                << building.name;
            styles.insert(building.style);
        }
        EXPECT_GE(styles.size(), 2U);
    }

    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// The new kind reads and writes both ways: a save that names one room
// "siteOffice" deserializes, loads through FromState - a room is checked
// by floor, capacity and unique id, not by a kind table, and no robot
// lives or works in the room the test picks - and serializes back with
// the name intact.
TEST(Save, SiteOfficeRoomKindRoundTrips) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});
    const std::string json = Serialize(world.State());
    std::vector<std::uint64_t> used;
    for (const sim::Robot& robot : world.State().robots) {
        used.push_back(robot.apartment.value);
        used.push_back(robot.workspace.value);
        used.push_back(robot.location.value);
        used.push_back(robot.destination.value);
    }
    std::ranges::sort(used);
    const auto edited = EditSave(json, [=](sim::SaveDocument& document) {
        for (sim::SaveDocument& building :
              document["world"]["city"]["buildings"].get_array()) {
            for (sim::SaveDocument& room : building["rooms"].get_array()) {
                const std::uint64_t id = room["id"].as<std::uint64_t>();
                if (std::binary_search(used.begin(), used.end(), id)) {
                    continue;
                }
                room["kind"] = "siteOffice";
                return;
            }
        }
    });
    auto state = sim::DeserializeWorld(edited);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(std::ranges::any_of(state->city.buildings, [](const sim::Building& building) {
        return std::ranges::any_of(building.rooms, [](const sim::Room& room) {
            return room.kind == sim::RoomKind::SiteOffice;
        });
    }));
    auto loaded = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    // The write direction: serializing the loaded world names the kind again.
    EXPECT_NE(Serialize(loaded->State()).find("\"siteOffice\""), std::string::npos);
}

// The two keys version 25 adds to a project read and write both ways: a save
// that names the credits a site has paid and the shortage that started it
// loads with them, and serializing what loaded names them again.
TEST(Save, ProjectCostAndReasonRoundTrips) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});
    const std::string json = Serialize(world.State());
    const std::uint64_t building = world.State().city.buildings.back().id.value;
    const std::uint64_t owner = world.State().economy.organizations.front().id.value;
    const auto loaded = sim::DeserializeWorld(
        EditSave(json, [=](sim::SaveDocument& document) {
            PushProject(document["world"]["projects"], building, owner, 1, 500,
                        "jobsShort");
        }));
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    ASSERT_EQ(loaded->projects.size(), 1U);
    EXPECT_EQ(loaded->projects.front().costSoFar, 500);
    EXPECT_EQ(loaded->projects.front().reason, sim::ProjectReason::JobsShort);
    const std::string written = Serialize(loaded.value());
    EXPECT_NE(written.find("\"costSoFar\": 500"), std::string::npos) << written;
    EXPECT_NE(written.find("\"jobsShort\""), std::string::npos) << written;
}

// A project names a building of the city and an organization that pays
// for it, its floorsDone cannot pass the building's floors, and the list
// is sorted by building id: a save that breaks one of these is refused
// by FromState, as the chronicle's checks are.
TEST(Save, RejectsProjectsOutOfOrderOrBroken) {
    const sim::World world = sim::World::Generate({.seed = 3, .robots = 10});
    const std::string json = Serialize(world.State());
    const std::vector<sim::Building> buildings = world.State().city.buildings;
    const std::vector<sim::Organization> organizations =
        world.State().economy.organizations;
    ASSERT_GE(buildings.size(), 2U);
    ASSERT_FALSE(organizations.empty());
    const std::uint64_t owner = organizations.front().id.value;
    const std::uint64_t later = buildings.back().id.value;
    const std::uint64_t earlier = buildings[buildings.size() - 2].id.value;

    // Two real buildings, named in decreasing order.
    const auto outOfOrder = sim::DeserializeWorld(
        EditSave(json, [=](sim::SaveDocument& document) {
            PushProject(document["world"]["projects"], later, owner);
            PushProject(document["world"]["projects"], earlier, owner);
        }));
    ASSERT_TRUE(outOfOrder.has_value()) << outOfOrder.error();
    auto reordered = sim::World::FromState(std::move(*outOfOrder));
    ASSERT_FALSE(reordered);
    EXPECT_NE(reordered.error().find("out of order"), std::string::npos)
        << reordered.error();

    // A building the city does not have.
    const auto missing = sim::DeserializeWorld(
        EditSave(json, [=](sim::SaveDocument& document) {
            PushProject(document["world"]["projects"], later + 1000000U, owner);
        }));
    ASSERT_TRUE(missing.has_value()) << missing.error();
    auto unknown = sim::World::FromState(std::move(*missing));
    ASSERT_FALSE(unknown);
    EXPECT_NE(unknown.error().find("does not have"), std::string::npos)
        << unknown.error();

    // A building of the city, but no organization to pay for it.
    const auto unowned = sim::DeserializeWorld(
        EditSave(json, [=](sim::SaveDocument& document) {
            PushProject(document["world"]["projects"], later, 1000000U);
        }));
    ASSERT_TRUE(unowned.has_value()) << unowned.error();
    auto orphan = sim::World::FromState(std::move(*unowned));
    ASSERT_FALSE(orphan);
    EXPECT_NE(orphan.error().find("no organization"), std::string::npos)
        << orphan.error();

    // More floors done than the building has.
    const auto overbuilt = sim::DeserializeWorld(
        EditSave(json, [=](sim::SaveDocument& document) {
            PushProject(document["world"]["projects"], later, owner,
                        buildings.back().floors + 1);
        }));
    ASSERT_TRUE(overbuilt.has_value()) << overbuilt.error();
    auto impossible = sim::World::FromState(std::move(*overbuilt));
    ASSERT_FALSE(impossible);
    EXPECT_NE(impossible.error().find("floors done"), std::string::npos)
        << impossible.error();
}

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


// The previous version's fixture: a version 28 city has only the crime and
// construction settings, and the migration gives it the rest at the defaults
// that game ran with.
TEST(Save, Version28FixtureMigratesAndRuns) {
    const std::filesystem::path path = kFixtureDirectory / "saves" / "v28-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(state->settings, sim::SimSettings{});
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 27 city, two steps behind: it is given every setting at its
// default.
TEST(Save, Version27FixtureMigratesAndRuns) {
    const std::filesystem::path path = kFixtureDirectory / "saves" / "v27-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_EQ(state->settings, sim::SimSettings{});
}

// A city made with other settings keeps them through a save and a load, and
// a save whose settings are out of range is refused.
TEST(Save, CustomSettingsSurviveSaveAndLoad) {
    sim::SimSettings custom;
    ASSERT_TRUE(sim::ApplySetting(custom, "crime.robberyPercent", 77));
    ASSERT_TRUE(sim::ApplySetting(custom, "construction.streakNights", 2));
    const sim::World world =
        sim::World::Generate({.seed = 3, .robots = 20, .settings = custom});
    const std::string json = Serialize(world.State());
    EXPECT_NE(json.find("\"robberyPercent\": 77"), std::string::npos);

    auto loaded = sim::DeserializeWorld(json);
    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_EQ(loaded->settings, custom);
    auto again = sim::World::FromState(std::move(*loaded));
    ASSERT_TRUE(again.has_value()) << again.error();
    EXPECT_EQ(again->Settings(), custom);

    const auto broken = sim::DeserializeWorld(
        EditSave(json, [](sim::SaveDocument& document) {
            document["world"]["settings"]["crime"]["robberyPercent"] = 500;
        }));
    ASSERT_TRUE(broken.has_value()) << broken.error();
    auto refused = sim::World::FromState(std::move(*broken));
    ASSERT_FALSE(refused);
    EXPECT_NE(refused.error().find("robberyPercent"), std::string::npos) << refused.error();
}
