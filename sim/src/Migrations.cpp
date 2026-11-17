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

namespace mechapunk::sim::migrations {

namespace {
using Array = SaveDocument::array_t;
using Object = SaveDocument::object_t;

struct V2Store {
    std::string_view kind;
    std::string_view name;
    std::uint64_t capacity;
};

// Every habitat of a version 1 city gets two ground-floor stores; the pairs
// rotate so a city has some of each kind.
constexpr std::array<std::array<V2Store, 2>, 3> kStorePairs{{
    {{{.kind = "bar", .name = "The Rusty Diode", .capacity = 20},
      {.kind = "chargingBay", .name = "Volt Station", .capacity = 10}}},
    {{{.kind = "diner", .name = "Neon Wok", .capacity = 14},
      {.kind = "repairShop", .name = "Fix-It Clinic", .capacity = 4}}},
    {{{.kind = "arcade", .name = "Pixel Den", .capacity = 14},
      {.kind = "shop", .name = "Parts Bazaar", .capacity = 10}}},
}};
constexpr std::uint64_t kApartmentsPerFloor = 6;
constexpr std::uint64_t kWorkspaceSize = 8;
constexpr std::uint64_t kWorkspacesPerFloor = 2;
constexpr std::uint64_t kWage = 100;
constexpr std::int64_t kStartingNeed = 800;
constexpr std::int64_t kStartingCredits = 100;

std::uint64_t CeilDiv(std::uint64_t value, std::uint64_t divisor) {
    return (value + divisor - 1) / divisor;
}

SaveDocument MakeRoom(std::uint64_t id, std::string name,
                      std::string_view kind, std::uint64_t floor,
                      std::uint64_t capacity) {
    SaveDocument room = Object{};
    room["id"] = id;
    room["name"] = std::move(name);
    room["kind"] = std::string{kind};
    room["floor"] = floor;
    room["capacity"] = capacity;
    return room;
}

struct BuildingRooms {
    std::vector<std::uint64_t> apartments;
    std::vector<std::uint64_t> workspaces;
    std::uint64_t residents{};
    std::uint64_t workers{};
};
}  // namespace

std::expected<void, std::string> V1ToV2(SaveDocument& world) try {
    std::uint64_t next = world["ids"]["next"].as<std::uint64_t>();
    const std::uint64_t tick = world["tick"].as<std::uint64_t>();
    std::map<std::uint64_t, BuildingRooms> roomsOf;

    std::uint64_t habitats = 0;
    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        const std::uint64_t id = building["id"].as<std::uint64_t>();
        const std::uint64_t capacity = building["capacity"].as<std::uint64_t>();
        const bool habitat = building["kind"].get_string() == "habitat";
        BuildingRooms& rooms = roomsOf[id];
        SaveDocument roomList = Array{};
        std::uint64_t floors = 1;
        if (habitat) {
            for (const V2Store& store :
                 kStorePairs.at(habitats++ % kStorePairs.size())) {
                roomList.get_array().push_back(MakeRoom(
                    next++, std::string{store.name}, store.kind, 0,
                    store.capacity));
            }
            for (std::uint64_t index = 0; index < capacity; ++index) {
                const std::uint64_t floor = 1 + (index / kApartmentsPerFloor);
                rooms.apartments.push_back(next);
                roomList.get_array().push_back(MakeRoom(
                    next++,
                    std::format("Apartment {}-{:02}", floor,
                                (index % kApartmentsPerFloor) + 1),
                    "apartment", floor, 1));
                floors = floor + 1;
            }
            building["shift"]["wage"] = std::uint64_t{0};
        } else {
            const std::uint64_t workspaces = CeilDiv(capacity, kWorkspaceSize);
            for (std::uint64_t index = 0; index < workspaces; ++index) {
                const std::uint64_t floor = index / kWorkspacesPerFloor;
                rooms.workspaces.push_back(next);
                roomList.get_array().push_back(MakeRoom(
                    next++, std::format("Workspace {}", index + 1),
                    "workspace", floor,
                    std::min(kWorkspaceSize,
                             capacity - (index * kWorkspaceSize))));
                floors = std::max(floors, floor + 1);
            }
            building["shift"]["wage"] = kWage;
        }
        building.get_object().erase("capacity");
        building["floors"] = floors;
        building["rooms"] = std::move(roomList);
    }

