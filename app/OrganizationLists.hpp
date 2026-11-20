#pragma once

#include <cstdint>
#include <vector>

#include "mechapunk/sim/City.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// A room an account owns, with its building and floor.
struct OwnedRoom {
    sim::EntityId building;
    std::uint32_t floor{};
    sim::EntityId room;
};

// Every room `owner` owns, by building id, then floor, then room id.
std::vector<OwnedRoom> RoomsOwnedBy(const EconomyFrame& economy,
                                    const sim::RoomIndex& rooms,
                                    sim::EntityId owner);

// Every living robot whose job room `employer` owns, by robot id.
std::vector<const RobotFrame*> WorkersOf(const WorldFrame& frame,
                                         sim::EntityId employer);

// Robots and organizations owing `bank`, largest debt first, then by id.
std::vector<const sim::Loan*> LoansFrom(const EconomyFrame& economy,
                                        sim::EntityId bank);

// The owner of a room, or none. EconomyFrame::OwnerOf does the same but is
// defined in SimulationHost.cpp, which the layout library does not link.
[[nodiscard]] inline sim::EntityId OwnerOf(const EconomyFrame& economy,
                                           sim::EntityId room) {
    if (economy.owners == nullptr) {
        return {};
    }
    auto it = economy.owners->find(room.value);
    return it != economy.owners->end() ? sim::EntityId{it->second} : sim::EntityId{};
}

}  // namespace mechapunk
