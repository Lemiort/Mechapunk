// World::ReviewMetroAuthority: a growing town gets its Metro Authority with its
// first metro line, and each line a depot with a crew room, where its drivers
// change shifts. The authority's capital is lent by the central bank, which is
// where the money comes from. A depot is raised on a free corner of an
// intersection on the line's street, the one nearest its first station; the
// view puts the trains' shed on its roof and turns a track off the line into it.
// While the line's street has no free corner the review tries again the next
// night: a ring of streets the town grows adds corners at both ends of it.
// Its drivers are hired as any staff are; robots out of work and newcomers
// take an empty place in a crew room before any other job (Careers.cpp,
// LifeCycle.cpp).

#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <format>

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

    // A depot for every line that has none; lines numbered anew when another
    // opens keep theirs (LineDepots).
    const std::vector<EntityId> depots = LineDepots(state_.city, metro_);
    auto built = static_cast<std::size_t>(std::ranges::count_if(state_.city.buildings, IsDepot));
    bool changed = false;
    for (std::size_t line = 0; line < lines.size(); ++line) {
        if (depots[line].IsValid()) {
            continue;
        }
        const CityNode& first =
            state_.city.nodes[state_.city.stations[lines[line].stations.front()].node.value];
        auto lot = state_.city.lots.end();
        std::int64_t nearest = 0;
        for (auto plot = state_.city.lots.begin(); plot != state_.city.lots.end(); ++plot) {
            // A node id is its position in City::nodes (Ids.hpp).
            const CityNode& at = state_.city.nodes[plot->node.value];
            if (!OnLineStreet(state_.city, lines[line], plot->node)) {
                continue;
            }
            const std::int64_t distance = std::abs(static_cast<std::int64_t>(at.x) - first.x) +
                                          std::abs(static_cast<std::int64_t>(at.z) - first.z);
            if (lot == state_.city.lots.end() || distance < nearest) {
                lot = plot;
                nearest = distance;
            }
        }
        if (lot == state_.city.lots.end()) {
            continue;  // No free corner on the line's street tonight.
        }
        // A node's lowest free corner, which the view gives the newest of its
        // buildings, as construction takes its plots.
        const NodeId node = lot->node;
        state_.city.lots.erase(lot);
        const EntityId id = state_.ids.Allocate();
        Building depot{.id = id,
                       .name = std::format("Metro Depot {}", ++built),
                       .kind = BuildingKind::Workplace,
                       .entrance = node,
                       .floors = 1,
                       .style = StyleOf(BuildingKind::Workplace, id, 1),
                       .shift = {},
                       .rooms = {}};
        Room crew{.id = state_.ids.Allocate(),
                  .name = std::format("Crew Room {}", built),
                  .kind = RoomKind::CrewRoom,
                  .floor = 0,
                  .capacity = std::max<std::uint32_t>(2, lines[line].trains)};
        crew.owner = transit_;
        crew.pricePercent = 100;
        depot.rooms.push_back(std::move(crew));
        // The new id is the highest there is, so the list stays sorted.
        state_.city.buildings.push_back(std::move(depot));
        changed = true;
    }
    if (changed) {
        rooms_ = RoomIndex(state_.city);
        ++cityVersion_;
    }
}

}  // namespace mechapunk::sim
