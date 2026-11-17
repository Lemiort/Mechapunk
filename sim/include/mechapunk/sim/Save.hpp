#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <glaze/json/generic.hpp>

#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

// Save files are JSON:
//
//   { "format": "mechapunk-save", "version": 1, "gameVersion": "0.1.0",
//     "world": { ...WorldState... } }
//
// Field names, enum names and nesting of WorldState are part of the format.
// Any change to them needs a new kCurrentSaveVersion, a migration from the
// previous version, and a new fixture under tests/fixtures/saves.
inline constexpr std::string_view kSaveFormatName = "mechapunk-save";
inline constexpr std::uint32_t kCurrentSaveVersion = 8;
inline constexpr std::string_view kGameVersion = "0.8.0";

// Untyped JSON tree of the "world" object, which migrations edit. Keys are
// sorted and 64-bit integers are kept exactly.
using SaveDocument = glz::generic_sorted_u64;

// Upgrades the world document of a save from `fromVersion` to
// `fromVersion + 1`. Migrations work on untyped JSON so they never depend on
// the current C++ types, which may have changed many versions later.
struct Migration {
    std::uint32_t fromVersion{};
    std::string_view summary;
    std::expected<void, std::string> (*apply)(SaveDocument& world){};
};

// Migrations shipped with the game, ordered by fromVersion.
[[nodiscard]] std::span<const Migration> SaveMigrations() noexcept;

// Applies `migrations` in order to bring `world` from `fromVersion` to
// `toVersion`. Fails when a step in the chain is missing or a migration
// reports an error.
[[nodiscard]] std::expected<void, std::string> MigrateSave(
    SaveDocument& world, std::uint32_t fromVersion, std::uint32_t toVersion,
    std::span<const Migration> migrations);

[[nodiscard]] std::expected<std::string, std::string> SerializeWorld(
    const WorldState& state);
// Parses, migrates to the current version, and reads the world state. Does
// not validate it; World::FromState does.
[[nodiscard]] std::expected<WorldState, std::string> DeserializeWorld(
    std::string_view json);

// Writes to a temporary file next to `path`, then renames it over `path`, so
// an interrupted save never leaves a truncated file behind.
[[nodiscard]] std::expected<void, std::string> SaveWorldToFile(
    const WorldState& state, const std::filesystem::path& path);
[[nodiscard]] std::expected<WorldState, std::string> LoadWorldFromFile(
    const std::filesystem::path& path);

}  // namespace mechapunk::sim
