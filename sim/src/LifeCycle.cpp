// Robots age and wear out. Clinics undo some of the wear; a robot worn out
// completely is scrapped, its friends remember losing it, and a newcomer
// moves into the city to take its place.
#include <algorithm>
#include <limits>
#include <bit>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Names.hpp"
#include "mechapunk/sim/Body.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::uint32_t kDaysPerExtraWear = 300;

// Wear added every night: a base amount, more with age, more for a robot
// left run down.
constexpr std::int32_t kBaseWearPerDay = 2;

}  // namespace

std::int32_t WearAtAge(std::uint32_t days) noexcept {
    // The nightly wear summed over a life of `days`.
    const auto age = static_cast<std::int64_t>(days);
    const std::int64_t wear =
        (kBaseWearPerDay * age) +
        ((age * age) / (2 * static_cast<std::int64_t>(kDaysPerExtraWear)));
    return static_cast<std::int32_t>(std::min<std::int64_t>(wear, kMaxWear));
}

std::string_view World::NameOf(EntityId robot) const {
    if (const Robot* living = FindRobot(robot)) {
        return living->name;
    }
    const auto found = std::ranges::lower_bound(state_.departed, robot,
                                                std::less{}, &DepartedRobot::id);
    if (found != state_.departed.end() && found->id == robot) {
        return found->name;
    }
    return "unknown robot";
}

void World::AgeRobots() {
    std::vector<EntityId> wornOut;
    for (Robot& robot : state_.robots) {
        ++robot.age;
        std::int32_t wear =
            kBaseWearPerDay + static_cast<std::int32_t>(robot.age / kDaysPerExtraWear);
        if (robot.needs.condition < static_cast<std::int32_t>(state_.settings.population.neglectedCondition)) {
            wear += static_cast<std::int32_t>(state_.settings.population.neglectWear);
        }
        robot.wear = std::min(robot.wear + wear, kMaxWear);
        robot.needs.condition = std::min(robot.needs.condition, kMaxWear - robot.wear);
        if (robot.wear >= kMaxWear) {
            wornOut.push_back(robot.id);
        }
    }
    for (const EntityId id : wornOut) {
        Scrap(id);
    }
    for (std::size_t newcomer = 0; newcomer < wornOut.size(); ++newcomer) {
        AddNewcomer();
    }
    // Immigrants come on top of the replacements, while there is room.
    if (state_.settings.population.immigration != 0) {
        for (std::int64_t arrived = 0;
             arrived < state_.settings.population.immigrantsPerNight && RoomForNewcomer();
             ++arrived) {
            AddNewcomer();
        }
    }
}

bool World::FreeDormBed() const {
    std::unordered_set<std::uint64_t> homes;
    for (const Robot& robot : state_.robots) {
        homes.insert(robot.apartment.value);
    }
    return std::ranges::any_of(
        rooms_.RoomsOfKind(RoomKind::Apartment), [&](const RoomRef& apartment) {
            return apartment.room->apartmentClass == ApartmentClass::Dorm &&
                   !homes.contains(apartment.room->id.value) &&
                   !IsUnderConstruction(state_.projects, apartment.building->id);
        });
}

bool World::RoomForNewcomer() const {
    // A job as the next robot would be offered it: ids only grow.
    return FreeDormBed() && !OpeningsFor(EntityId{state_.ids.next}, StaffCounts()).empty();
}

void World::Scrap(EntityId id) {
    const auto found =
        std::ranges::lower_bound(state_.robots, id, std::less{}, &Robot::id);
    if (found == state_.robots.end() || found->id != id) {
        return;
    }
    Robot& robot = *found;
    // Free its place at a venue, or the one it was heading to.
    if (robot.activity == RobotActivity::Visiting) {
        --reserved_[robot.location.value];
        --guests_[robot.location.value];
    } else if (robot.activity == RobotActivity::Walking) {
        if (IsGuestTrip(robot, robot.destination)) {
            --reserved_[robot.destination.value];
        }
    }
    Emit({.tick = state_.tick,
          .kind = EventKind::Scrapped,
          .robot = robot.id,
          .room = robot.apartment});
    ++state_.today.scrapped;
    Bequeath(robot);
    const auto departed = std::ranges::lower_bound(
        state_.departed, id, std::less{}, &DepartedRobot::id);
    state_.departed.insert(
        departed, {.id = robot.id, .name = robot.name, .scrappedAt = state_.tick});
    state_.robots.erase(found);
    ++populationVersion_;
    CountResidents();

    // Friends remember; everyone forgets the bond.
    for (Robot& other : state_.robots) {
        const auto bond =
            std::ranges::find(other.bonds, id, &Bond::robot);
        if (bond == other.bonds.end()) {
            continue;
        }
        const bool friends = bond->level == BondLevel::Friend ||
                             bond->level == BondLevel::CloseFriend;
        other.bonds.erase(bond);
        if (friends) {
            Remember(other, MemoryKind::LostFriend, id, {});
        }
    }
}

