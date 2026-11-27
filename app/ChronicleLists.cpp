// The chronicle's lists: which entries a robot, room, building or organization
// page shows, and which entries the Chronicle tab's filter buttons keep. The
// game draws these rows; the tests in tests/ChronicleListsTests.cpp check them
// without a window.

#include "ChronicleLists.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "OrganizationLists.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk {

namespace {

// Every list below is the frame's chronicle walked backwards, keeping the
// entries a predicate names: newest first, and stable, because the saved
// order is oldest first and no entry is visited twice.
template <typename Keep>
std::vector<const sim::ChronicleEntry*> Matching(const WorldFrame& frame, Keep keep) {
    std::vector<const sim::ChronicleEntry*> result;
    if (frame.chronicle == nullptr) {
        return result;
    }
    for (auto it = frame.chronicle->rbegin(); it != frame.chronicle->rend(); ++it) {
        if (keep(*it)) {
            result.push_back(&*it);
        }
    }
    return result;
}

// A name as the search box holds it: lower case, so "Kato" finds "Kato-9".
std::string Lower(std::string_view name) {
    std::string out{name};
    for (char& symbol : out) {
        symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
    }
    return out;
}

}  // namespace

std::vector<const sim::ChronicleEntry*> EntriesAboutRobot(
    const WorldFrame& frame, sim::EntityId robot) {
    if (!robot.IsValid()) {
        return {};
    }
    return Matching(frame, [&](const sim::ChronicleEntry& entry) {
        return entry.robot == robot || entry.otherRobot == robot;
    });
}

std::vector<const sim::ChronicleEntry*> EntriesAboutRoom(
    const WorldFrame& frame, sim::EntityId room) {
    if (!room.IsValid()) {
        return {};
    }
    return Matching(frame, [&](const sim::ChronicleEntry& entry) {
        return entry.room == room;
    });
}

std::vector<const sim::ChronicleEntry*> EntriesAboutBuilding(
    const WorldFrame& frame, sim::EntityId building) {
    if (!building.IsValid() || frame.city == nullptr) {
        return {};
    }
    // The building's room ids: membership only, so the unordered set cannot
    // decide an order (the guide's determinism rule is about iteration order).
    std::unordered_set<std::uint64_t> rooms;
    for (const auto& city_building : frame.city->buildings) {
        if (city_building.id != building) {
            continue;
        }
        for (const auto& room : city_building.rooms) {
            rooms.insert(room.id.value);
        }
        break;
    }
    if (rooms.empty()) {
        return {};
    }
    return Matching(frame, [&](const sim::ChronicleEntry& entry) {
        return rooms.contains(entry.room.value);
    });
}

std::vector<const sim::ChronicleEntry*> EntriesAboutOrganization(
    const WorldFrame& frame, sim::EntityId organization) {
    if (!organization.IsValid()) {
        return {};
    }
    // Rooms it owns, straight from the frame's owner map.
    std::unordered_set<std::uint64_t> owned_rooms;
    if (frame.economy != nullptr && frame.economy->owners != nullptr) {
        for (const auto& [room_id, owner] : *frame.economy->owners) {
            if (owner == organization) {
                owned_rooms.insert(room_id);
            }
        }
    }
    // Robots it employs now, by the same rule the organization page lists
    // its workers with: a robot's employer owns its workspace.
    std::unordered_set<std::uint64_t> workers;
    for (const auto* robot : WorkersOf(frame, organization)) {
        workers.insert(robot->id.value);
    }
    // The third rule needs neither set: an emission that funded the organization
    // names no room and no robot, only its account, so an organization with
    // neither a room nor a worker still has a money row to show. Ids come from
    // one counter per world, so a robot's own savings emission cannot answer
    // this rule by accident.
    return Matching(frame, [&](const sim::ChronicleEntry& entry) {
        return entry.account == organization || owned_rooms.contains(entry.room.value) ||
               workers.contains(entry.robot.value) || workers.contains(entry.otherRobot.value);
    });
}

std::optional<ChronicleGroup> GroupOf(const sim::ChronicleEntry& entry) {
    switch (entry.kind) {
        case sim::EventKind::Scrapped:
        case sim::EventKind::Arrived:
            return ChronicleGroup::People;
        case sim::EventKind::BoughtProperty:
            return ChronicleGroup::Homes;
        case sim::EventKind::UnitOpened:
        case sim::EventKind::UnitClosed:
            return ChronicleGroup::Venues;
        case sim::EventKind::Robbed:
        case sim::EventKind::Arrested:
        case sim::EventKind::Fought:
        case sim::EventKind::JoinedGang:
        case sim::EventKind::LeftGang:
        case sim::EventKind::GangFormed:
            // A robot joining a gang reads as the city's crime story, the
            // way the Crime tab already tells it; so does a gang forming.
            return ChronicleGroup::Crime;
        case sim::EventKind::MoneySupplyChanged:
        case sim::EventKind::BankFounded:
            return ChronicleGroup::Money;
        case sim::EventKind::CorporationFounded:
            return ChronicleGroup::Work;
        case sim::EventKind::StationOpened:
            return ChronicleGroup::Venues;
        case sim::EventKind::Remembered:
            // The chronicle keeps only five memories, and each reads as one
            // of the buttons: see sim/src/Chronicle.cpp.
            switch (entry.memory) {
                case sim::MemoryKind::BecameCloseFriends:
                case sim::MemoryKind::BecameRivals:
                    return ChronicleGroup::People;
                case sim::MemoryKind::LostJob:
                case sim::MemoryKind::Promoted:
                    return ChronicleGroup::Work;
                case sim::MemoryKind::Evicted:
                    return ChronicleGroup::Homes;
                default:
                    return std::nullopt;
            }
        default:
            return std::nullopt;
    }
}

std::vector<const sim::ChronicleEntry*> FilterChronicle(
    const WorldFrame& frame, const ChronicleFilter& filter) {
    const auto wanted = Lower(filter.text);
    return Matching(frame, [&](const sim::ChronicleEntry& entry) {
        if (filter.group) {
            const auto group = GroupOf(entry);
            if (!group || *group != *filter.group) {
                return false;
            }
        }
        const auto day = sim::DayNumber(entry.tick);
        if (filter.fromDay != 0 && day < filter.fromDay) {
            return false;
        }
        if (filter.toDay != 0 && day > filter.toDay) {
            return false;
        }
        if (!wanted.empty()) {
            // The entry names at most two robots; either name matching is
            // enough, and a name nobody has is simply no match.
            const auto matches = [&](sim::EntityId robot) {
                if (!robot.IsValid() || frame.directory == nullptr) {
                    return false;
                }
                const auto found = frame.directory->find(robot.value);
                return found != frame.directory->end() &&
                       Lower(found->second.name).find(wanted) != std::string::npos;
            };
            if (!matches(entry.robot) && !matches(entry.otherRobot)) {
                return false;
            }
        }
        return true;
    });
}

}  // namespace mechapunk
