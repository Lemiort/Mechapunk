// Who lives where, and the nightly rent collection.

#include "mechapunk/sim/World.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Names.hpp"
#include "mechapunk/sim/Venues.hpp"

#include "WorldInternal.hpp"

namespace mechapunk::sim {

std::int64_t World::RentOf(const Robot& robot) const {
    // Tenants share the rent; owners pay the upkeep of their home.
    const auto apartment = rooms_.Find(robot.apartment);
    if (!apartment) {
        return 0;
    }
    if (apartment->room->owner == robot.id) {
        return UpkeepOf(*apartment->room);
    }
    const std::int64_t sharers = std::max<std::uint32_t>(1, ResidentsOf(robot.apartment));
    return (RentFor(*apartment->room) + sharers - 1) / sharers;
}

std::uint32_t World::ResidentsOf(EntityId apartment) const {
    const auto found = residents_.find(apartment.value);
    return found != residents_.end() ? found->second : 0;
}

void World::CountResidents() {
    residents_.clear();
    for (const Robot& robot : state_.robots) {
        ++residents_[robot.apartment.value];
    }
}

void World::CollectRent() {
    // Takes in a payment: revenue for an organization, which buys supplies
    // with part of it.
    const auto received = [&](EntityId payee, std::int64_t amount) {
        if (Organization* organization = MutableOrganization(payee)) {
            organization->today.revenue += amount;
            PaySupplies(*organization, amount);
        }
    };
    std::unordered_set<std::uint64_t> settled;
    for (Robot& robot : state_.robots) {
        const auto home = rooms_.Find(robot.apartment);
        if (!home) {
            continue;
        }
        const Room& room = *home->room;
        const bool owns = room.owner == robot.id;
        const EntityId payee = owns ? utilities_ : room.owner;
        const std::int64_t owedBefore = robot.rentOwed;
        // Anything owed first, then tonight's rent or upkeep.
        const std::int64_t due = RentOf(robot) + robot.rentOwed;
        robot.rentOwed = 0;
        const std::int64_t paid = PayBill(robot, payee, due);
        received(payee, paid);
        (owns ? state_.today.upkeep : state_.today.rent) += paid;
        if (owns) {
            settled.insert(room.id.value);
        }
        if (owedBefore == 0 && robot.rentOwed > 0) {
            Remember(robot, MemoryKind::FellBehindOnRent, {}, robot.apartment);
        } else if (owedBefore > 0 && robot.rentOwed == 0) {
            Remember(robot, MemoryKind::PaidOffDebt, {}, robot.apartment);
        }
        if (paid > 0) {
            Emit({.tick = state_.tick,
                  .kind = EventKind::RentPaid,
                  .robot = robot.id,
                  .room = robot.apartment,
                  .credits = paid});
        }
        if (robot.rentOwed > 0) {
            Emit({.tick = state_.tick,
                  .kind = EventKind::RentMissed,
                  .robot = robot.id,
                  .room = robot.apartment,
                  .credits = robot.rentOwed});
        }
    }
    // Landlords pay the upkeep of the apartments they let.
    for (const Robot& tenant : state_.robots) {
        const auto home = rooms_.Find(tenant.apartment);
        if (!home || !settled.insert(home->room->id.value).second) {
            continue;
        }
        const std::int64_t upkeep = UpkeepOf(*home->room);
        const EntityId owner = home->room->owner;
        std::int64_t paid = upkeep;
        if (Organization* organization = MutableOrganization(owner)) {
            organization->today.upkeep += upkeep;
            PayOrBorrow(*organization, utilities_, upkeep);
        } else if (Robot* landlord = MutableRobot(owner)) {
            paid = PayBill(*landlord, utilities_, upkeep);
        }
        received(utilities_, paid);
        state_.today.upkeep += paid;
    }
    // Owners pay City Hall's property tax on every home they own.
    Organization* city = MutableOrganization(cityHall_);
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const Room& room = *apartment.room;
        if (room.owner == cityHall_ || city == nullptr) {
            continue;
        }
        const std::int64_t tax = RentFor(room) * static_cast<std::int64_t>(state_.settings.economy.propertyTaxPercent) / 100;
        std::int64_t paid = tax;
        if (Organization* owner = MutableOrganization(room.owner)) {
            owner->today.taxes += tax;
            PayOrBorrow(*owner, cityHall_, tax);
        } else if (Robot* robot = MutableRobot(room.owner)) {
            paid = PayBill(*robot, cityHall_, tax);
        }
        city = MutableOrganization(cityHall_);
        city->today.revenue += paid;
        state_.today.propertyTax += paid;
    }
}

}  // namespace mechapunk::sim
