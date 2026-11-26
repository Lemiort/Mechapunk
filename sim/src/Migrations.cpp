#include "Migrations.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mechapunk/sim/Save.hpp"

#include "MigrationsInternal.hpp"

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

std::expected<void, std::string> V19ToV20(SaveDocument& world) try {
    // Version 20 makes arrests name the officer that made them (task B,
    // step B15b), and turns around what an `arrested` chronicle entry
    // stores: version 19 wrote the criminal into `robot` and knew no
    // officer, because neither arrest path worked out which officer it
    // was. The criminal moves to `otherRobot` and `robot` becomes none,
    // which the game reads as "an officer arrested him": the migration
    // must not invent an officer the old game never recorded. No other
    // key changes, and the memory kind `arrestedSomeone`, which only
    // version 20 writes, can never be missing from a version 19 file.
    for (SaveDocument& entry : world["chronicle"].get_array()) {
        if (entry["kind"].is_string() &&
            entry["kind"].get_string() == "arrested") {
            entry["otherRobot"] = entry["robot"];
            entry["robot"] = std::uint64_t{0};
        }
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 19 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V20ToV21(SaveDocument& world) try {
    // Version 21 gives every chronicle entry the account its event moved
    // money for: the account a `MoneySupplyChanged` event created money
    // in or destroyed it from, and the owner of a unit that opened or
    // closed. A version 20 entry recorded no account anywhere a migration
    // could read it back - the emission log is capped and matching an
    // entry to it by tick would invent facts the old game never wrote -
    // so every entry gains none, and the story's money names its account
    // only from the minute the current game runs the save.
    for (SaveDocument& entry : world["chronicle"].get_array()) {
        entry["account"] = std::uint64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 20 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V21ToV22(SaveDocument& world) try {
    // Version 22 saves the city's free building plots: out of four corners
    // per intersection, those no building of that intersection stands on.
    // A version 21 city names its nodes and every building's entrance, so
    // the plots are computed from them here instead of leaving the land
    // construction builds on empty. The view puts a node's buildings on its
    // first corners and buildings are saved sorted by id, so the free
    // corners of a node are the slots after the buildings at it.
    constexpr std::uint32_t kCorners = 4;
    const std::size_t nodeCount = world["city"]["nodes"].get_array().size();
    std::vector<std::uint32_t> taken(nodeCount, 0);
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        if (building["entrance"].is_number()) {
            const std::uint64_t node = building["entrance"].get<std::uint64_t>();
            if (node < nodeCount) {
                ++taken.at(static_cast<std::size_t>(node));
            }
        }
    }
    Array lots;
    for (std::size_t node = 0; node < nodeCount; ++node) {
        for (std::uint32_t corner = taken.at(node); corner < kCorners; ++corner) {
            SaveDocument lot = SaveDocument::object_t{};
            lot["node"] = std::uint64_t{node};
            lot["corner"] = std::uint64_t{corner};
            lots.push_back(std::move(lot));
        }
    }
    world["city"]["lots"] = std::move(lots);
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 21 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V22ToV23(SaveDocument& world) try {
    // Version 23 saves the projects: the buildings going up. A version 22
    // game never raised one, and its story of construction begins when the
    // current game runs the save: the nightly review starts projects,
    // this migration only opens the list.
    world["projects"] = SaveDocument::array_t{};
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 22 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V23ToV24(SaveDocument& world) try {
    // Version 24 gives rooms a new kind: the site office of a building
    // going up (task D, step D3a). Nothing to change: a version 23 game
    // never wrote a site office, so no room of an old save can name the
    // kind, and no key was added. The migration is the version number alone,
    // and the bump is there because a version 24 save may name a kind a
    // version 23 game cannot read.
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 23 world is malformed ({})", error.what()));
}

}  // namespace mechapunk::sim::migrations
