// World::Generate: a new city, its robots, their dorm beds and first jobs.

#include "mechapunk/sim/World.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Names.hpp"
#include "mechapunk/sim/Body.hpp"
#include "mechapunk/sim/Venues.hpp"

#include "WorldInternal.hpp"

namespace mechapunk::sim {
namespace {
constexpr std::uint32_t kMaxSerialNumber = 999;

constexpr std::uint32_t kMinSerialNumber = 100;


// Independent random streams per purpose, so for example a change in robot
// generation does not reshape the city.
constexpr std::uint64_t kCityStream = 1;

constexpr std::uint64_t kPopulationStream = 2;

constexpr std::uint64_t kRuntimeStream = 3;

constexpr std::uint64_t kInterestStream = 4;

constexpr std::uint64_t kCareerStream = 5;

constexpr std::uint64_t kAgeStream = 6;
// Which robots are rich (population.wealthSpread). A stream of its own, so a
// city without the spread draws exactly what it always did.
constexpr std::uint64_t kWealthStream = 8;

// The gang stream (7) lives in Gangs.hpp, where both founders can see it.

constexpr std::array<std::string_view, 8> kModelPrefixes{
    "KX", "VR", "NE", "TS", "OM", "ZR", "HK", "LU"};

constexpr std::array<std::string_view, 24> kGivenNames{
    "Vera",  "Ash",  "Juno", "Rook",  "Mira",  "Dex",  "Echo", "Nova",
    "Iris",  "Bolt", "Kai",  "Lumen", "Sable", "Tarn", "Vex",  "Wren",
    "Quill", "Onyx", "Pike", "Rhea",  "Sol",   "Tess", "Ulla", "Zed"};

// Picks a room of `kind` with space left, starting from a random one.
// A dorm bed for a new robot; everyone starts out in the city's dorms.
EntityId AssignDorm(const RoomIndex& rooms, std::unordered_map<std::uint64_t, std::uint32_t>& used,
                    Pcg32& random) {
    std::vector<const Room*> dorms;
    for (const RoomRef& apartment : rooms.RoomsOfKind(RoomKind::Apartment)) {
        if (apartment.room->apartmentClass == ApartmentClass::Dorm) {
            dorms.push_back(apartment.room);
        }
    }
    if (dorms.empty()) {
        throw std::logic_error("Generated city has no dorm rooms");
    }
    const auto start = random.UniformInt(0, static_cast<std::uint32_t>(dorms.size() - 1));
    for (std::size_t offset = 0; offset < dorms.size(); ++offset) {
        const Room& room = *dorms[(start + offset) % dorms.size()];
        if (used[room.id.value] < room.capacity) {
            ++used[room.id.value];
            return room.id;
        }
    }
    throw std::logic_error("Generated city has too few dorm beds");
}

// A job for a new robot: a venue short of staff on the shift it would work,
// or failing that a workspace with room. Venues are staffed first, so every
// bar has its barman; workspaces take everyone else. When every place is
// taken a seed town leaves the robot out of work, which its careers handle as
// ever; a full city is generated with a job for everyone, so it is a bug.
EntityId AssignJob(const RoomIndex& rooms, EntityId robot,
                   std::unordered_map<std::uint64_t, std::uint32_t>& staff, Pcg32& random,
                   bool mayBeJobless) {
    std::vector<const Room*> venues;
    for (const RoomKind kind : VenueKinds()) {
        for (const RoomRef& venue : rooms.RoomsOfKind(kind)) {
            if (staff[JobKey(*venue.room, robot)] < StaffPerShift(*venue.room)) {
                venues.push_back(venue.room);
            }
        }
    }
    // Police stations and crew rooms: one slot per capacity, checked via JobKey.
    for (const RoomKind kind : std::array{RoomKind::PoliceStation, RoomKind::CrewRoom}) {
        for (const RoomRef& room : rooms.RoomsOfKind(kind)) {
            if (staff[JobKey(*room.room, robot)] < StaffPerShift(*room.room)) {
                venues.push_back(room.room);
            }
        }
    }
    if (!venues.empty()) {
        const Room& venue = *venues.at(random.UniformInt(0, static_cast<std::uint32_t>(venues.size() - 1)));
        ++staff[JobKey(venue, robot)];
        return venue.id;
    }
    const std::vector<RoomRef>& workspaces = rooms.RoomsOfKind(RoomKind::Workspace);
    if (workspaces.empty()) {
        throw std::logic_error("Generated city has no workspace");
    }
    const auto start = random.UniformInt(0, static_cast<std::uint32_t>(workspaces.size() - 1));
    for (std::size_t offset = 0; offset < workspaces.size(); ++offset) {
        const Room& room = *workspaces[(start + offset) % workspaces.size()].room;
        if (staff[JobKey(room, robot)] < room.capacity) {
            ++staff[JobKey(room, robot)];
            return room.id;
        }
    }
    if (mayBeJobless) {
        return EntityId{};
    }
    throw std::logic_error("Generated city has too few jobs");
}

}  // namespace

// namespace

std::string RandomRobotName(Pcg32& random) {
    const auto prefix = kModelPrefixes.at(random.UniformInt(
        0, static_cast<std::uint32_t>(kModelPrefixes.size() - 1)));
    const auto serial = random.UniformInt(kMinSerialNumber, kMaxSerialNumber);
    const auto given = kGivenNames.at(random.UniformInt(
        0, static_cast<std::uint32_t>(kGivenNames.size() - 1)));
    return std::format("{}-{} {}", prefix, serial, given);
}

World World::Generate(const WorldParams& params) {
    WorldState state;
    state.seed = params.seed;
    Pcg32 cityRandom = Pcg32::FromSeed(params.seed, kCityStream);
    state.city = GenerateCity(CityParams{.residents = params.robots, .city = params.settings.city},
                              cityRandom, state.ids);

    const RoomIndex rooms(state.city);
    Pcg32 population = Pcg32::FromSeed(params.seed, kPopulationStream);
    std::unordered_map<std::uint64_t, std::uint32_t> used;
    std::unordered_map<std::uint64_t, std::uint32_t> staff;
    const auto need = [&] {
        return static_cast<std::int32_t>(population.UniformInt(
            static_cast<std::int32_t>(params.settings.population.minStartingNeed), static_cast<std::int32_t>(params.settings.population.maxStartingNeed)));
    };
    Pcg32 wealth = Pcg32::FromSeed(params.seed, kWealthStream);
    for (std::uint32_t index = 0; index < params.robots; ++index) {
        Robot robot;
        robot.name = RandomRobotName(population);
        robot.id = state.ids.Allocate();
        robot.apartment = AssignDorm(rooms, used, population);
        robot.workspace = AssignJob(rooms, robot.id, staff, population,
                                    params.settings.city.seedTown != 0);
        robot.location = robot.apartment;
        robot.punctualityMinutes = population.UniformInt(
            static_cast<std::uint32_t>(params.settings.population.minPunctualityMinutes), static_cast<std::uint32_t>(params.settings.population.maxPunctualityMinutes));
        robot.needs = {.charge = need(), .condition = need(),
                       .social = need(), .fun = need()};
        robot.credits = population.UniformInt(static_cast<std::uint32_t>(params.settings.population.minStartingCredits),
                                              static_cast<std::uint32_t>(params.settings.population.maxStartingCredits));
        if (params.settings.population.wealthSpread != 0 &&
            wealth.UniformInt(1, 100) <=
                static_cast<std::uint32_t>(params.settings.population.richPercent)) {
            robot.credits *= params.settings.population.richMultiplier;
        }
        // Two consecutive days off a week, different for each robot, so
        // workplaces stay staffed every day.
        const std::uint32_t firstDayOff =
            population.UniformInt(0, kDaysPerWeek - 1);
        robot.daysOff = static_cast<std::uint8_t>(
            (1U << firstDayOff) | (1U << ((firstDayOff + 1) % kDaysPerWeek)));
        state.robots.push_back(std::move(robot));
    }

    Pcg32 tastes = Pcg32::FromSeed(params.seed, kInterestStream);
    for (Robot& robot : state.robots) {
        while (std::popcount(robot.interests) <
               static_cast<int>(static_cast<std::uint32_t>(params.settings.population.interestsPerRobot))) {
            robot.interests = static_cast<std::uint8_t>(
                robot.interests | (1U << tastes.UniformInt(0, kInterestCount - 1)));
        }
    }

    Pcg32 ages = Pcg32::FromSeed(params.seed, kAgeStream);
    for (Robot& robot : state.robots) {
        robot.age = ages.UniformInt(0, static_cast<std::uint32_t>(params.settings.population.maxStartingAge));
        robot.wear = WearAtAge(robot.age);
        robot.needs.condition =
            std::min(robot.needs.condition, kMaxWear - robot.wear);
        // One robot in ten is left-handed, decided by its id so that no
        // random draw moves, and its parts are new but capped by its age.
        robot.body.dominantHand =
            robot.id.value % 10 == 0 ? BodyPart::LeftHand
                                      : BodyPart::RightHand;
        CapBody(robot);
    }

    Pcg32 careers = Pcg32::FromSeed(params.seed, kCareerStream);
    for (Robot& robot : state.robots) {
        robot.jobLevel =
            static_cast<std::uint8_t>(careers.UniformInt(0, static_cast<std::uint32_t>(params.settings.population.highestStartingLevel)));
    }
    state.weather = StartingWeather(params.seed, state.tick);

    state.random = Pcg32::FromSeed(params.seed, kRuntimeStream);
    state.settings = params.settings;
    World world(std::move(state));
    Pcg32 gangRandom = Pcg32::FromSeed(params.seed, kGangStream);
    world.FoundGangs(gangRandom);
    world.FoundEconomy(params.seed);
    world.IndexEconomy();
    world.CountResidents();
    for (Robot& robot : world.state_.robots) {
        world.ScheduleNextCommute(robot);
    }
    return world;
}

}  // namespace mechapunk::sim
