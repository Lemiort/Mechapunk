#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mechapunk/sim/Chronicle.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

// The rows of every history list the game shows: the chronicle entries about
// one robot, room, building or organization, and the filters the Chronicle
// tab's buttons and search box hold. Plain functions over a `WorldFrame`, so
// tests can check what a page lists without the game window (as
// `app/OrganizationLists.cpp` does for organization pages); the drawing code
// only draws what these return.
//
// Every list comes back newest first: the chronicle is saved oldest first,
// so reading it backwards is the order a player reads a story. The pointers
// are into `WorldFrame::chronicle`, which the frame keeps alive.

// Entries naming `robot`, as either of the two robots, newest first. A
// scrapped robot keeps its entries: the chronicle outlives the population
// on purpose, so this finds a departed robot's history too.
std::vector<const sim::ChronicleEntry*> EntriesAboutRobot(
    const WorldFrame& frame, sim::EntityId robot);

// Entries whose room is `room`, newest first. A unit that closed since keeps
// its entries, so a room's history outlives its opening hours.
std::vector<const sim::ChronicleEntry*> EntriesAboutRoom(
    const WorldFrame& frame, sim::EntityId room);

// Entries whose room is one of `building`'s rooms, newest first.
std::vector<const sim::ChronicleEntry*> EntriesAboutBuilding(
    const WorldFrame& frame, sim::EntityId building);

// Entries about `organization`: in a room it owns, or naming a robot that
// works for it today, newest first.
std::vector<const sim::ChronicleEntry*> EntriesAboutOrganization(
    const WorldFrame& frame, sim::EntityId organization);

// The six groups the Chronicle tab's filter buttons show. An entry belongs
// to the group its kind (or, for a `Remembered` entry, its memory) reads as
// in the game; a kind no button names belongs to none, so a button hides it.
enum class ChronicleGroup : std::uint8_t {
    People,  // scrapped, arriving in the city, friends and rivals forming
    Work,    // losing a job, a promotion to lead
    Homes,   // buying a home, an eviction
    Venues,  // a unit opening or closing
    Crime,   // robbery, arrest, gang fight, joining or leaving a gang
    Money,   // the central bank's emissions
};

// The group an entry answers to, or none when no button names its kind.
[[nodiscard]] std::optional<ChronicleGroup> GroupOf(
    const sim::ChronicleEntry& entry);

// What the Chronicle tab's controls hold. The default shows everything: no
// button on means every group, a day bound of 0 means no bound, and an empty
// `text` means no name was typed.
struct ChronicleFilter {
    std::optional<ChronicleGroup> group{};
    std::uint64_t fromDay{};  // Inclusive day numbers, as sim::DayNumber gives.
    std::uint64_t toDay{};
    std::string text{};  // A robot's name, case-insensitively, as a substring.
};

// The entries of `frame.chronicle` that pass `filter`, newest first. The name
// is looked up in `frame.directory`, so a search finds entries about a robot
// that has since been scrapped.
std::vector<const sim::ChronicleEntry*> FilterChronicle(
    const WorldFrame& frame, const ChronicleFilter& filter);

}  // namespace mechapunk