    for (SaveDocument& robot : world["robots"].get_array()) {
        BuildingRooms& home = roomsOf.at(robot["home"].as<std::uint64_t>());
        BuildingRooms& work = roomsOf.at(robot["workplace"].as<std::uint64_t>());
        if (home.residents >= home.apartments.size() ||
            work.workers / kWorkspaceSize >= work.workspaces.size()) {
            return std::unexpected("a robot has no free apartment or workspace");
        }
        const std::uint64_t apartment = home.apartments[home.residents++];
        const std::uint64_t workspace =
            work.workspaces[(work.workers++) / kWorkspaceSize];

        const std::string activity = robot["activity"].get_string();
        const std::uint64_t arrives = robot["nextActionAt"].as<std::uint64_t>();
        std::uint64_t location = apartment;
        std::uint64_t destination = 0;
        std::uint64_t departed = 0;
        std::string v2Activity = "resting";
        if (activity == "commutingToWork") {
            v2Activity = "walking";
            destination = workspace;
            departed = std::min(tick, arrives);
        } else if (activity == "working") {
            v2Activity = "working";
            location = workspace;
        } else if (activity == "commutingHome") {
            v2Activity = "walking";
            location = workspace;
            destination = apartment;
            departed = std::min(tick, arrives);
        } else if (activity != "atHome") {
            return std::unexpected(
                std::format("unknown robot activity '{}'", activity));
        }

        auto& fields = robot.get_object();
        fields.erase("home");
        fields.erase("workplace");
        robot["apartment"] = apartment;
        robot["workspace"] = workspace;
        robot["activity"] = v2Activity;
        robot["location"] = location;
        robot["destination"] = destination;
        robot["departedAt"] = departed;
        SaveDocument needs = Object{};
        needs["charge"] = kStartingNeed;
        needs["condition"] = kStartingNeed;
        needs["social"] = kStartingNeed;
        needs["fun"] = kStartingNeed;
        robot["needs"] = std::move(needs);
        robot["credits"] = kStartingCredits;
    }

