// Buildings going up: `World::ReviewConstruction` pays a site the supplies
// it costs - counting into the project what that site has paid so far -
// raises one of its floors, ends a project whose nights are up - its office
// becoming an empty unit again (step D3d) - and starts a new habitat or
// workplace when homes or jobs have run short for a week, naming the
// shortage that started it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {
namespace {

// Nights of the same shortage before the city raises another building. One
// tight night is weather; a week of it is the city running short of homes
// or of jobs.
constexpr std::size_t kStreakNights = 7;

// Floors a new site raises above its ground floor, one of them a night, so
// `daysLeft` is also the floors still to come. The ground floor stands from
// the first night and holds only the site office (step D3b): `ReviewUnits`,
// which only works on ground-floor units, branches that loop by kind - a venue,
// a workspace, an empty unit - and so leaves the office alone.
constexpr std::uint32_t kHabitatFloors = 6;
constexpr std::uint32_t kWorkplaceFloors = 4;

// Builders a site's office holds: the crew that raises its floors while the
// project is up, the jobs step D3c gives out. Until that step lands nobody
// works there, and the capacity only has to be the non-zero number
// `ValidateWorldState` asks of every room.
constexpr std::uint32_t kSiteBuilders = 6;

// Credits a night the site asks for. `PaySupplies` (Money.cpp) moves the
// owner's sector's share of them into the supply market: half for the
// housing trust, and nothing for an industry corporation, whose supplies
// are what every other sector buys from it.
constexpr std::int64_t kSiteSuppliesNight = 200;

// The share of the market's homes that may stand empty before homes are short.
// The market's homes are the ones `SetPrices` (Units.cpp) counts class by class
// when it moves market rents: every apartment but a government dorm room, that
// is the homes a robot can afford and move into. A quarter of them standing
// empty is room enough to live in; under that for a week the city is running
// out of homes. Measured: a young city leaves 75 to 85 per cent of them empty
// around its twelfth night (most robots still live in a dorm bed) and fills
// them by its thirtieth, so this bites when the market really runs out.
constexpr std::int64_t kMarketVacancyPercent = 25;

// The share of the population out of work above which jobs are short: the
// same share at which `ReviewUnits` (Units.cpp) reopens an empty unit as a
// workspace. Below it the city has work for everyone it needs; above it for a
// week it needs more, and a workplace is raised when no empty unit is left.
// Measured: a young city has 0 to 3 per cent of its robots out of work, so
// this bites only in a city whose jobs really have gone.
constexpr std::int64_t kUnemploymentPercent = 5;

// Workplaces on a finished floor of a new workplace, and the places in one.
constexpr std::uint32_t kWorkspacesPerFloor = 2;
constexpr std::uint32_t kWorkspacePlaces = 8;

// Daily rent of a new habitat's lowest floor, a credit more a floor up,
// as generation asks of its own habitats (City.cpp).
constexpr std::uint32_t kBaseRent = 30;

// The hours a new workplace's staff stand: an assembly line's shift, the
// plainest of generation's, so a new job is not the easiest in the city.
constexpr Shift kSiteShift{.startMinute = 7 * 60, .lengthMinutes = 480, .wage = 100};

// The class of homes on a floor of a new habitat: studios low, flats in
// the middle, two-bedroom flats near the top, the rule generation uses for
// a habitat of the same finished height.
[[nodiscard]] ApartmentClass HomeClassFor(std::uint32_t floor,
                                          std::uint32_t floors) noexcept {
    if (floor * 3 <= floors) {
        return ApartmentClass::Studio;
    }
    if (floor * 3 <= floors * 2) {
        return ApartmentClass::OneBedroom;
    }
    return ApartmentClass::TwoBedroom;
}

// Rent of a class over the floor's base rent, and the word its rooms are
// named with: copies of generation's file-local helpers, so a home raised
// now reads and costs like a home raised with the city.
[[nodiscard]] std::uint32_t RentPercent(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::OneBedroom: return 150;
        case ApartmentClass::TwoBedroom: return 240;
        default: return 100;
    }
}

