#include "mechapunk/sim/Save.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <glaze/glaze.hpp>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Social.hpp"
#include "Migrations.hpp"
#include "mechapunk/sim/World.hpp"

// Ids are written as plain numbers rather than {"value": n}.
template <>
struct glz::meta<mechapunk::sim::EntityId> {
    static constexpr auto value = &mechapunk::sim::EntityId::value;
};
template <>
struct glz::meta<mechapunk::sim::NodeId> {
    static constexpr auto value = &mechapunk::sim::NodeId::value;
};

// Enum names are spelled out so renaming a C++ enumerator cannot silently
// change the save format.
template <>
struct glz::meta<mechapunk::sim::BuildingKind> {
    using enum mechapunk::sim::BuildingKind;
    static constexpr auto value =
        enumerate("habitat", Habitat, "workplace", Workplace);
};
template <>
struct glz::meta<mechapunk::sim::RoomKind> {
    using enum mechapunk::sim::RoomKind;
    static constexpr auto value = enumerate(
        "apartment", Apartment, "workspace", Workspace, "bar", Bar, "diner",
        Diner, "chargingBay", ChargingBay, "repairShop", RepairShop, "arcade",
        Arcade, "shop", Shop, "clinic", Clinic, "blackMarket", BlackMarket, "vacantStore",
        VacantStore, "policeStation", PoliceStation, "crewRoom", CrewRoom);
};
template <>
struct glz::meta<mechapunk::sim::ApartmentClass> {
    using enum mechapunk::sim::ApartmentClass;
    static constexpr auto value =
        enumerate("none", None, "dorm", Dorm, "studio", Studio, "oneBedroom", OneBedroom,
                  "twoBedroom", TwoBedroom, "penthouse", Penthouse);
};
template <>
struct glz::meta<mechapunk::sim::RobotActivity> {
    using enum mechapunk::sim::RobotActivity;
    static constexpr auto value =
        enumerate("resting", Resting, "walking", Walking, "working", Working,
                  "visiting", Visiting);
};

template <>
struct glz::meta<mechapunk::sim::BondLevel> {
    using enum mechapunk::sim::BondLevel;
    static constexpr auto value =
        enumerate("acquaintance", Acquaintance, "friend", Friend,
                  "closeFriend", CloseFriend, "rival", Rival);
};
template <>
struct glz::meta<mechapunk::sim::Stance> {
    using enum mechapunk::sim::Stance;
    static constexpr auto value =
        enumerate("friendly", Friendly, "neutral", Neutral, "rival", Rival);
};
template <>
struct glz::meta<mechapunk::sim::MemoryKind> {
    using enum mechapunk::sim::MemoryKind;
    static constexpr auto value = enumerate(
        "becameFriends", BecameFriends, "becameCloseFriends",
        BecameCloseFriends, "driftedApart", DriftedApart, "becameRivals",
        BecameRivals, "madePeace", MadePeace, "fellBehindOnRent",
        FellBehindOnRent, "paidOffDebt", PaidOffDebt, "promoted", Promoted,
        "lostJob", LostJob, "foundJob", FoundJob, "movedUp", MovedUp,
        "evicted", Evicted, "lostFriend", LostFriend, "arrivedInCity",
        ArrivedInCity, "gotBadPart", GotBadPart, "boughtProperty",
        BoughtProperty, "soldHome", SoldHome, "tookLoan", TookLoan,
        "inherited", Inherited, "movedInTogether", MovedInTogether,
        "joinedGang", JoinedGang, "leftGang", LeftGang, "wasRobbed",
        WasRobbed, "robbedSomeone", RobbedSomeone, "arrested", Arrested,
        "arrestedSomeone", ArrestedSomeone);
};
template <>
struct glz::meta<mechapunk::sim::OrganizationKind> {
    using enum mechapunk::sim::OrganizationKind;
    static constexpr auto value = enumerate("corporation", Corporation, "bank", Bank,
                                            "centralBank", CentralBank, "government",
                                            Government);
};
template <>
struct glz::meta<mechapunk::sim::Sector> {
    using enum mechapunk::sim::Sector;
    static constexpr auto value =
        enumerate("industry", Industry, "venues", Venues, "housing", Housing, "utilities",
                  Utilities, "transit", Transit, "banking", Banking, "government",
                  Government);
};
template <>
struct glz::meta<mechapunk::sim::EmissionReason> {
    using enum mechapunk::sim::EmissionReason;
    static constexpr auto value =
        enumerate("founding", Founding, "newcomerSavings", NewcomerSavings, "bankRescue",
                  BankRescue, "repaid", Repaid, "interest", Interest);
};

