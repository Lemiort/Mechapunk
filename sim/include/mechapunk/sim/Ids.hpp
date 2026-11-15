#pragma once

#include <compare>
#include <cstdint>

namespace mechapunk::sim {

// Stable identifier of a simulated entity (building, robot, ...). Ids are
// allocated from one counter per world and never reused, so references
// between entities stay valid across saving, loading and removals.
// Zero is never allocated and means "none".
struct EntityId {
    std::uint64_t value{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    friend constexpr auto operator<=>(const EntityId&,
                                      const EntityId&) = default;
};

// Identifier of a city graph node. Its value equals the node's position in
// City::nodes, which loading validates.
struct NodeId {
    std::uint32_t value{};

    friend constexpr auto operator<=>(const NodeId&, const NodeId&) = default;
};

}  // namespace mechapunk::sim
