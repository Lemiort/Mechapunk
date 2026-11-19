// Prices and rents, and ground-floor units: closing, opening (venues,
// workspaces, police stations) and nightly review; venue price setting.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/World.hpp"

#include "EconomyInternal.hpp"

namespace mechapunk::sim {
namespace {

// Venue prices move a step a night between these, in percent of base;
// a venue counts as busy when it had kBusyTurns times its places in guests.
constexpr std::uint32_t kPriceStep = 1;

constexpr std::uint32_t kMinPricePercent = 50;

constexpr std::uint32_t kMaxPricePercent = 300;

constexpr std::uint32_t kBusyTurns = 3;

// Market rents move a point a night while fewer homes than the tight share
// stand empty, or more than the loose share; within these bounds.
constexpr std::int64_t kTightVacancyPercent = 5;

constexpr std::int64_t kLooseVacancyPercent = 15;

constexpr std::int64_t kMinRentPercent = 50;

constexpr std::int64_t kMaxRentPercent = 300;

// A venue closes after this many nights in a row with fewer guests than
// half its places, its prices already cut to kQuietPricePercent or less; a
// workspace after as many under half staffed, while under the low share of
// robots, in percent, is out of work.
constexpr std::uint32_t kCloseAfterNights = 10;

constexpr std::uint32_t kQuietPricePercent = 80;

constexpr std::int64_t kLowUnemploymentPercent = 2;

// Each night an empty unit may open, with this chance in percent: as a
// workspace in a workplace while over the high share of robots is out of
// work, otherwise as the venue the city is most short of.
constexpr std::uint32_t kOpenUnitPercent = 10;

constexpr std::int64_t kHighUnemploymentPercent = 5;

// Venues of a kind count as in demand while their prices average this, in
// percent of usual.
constexpr std::int64_t kInDemandPercent = 105;

constexpr std::uint32_t kMinWorkspaceStaff = 5;

constexpr std::uint32_t kMaxWorkspaceStaff = 10;

}  // namespace

std::int64_t World::PriceOf(const Room& apartment) const {
    return RentFor(apartment) * kPriceNights;
}

std::int64_t World::RentFor(const Room& apartment) const {
    const auto kind = static_cast<std::size_t>(apartment.apartmentClass);
    if (apartment.apartmentClass == ApartmentClass::Dorm || kind < 2) {
        return apartment.rent;
    }
    return std::max<std::int64_t>(
        1, static_cast<std::int64_t>(apartment.rent) * state_.economy.rentPercent.at(kind - 2) / 100);
}

std::int64_t World::PriceAt(const Room& venue) const {
    const VenueRules* rules = FindVenueRules(venue.kind);
    return rules != nullptr
               ? std::max<std::int64_t>(1, static_cast<std::int64_t>(rules->price) * venue.pricePercent / 100)
               : 0;
}

void World::CloseUnit(Room& room) {
    const RoomKind was = room.kind;
    // Its staff are laid off.
    for (Robot& robot : state_.robots) {
        if (robot.workspace != room.id) {
            continue;
        }
        robot.workspace = {};
        robot.jobLevel = 0;
        robot.performance = 0;
        robot.commuteAt = kNoCommute;
        Remember(robot, MemoryKind::LostJob, {}, room.id);
        if (robot.activity == RobotActivity::Resting) {
            PlanAtHome(robot);
        }
    }
    room.kind = RoomKind::VacantStore;
    room.name = "Empty Unit";
    room.pricePercent = 100;
    room.visits = 0;
    room.quietNights = 0;
    ++cityVersion_;
    ++state_.today.venuesClosed;
    Emit({.tick = state_.tick, .kind = EventKind::UnitClosed, .room = room.id,
          .account = room.owner, .change = static_cast<std::int32_t>(was)});
}

void World::OpenUnit(Room& room, RoomKind kind) {
    // City Hall opens a police station. Otherwise the chain with most
    // venues of the kind takes it on, or for a workspace the corporation
    // owning the rest of its workplace.
    std::unordered_map<std::uint64_t, std::uint32_t> held;
    const auto building = rooms_.Find(room.id);
    if (kind == RoomKind::PoliceStation) {
        room.owner = cityHall_;
    } else {
        if (kind == RoomKind::Workspace) {
            for (const Room& other : building->building->rooms) {
                if (other.kind == RoomKind::Workspace) {
                    ++held[other.owner.value];
                }
            }
        } else {
            for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
                ++held[venue.room->owner.value];
            }
        }
        EntityId owner;
        std::uint32_t most = 0;
        for (const Organization& organization : state_.economy.organizations) {
            const auto found = held.find(organization.id.value);
            const std::uint32_t count = found != held.end() ? found->second : 0;
            const Sector wanted = kind == RoomKind::Workspace ? Sector::Industry : Sector::Venues;
            if (organization.sector == wanted && (!owner.IsValid() || count > most)) {
                owner = organization.id;
                most = count;
            }
        }
        if (owner.IsValid()) {
            room.owner = owner;
        }
    }
    room.kind = kind;
    if (kind == RoomKind::PoliceStation) {
        room.name = "Police Station";
        room.capacity = 2;
    } else if (const VenueRules* rules = FindVenueRules(kind)) {
        room.name = std::string{VenueNames(kind)[state_.random.UniformInt(
            0, static_cast<std::uint32_t>(VenueNames(kind).size() - 1))]};
        room.capacity = state_.random.UniformInt(rules->minCapacity, rules->maxCapacity);
    } else {
        room.name = "Workshop";
        room.capacity = state_.random.UniformInt(kMinWorkspaceStaff, kMaxWorkspaceStaff);
    }
    room.pricePercent = 100;
    room.visits = 0;
    room.quietNights = 0;
    ++cityVersion_;
    ++state_.today.venuesOpened;
    Emit({.tick = state_.tick, .kind = EventKind::UnitOpened, .room = room.id,
          .account = room.owner});
}