template <>
struct glz::meta<mechapunk::sim::EventKind> {
    using enum mechapunk::sim::EventKind;
    static constexpr auto value = enumerate(
        "departed", Departed, "clockedIn", ClockedIn, "shiftEnded", ShiftEnded,
        "arrivedAtVenue", ArrivedAtVenue, "arrivedHome", ArrivedHome, "rentPaid",
        RentPaid, "rentMissed", RentMissed, "talked", Talked, "remembered",
        Remembered, "scrapped", Scrapped, "arrived", Arrived, "weatherChanged",
        WeatherChanged, "borrowed", Borrowed, "boughtProperty", BoughtProperty,
        "soldProperty", SoldProperty, "inherited", Inherited, "moneySupplyChanged",
        MoneySupplyChanged, "unitOpened", UnitOpened, "unitClosed", UnitClosed,
        "robbed", Robbed, "arrested", Arrested, "fought", Fought, "joinedGang",
        JoinedGang, "leftGang", LeftGang);
};

namespace mechapunk::sim {

namespace {
constexpr glz::opts kWriteOptions{.prettify = true};
// A key missing from a save means a migration was forgotten; fail loudly
// instead of silently filling in a default.
constexpr glz::opts kReadOptions{.error_on_missing_keys = true};

constexpr std::array kMigrations{
    Migration{.fromVersion = 1,
              .summary = "rooms, needs and credits",
              .apply = &migrations::V1ToV2},
    Migration{.fromVersion = 2,
              .summary = "rent and days off",
              .apply = &migrations::V2ToV3},
    Migration{.fromVersion = 3,
              .summary = "interests, relationships and memories",
              .apply = &migrations::V3ToV4},
    Migration{.fromVersion = 4,
              .summary = "job levels and performance",
              .apply = &migrations::V4ToV5},
    Migration{.fromVersion = 5,
              .summary = "age, wear and scrapped robots",
              .apply = &migrations::V5ToV6},
    Migration{.fromVersion = 6,
              .summary = "metro stations and rides",
              .apply = &migrations::V6ToV7},
    Migration{.fromVersion = 7,
              .summary = "weather",
              .apply = &migrations::V7ToV8},
    Migration{.fromVersion = 8,
              .summary = "metro timetable and changing lines",
              .apply = &migrations::V8ToV9},
    Migration{.fromVersion = 9,
              .summary = "a closed economy with owners, banks and statistics",
              .apply = &migrations::V9ToV10},
    Migration{.fromVersion = 10,
              .summary = "daily budgets",
              .apply = &migrations::V10ToV11},
    Migration{.fromVersion = 11,
              .summary = "apartment classes and the city's housing office",
              .apply = &migrations::V11ToV12},
    Migration{.fromVersion = 12,
              .summary = "venue prices and market rents",
              .apply = &migrations::V12ToV13},
    Migration{.fromVersion = 13,
              .summary = "venues and workspaces opening and closing",
              .apply = &migrations::V13ToV14},
    Migration{.fromVersion = 14,
              .summary = "City Hall: taxes, support and the city's metro",
              .apply = &migrations::V14ToV15},
    Migration{.fromVersion = 15,
              .summary = "the central bank's rate",
              .apply = &migrations::V15ToV16},
    Migration{.fromVersion = 16,
              .summary = "police and metro staff",
              .apply = &migrations::V16ToV17},
    Migration{.fromVersion = 17,
              .summary = "gangs and crime statistics",
              .apply = &migrations::V17ToV18},
    Migration{.fromVersion = 18,
              .summary = "the chronicle",
              .apply = &migrations::V18ToV19},
    Migration{.fromVersion = 19,
              .summary = "arrests name the arresting officer",
              .apply = &migrations::V19ToV20},
    Migration{.fromVersion = 20,
              .summary = "chronicle entries name the account their money moved for",
              .apply = &migrations::V20ToV21},
    Migration{.fromVersion = 21,
              .summary = "the city's free building plots",
              .apply = &migrations::V21ToV22},
    Migration{.fromVersion = 22,
              .summary = "buildings under construction",
              .apply = &migrations::V22ToV23},
};
}  // namespace

std::span<const Migration> SaveMigrations() noexcept { return kMigrations; }

std::expected<void, std::string> MigrateSave(
    SaveDocument& world, std::uint32_t fromVersion, std::uint32_t toVersion,
    std::span<const Migration> migrations) {
    if (fromVersion > toVersion) {
        return std::unexpected(std::format(
            "Save version {} is newer than this game supports ({})",
            fromVersion, toVersion));
    }
    for (std::uint32_t version = fromVersion; version < toVersion; ++version) {
        const Migration* step = nullptr;
        for (const Migration& migration : migrations) {
            if (migration.fromVersion == version) {
                step = &migration;
                break;
            }
        }
        if (step == nullptr || step->apply == nullptr) {
            return std::unexpected(std::format(
                "No migration from save version {} to {}", version,
                version + 1));
        }
        if (auto applied = step->apply(world); !applied) {
            return std::unexpected(std::format(
                "Migration from save version {} ({}) failed: {}", version,
                step->summary, applied.error()));
        }
    }
    return {};
}

std::expected<std::string, std::string> SerializeWorld(
    const WorldState& state) {
    std::string buffer;
    const auto error = glz::write<kWriteOptions>(
        glz::obj{"format", kSaveFormatName, "version", kCurrentSaveVersion,
                 "gameVersion", kGameVersion, "world", state},
        buffer);
    if (error) {
        return std::unexpected(std::format("Cannot serialize the world: {}",
                                           glz::format_error(error)));
    }
    buffer.push_back('\n');
    return buffer;
}

std::expected<WorldState, std::string> DeserializeWorld(std::string_view json) {
    SaveDocument document;
    if (const auto error = glz::read_json(document, json)) {
        return std::unexpected(std::format("Save is not valid JSON: {}",
                                           glz::format_error(error, json)));
    }
    if (!document.is_object()) {
        return std::unexpected("Save is not a JSON object");
    }
    auto& root = document.get_object();

    const auto format = root.find("format");
    if (format == root.end() || !format->second.is_string() ||
        format->second.get_string() != kSaveFormatName) {
        return std::unexpected("Not a Mechapunk save file");
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->second.is_uint64()) {
        return std::unexpected("Save has no valid version number");
    }
    const std::uint64_t saveVersion = version->second.get<std::uint64_t>();
    if (saveVersion == 0 || saveVersion > kCurrentSaveVersion) {
        return std::unexpected(std::format(
            "Save version {} is not supported; this game reads versions 1 "
            "to {}",
            saveVersion, kCurrentSaveVersion));
    }
    const auto worldEntry = root.find("world");
    if (worldEntry == root.end() || !worldEntry->second.is_object()) {
        return std::unexpected("Save has no world");
    }

    SaveDocument world = std::move(worldEntry->second);
    if (auto migrated =
            MigrateSave(world, static_cast<std::uint32_t>(saveVersion),
                        kCurrentSaveVersion, SaveMigrations());
        !migrated) {
        return std::unexpected(std::move(migrated.error()));
    }

    // Typed reading goes through text so it uses the same strict options as
    // reading a file directly.
    const auto worldJson = glz::write_json(world);
    if (!worldJson) {
        return std::unexpected(std::format("Cannot re-encode the world: {}",
                                           glz::format_error(worldJson.error())));
    }
    WorldState state;
    if (const auto error = glz::read<kReadOptions>(state, *worldJson)) {
        return std::unexpected(std::format(
            "Save world does not match version {}: {}", kCurrentSaveVersion,
            glz::format_error(error, *worldJson)));
    }
    return state;
}

std::expected<void, std::string> SaveWorldToFile(
    const WorldState& state, const std::filesystem::path& path) {
    auto json = SerializeWorld(state);
    if (!json) {
        return std::unexpected(std::move(json.error()));
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(json->data(), static_cast<std::streamsize>(json->size()));
        file.close();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return std::unexpected(
                std::format("Cannot write '{}'", temporary.string()));
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected(std::format("Cannot replace '{}': {}",
                                           path.string(),
                                           renameError.message()));
    }
    return {};
}

std::expected<WorldState, std::string> LoadWorldFromFile(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Cannot open '{}'", path.string()));
    }
    const std::string json{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return std::unexpected(std::format("Cannot read '{}'", path.string()));
    }
    return DeserializeWorld(json);
}

}  // namespace mechapunk::sim
