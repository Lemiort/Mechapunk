#include "GangLists.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace mechapunk {

// All members of the gang, sorted by id.
std::vector<const RobotFrame*> MembersOf(const WorldFrame& frame, sim::EntityId gang) {
    std::vector<const RobotFrame*> result;
    for (const auto& robot : frame.robots) {
        if (robot.gang == gang) {
            result.push_back(&robot);
        }
    }
    std::sort(result.begin(), result.end(),
        [](const RobotFrame* a, const RobotFrame* b) {
            return a->id < b->id;
        });
    return result;
}

// Crimes remembered by the gang's own members, newest first.
std::vector<GangCrime> GangCrimes(const WorldFrame& frame, sim::EntityId gang) {
    std::vector<GangCrime> result;
    if (frame.social == nullptr) {
        return result;
    }
    // A frame publishes bonds and memories in the order of `robots`, so the
    // two lists are walked in parallel and each memory is read under the
    // robot that owns it; only members of `gang` contribute.
    const std::size_t count = std::min(frame.robots.size(), frame.social->size());
    for (std::size_t i = 0; i < count; ++i) {
        const RobotFrame& member = frame.robots[i];
        if (member.gang != gang) {
            continue;
        }
        for (const sim::Memory& memory : (*frame.social)[i].memories) {
            GangCrime crime;
            crime.tick = memory.tick;
            crime.member = member.id;
            crime.location = memory.room;
            switch (memory.kind) {
                case sim::MemoryKind::RobbedSomeone:
                    crime.kind = CrimeKind::Robbery;
                    crime.perpetrator = member.id;
                    crime.victim = memory.robot;
                    crime.text = "robbed";
                    break;
                case sim::MemoryKind::WasRobbed:
                    crime.kind = CrimeKind::WasRobbed;
                    crime.perpetrator = memory.robot;
                    crime.victim = member.id;
                    crime.text = "was robbed";
                    break;
                case sim::MemoryKind::Arrested:
                    crime.kind = CrimeKind::Arrest;
                    // B15: an arrest now remembers the officer that made it,
                    // so the row says who did it and who was done to; a
                    // memory from a version 19 save recorded no officer
                    // (the game never knew which one it was), and that row
                    // keeps an empty perpetrator.
                    crime.perpetrator = memory.robot;
                    crime.victim = member.id;
                    crime.text = "arrested";
                    break;
                default:
                    continue;
            }
            result.push_back(std::move(crime));
        }
    }
    // Newest first; rows of one minute fall back to member id, so the order
    // never depends on the memory stores' order.
    std::sort(result.begin(), result.end(),
        [](const GangCrime& a, const GangCrime& b) {
            if (a.tick != b.tick) {
                return a.tick > b.tick;
            }
            return a.member < b.member;
        });
    return result;
}

// Rival gangs' robots whose current room is a room of the gang's turf building.
std::vector<const RobotFrame*> RivalsInTurf(const WorldFrame& frame,
    sim::EntityId gang) {
    std::vector<const RobotFrame*> result;
    if (frame.gangs == nullptr || frame.city == nullptr) {
        return result;
    }
    const sim::Gang* self = nullptr;
    for (const sim::Gang& g : *frame.gangs) {
        if (g.id == gang) {
            self = &g;
            break;
        }
    }
    if (self == nullptr || !self->turf.IsValid()) {
        return result;  // No gang or no turf: nobody can be on it.
    }

    // The gangs that stand as rivals in this gang's relations.
    std::vector<sim::EntityId> rivalIds;
    for (const sim::GangRelation& rel : self->relations) {
        if (rel.stance == sim::Stance::Rival) {
            rivalIds.push_back(rel.gang);
        }
    }
    if (rivalIds.empty()) {
        return result;
    }

    // Room ids of the turf building; sorted for a binary search per robot.
    std::vector<std::uint64_t> turfRooms;
    for (const sim::Building& building : frame.city->buildings) {
        if (building.id != self->turf) {
            continue;
        }
        for (const sim::Room& room : building.rooms) {
            turfRooms.push_back(room.id.value);
        }
        break;
    }
    if (turfRooms.empty()) {
        return result;
    }
    std::sort(turfRooms.begin(), turfRooms.end());

    for (const RobotFrame& robot : frame.robots) {
        if (!robot.gang.IsValid()) {
            continue;
        }
        if (std::find(rivalIds.begin(), rivalIds.end(), robot.gang)
            == rivalIds.end()) {
            continue;  // Not a rival of this gang (own members and neutrals stay out).
        }
        if (std::binary_search(turfRooms.begin(), turfRooms.end(),
            robot.location.value)) {
            result.push_back(&robot);
        }
    }
    std::sort(result.begin(), result.end(),
        [](const RobotFrame* a, const RobotFrame* b) {
            return a->id < b->id;
        });
    return result;
}

// The gang whose turf this building is, if any.
std::optional<sim::EntityId> TurfOf(const WorldFrame& frame, sim::EntityId building) {
    if (!frame.gangs) {
        return std::nullopt;
    }
    for (const auto& gang : *frame.gangs) {
        if (gang.turf == building) {
            return gang.id;
        }
    }
    return std::nullopt;
}

}  // namespace mechapunk
