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

Robot* World::MutableRobot(EntityId id) noexcept {
    return const_cast<Robot*>(FindRobot(id));
}

void SetCredits(WorldState& state, Robot& robot, std::int64_t credits) {
    const std::int64_t change = credits - robot.credits;
    if (change >= 0) {
        state.economy.created += change;
    } else {
        state.economy.destroyed -= change;
    }
    robot.credits = credits;
}

std::expected<void, std::string> ValidateEconomy(const WorldState& state) {
    const Economy& economy = state.economy;
    const auto fail = [](std::string message) { return std::unexpected(std::move(message)); };
    if (!std::ranges::is_sorted(economy.organizations, {},
                                [](const Organization& each) { return each.id.value; })) {
        return fail("Organizations are not sorted by id");
    }
    const auto organization = [&](EntityId id) -> const Organization* {
        const auto found = std::ranges::lower_bound(economy.organizations, id.value, {},
                                                    [](const Organization& each) { return each.id.value; });
        return found != economy.organizations.end() && found->id == id ? &*found : nullptr;
    };
    const auto robot = [&](EntityId id) {
        const auto found = std::ranges::lower_bound(state.robots, id.value, {},
                                                    [](const Robot& each) { return each.id.value; });
        return found != state.robots.end() && found->id == id;
    };
    std::int64_t money = economy.supplyMarket;
    for (const Organization& each : economy.organizations) {
        money += each.credits;
        if (each.credits < 0) {
            return fail(std::format("{} has {} credits", each.name, each.credits));
        }
        for (const Share& share : each.owners) {
            if (!robot(share.owner) && organization(share.owner) == nullptr) {
                return fail(std::format("{} has a missing owner", each.name));
            }
        }
        if (each.kind != OrganizationKind::CentralBank && organization(each.bank) == nullptr) {
            return fail(std::format("{} has no bank", each.name));
        }
    }
    for (const Robot& each : state.robots) {
        money += each.credits;
        const Organization* bank = organization(each.bank);
        if (bank == nullptr || bank->kind != OrganizationKind::Bank || each.savings < 0) {
            return fail(std::format("Robot '{}' has no bank, or negative savings", each.name));
        }
    }
    if (money != economy.created - economy.destroyed) {
        return fail(std::format("The city holds {} credits, but {} were created and {} destroyed",
                                money, economy.created, economy.destroyed));
    }
    for (const Building& building : state.city.buildings) {
        for (const Room& room : building.rooms) {
            if (!robot(room.owner) && organization(room.owner) == nullptr) {
                return fail(std::format("{} in {} has no owner", room.name, building.name));
            }
        }
    }
    std::unordered_set<std::uint64_t> borrowers;
    for (const Loan& loan : economy.loans) {
        const Organization* lender = organization(loan.lender);
        if (lender == nullptr || lender->kind == OrganizationKind::Corporation || loan.owed <= 0 ||
            !borrowers.insert(loan.borrower.value).second ||
            (!robot(loan.borrower) && organization(loan.borrower) == nullptr)) {
            return fail(std::format("Loan {} is not valid", loan.id.value));
        }
    }
    return {};
}

}  // namespace mechapunk::sim
