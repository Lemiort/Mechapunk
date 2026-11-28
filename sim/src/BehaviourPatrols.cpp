// An officer on shift: leaving its station to look over a venue that was
// robbed recently and coming back before the shift ends. The rest of a robot's
// day - acting, arriving, deciding, walking and commuting - stays in
// `Behaviour.cpp`; this file is the police half of it, moved out by topic to
// keep the `world` module inside its context budget (task L, step L3a).
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/World.hpp"

#include "WorldInternal.hpp"

namespace mechapunk::sim {

Tick World::NextPatrolAt(const Robot& robot) const {
    // On the next whole hour, but not before the shift starts.
    const Tick hour = state_.tick + (60 - (MinuteOfDay(state_.tick) % 60));
    return std::max(hour, robot.shiftStartsAt);
}

void World::Patrol(Robot& robot, Tick shiftEnd) {
    // Only officers at their station patrol, on the hour once the shift has
    // started, and only to a venue close by that they can walk to, look
    // round and get back from before the shift ends.
    const auto station = rooms_.Find(robot.workspace);
    if (!station || station->room->kind != RoomKind::PoliceStation ||
        robot.location != robot.workspace) {
        robot.nextActionAt = shiftEnd;
        return;
    }
    if (state_.tick >= robot.shiftStartsAt && MinuteOfDay(state_.tick) % 60 == 0) {
        std::vector<EntityId> candidates;
        for (const RoomKind kind : VenueKinds()) {
            for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
                const EntityId id = venue.room->id;
                const Tick there = TravelMinutes(robot.location, id);
                const Tick back = TravelMinutes(id, robot.location);
                if (there <= static_cast<Tick>(state_.settings.crime.patrolReachMinutes) &&
                    state_.tick + there + static_cast<Tick>(state_.settings.crime.patrolStayMinutes) + back <= shiftEnd) {
                    candidates.push_back(id);
                }
            }
        }
        // Sorted, so the pick below is deterministic.
        std::ranges::sort(candidates);
        // The chronicle keeps every `Robbed` event oldest first (task B
        // made the crimes notable, task C keeps them), so walking it back
        // from the newest entry stops at the first robbery older than the
        // lookback. The officer then goes to a venue on that list when one
        // is in reach: its robbers still stand in its room, which is where
        // `PatrolArrests` (B13) arrests a member that robbed in that hour.
        // Without this the arrest rule needs an officer to visit the crime
        // venue by luck out of ~484 venues, which gave 9 arrests in 40 days.
        std::vector<EntityId> crimeRooms;
        for (auto it = state_.chronicle.rbegin();
             it != state_.chronicle.rend() &&
             it->tick + static_cast<Tick>(state_.settings.crime.crimeLookbackMinutes) >= state_.tick;
             ++it) {
            if (it->kind == EventKind::Robbed && it->room.IsValid()) {
                crimeRooms.push_back(it->room);
            }
        }
        std::ranges::sort(crimeRooms);
        crimeRooms.erase(std::unique(crimeRooms.begin(), crimeRooms.end()),
                         crimeRooms.end());
        std::vector<EntityId> crimeVenues;
        std::ranges::set_intersection(candidates, crimeRooms,
                                      std::back_inserter(crimeVenues));
        const std::vector<EntityId>& pick =
            crimeVenues.empty() ? candidates : crimeVenues;
        if (!pick.empty()) {
            robot.patrol = pick[state_.random.UniformInt(
                0, static_cast<std::uint32_t>(pick.size() - 1))];
            StartWalk(robot, robot.patrol);
            return;
        }
    }
    robot.nextActionAt = std::min(NextPatrolAt(robot), shiftEnd);
}

}  // namespace mechapunk::sim
