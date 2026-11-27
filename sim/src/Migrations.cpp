// The save migrations still in play: new ones are appended here. Once a
// version is released its migration is frozen; when the saves module needs
// room, frozen migrations move unchanged to MigrationsMid.cpp (versions
// 19 on) or MigrationsEarly.cpp (versions 1 to 18).

#include "Migrations.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

std::expected<void, std::string> V18ToV19(SaveDocument& world) try {
    // The chronicle: a list of notable events. A version 18 world has none,
    // and its story begins when the current game runs it; entries are added
    // by the recording step, not by this migration.
    world["chronicle"] = SaveDocument::array_t{};
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 18 world is malformed ({})", error.what()));
}

// Version 27 gives every building its architectural style: how the block is
// massed and roofed, which changes no room, no job, no price and no walk.
// A version 26 save already names the three facts the style is read from -
// the building's id, its kind and its floor count - and `StyleOf` is a pure
// function of exactly those, drawing no random number, so the migration
// writes the style the current game would give the building rather than
// inventing a plain box for every old city. The names are spelled out here
// because a frozen migration keeps the names of the version it was written
// for; they are the ones `glz::meta<BuildingStyle>` in Save.cpp reads.
std::expected<void, std::string> V26ToV27(SaveDocument& world) try {
    constexpr std::array<std::string_view, 8> kStyleNames{
        "plain", "setback", "stepped", "twinTowers", "plinth", "gabled",
        "sawtooth", "tower"};
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        const std::string_view kindName =
            building["kind"].is_string() ? building["kind"].get_string()
                                         : std::string_view{};
        if (kindName != "habitat" && kindName != "workplace") {
            return std::unexpected(
                "a version 26 building names no kind this game can read");
        }
        const sim::BuildingStyle style = sim::StyleOf(
            kindName == "workplace" ? sim::BuildingKind::Workplace
                                    : sim::BuildingKind::Habitat,
            sim::EntityId{building["id"].get<std::uint64_t>()},
            static_cast<std::uint32_t>(
                building["floors"].get<std::uint64_t>()));
        building["style"] =
            std::string(kStyleNames.at(static_cast<std::size_t>(style)));
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 26 world is malformed ({})", error.what()));
}

}  // namespace mechapunk::sim::migrations