[[nodiscard]] std::string_view HomeLabel(ApartmentClass kind) noexcept {
    switch (kind) {
        case ApartmentClass::Studio: return "Studio";
        case ApartmentClass::OneBedroom: return "Flat";
        case ApartmentClass::TwoBedroom: return "Two-Bedroom Flat";
        default: return "Apartment";
    }
}

}  // namespace

// Runs at midnight after `SettleEconomy`, so tonight's statistics are already
// in `state_.history` and the supplies a site pays count into the new day's
// books. No random number is taken: the same city raises the same buildings
// on every machine (rule 1), and the saved random stream is left alone.
void World::ReviewConstruction() {
    // The city's buildings by id, the order `City::buildings` keeps.
    const auto buildingFor = [this](EntityId id) -> Building* {
        const auto found = std::ranges::lower_bound(state_.city.buildings, id, std::less{},
                                                    &Building::id);
        return found != state_.city.buildings.end() && found->id == id ? &*found
                                                                       : nullptr;
    };

    // The free plot nearest the middle of the grid, as an index into
    // `City::lots`, or `noPlot` when the city has none left: construction
    // takes land from the middle out, and a node's lowest free corner is
    // where the view puts the building that takes it (D1a: the buildings
    // at a node hold its first slots in id order), so consuming that one
    // lot keeps the saved plots true.
    constexpr std::size_t noPlot = std::numeric_limits<std::size_t>::max();
    const auto nearestLot = [this]() -> std::size_t {
        if (state_.city.nodes.empty()) {
            return noPlot;
        }
        std::int64_t centreX = 0;
        std::int64_t centreZ = 0;
        for (const CityNode& node : state_.city.nodes) {
            centreX += node.x;
            centreZ += node.z;
        }
        const std::int64_t count = static_cast<std::int64_t>(state_.city.nodes.size());
        centreX /= count;
        centreZ /= count;
        std::size_t best = noPlot;
        std::int64_t bestDistance = 0;
        for (std::size_t index = 0; index != state_.city.lots.size(); ++index) {
            // A node id is its position in `City::nodes` (Ids.hpp).
            const CityNode& node = state_.city.nodes[state_.city.lots[index].node.value];
            const std::int64_t dx = node.x - centreX;
            const std::int64_t dz = node.z - centreZ;
            const std::int64_t distance = (dx * dx) + (dz * dz);
            if (best == noPlot || distance < bestDistance) {
                best = index;
                bestDistance = distance;
            }
        }
        return best;
    };

    // Starts a site: the building stands from the first night on the plot
    // the project consumes, its ground floor is the site office, and `daysLeft`
    // nights raise the floors above it. `reason` names the shortage that
    // started it, which the site's page shows and the save carries.
    const auto startSite = [this, &nearestLot](BuildingKind kind, EntityId owner,
                                               std::uint32_t floors,
                                               std::string_view prefix,
                                               ProjectReason reason) {
        const std::size_t plot = nearestLot();
        if (plot == noPlot) {
            return;
        }
        const CityLot lot = state_.city.lots[plot];
        const EntityId id = state_.ids.Allocate();
        Building building{
            .id = id,
            .name = std::format("{} {}", prefix, id.value),
            .kind = kind,
            .entrance = lot.node,
            .floors = 1,
            .shift = kind == BuildingKind::Workplace ? kSiteShift : Shift{},
            .rooms = {},
        };
        // The office of the site: the builders' room, owned by the site's
        // owner like its floors, asking no rent because nobody is charged
        // for an office. It is the only room on the ground floor, and
        // `ReviewUnits` (Units.cpp) leaves a room of its kind alone.
        building.rooms.push_back({
            .id = state_.ids.Allocate(),
            .name = "Site Office",
            .kind = RoomKind::SiteOffice,
            .floor = 0,
            .capacity = kSiteBuilders,
            .owner = owner,
        });
        // The new id is the highest there is, so both lists stay sorted.
        state_.city.buildings.push_back(std::move(building));
        state_.city.lots.erase(state_.city.lots.begin() +
                               static_cast<std::ptrdiff_t>(plot));
        state_.projects.push_back({
            .building = id,
            .owner = owner,
            .daysLeft = floors,
            // The ground floor is finished the night the site starts.
            .floorsDone = 1,
            // Nothing paid yet: the review advances the sites going up before
            // it starts this one, so its first night's supplies come at the
            // next midnight.
            .costSoFar = 0,
            .reason = reason,
        });
    };

    // One floor of a site: the floor above those standing, of the class the
    // height the site will reach asks for, holding homes or workspaces.
    const auto raiseFloor = [this](Building& building, Project& project) {
        const std::uint32_t floor = building.floors;
        // The height the site will reach: the floors standing plus the
        // nights it has left, one floor a night.
        const std::uint32_t floors = building.floors + project.daysLeft;
        building.floors += 1;
        ++project.floorsDone;
        if (building.kind == BuildingKind::Habitat) {
            const ApartmentClass kind = HomeClassFor(floor, floors);
            const std::uint32_t perFloor = ApartmentsPerFloor(kind);
            for (std::uint32_t index = 0; index != perFloor; ++index) {
                building.rooms.push_back({
                    .id = state_.ids.Allocate(),
                    .name = perFloor == 1 ? std::format("{} {}", HomeLabel(kind), floor)
                          : std::format("{} {}-{:02}", HomeLabel(kind), floor, index + 1),
                    .kind = RoomKind::Apartment,
                    .floor = floor,
                    .capacity = Sleeps(kind),
                    .rent = (kBaseRent + floor) * RentPercent(kind) / 100,
                    .owner = project.owner,
                    .apartmentClass = kind,
                });
            }
        } else {
            for (std::uint32_t index = 0; index != kWorkspacesPerFloor; ++index) {
                building.rooms.push_back({
                    .id = state_.ids.Allocate(),
                    .name = std::format("Work Floor {}",
                                        ((floor - 1) * kWorkspacesPerFloor) + index + 1),
                    .kind = RoomKind::Workspace,
                    .floor = floor,
                    .capacity = kWorkspacePlaces,
                    .owner = project.owner,
                });
            }
        }
    };

    // The shortage that decides what to raise, read off the saved history: the
    // last `kStreakNights` days each. The homes a past day was counted over are
    // not saved, so every day of the streak is measured against the homes the
    // city has now - which is what `SetPrices` does with the market tonight.
    // The market's homes, counted as `SetPrices` (Units.cpp) counts them: the
    // four classes from `ApartmentClass::Studio` up, a government dorm bed
    // being off the market, and `DailyStats::residentsByClass` from the same
    // classes being who lives in them. A home two robots share therefore reads
    // as one vacancy fewer than it is; the share is set with room for that.
    std::array<std::int64_t, 4> marketHomes{};
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const std::size_t kind = static_cast<std::size_t>(apartment.room->apartmentClass);
        if (kind < 2) {
            // `None` and `Dorm`: not homes the market sells.
            continue;
        }
        ++marketHomes.at(kind - 2);
    }
    const std::int64_t market = marketHomes[0] + marketHomes[1] + marketHomes[2] + marketHomes[3];
    const auto homesShort = [this, market]() {
        if (state_.history.size() < kStreakNights || market == 0) {
            return false;
        }
        const std::size_t first = state_.history.size() - kStreakNights;
        for (std::size_t day = first; day != state_.history.size(); ++day) {
            const DailyStats& stats = state_.history[day];
            // Robots living in the market's homes: `residentsByClass` starts at
            // the dorm beds, so the market is the entries after it.
            std::int64_t lived = 0;
            for (std::size_t index = 1; index != stats.residentsByClass.size(); ++index) {
                lived += stats.residentsByClass.at(index);
            }
            const std::int64_t vacant = market > lived ? market - lived : 0;
            if (vacant * 100 >= kMarketVacancyPercent * market) {
                return false;
            }
        }
        return true;
    };
    const auto jobsShort = [this]() {
        if (state_.history.size() < kStreakNights) {
            return false;
        }
        const std::size_t first = state_.history.size() - kStreakNights;
        for (std::size_t day = first; day != state_.history.size(); ++day) {
            const DailyStats& stats = state_.history[day];
            // `DailyStats::unemployed` counts the robots with no workplace, as
            // `ReviewUnits` counts them when it decides whether to reopen a
            // unit; `population` is the robots that night.
            if (stats.unemployed * 100 <= kUnemploymentPercent * stats.population) {
                return false;
            }
        }
        return true;
    };
    // The corporation that owns a new workplace: the first of industry, the
    // order `Economy::organizations` keeps.
    const auto industryEmployer = [this]() -> EntityId {
        for (const Organization& organization : state_.economy.organizations) {
            if (organization.kind == OrganizationKind::Corporation &&
                organization.sector == Sector::Industry) {
                return organization.id;
            }
        }
        return EntityId{};
    };

    const bool homesShortNow = homesShort();
    const bool jobsShortNow = jobsShort();

    // The sites going up: pay the supplies the site costs, raise a floor,
    // and end the ones whose nights are used up.
    bool cityChanged = false;
    for (Project& project : state_.projects) {
        Organization* owner = MutableOrganization(project.owner);
        if (owner != nullptr) {
            // The site's own books count what its owner was charged tonight:
            // `PaySupplies` (Money.cpp) moves the owner's sector's share of
            // the night's ask - half for the housing trust, nothing for an
            // industry corporation - out of the owner's credits, and those
            // credits are the amount added up here. Counting the difference
            // the call made, rather than the ask, keeps the site's cost equal
            // to money that really moved (rule 2: no new accounting path,
            // nothing counted twice).
            const std::int64_t creditsBefore = owner->credits;
            PaySupplies(*owner, kSiteSuppliesNight);
            project.costSoFar += creditsBefore - owner->credits;
        }
        if (project.daysLeft == 0) {
            // A save may hold a site an older game left finished: nothing
            // is left to raise, and the review below drops it.
            continue;
        }
        Building* building = buildingFor(project.building);
        if (building == nullptr) {
            // ValidateWorldState refuses a project without its building.
            continue;
        }
        raiseFloor(*building, project);
        --project.daysLeft;
        cityChanged = true;
    }
    // A site whose nights are used up: its office becomes an ordinary empty
    // unit (step D3d), which `ReviewUnits` may then reopen as a venue or a
    // workspace like any other. `CloseUnit` (Units.cpp) is the existing way
    // to close one: it lays off the builders as a closing venue lays off its
    // staff - workplace cleared, junior, no performance, no commute planned
    // - renames the room "Empty Unit" and records the closing. A room may
    // change kind only when nobody stands in it or is on the way to it, and
    // the layoff is what keeps that true here: the review runs at midnight,
    // when a site's 07:00-to-15:00 shift is over and no robot is walking to
    // its office or standing in it, so a builder's only link to the office
    // is the departure it still plans that morning, and clearing `commuteAt`
    // cancels it. The projects list is walked in its saved order and the
    // robots in id order, so the same city closes the same offices on every
    // machine (rule 1).
    for (const Project& project : state_.projects) {
        if (project.daysLeft != 0) {
            continue;
        }
        Building* building = buildingFor(project.building);
        if (building == nullptr) {
            // ValidateWorldState refuses a project without its building.
            continue;
        }
        for (Room& room : building->rooms) {
            if (room.kind != RoomKind::SiteOffice) {
                continue;
            }
            CloseUnit(room);
            cityChanged = true;
            break;
        }
    }
    std::erase_if(state_.projects,
                  [](const Project& project) { return project.daysLeft == 0; });

    // Then the new sites. Homes first: the habitat takes the plot nearest
    // the middle, and a workplace the next one, so the two never share a
    // corner. The housing trust owns the homes nobody else owns; an
    // industry corporation owns the jobs.
    if (homesShortNow && housing_.IsValid()) {
        startSite(BuildingKind::Habitat, housing_, kHabitatFloors, "Hab Block",
                  ProjectReason::HomesShort);
        cityChanged = true;
    }
    if (jobsShortNow) {
        const EntityId employer = industryEmployer();
        if (employer.IsValid()) {
            startSite(BuildingKind::Workplace, employer, kWorkplaceFloors, "Work Yard",
                      ProjectReason::JobsShort);
            cityChanged = true;
        }
    }

    if (cityChanged) {
        // Rooms were added, and buildings: the index of rooms by kind and
        // the views of the city must follow.
        rooms_ = RoomIndex(state_.city);
        ++cityVersion_;
    }
}

}  // namespace mechapunk::sim