void World::AddNewcomer() {
    std::unordered_set<std::uint64_t> homes;
    for (const Robot& robot : state_.robots) {
        homes.insert(robot.apartment.value);
    }
    // Newcomers start in a free dorm room if there is one. A home in a
    // building a project is still raising is not free: its floors are going
    // up, so nobody moves in until the project is done (task D, step D2c2).
    std::vector<EntityId> freeHomes;
    std::vector<EntityId> freeDorms;
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        if (!homes.contains(apartment.room->id.value) &&
            !IsUnderConstruction(state_.projects, apartment.building->id)) {
            freeHomes.push_back(apartment.room->id);
            if (apartment.room->apartmentClass == ApartmentClass::Dorm) {
                freeDorms.push_back(apartment.room->id);
            }
        }
    }
    if (!freeDorms.empty()) {
        freeHomes = std::move(freeDorms);
    }
    if (freeHomes.empty()) {
        return;  // Nowhere to live: nobody moves in.
    }

    Pcg32& random = state_.random;
    const auto pick = [&](const std::vector<EntityId>& from) {
        return from.at(random.UniformInt(0, static_cast<std::uint32_t>(from.size() - 1)));
    };
    const auto need = [&] {
        return static_cast<std::int32_t>(
            random.UniformInt(static_cast<std::int32_t>(state_.settings.population.minNewcomerNeed), static_cast<std::int32_t>(state_.settings.population.maxNewcomerNeed)));
    };
    Robot robot;
    robot.id = state_.ids.Allocate();
    robot.name = RandomRobotName(random);
    robot.apartment = pick(freeHomes);
    const std::vector<EntityId> openings = OpeningsFor(robot.id, StaffCounts());
    robot.workspace = openings.empty() ? EntityId{} : pick(openings);
    // The trains need drivers: a place in a crew room comes first. The draw
    // above is made all the same, so the random stream does not move.
    if (const auto driver = std::ranges::find_if(openings, [&](EntityId job) {
            const auto found = rooms_.Find(job);
            return found && found->room->kind == RoomKind::CrewRoom;
        });
        driver != openings.end()) {
        robot.workspace = *driver;
    }
    robot.location = robot.apartment;
    robot.punctualityMinutes = random.UniformInt(static_cast<std::uint32_t>(state_.settings.population.minPunctuality), static_cast<std::uint32_t>(state_.settings.population.maxPunctuality));
    robot.needs = {.charge = need(), .condition = need(), .social = need(), .fun = need()};
    std::int64_t savings = random.UniformInt(static_cast<std::uint32_t>(state_.settings.population.minNewcomerCredits), static_cast<std::uint32_t>(state_.settings.population.maxNewcomerCredits));
    // With the wealth spread on, some arrive rich: one more draw, made only then
    // so a city without it keeps its random stream.
    if (state_.settings.population.wealthSpread != 0 &&
        random.UniformInt(1, 100) <=
            static_cast<std::uint32_t>(state_.settings.population.richPercent)) {
        savings *= state_.settings.population.richMultiplier;
    }
    // A bank of its own, the way the first robots were given theirs.
    std::vector<EntityId> banks;
    for (const Organization& organization : state_.economy.organizations) {
        if (organization.kind == OrganizationKind::Bank) {
            banks.push_back(organization.id);
        }
    }
    if (!banks.empty() && state_.settings.economy.robotsPerBank != 0) {
        // The bank with fewest accounts, the lowest id among equals: no draw.
        std::size_t fewest = std::numeric_limits<std::size_t>::max();
        for (const EntityId bank : banks) {
            const auto accounts = static_cast<std::size_t>(std::ranges::count(
                state_.robots, bank, &Robot::bank));
            if (accounts < fewest) {
                fewest = accounts;
                robot.bank = bank;
            }
        }
    } else if (!banks.empty()) {
        robot.bank = pick(banks);
    }
    const std::uint32_t firstDayOff = random.UniformInt(0, kDaysPerWeek - 1);
    robot.daysOff = static_cast<std::uint8_t>((1U << firstDayOff) |
                                              (1U << ((firstDayOff + 1) % kDaysPerWeek)));
    while (std::popcount(robot.interests) < static_cast<int>(static_cast<std::uint32_t>(state_.settings.population.newcomerInterests))) {
        robot.interests = static_cast<std::uint8_t>(
            robot.interests | (1U << random.UniformInt(0, kInterestCount - 1)));
    }
    robot.age = random.UniformInt(static_cast<std::uint32_t>(state_.settings.population.minNewcomerAge), static_cast<std::uint32_t>(state_.settings.population.maxNewcomerAge));
    robot.wear = WearAtAge(robot.age);
    // Its body parts are new; the hand it works with follows its id, like a
    // built robot's, so no random draw moves, and its age caps the head and
    // the torso (task L, step L1).
    robot.body.dominantHand =
        robot.id.value % 10 == 0 ? BodyPart::LeftHand : BodyPart::RightHand;
    CapBody(robot);
    // Ids only grow, so the newcomer goes last and robots stay sorted.
    state_.robots.push_back(std::move(robot));
    ++populationVersion_;
    CountResidents();
    Robot& arrived = state_.robots.back();
    ++state_.today.arrivals;
    // It brings its savings from outside: new money in the city.
    ChangeMoney(arrived.id, savings, EmissionReason::NewcomerSavings);
    Emit({.tick = state_.tick,
          .kind = EventKind::Arrived,
          .robot = arrived.id,
          .room = arrived.apartment});
    Remember(arrived, MemoryKind::ArrivedInCity, {}, arrived.apartment);
    ScheduleNextCommute(arrived);
}

void World::ForgetDeparted() {
    // Keep a scrapped robot's name only while someone still remembers it.
    std::unordered_set<std::uint64_t> remembered;
    for (const Robot& robot : state_.robots) {
        for (const Memory& memory : robot.memories) {
            remembered.insert(memory.robot.value);
        }
    }
    if (std::erase_if(state_.departed, [&](const DepartedRobot& departed) {
            return !remembered.contains(departed.id.value);
        }) > 0) {
        ++populationVersion_;
    }
}

}  // namespace mechapunk::sim
