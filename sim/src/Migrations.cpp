// The save migrations still in play: new ones are appended here. Once a
// version is released its migration is frozen; when the saves module needs
// room, frozen migrations move unchanged to MigrationsMid.cpp (versions
// 19 on) or MigrationsEarly.cpp (versions 1 to 18).

#include "Migrations.hpp"

#include <exception>
#include <expected>
#include <format>
#include <string>

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

}  // namespace mechapunk::sim::migrations
