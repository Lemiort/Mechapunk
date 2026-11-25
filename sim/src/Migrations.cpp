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

std::expected<void, std::string> V9ToV10(SaveDocument& world) try {
    // Frozen copies of the version 10 names and starting rules.
    constexpr std::array<std::string_view, 3> kBanks{
        "First Chrome Bank", "Ironside Savings", "Neon Credit Union"};
    constexpr std::array<std::string_view, 12> kIndustry{
        "Kessler", "Omni", "Vortek", "Hexa", "Ironclad", "Synthex",
        "Chrome", "Atlas", "Raijin", "Brassworks", "Quantum", "Meridian"};
    constexpr std::array<std::string_view, 8> kIndustryKinds{
        "Dynamics", "Forge", "Works", "Systems", "Industries", "Fabrication",
        "Logistics", "Robotics"};
    constexpr std::array<std::string_view, 6> kChains{"Neon", "Volt", "Rust",
                                                      "Static", "Glow", "Byte"};
    constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kVenues{{
        {"bar", "Taps"}, {"diner", "Noodle Co."}, {"chargingBay", "Charge"},
        {"repairShop", "Repairs"}, {"arcade", "Arcades"}, {"shop", "Goods"},
        {"clinic", "Clinics"}, {"blackMarket", "Syndicate"}}};
    constexpr std::array<std::string_view, 7> kBookKeys{
        "revenue", "wages", "supplies", "upkeep", "rent", "dividends", "repaid"};
    constexpr std::array<std::string_view, 31> kCounterKeys{
        "walks", "metroRides", "visits", "lateArrivals", "talks", "arguments",
        "wages", "venueSales", "rent", "upkeep", "fares", "supplies", "dividends",
        "interest", "created", "destroyed", "loansTaken", "lent", "writtenOff",
        "propertySales", "propertySalesValue", "promotions", "jobsLost", "jobsFound",
        "movedUp", "evictions", "newFriendships", "newRivalries", "arrivals",
        "scrapped", "badParts"};
    const auto zeros = [](const auto& keys) {
        SaveDocument object = Object{};
        for (const std::string_view key : keys) {
            object[std::string{key}] = std::int64_t{0};
        }
        return object;
    };

    std::uint64_t next = world["ids"]["next"].as<std::uint64_t>();
    const std::uint64_t tick = world["tick"].as<std::uint64_t>();
    Array& robots = world["robots"].get_array();
    Array& buildings = world["city"]["buildings"].get_array();
    const auto population = static_cast<std::int64_t>(robots.size());

    Array organizations;
    const auto found = [&](std::string name, std::string_view kind, std::string_view sector,
                           std::int64_t credits) -> std::uint64_t {
        SaveDocument organization = Object{};
        const std::uint64_t id = next++;
        organization["id"] = id;
        organization["name"] = std::move(name);
        organization["kind"] = std::string{kind};
        organization["sector"] = std::string{sector};
        organization["credits"] = credits;
        organization["owners"] = Array{};
        organization["bank"] = std::uint64_t{0};
        organization["payPercent"] = std::uint64_t{100};
        organization["today"] = zeros(kBookKeys);
        organization["yesterday"] = zeros(kBookKeys);
        organizations.push_back(std::move(organization));
        return id;
    };
    const std::uint64_t central = found("Central Reserve", "centralBank", "banking", 0);
    std::vector<std::uint64_t> banks;
    for (const std::string_view name : kBanks) {
        banks.push_back(found(std::string{name}, "bank", "banking", 1000 + (population * 4)));
    }
    const std::uint64_t trust = found("Habitat Trust", "corporation", "housing", 1000 + (population / 2));
    found("Gridline Power & Water", "corporation", "utilities", 1000 + (population / 2));
    found("Metro Authority", "corporation", "transit", 1000 + (population / 2));

    std::vector<SaveDocument*> workplaces;
    for (SaveDocument& building : buildings) {
        if (building["kind"].get_string() == "workplace") {
            workplaces.push_back(&building);
        }
    }
    const std::size_t corporations =
        std::clamp<std::size_t>(workplaces.size() / 4, 1, kIndustry.size() * 2);
    const std::size_t firstIndustry = organizations.size();
    for (std::size_t index = 0; index < corporations; ++index) {
        found(std::format("{} {}", kIndustry.at(index % kIndustry.size()),
                          kIndustryKinds.at((index + (index / kIndustry.size())) % kIndustryKinds.size())),
              "corporation", "industry", 0);
    }
    for (std::size_t index = 0; index < workplaces.size(); ++index) {
        SaveDocument& owner = organizations[firstIndustry + (index % corporations)];
        SaveDocument& building = *workplaces[index];
        const auto wage = building["shift"]["wage"].as<std::int64_t>();
        for (SaveDocument& room : building["rooms"].get_array()) {
            room["owner"] = owner["id"].as<std::uint64_t>();
            owner["credits"] = owner["credits"].as<std::int64_t>() +
                               (3 * room["capacity"].as<std::int64_t>() * wage);
        }
    }
    std::size_t chain = 0;
    for (const auto& [kind, suffix] : kVenues) {
        std::vector<SaveDocument*> rooms;
        for (SaveDocument& building : buildings) {
            for (SaveDocument& room : building["rooms"].get_array()) {
                if (room["kind"].get_string() == kind) {
                    rooms.push_back(&room);
                }
            }
        }
        if (rooms.empty()) {
            continue;
        }
        const std::size_t chains = std::clamp<std::size_t>(rooms.size() / 12, 1, 3);
        const std::size_t first = organizations.size();
        for (std::size_t index = 0; index < chains; ++index) {
            found(std::format("{} {}", kChains.at(chain++ % kChains.size()), suffix), "corporation",
                  "venues", 0);
        }
        for (std::size_t index = 0; index < rooms.size(); ++index) {
            SaveDocument& owner = organizations[first + (index % chains)];
            (*rooms[index])["owner"] = owner["id"].as<std::uint64_t>();
            owner["credits"] = owner["credits"].as<std::int64_t>() + 100;
        }
    }
    // Robots own the apartments they live in; the trust owns the rest.
    std::map<std::uint64_t, std::uint64_t> resident;
    std::int64_t savings = 0;
    for (std::size_t index = 0; index < robots.size(); ++index) {
        SaveDocument& robot = robots[index];
        resident.try_emplace(robot["apartment"].as<std::uint64_t>(), robot["id"].as<std::uint64_t>());
        robot["bank"] = banks.at(index % banks.size());
        robot["savings"] = std::int64_t{0};
        savings += robot["credits"].as<std::int64_t>();
    }
    for (SaveDocument& building : buildings) {
        for (SaveDocument& room : building["rooms"].get_array()) {
            if (room["kind"].get_string() == "apartment") {
                const auto found = resident.find(room["id"].as<std::uint64_t>());
                room["owner"] = found != resident.end() ? found->second : trust;
            }
        }
    }
    // Banks and owners; every organization but the central bank has one
    // owner, a robot picked across the population.
    Array emissions;
    std::int64_t created = savings;
    for (std::size_t index = 0; index < organizations.size(); ++index) {
        SaveDocument& organization = organizations[index];
        const std::string kind = organization["kind"].get_string();
        if (kind == "corporation") {
            organization["bank"] = banks.at(index % banks.size());
        } else if (kind == "bank") {
            organization["bank"] = central;
        }
        if (kind != "centralBank" && !robots.empty()) {
            SaveDocument share = Object{};
            share["owner"] = robots[(index * 7919) % robots.size()]["id"].as<std::uint64_t>();
            share["shares"] = std::uint64_t{50};
            organization["owners"].get_array().push_back(std::move(share));
        }
        const auto credits = organization["credits"].as<std::int64_t>();
        created += credits;
        if (credits > 0) {
            SaveDocument emission = Object{};
            emission["tick"] = tick;
            emission["account"] = organization["id"].as<std::uint64_t>();
            emission["amount"] = credits;
            emission["reason"] = "founding";
            emissions.push_back(std::move(emission));
        }
    }
    SaveDocument robotsSavings = Object{};
    robotsSavings["tick"] = tick;
    robotsSavings["account"] = std::uint64_t{0};
    robotsSavings["amount"] = savings;
    robotsSavings["reason"] = "founding";
    emissions.push_back(std::move(robotsSavings));

    SaveDocument economy = Object{};
    economy["organizations"] = std::move(organizations);
    economy["loans"] = Array{};
    economy["created"] = created;
    economy["destroyed"] = std::int64_t{0};
    economy["emissions"] = std::move(emissions);
    economy["supplyMarket"] = std::int64_t{0};
    world["economy"] = std::move(economy);
    world["today"] = zeros(kCounterKeys);
    world["history"] = Array{};
    world["ids"]["next"] = next;
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 9 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V10ToV11(SaveDocument& world) try {
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["spentToday"] = std::int64_t{0};
    }
    world["economy"]["supplyPercent"] = std::int64_t{60};
    for (SaveDocument& day : world["history"].get_array()) {
        day["supplyPercent"] = std::int64_t{60};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 10 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V11ToV12(SaveDocument& world) try {
    constexpr std::array<std::string_view, 7> kBookKeys{
        "revenue", "wages", "supplies", "upkeep", "rent", "dividends", "repaid"};
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        for (SaveDocument& room : building["rooms"].get_array()) {
            room["apartmentClass"] =
                room["kind"].get_string() == "apartment" ? "studio" : "none";
        }
    }
    // The city's housing office, with nothing yet: older cities have no
    // dorms.
    SaveDocument& economy = world["economy"];
    std::uint64_t bank = 0;
    for (SaveDocument& organization : economy["organizations"].get_array()) {
        if (bank == 0 && organization["kind"].get_string() == "bank") {
            bank = organization["id"].as<std::uint64_t>();
        }
    }
    SaveDocument office = Object{};
    const std::uint64_t id = world["ids"]["next"].as<std::uint64_t>();
    world["ids"]["next"] = id + 1;
    office["id"] = id;
    office["name"] = "City Housing Office";
    office["kind"] = "government";
    office["sector"] = "government";
    office["credits"] = std::int64_t{0};
    office["owners"] = Array{};
    office["bank"] = bank;
    office["payPercent"] = std::uint64_t{100};
    for (const char* books : {"today", "yesterday"}) {
        SaveDocument zero = Object{};
        for (const std::string_view key : kBookKeys) {
            zero[std::string{key}] = std::int64_t{0};
        }
        office[books] = std::move(zero);
    }
    economy["organizations"].get_array().push_back(std::move(office));
    world["today"]["movedInTogether"] = std::int64_t{0};
    for (SaveDocument& day : world["history"].get_array()) {
        day["counted"]["movedInTogether"] = std::int64_t{0};
        day["governmentMoney"] = std::int64_t{0};
        day["residentsByClass"] = Array{std::int64_t{0}, std::int64_t{0}, std::int64_t{0},
                                        std::int64_t{0}, std::int64_t{0}};
        day["sharing"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 11 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V12ToV13(SaveDocument& world) try {
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        for (SaveDocument& room : building["rooms"].get_array()) {
            room["pricePercent"] = std::uint64_t{100};
            room["visits"] = std::uint64_t{0};
        }
    }
    world["economy"]["rentPercent"] =
        Array{std::int64_t{100}, std::int64_t{100}, std::int64_t{100}, std::int64_t{100}};
    for (SaveDocument& day : world["history"].get_array()) {
        day["averageRent"] = Array{std::int64_t{0}, std::int64_t{0}, std::int64_t{0},
                                   std::int64_t{0}, std::int64_t{0}};
        day["venuePricePercent"] = std::int64_t{100};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 12 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V13ToV14(SaveDocument& world) try {
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        for (SaveDocument& room : building["rooms"].get_array()) {
            room["quietNights"] = std::uint64_t{0};
        }
    }
    world["today"]["venuesOpened"] = std::int64_t{0};
    world["today"]["venuesClosed"] = std::int64_t{0};
    for (SaveDocument& day : world["history"].get_array()) {
        day["counted"]["venuesOpened"] = std::int64_t{0};
        day["counted"]["venuesClosed"] = std::int64_t{0};
        day["openVenues"] = std::int64_t{0};
        day["emptyUnits"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 13 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V14ToV15(SaveDocument& world) try {
    constexpr std::array<std::string_view, 5> kCounters{
        "wageTax", "propertyTax", "profitTax", "benefits", "publicSpending"};
    SaveDocument& economy = world["economy"];
    std::uint64_t city = 0;
    for (SaveDocument& organization : economy["organizations"].get_array()) {
        if (organization["kind"].get_string() == "government") {
            organization["name"] = "City Hall";
            city = organization["id"].as<std::uint64_t>();
        }
        organization["today"]["taxes"] = std::int64_t{0};
        organization["yesterday"]["taxes"] = std::int64_t{0};
    }
    for (SaveDocument& organization : economy["organizations"].get_array()) {
        if (organization["sector"].get_string() == "transit" && city != 0) {
            SaveDocument share = Object{};
            share["owner"] = city;
            share["shares"] = std::uint64_t{100};
            organization["owners"] = Array{std::move(share)};
        }
    }
    for (const std::string_view key : kCounters) {
        world["today"][std::string{key}] = std::int64_t{0};
    }
    for (SaveDocument& day : world["history"].get_array()) {
        for (const std::string_view key : kCounters) {
            day["counted"][std::string{key}] = std::int64_t{0};
        }
        day["governmentDebt"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 14 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V15ToV16(SaveDocument& world) try {
    world["economy"]["policyRate"] = std::int64_t{10};
    world["today"]["savingsInterest"] = std::int64_t{0};
    for (SaveDocument& day : world["history"].get_array()) {
        day["counted"]["savingsInterest"] = std::int64_t{0};
        day["policyRate"] = std::int64_t{10};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 15 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V16ToV17(SaveDocument& world) try {
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["patrol"] = std::uint64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 16 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V17ToV18(SaveDocument& world) try {
    // No gangs in a version 17 world: `ReviewGangs` founds them at the
    // next midnight, from this save's own seed (see World::ReviewGangs).
    if (!world.contains("gangs")) {
        world["gangs"] = SaveDocument::array_t{};
    }
    // Robots start with no gang and zero arrests.
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["gang"] = std::uint64_t{0};
        robot["arrests"] = std::uint32_t{0};
    }
    // Counters in today and every history element.
    for (std::string_view counter : {"robberies", "robbedCredits", "arrests",
                                     "fights", "joinedGangs", "leftGangs"}) {
        world["today"][counter] = std::int64_t{0};
        for (SaveDocument& day : world["history"].get_array()) {
            day["counted"][counter] = std::int64_t{0};
        }
    }
    // Gang members in every history element.
    for (SaveDocument& day : world["history"].get_array()) {
        day["gangMembers"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 17 world is malformed ({})", error.what()));
}

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

}  // namespace mechapunk::sim::migrations
