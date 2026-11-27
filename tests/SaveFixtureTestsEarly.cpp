// The fixtures of the frozen save versions 1 to 8, and of versions 19 up
// to the one before the current: each one loads through the migrations
// of MigrationsEarly.cpp and MigrationsMid.cpp and goes on living, which
// is how players of an old save know nothing broke. The fixtures of
// versions 9 to 18 are in SaveFixtureTests.cpp, and the current version's
// fixture and the test for the previous version stay in SaveTests.cpp
// (guide, rule 3).

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

    // A story that saw an arrest: version 19 wrote the criminal into
    // `robot` and knew no officer, so the migration turns the entry
    // around - criminal in `otherRobot`, `robot` none - instead of inventing
    // an officer. The entry is edited into the fixture's JSON, so what
    // loads is the migration's work.
    const auto edited = EditSave(fixture, [](sim::SaveDocument& document) {
        sim::SaveDocument entry = sim::SaveDocument::object_t{};
        entry["tick"] = std::uint64_t{1000};
        entry["kind"] = "arrested";
        entry["robot"] = std::uint64_t{424242};
        entry["otherRobot"] = std::uint64_t{0};
        entry["room"] = std::uint64_t{0};
        entry["credits"] = std::int64_t{0};
        entry["memory"] = "becameFriends";
        document["world"]["chronicle"].get_array().push_back(std::move(entry));
    });
    auto story = sim::DeserializeWorld(edited);
    ASSERT_TRUE(story.has_value()) << story.error();
    ASSERT_FALSE(story->chronicle.empty());
    const sim::ChronicleEntry& last = story->chronicle.back();
    EXPECT_EQ(last.kind, sim::EventKind::Arrested);
    EXPECT_FALSE(last.robot.IsValid());
    EXPECT_EQ(last.otherRobot.value, 424242U);
}

// A version 20 save has a chronicle whose entries name no account: the
// migration adds the key as none, because the old game recorded the money's
// account nowhere the entry kept it, and the migration must not invent one.
// The fixture is checked entry by entry for the absence of the key - a
// text search would hit the emissions log, which has always named its
// account - so what loads is the migration's work and not a fixture that
// quietly grew the field.
TEST(Save, Version20FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v20-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    sim::SaveDocument document;
    ASSERT_FALSE(glz::read_json(document, fixture));
    const sim::SaveDocument& chronicle = document["world"]["chronicle"];
    ASSERT_TRUE(chronicle.is_array());
    ASSERT_FALSE(chronicle.get_array().empty());
    for (const sim::SaveDocument& entry : chronicle.get_array()) {
        EXPECT_FALSE(entry.contains("account"));
    }

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    // The story migrates rather than being replaced: the fixture's
    // chronicle already tells one, and none of its entries can know an
    // account.
    EXPECT_FALSE(state->chronicle.empty());
    for (const sim::ChronicleEntry& entry : state->chronicle) {
        EXPECT_FALSE(entry.account.IsValid());
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// The chronicle is kept in order and in check: an entry older than the one
// before it is refused, as is a save holding more than the game keeps.
// A version 21 save knows nothing about free plots: the migration computes
// them from the save's own grid - four corners per node, minus the corners
// its buildings stand on - so what loads is the migration's work and not a
// fixture that quietly grew the field.
TEST(Save, Version21FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v21-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    sim::SaveDocument document;
    ASSERT_FALSE(glz::read_json(document, fixture));
    ASSERT_TRUE(document["world"]["city"].is_object());
    EXPECT_FALSE(document["world"]["city"].contains("lots"));

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    std::vector<std::uint32_t> taken(state->city.nodes.size(), 0);
    for (const sim::Building& building : state->city.buildings) {
        ++taken.at(building.entrance.value);
    }
    // The plots are the fixture's own free corners, not an empty list.
    EXPECT_FALSE(state->city.lots.empty());
    for (const sim::CityLot& lot : state->city.lots) {
        EXPECT_LT(lot.corner, sim::kCornersPerNode);
        EXPECT_GE(lot.corner, taken.at(lot.node.value));
    }
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// A version 22 save knows nothing about buildings under construction: the
// migration opens the empty list, and the current game starts its first
// project when homes or jobs run short (step D2b), so what loads is the
// migration's work and not a fixture that quietly grew the field.
TEST(Save, Version22FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v22-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();

    sim::SaveDocument document;
    ASSERT_FALSE(glz::read_json(document, fixture));
    EXPECT_FALSE(document["world"].contains("projects"));

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    EXPECT_TRUE(state->projects.empty());
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// Version 24 gives rooms a new kind, the site office of a building going
// up (task D, step D3a). A version 23 game never wrote one, so the fixture
// text is checked for the absence of that kind: what loads is the
// migration's work - which for this version is the version number alone -
// and not a fixture that quietly grew the new kind.
TEST(Save, Version23FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v23-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    EXPECT_EQ(fixture.find("\"siteOffice\""), std::string::npos)
        << path.string() << " already names the new kind";

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));
}

// Version 25 gives a project the credits its site has paid for its supplies
// and the shortage that started it (task D, step D5b2). The version 24 fixture
// names neither key, so what loads is the migration's work; and a version 24
// save that does have a project names only its four old keys, which the
// migration fills with cost 0 and a reason it cannot know.
TEST(Save, Version24FixtureMigratesAndRuns) {
    const std::filesystem::path path =
        kFixtureDirectory / "saves" / "v24-city.json";
    const std::string fixture = ReadText(path);
    ASSERT_FALSE(fixture.empty()) << "missing " << path.string();
    // The fixture names neither new key: `"reason"` alone would prove nothing,
    // an emission of any version has one, so the test looks for the cost key
    // and for the reason name only version 25 writes into a project.
    EXPECT_EQ(fixture.find("\"costSoFar\""), std::string::npos)
        << path.string() << " already names the new cost";
    EXPECT_EQ(fixture.find("\"unknown\""), std::string::npos)
        << path.string() << " already names the new reason";

    auto state = sim::DeserializeWorld(fixture);
    ASSERT_TRUE(state.has_value()) << state.error();
    auto world = sim::World::FromState(std::move(*state));
    ASSERT_TRUE(world.has_value()) << world.error();
    world->RunFor(sim::kMinutesPerDay);
    EXPECT_TRUE(sim::ValidateWorldState(world->State()));

    // The same fixture with one project written in as a version 24 game
    // would have written it: the loaded project has the two keys the
    // migration added, and reads as a site that has paid nothing yet for
    // reasons the save never told.
    const auto migrated = sim::DeserializeWorld(
        EditSave(fixture, [](sim::SaveDocument& document) {
            const auto& buildings = document["world"]["city"]["buildings"].get_array();
            const auto& organizations = document["world"]["economy"]["organizations"].get_array();
            sim::SaveDocument project = sim::SaveDocument::object_t{};
            project["building"] = buildings[0]["id"].as<std::uint64_t>();
            project["owner"] = organizations[0]["id"].as<std::uint64_t>();
            project["daysLeft"] = std::uint64_t{1};
            project["floorsDone"] = std::uint64_t{1};
            document["world"]["projects"].get_array().push_back(std::move(project));
        }));
    ASSERT_TRUE(migrated.has_value()) << migrated.error();
    ASSERT_EQ(migrated->projects.size(), 1U);
    EXPECT_EQ(migrated->projects.front().costSoFar, 0);
    EXPECT_EQ(migrated->projects.front().reason, sim::ProjectReason::Unknown);
}

