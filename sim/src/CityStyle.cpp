// A building's architectural style: the massing each kind of building may
// have, and the hash of its own data that picks one. Purely decorative - it
// changes no room, no job, no price and no walk - and derived from facts a
// save already holds, so `Migrations.cpp` can read the style of an old
// building exactly as the generator reads the style of a new one. The
// streets, buildings and rooms of the city are in City.cpp.

#include "mechapunk/sim/City.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mechapunk::sim {

namespace {

// The massing a building may have, each with the floors it needs: a setback
// needs a top third to pull in, a step needs two of them, a plinth two floors
// of base under a narrower shaft, a crown a tall block to crown it, and the
// two roof styles need nothing but the block itself, since a one floor
// workshop with roof lights or a one floor house with a pitched roof is
// ordinary. A habitat never has the sawtooth lights of a workshop; a workplace
// never a gabled home roof nor a crowned tower. The order is the order the
// styles are counted in below, so it is part of the style a given building
// gets.
struct Massing {
    BuildingStyle style;
    std::uint32_t minFloors;
};
constexpr std::array<Massing, 7> kHabitatMassing{
    Massing{.style = BuildingStyle::Plain, .minFloors = 1},
    Massing{.style = BuildingStyle::Setback, .minFloors = 3},
    Massing{.style = BuildingStyle::Stepped, .minFloors = 6},
    Massing{.style = BuildingStyle::TwinTowers, .minFloors = 2},
    Massing{.style = BuildingStyle::Plinth, .minFloors = 3},
    Massing{.style = BuildingStyle::Gabled, .minFloors = 1},
    Massing{.style = BuildingStyle::Tower, .minFloors = 6},
};
constexpr std::array<Massing, 6> kWorkplaceMassing{
    Massing{.style = BuildingStyle::Plain, .minFloors = 1},
    Massing{.style = BuildingStyle::Setback, .minFloors = 3},
    Massing{.style = BuildingStyle::Stepped, .minFloors = 6},
    Massing{.style = BuildingStyle::TwinTowers, .minFloors = 2},
    Massing{.style = BuildingStyle::Plinth, .minFloors = 3},
    Massing{.style = BuildingStyle::Sawtooth, .minFloors = 1},
};

// A hash of the three facts the style is read from, written out rather than
// taken from `std::hash`: the number must be the same on every machine, and
// it must spread the styles along a street instead of clumping them.
std::uint64_t StyleHash(BuildingKind kind, EntityId id,
                        std::uint32_t floors) noexcept {
    std::uint64_t hash = id.value;
    hash = (hash ^ static_cast<std::uint64_t>(floors) * 0x9e3779b97f4a7c15ull) *
           0xbf58476dca4e78bcull;
    hash = (hash ^ static_cast<std::uint64_t>(kind) * 0x94d049bb5042821bull) *
           0xc2ac530986619681ull;
    // Fold the mixed high bits down: the pick below reads the low bits, and
    // multiplying alone leaves those barely changed.
    hash ^= hash >> 33;
    hash ^= hash >> 11;
    return hash;
}

}  // namespace

std::string_view StyleName(BuildingStyle style) noexcept {
    switch (style) {
        case BuildingStyle::Plain:
            return "plain block";
        case BuildingStyle::Setback:
            return "setback block";
        case BuildingStyle::Stepped:
            return "stepped block";
        case BuildingStyle::TwinTowers:
            return "pair of twin towers";
        case BuildingStyle::Plinth:
            return "tower on a plinth";
        case BuildingStyle::Gabled:
            return "gabled block";
        case BuildingStyle::Sawtooth:
            return "sawtooth yard";
        case BuildingStyle::Tower:
            return "crowned tower";
    }
    return "block";
}

// The style of a building, read off its id, its kind and its saved floor
// count. No random number is drawn, so a city's layout, its rooms and its
// story do not move when the styles are read, and one seed dresses the city
// the same way on every machine and after a save round trip.
BuildingStyle StyleOf(BuildingKind kind, EntityId id,
                      std::uint32_t floors) noexcept {
    const std::span<const Massing> allowed =
        kind == BuildingKind::Habitat ? std::span<const Massing>{kHabitatMassing}
                                      : std::span<const Massing>{kWorkplaceMassing};
    std::uint32_t room = 0;
    for (const Massing& massing : allowed) {
        if (floors >= massing.minFloors) {
            ++room;
        }
    }
    // A plain box needs one floor, which every building has, so `room` is
    // never zero and the modulo below is safe.
    std::uint32_t pick =
        static_cast<std::uint32_t>(StyleHash(kind, id, floors) % room);
    for (const Massing& massing : allowed) {
        if (floors < massing.minFloors) {
            continue;
        }
        if (pick == 0) {
            return massing.style;
        }
        --pick;
    }
    return BuildingStyle::Plain;
}

}  // namespace mechapunk::sim
