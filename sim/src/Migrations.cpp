// The save migrations this game still reads. Once a version is released its
// migration is frozen; append new ones here and register them in
// `kMigrations` in Save.cpp.

#include "Migrations.hpp"

#include <exception>
#include <expected>
#include <format>
#include <string>

#include "mechapunk/sim/Save.hpp"

namespace mechapunk::sim::migrations {

// Version 28 saves the simulation's settings with the city. A version 27
// city was made and played with the defaults of that game, so the migration
// writes those values, spelled out here because a frozen migration keeps the
// numbers of the version it was written for (and never calls game code).
std::expected<void, std::string> V27ToV28(SaveDocument& world) try {
    SaveDocument crime = SaveDocument::object_t{};
    crime["robberyPercent"] = 3;
    crime["gangOutingPercent"] = 50;
    crime["turfReachMeters"] = 40;
    crime["patrolReachMinutes"] = 10;
    crime["patrolStayMinutes"] = 15;
    crime["crimeLookbackMinutes"] = 60;
    SaveDocument construction = SaveDocument::object_t{};
    construction["streakNights"] = 7;
    construction["habitatFloors"] = 6;
    construction["workplaceFloors"] = 4;
    construction["siteBuilders"] = 6;
    construction["siteSuppliesNight"] = 200;
    construction["marketVacancyPercent"] = 25;
    construction["unemploymentPercent"] = 5;
    SaveDocument settings = SaveDocument::object_t{};
    settings["crime"] = std::move(crime);
    settings["construction"] = std::move(construction);
    world["settings"] = std::move(settings);
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 27 world is malformed ({})", error.what()));
}

}  // namespace mechapunk::sim::migrations
