#include "OrganizationLists.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace mechapunk {

std::vector<OwnedRoom> RoomsOwnedBy(const EconomyFrame& economy,
                                    const sim::RoomIndex& rooms,
                                    sim::EntityId owner) {
    std::vector<OwnedRoom> result;
    if (economy.owners == nullptr) {
        return result;
    }
    for (const auto& [room_id_value, room_owner] : *economy.owners) {
        if (room_owner == owner) {
            const auto ref = rooms.Find(sim::EntityId{room_id_value});
            if (ref) {
                result.push_back(OwnedRoom{
                    .building = ref->building->id,
                    .floor = ref->room->floor,
                    .room = ref->room->id});
            }
        }
    }
    // Sort by building id, then floor, then room id for stable display order.
    std::sort(result.begin(), result.end(), [](const OwnedRoom& a, const OwnedRoom& b) {
        if (a.building != b.building) return a.building < b.building;
        if (a.floor != b.floor) return a.floor < b.floor;
        return a.room < b.room;
    });
    return result;
}

std::vector<const RobotFrame*> WorkersOf(const WorldFrame& frame, sim::EntityId employer) {
    std::vector<const RobotFrame*> workers;
    if (frame.economy == nullptr || frame.economy->owners == nullptr || !employer.IsValid()) {
        return workers;
    }
    // A robot's workspace is a room; its employer is whoever owns that room.
    for (const auto& robot : frame.robots) {
        if (robot.workspace.IsValid() && OwnerOf(*frame.economy, robot.workspace) == employer) {
            workers.push_back(&robot);
        }
    }
    // Sort by id so the order is stable across frames.
    std::sort(workers.begin(), workers.end(), [](const RobotFrame* a, const RobotFrame* b) {
        return a->id < b->id;
    });
    return workers;
}

std::vector<const sim::Loan*> LoansFrom(const EconomyFrame& economy, sim::EntityId bank) {
    std::vector<const sim::Loan*> loans;
    for (const auto& loan : economy.loans) {
        if (loan.lender == bank) {
            loans.push_back(&loan);
        }
    }
    // Largest debt first, then by borrower id.
    std::sort(loans.begin(), loans.end(), [](const sim::Loan* a, const sim::Loan* b) {
        if (a->owed != b->owed) return a->owed > b->owed;  // larger first
        return a->borrower < b->borrower;                   // then by id
    });
    return loans;
}

}  // namespace mechapunk
