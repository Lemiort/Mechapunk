// World::ReviewMetroAuthority: a growing town gets its Metro Authority with its
// first metro line, and each line a crew room, where its drivers change shifts.
// The authority's capital is lent by the central bank, which is where the
// money comes from. A crew room is an empty ground-floor unit near the line's
// first station; it is taken only when nobody stands in it or walks to it, so
// when none is free the review tries again the next night.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>

#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

void World::ReviewMetroAuthority() {
    const std::vector<MetroLine>& lines = metro_.Lines();
    if (lines.empty() || state_.settings.metro.minBuildings == 0) {
        return;
    }
    Economy& economy = state_.economy;
    if (!transit_.IsValid()) {
        EntityId bank;
        for (const Organization& organization : economy.organizations) {
            if (organization.kind == OrganizationKind::Bank) {
                bank = organization.id;
                break;
            }
        }
        if (!bank.IsValid() || !cityHall_.IsValid() || !centralBank_.IsValid()) {
            return;
        }
        const EntityId id = state_.ids.Allocate();
        economy.organizations.push_back({.id = id,
                                         .name = "Metro Authority",
                                         .kind = OrganizationKind::Corporation,
                                         .sector = Sector::Transit,
                                         .owners = {{.owner = cityHall_, .shares = 100}},
                                         .bank = bank});
        IndexEconomy();
        // Capital as the first founding gives the authority, but lent: the
        // central bank creates it.
        Lend(centralBank_, id,
             1000 + (static_cast<std::int64_t>(state_.robots.size()) / 2));
        Emit({.tick = state_.tick, .kind = EventKind::CorporationFounded, .account = id});
    }

    // One crew room for every line, taken from the empty unit nearest the
    // line's first station.
    std::size_t crewRooms = rooms_.RoomsOfKind(RoomKind::CrewRoom).size();
    bool changed = false;
    for (std::size_t line = crewRooms; line < lines.size(); ++line) {
        const CityNode& first =
            state_.city.nodes[state_.city.stations[lines[line].stations.front()].node.value];
        Room* best = nullptr;
        std::int64_t bestDistance = std::numeric_limits<std::int64_t>::max();
        for (Building& building : state_.city.buildings) {
            if (IsUnderConstruction(state_.projects, building.id)) {
                continue;
            }
            const CityNode& at = state_.city.nodes[building.entrance.value];
            const std::int64_t dx = at.x - first.x;
            const std::int64_t dz = at.z - first.z;
            const std::int64_t distance = (dx * dx) + (dz * dz);
            for (Room& room : building.rooms) {
                if (room.kind != RoomKind::VacantStore || room.floor != 0 ||
                    distance >= bestDistance) {
                    continue;
                }
                const bool busy = std::ranges::any_of(state_.robots, [&](const Robot& robot) {
                    return robot.location == room.id || robot.destination == room.id;
                });
                if (!busy) {
                    best = &room;
                    bestDistance = distance;
                }
            }
        }
        if (best == nullptr) {
            break;  // No empty unit tonight: try again tomorrow.
        }
        best->kind = RoomKind::CrewRoom;
        best->name = std::format("Crew Room {}", line + 1);
        best->capacity = std::max<std::uint32_t>(2, lines[line].trains);
        best->owner = transit_;
        best->pricePercent = 100;
        best->visits = 0;
        best->quietNights = 0;
        changed = true;
    }
    if (changed) {
        rooms_ = RoomIndex(state_.city);
        ++cityVersion_;
    }
}

}  // namespace mechapunk::sim
