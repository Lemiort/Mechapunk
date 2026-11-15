#include "mechapunk/sim/World.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mechapunk::sim {

namespace {
// Walking pace of 1.25 m/s.
constexpr std::uint32_t kWalkingMetersPerMinute = 75;
constexpr std::uint32_t kMinPunctualityMinutes = 5;
constexpr std::uint32_t kMaxPunctualityMinutes = 20;
// Daily variation added to a robot's punctuality.
constexpr std::uint32_t kMaxDailyExtraMinutes = 10;
constexpr std::uint32_t kMinSerialNumber = 100;
constexpr std::uint32_t kMaxSerialNumber = 999;
// Independent random streams per purpose, so for example a change in robot
// generation does not reshape the city.
constexpr std::uint64_t kCityStream = 1;
constexpr std::uint64_t kPopulationStream = 2;
constexpr std::uint64_t kRuntimeStream = 3;

constexpr std::array<std::string_view, 8> kModelPrefixes{
    "KX", "VR", "NE", "TS", "OM", "ZR", "HK", "LU"};
constexpr std::array<std::string_view, 24> kGivenNames{
    "Vera",  "Ash",   "Juno", "Rook",  "Mira", "Dex",  "Echo", "Nova",
    "Iris",  "Bolt",  "Kai",  "Lumen", "Sable", "Tarn", "Vex", "Wren",
    "Quill", "Onyx",  "Pike", "Rhea",  "Sol",  "Tess", "Ulla", "Zed"};

// Picks a building of `kind` that still has room, starting from a random one.
EntityId AssignBuilding(const City& city, BuildingKind kind,
                        std::vector<std::uint32_t>& occupancy,
                        Pcg32& random) {
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < city.buildings.size(); ++index) {
        if (city.buildings[index].kind == kind) {
            candidates.push_back(index);
        }
    }
    const auto start = random.UniformInt(
        0, static_cast<std::uint32_t>(candidates.size() - 1));
    for (std::size_t offset = 0; offset < candidates.size(); ++offset) {
        const std::size_t index =
            candidates[(start + offset) % candidates.size()];
        if (occupancy[index] < city.buildings[index].capacity) {
            ++occupancy[index];
            return city.buildings[index].id;
        }
    }
    throw std::logic_error("Generated city has too little capacity");
}

std::string_view BuildingName(const City& city, EntityId id) {
    const Building* building = city.FindBuilding(id);
    return building != nullptr ? std::string_view{building->name}
                               : std::string_view{"somewhere"};
}
}  // namespace

World::World(WorldState state)
    : state_(std::move(state)), routes_(state_.city) {}

World World::Generate(const WorldParams& params) {
    WorldState state;
    state.seed = params.seed;
    Pcg32 cityRandom = Pcg32::FromSeed(params.seed, kCityStream);
    state.city = GenerateCity(CityParams{.residents = params.robots},
                              cityRandom, state.ids);

    Pcg32 population = Pcg32::FromSeed(params.seed, kPopulationStream);
    std::vector<std::uint32_t> occupancy(state.city.buildings.size(), 0);
    for (std::uint32_t index = 0; index < params.robots; ++index) {
        const auto prefix = kModelPrefixes.at(population.UniformInt(
            0, static_cast<std::uint32_t>(kModelPrefixes.size() - 1)));
        const auto serial =
            population.UniformInt(kMinSerialNumber, kMaxSerialNumber);
        const auto given = kGivenNames.at(population.UniformInt(
            0, static_cast<std::uint32_t>(kGivenNames.size() - 1)));
        Robot robot;
        robot.id = state.ids.Allocate();
        robot.name = std::format("{}-{} {}", prefix, serial, given);
        robot.home = AssignBuilding(state.city, BuildingKind::Habitat,
                                    occupancy, population);
        robot.workplace = AssignBuilding(state.city, BuildingKind::Workplace,
                                         occupancy, population);
        robot.punctualityMinutes = population.UniformInt(
            kMinPunctualityMinutes, kMaxPunctualityMinutes);
        state.robots.push_back(std::move(robot));
    }

    state.random = Pcg32::FromSeed(params.seed, kRuntimeStream);
    World world(std::move(state));
    for (Robot& robot : world.state_.robots) {
        world.ScheduleNextCommute(robot);
    }
    return world;
}

