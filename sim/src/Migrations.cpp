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

}  // namespace mechapunk::sim::migrations