void World::ReviewUnits() {
    const std::uint64_t version = cityVersion_;
    // Rooms someone is in or on the way to stay as they are tonight.
    std::unordered_set<std::uint64_t> busy;
    std::unordered_map<std::uint64_t, std::uint32_t> staff;
    std::int64_t unemployed = 0;
    for (const Robot& robot : state_.robots) {
        busy.insert(robot.location.value);
        busy.insert(robot.destination.value);
        if (robot.workspace.IsValid()) {
            ++staff[robot.workspace.value];
        } else {
            ++unemployed;
        }
    }
    const std::int64_t population = std::max<std::int64_t>(1, static_cast<std::int64_t>(state_.robots.size()));
    // City Hall keeps one police station per 2000 residents, opening more in
    // empty units as the city grows.
    std::int64_t stations = static_cast<std::int64_t>(rooms_.RoomsOfKind(RoomKind::PoliceStation).size());
    const std::int64_t unemployment = unemployed * 100 / population;
    // How much each kind of venue is in demand: its average price, and
    // whether the city has any.
    std::map<RoomKind, std::pair<std::int64_t, std::int64_t>> demand;
    for (const RoomKind kind : VenueKinds()) {
        demand[kind] = {0, 0};
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            demand[kind].first += venue.room->pricePercent;
            ++demand[kind].second;
        }
    }
    const auto mostWanted = [&]() -> std::optional<RoomKind> {
        std::optional<RoomKind> best;
        std::int64_t bestScore = 0;
        for (const auto& [kind, sum] : demand) {
            // A kind the city lacks altogether is wanted most.
            const std::int64_t score = sum.second == 0 ? 1000 : sum.first / sum.second;
            if (score >= kInDemandPercent && score > bestScore) {
                best = kind;
                bestScore = score;
            }
        }
        return best;
    };
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.floor != 0) {
                continue;
            }
            if (IsVenue(room.kind)) {
                const bool quiet = room.visits * 2 < room.capacity && room.pricePercent <= kQuietPricePercent;
                room.quietNights = quiet ? room.quietNights + 1 : 0;
                if (room.quietNights >= kCloseAfterNights && !busy.contains(room.id.value)) {
                    CloseUnit(room);
                }
            } else if (room.kind == RoomKind::Workspace) {
                room.quietNights = staff[room.id.value] * 2 < room.capacity ? room.quietNights + 1 : 0;
                if (room.quietNights >= kCloseAfterNights && unemployment < kLowUnemploymentPercent &&
                    !busy.contains(room.id.value)) {
                    CloseUnit(room);
                }
            } else if (room.kind == RoomKind::VacantStore) {
                ++room.quietNights;
                if (state_.random.UniformInt(1, 100) > kOpenUnitPercent || busy.contains(room.id.value)) {
                    continue;
                }
                if (building.kind == BuildingKind::Workplace &&
                    unemployment >= kHighUnemploymentPercent) {
                    OpenUnit(room, RoomKind::Workspace);
                } else if (stations * 2000 < population) {
                    // City Hall takes the unit before any venue opens.
                    OpenUnit(room, RoomKind::PoliceStation);
                    ++stations;
                } else if (const auto kind = mostWanted()) {
                    OpenUnit(room, *kind);
                    demand[*kind].first += 100;
                    ++demand[*kind].second;
                }
            }
        }
    }
    if (cityVersion_ != version) {
        // Kinds changed: the index of rooms by kind must follow.
        rooms_ = RoomIndex(state_.city);
    }
}

void World::SetPrices() {
    // A venue that had three times its places in guests asks more; one
    // that had fewer guests than places, less.
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (!IsVenue(room.kind)) {
                continue;
            }
            if (room.visits >= kBusyTurns * room.capacity) {
                room.pricePercent = std::min(kMaxPricePercent, room.pricePercent + kPriceStep);
            } else if (room.visits < room.capacity) {
                room.pricePercent = std::max(kMinPricePercent, room.pricePercent - kPriceStep);
            }
            room.visits = 0;
        }
    }
    // Market rents follow how many homes of each class stand empty.
    std::array<std::int64_t, 4> homes{};
    std::array<std::int64_t, 4> empty{};
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const auto kind = static_cast<std::size_t>(apartment.room->apartmentClass);
        if (kind < 2) {
            continue;
        }
        ++homes.at(kind - 2);
        empty.at(kind - 2) += ResidentsOf(apartment.room->id) == 0 ? 1 : 0;
    }
    for (std::size_t kind = 0; kind < homes.size(); ++kind) {
        if (homes[kind] == 0) {
            continue;
        }
        std::int64_t& rent = state_.economy.rentPercent[kind];
        const std::int64_t vacancy = empty[kind] * 100 / homes[kind];
        if (vacancy < kTightVacancyPercent) {
            rent = std::min(kMaxRentPercent, rent + 1);
        } else if (vacancy > kLooseVacancyPercent) {
            rent = std::max(kMinRentPercent, rent - 1);
        }
    }
}

std::int64_t World::UpkeepOf(const Room& apartment) const {
    return RentFor(apartment) * kUpkeepPercent / 100;
}

}  // namespace mechapunk::sim
