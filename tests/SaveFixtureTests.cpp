// The committed fixtures of save versions 7 up to the one before the current:
// each one loads through the migrations and goes on living, which is how
// players of an old save know nothing broke. The fixtures before version 7
// are in SaveFixtureTestsEarly.cpp, and the current version's fixture and
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

