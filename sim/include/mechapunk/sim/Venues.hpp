#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "mechapunk/sim/City.hpp"

namespace mechapunk::sim {

// A robot's needs, each from 0 (desperate) to 1000 (fully satisfied).
struct Needs {
    static constexpr std::int32_t kMax = 1000;

    std::int32_t charge{800};     // Battery.
    std::int32_t condition{800};  // Wear and tear; repaired at repair shops.
    std::int32_t social{800};     // Company of other robots.
    std::int32_t fun{800};        // Entertainment.
};

// Change of each need over ten minutes of an activity.
struct NeedEffects {
    std::int32_t charge{};
    std::int32_t condition{};
    std::int32_t social{};
    std::int32_t fun{};
};

// How a kind of venue works. These are game rules, not saved state: changing
// them changes behavior but never the save format.
struct VenueRules {
    std::string_view label;  // "bar", "noodle diner", ...
    // Opening hours as minutes of the day; a venue open past midnight closes
    // on a smaller minute than it opens. Equal values mean always open.
    std::uint32_t opensAt{};
    std::uint32_t closesAt{};
    std::uint32_t price{};  // Credits per visit, paid on arrival.
    // Added to the visiting robot's needs every ten minutes.
    NeedEffects effects;
    // Extra social per other guest every ten minutes, up to the cap.
    std::int32_t socialPerGuest{};
    std::int32_t socialGuestCap{};
    std::uint32_t minVisitMinutes{};
    std::uint32_t maxVisitMinutes{};
    std::uint32_t minCapacity{};
    std::uint32_t maxCapacity{};
};

// Rules for a venue room; nothing for apartments and workspaces.
[[nodiscard]] const VenueRules* FindVenueRules(RoomKind kind) noexcept;
[[nodiscard]] bool IsVenue(RoomKind kind) noexcept;
[[nodiscard]] bool IsOpen(const VenueRules& rules,
                          std::uint32_t minuteOfDay) noexcept;
// Minutes from `minuteOfDay` until closing; a large number when always open.
[[nodiscard]] std::uint32_t MinutesUntilClose(const VenueRules& rules,
                                              std::uint32_t minuteOfDay) noexcept;

// Every venue kind, for generation.
[[nodiscard]] std::span<const RoomKind> VenueKinds() noexcept;
// Name ideas for venues of a kind, for generation.
[[nodiscard]] std::span<const std::string_view> VenueNames(
    RoomKind kind) noexcept;

}  // namespace mechapunk::sim