std::expected<World, std::string> World::FromState(WorldState state) {
    if (auto valid = ValidateWorldState(state); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    return World(std::move(state));
}

const Robot* World::FindRobot(EntityId id) const noexcept {
    const auto found =
        std::ranges::lower_bound(state_.robots, id, std::less{}, &Robot::id);
    return found != state_.robots.end() && found->id == id ? &*found
                                                           : nullptr;
}

void World::Step() {
    ++state_.tick;
    // Robots act in id order, so random draws happen in a fixed order.
    for (Robot& robot : state_.robots) {
        if (robot.nextActionAt <= state_.tick) {
            UpdateRobot(robot);
        }
    }
}

void World::RunFor(Tick minutes) {
    for (Tick minute = 0; minute < minutes; ++minute) {
        Step();
    }
}

void World::UpdateRobot(Robot& robot) {
    const Tick now = state_.tick;
    switch (robot.activity) {
        case RobotActivity::AtHome: {
            const std::uint32_t walk =
                WalkingMinutes(robot.home, robot.workplace);
            robot.activity = RobotActivity::CommutingToWork;
            robot.nextActionAt = now + walk;
            Emit({.tick = now,
                  .kind = EventKind::LeftHome,
                  .robot = robot.id,
                  .place = robot.home,
                  .otherPlace = robot.workplace,
                  .minutes = walk});
            break;
        }
        case RobotActivity::CommutingToWork: {
            const Building* work = state_.city.FindBuilding(robot.workplace);
            robot.activity = RobotActivity::Working;
            // A late robot still leaves at the regular end of its shift.
            robot.nextActionAt =
                std::max(robot.shiftStartsAt + work->shift.lengthMinutes, now);
            const auto late = static_cast<std::uint32_t>(
                now > robot.shiftStartsAt ? now - robot.shiftStartsAt : 0);
            Emit({.tick = now,
                  .kind = EventKind::ArrivedAtWork,
                  .robot = robot.id,
                  .place = robot.workplace,
                  .otherPlace = robot.home,
                  .minutes = late});
            break;
        }
        case RobotActivity::Working: {
            const std::uint32_t walk =
                WalkingMinutes(robot.workplace, robot.home);
            robot.activity = RobotActivity::CommutingHome;
            robot.nextActionAt = now + walk;
            Emit({.tick = now,
                  .kind = EventKind::LeftWork,
                  .robot = robot.id,
                  .place = robot.workplace,
                  .otherPlace = robot.home,
                  .minutes = walk});
            break;
        }
        case RobotActivity::CommutingHome: {
            robot.activity = RobotActivity::AtHome;
            Emit({.tick = now,
                  .kind = EventKind::ArrivedHome,
                  .robot = robot.id,
                  .place = robot.home,
                  .otherPlace = robot.workplace,
                  .minutes = 0});
            ScheduleNextCommute(robot);
            break;
        }
    }
}

void World::ScheduleNextCommute(Robot& robot) {
    const Building* work = state_.city.FindBuilding(robot.workplace);
    const Tick now = state_.tick;
    const Tick lead = WalkingMinutes(robot.home, robot.workplace) +
                      robot.punctualityMinutes +
                      state_.random.UniformInt(0, kMaxDailyExtraMinutes);
    // The next shift whose departure time is still ahead.
    Tick start = StartOfDay(now) + work->shift.startMinute;
    while (start <= now + lead) {
        start += kMinutesPerDay;
    }
    robot.shiftStartsAt = start;
    robot.nextActionAt = start - lead;
}

std::uint32_t World::WalkingMinutes(EntityId from, EntityId to) {
    const Building* origin = state_.city.FindBuilding(from);
    const Building* destination = state_.city.FindBuilding(to);
    const auto meters =
        routes_.DistanceMeters(origin->entrance, destination->entrance);
    if (!meters) {
        throw std::logic_error("No route between buildings");
    }
    // Round up; even next door takes a minute.
    return std::max<std::uint32_t>(
        1, (*meters + kWalkingMetersPerMinute - 1) / kWalkingMetersPerMinute);
}

void World::Emit(const SimEvent& event) const {
    if (sink_) {
        sink_(event);
    }
}

std::string World::Describe(const SimEvent& event) const {
    const Robot* robot = FindRobot(event.robot);
    const std::string_view who =
        robot != nullptr ? std::string_view{robot->name} : "unknown robot";
    const std::string_view place = BuildingName(state_.city, event.place);
    const std::string_view other = BuildingName(state_.city, event.otherPlace);
    const std::string when = FormatTime(event.tick);
    switch (event.kind) {
        case EventKind::LeftHome:
            return std::format("{}  {} left {} for {} ({} min walk)", when, who,
                               place, other, event.minutes);
        case EventKind::ArrivedAtWork:
            return event.minutes == 0
                       ? std::format("{}  {} arrived at {}", when, who, place)
                       : std::format("{}  {} arrived at {} ({} min late)",
                                     when, who, place, event.minutes);
        case EventKind::LeftWork:
            return std::format("{}  {} finished at {}, heading to {} ({} min "
                               "walk)",
                               when, who, place, other, event.minutes);
        case EventKind::ArrivedHome:
            return std::format("{}  {} got home to {}", when, who, place);
    }
    return std::format("{}  {}: unknown event", when, who);
}

std::expected<void, std::string> ValidateWorldState(const WorldState& state) {
    const auto fail = [](std::string message) {
        return std::unexpected(std::move(message));
    };
    const City& city = state.city;
    if (city.nodes.empty()) {
        return fail("The city has no street nodes");
    }
    for (std::size_t index = 0; index < city.nodes.size(); ++index) {
        if (city.nodes[index].id.value != index) {
            return fail(std::format("Street node {} has id {}", index,
                                    city.nodes[index].id.value));
        }
    }
    const auto nodeCount = city.nodes.size();
    for (const CityEdge& edge : city.edges) {
        if (edge.from.value >= nodeCount || edge.to.value >= nodeCount ||
            edge.lengthMeters == 0) {
            return fail(std::format("Invalid street {} -> {}",
                                    edge.from.value, edge.to.value));
        }
    }

    EntityId previous;
    for (const Building& building : city.buildings) {
        if (!building.id.IsValid() || building.id <= previous ||
            building.id.value >= state.ids.next) {
            return fail(std::format("Building '{}' has an invalid or "
                                    "out-of-order id {}",
                                    building.name, building.id.value));
        }
        previous = building.id;
        if (building.entrance.value >= nodeCount) {
            return fail(std::format("Building '{}' opens onto a missing node",
                                    building.name));
        }
        if (building.kind == BuildingKind::Workplace &&
            (building.shift.startMinute >= kMinutesPerDay ||
             building.shift.lengthMinutes == 0 ||
             building.shift.lengthMinutes >= kMinutesPerDay)) {
            return fail(std::format("Workplace '{}' has an invalid shift",
                                    building.name));
        }
    }

    RoutePlanner routes(city);
    if (!city.buildings.empty()) {
        const NodeId origin = city.buildings.front().entrance;
        for (const Building& building : city.buildings) {
            if (!routes.DistanceMeters(origin, building.entrance)) {
                return fail(std::format("Building '{}' cannot be reached",
                                        building.name));
            }
        }
    }

    std::vector<std::uint32_t> occupancy(city.buildings.size(), 0);
    const auto occupy = [&](EntityId id) {
        const Building* building = city.FindBuilding(id);
        ++occupancy[static_cast<std::size_t>(building - city.buildings.data())];
        return occupancy[static_cast<std::size_t>(
                   building - city.buildings.data())] <= building->capacity;
    };
    previous = EntityId{};
    for (const Robot& robot : state.robots) {
        if (!robot.id.IsValid() || robot.id <= previous ||
            robot.id.value >= state.ids.next) {
            return fail(std::format("Robot '{}' has an invalid or out-of-order "
                                    "id {}",
                                    robot.name, robot.id.value));
        }
        previous = robot.id;
        const Building* home = city.FindBuilding(robot.home);
        if (home == nullptr || home->kind != BuildingKind::Habitat) {
            return fail(std::format("Robot '{}' has no valid home",
                                    robot.name));
        }
        const Building* work = city.FindBuilding(robot.workplace);
        if (work == nullptr || work->kind != BuildingKind::Workplace) {
            return fail(std::format("Robot '{}' has no valid workplace",
                                    robot.name));
        }
        if (city.FindBuilding(robot.id) != nullptr) {
            return fail(std::format("Robot '{}' shares id {} with a building",
                                    robot.name, robot.id.value));
        }
        if (!occupy(robot.home) || !occupy(robot.workplace)) {
            return fail(std::format("Robot '{}' exceeds the capacity of its "
                                    "home or workplace",
                                    robot.name));
        }
    }
    return {};
}

}  // namespace mechapunk::sim