    world["ids"]["next"] = next;
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 1 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V2ToV3(SaveDocument& world) try {
    // Rent of a version 2 apartment: a flat base plus a credit per floor.
    constexpr std::uint64_t kBaseRent = 30;
    constexpr std::uint64_t kDaysPerWeek = 7;

    for (SaveDocument& building : world["city"]["buildings"].get_array()) {
        for (SaveDocument& room : building["rooms"].get_array()) {
            const bool apartment = room["kind"].get_string() == "apartment";
            room["rent"] = apartment
                               ? kBaseRent + room["floor"].as<std::uint64_t>()
                               : std::uint64_t{0};
        }
    }
    for (SaveDocument& robot : world["robots"].get_array()) {
        // Two consecutive days off, spread over the week by robot id.
        const std::uint64_t first =
            robot["id"].as<std::uint64_t>() % kDaysPerWeek;
        robot["daysOff"] = (std::uint64_t{1} << first) |
                           (std::uint64_t{1} << ((first + 1) % kDaysPerWeek));
        // A robot at home was already waiting to leave for work; others plan
        // their departure when they next get home.
        robot["commuteAt"] = robot["activity"].get_string() == "resting"
                                 ? robot["nextActionAt"].as<std::uint64_t>()
                                 : std::uint64_t{0};
        robot["rentOwed"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 2 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V3ToV4(SaveDocument& world) try {
    // Two different interests out of eight, spread by robot id.
    constexpr std::uint64_t kInterests = 8;
    for (SaveDocument& robot : world["robots"].get_array()) {
        const std::uint64_t id = robot["id"].as<std::uint64_t>();
        const std::uint64_t first = id % kInterests;
        const std::uint64_t second =
            (first + 1 + ((id / kInterests) % (kInterests - 1))) % kInterests;
        robot["interests"] =
            (std::uint64_t{1} << first) | (std::uint64_t{1} << second);
        robot["bonds"] = Array{};
        robot["memories"] = Array{};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 3 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V4ToV5(SaveDocument& world) try {
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["jobLevel"] = std::uint64_t{0};
        robot["performance"] = std::int64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 4 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V5ToV6(SaveDocument& world) try {
    // Ages from 30 to 329 days spread by robot id, and the wear of a robot
    // that age: 2 a day plus a day for every 300 days of age.
    constexpr std::uint64_t kMinAge = 30;
    constexpr std::uint64_t kAgeSpread = 300;
    constexpr std::uint64_t kMaxWear = 1000;
    for (SaveDocument& robot : world["robots"].get_array()) {
        const std::uint64_t id = robot["id"].as<std::uint64_t>();
        const std::uint64_t age = kMinAge + ((id * 7919) % kAgeSpread);
        const std::uint64_t wear =
            std::min(kMaxWear, (2 * age) + ((age * age) / 600));
        robot["age"] = age;
        robot["wear"] = static_cast<std::int64_t>(wear);
    }
    world["departed"] = Array{};
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 5 world is malformed ({})", error.what()));
}

std::expected<void, std::string> V6ToV7(SaveDocument& world) try {
    constexpr std::uint64_t kSpacing = 3;
    constexpr std::uint64_t kOffset = 1;
    constexpr std::array<std::string_view, 12> kNames{
        "Neon Square",  "Foundry Gate", "Old Grid",     "Chrome Market",
        "Circuit Park", "Signal Hill",  "Rust Docks",   "Ion Plaza",
        "Coil Street",  "Relay Yard",   "Static Cross", "Diode Row"};
    SaveDocument& city = world["city"];
    const std::uint64_t nodes = city["nodes"].get_array().size();
    std::uint64_t size = 0;
    while ((size + 1) * (size + 1) <= nodes) {
        ++size;
    }
    if (size * size != nodes) {
        return std::unexpected("the street grid is not square");
    }
    SaveDocument stations = Array{};
    for (std::uint64_t row = kOffset; row < size; row += kSpacing) {
        for (std::uint64_t column = kOffset; column < size; column += kSpacing) {
            const std::uint64_t number = stations.get_array().size();
            std::string name{kNames.at(number % kNames.size())};
            if (number >= kNames.size()) {
                name += std::format(" {}", (number / kNames.size()) + 1);
            }
            SaveDocument station = Object{};
            station["node"] = (row * size) + column;
            station["name"] = std::move(name);
            stations.get_array().push_back(std::move(station));
        }
    }
    city["stations"] = std::move(stations);
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["rideFrom"] = std::uint64_t{0};
        robot["rideTo"] = std::uint64_t{0};
        robot["boardsAt"] = std::uint64_t{0};
        robot["alightsAt"] = std::uint64_t{0};
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 6 world is malformed ({})", error.what()));
}


std::expected<void, std::string> V7ToV8(SaveDocument& world) try {
    constexpr std::uint64_t kFirstDryMinutes = 6 * 60;
    SaveDocument weather = Object{};
    weather["rain"] = std::uint64_t{0};
    weather["changesAt"] = world["tick"].as<std::uint64_t>() + kFirstDryMinutes;
    weather["spells"] = std::uint64_t{0};
    world["weather"] = std::move(weather);
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 7 world is malformed ({})", error.what()));
}


std::expected<void, std::string> V8ToV9(SaveDocument& world) try {
    for (SaveDocument& robot : world["robots"].get_array()) {
        robot["rideVia"] = robot["rideTo"].as<std::uint64_t>();
    }
    return {};
} catch (const std::exception& error) {
    return std::unexpected(
        std::format("the version 8 world is malformed ({})", error.what()));
}


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

}  // namespace mechapunk::sim::migrations
