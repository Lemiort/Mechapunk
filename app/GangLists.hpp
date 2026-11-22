#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SimulationHost.hpp"

namespace mechapunk {

// Plain functions for gang data in the inspector, tested in layout_tests.
//
// MembersOf: all robots in a gang, sorted by id.
// GangCrimes: the crimes a gang's members remember, newest first.
// RivalsInTurf: rival gangs' robots standing in the gang's turf building.
// TurfOf: the gang whose turf a building is, if any.

// What a crime row says the member did or suffered.
enum class CrimeKind : std::uint8_t {
    Robbery,   // The member robbed another robot.
    WasRobbed, // Another robot robbed the member.
    Arrest,    // The member was arrested and fined.
};

struct GangCrime {
    std::string text;         // The act alone: "robbed", "was robbed", "arrested".
    sim::Tick tick{};         // When it happened; the page prints FormatTime(tick).
    CrimeKind kind{};
    sim::EntityId member;     // The gang member whose memory this is.
    sim::EntityId perpetrator;// Who did it: the member (robbery), the robber (was
                              //  robbed), the officer (arrest; empty for a memory
                              //  of a version 19 save, which recorded none).
    sim::EntityId victim;     // Who it was done to; the member (arrest).
    sim::EntityId location;   // Room where it happened, if the memory has one.
};

// All members of the gang, sorted by id.
std::vector<const RobotFrame*> MembersOf(const WorldFrame& frame, sim::EntityId gang);

// Crimes remembered by the gang's own members (never other robots'), newest
// first; rows with the same tick are in member id order.
std::vector<GangCrime> GangCrimes(const WorldFrame& frame, sim::EntityId gang);

// Rival gangs' robots whose current room is a room of this gang's turf building.
std::vector<const RobotFrame*> RivalsInTurf(const WorldFrame& frame, sim::EntityId gang);

// The gang whose turf this building is, if any.
std::optional<sim::EntityId> TurfOf(const WorldFrame& frame, sim::EntityId building);

}  // namespace mechapunk
