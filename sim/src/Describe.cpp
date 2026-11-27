// Plain-text descriptions of events and rooms, for the log and the inspector.

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

std::string World::DescribeRoom(EntityId room) const {
    const auto found = rooms_.Find(room);
    if (!found) {
        return "somewhere";
    }
    const Room& where = *found->room;
    const Building& building = *found->building;
    if (const VenueRules* rules = FindVenueRules(where.kind)) {
        return std::format("{} ({}, {})", where.name, rules->label,
                           building.name);
    }
    return std::format("{}, {}", where.name, building.name);
}

std::string World::Describe(const SimEvent& event) const {
    const std::string_view who = NameOf(event.robot);
    const std::string when = FormatTime(event.tick);
    const std::string room = DescribeRoom(event.room);
    switch (event.kind) {
        case EventKind::Departed:
            if (event.metro) {
                const auto& stations = state_.city.stations;
                if (event.viaStation != event.toStation) {
                    return std::format(
                        "{}  {} left {} for {} ({} min, by metro from {} to {}, "
                        "changing at {})",
                        when, who, room, DescribeRoom(event.otherRoom), event.minutes,
                        stations.at(event.fromStation).name,
                        stations.at(event.toStation).name,
                        stations.at(event.viaStation).name);
                }
                return std::format(
                    "{}  {} left {} for {} ({} min, by metro from {} to {})",
                    when, who, room, DescribeRoom(event.otherRoom),
                    event.minutes, stations.at(event.fromStation).name,
                    stations.at(event.toStation).name);
            }
            return std::format("{}  {} left {} for {} ({} min walk)", when, who,
                               room, DescribeRoom(event.otherRoom),
                               event.minutes);
        case EventKind::ClockedIn:
            return event.minutes == 0
                       ? std::format("{}  {} clocked in at {}", when, who, room)
                       : std::format("{}  {} clocked in at {}, {} min late",
                                     when, who, room, event.minutes);
        case EventKind::ShiftEnded:
            return std::format("{}  {} finished the shift at {}, earned {} cr",
                               when, who, room, event.credits);
        case EventKind::ArrivedAtVenue:
            return std::format(
                "{}  {} arrived at {}, paid {} cr, {} other{} there, staying "
                "{} min",
                when, who, room, event.credits, event.others,
                event.others == 1 ? "" : "s", event.minutes);
        case EventKind::ArrivedHome:
            return std::format("{}  {} got home to {}", when, who, room);
        case EventKind::RentPaid:
            return std::format("{}  {} paid {} cr rent for {}", when, who,
                               event.credits, room);
        case EventKind::RentMissed:
            return std::format("{}  {} is behind on rent, owes {} cr", when,
                               who, event.credits);
        case EventKind::Talked: {
            return std::format(
                "{}  {} {} {} about {} at {}", when, who,
                event.change < 0 ? "argued with" : "chatted with",
                NameOf(event.otherRobot), InterestName(event.topic), room);
        }
        case EventKind::Remembered: {
            return std::format(
                "{}  {} {}", when, who,
                MemoryText(event.memory, NameOf(event.otherRobot),
                           event.room.IsValid() ? room : std::string{}));
        }
        case EventKind::Scrapped:
            return std::format("{}  {} broke down for good and was scrapped",
                               when, who);
        case EventKind::Arrived:
            return std::format("{}  {} moved to the city, into {}", when, who,
                               room);
        case EventKind::Borrowed:
            return std::format("{}  {} borrowed {} cr", when, who, event.credits);
        case EventKind::BoughtProperty:
            return std::format("{}  {} bought {} for {} cr", when, who, room, event.credits);
        case EventKind::SoldProperty:
            return std::format("{}  {} sold {} for {} cr", when, who, room, event.credits);
        case EventKind::Inherited:
            return std::format("{}  {} inherited everything {} had", when, who,
                               NameOf(event.otherRobot));
        case EventKind::MoneySupplyChanged:
            return event.credits >= 0
                       ? std::format("{}  The central bank created {} cr for {} ({})", when,
                                     event.credits, AccountName(event.account),
                                     EmissionReasonName(event.emission))
                       : std::format("{}  The central bank destroyed {} cr from {} ({})", when,
                                     -event.credits, AccountName(event.account),
                                     EmissionReasonName(event.emission));
        case EventKind::UnitOpened:
            return std::format("{}  {} opened, run by {}", when, DescribeRoom(event.room),
                               AccountName(event.account));
        case EventKind::UnitClosed:
            return std::format("{}  A unit closed: {} stands empty", when,
                               DescribeRoom(event.room));
        case EventKind::StationOpened: {
            const auto index = static_cast<std::size_t>(event.credits);
            return std::format("{}  City Hall opened the metro station {}", when,
                               index < state_.city.stations.size()
                                   ? std::string_view{state_.city.stations[index].name}
                                   : std::string_view{"(unknown)"});
        }
        case EventKind::BankFounded:
            return std::format("{}  {} was founded, a new bank", when, AccountName(event.account));
        case EventKind::CorporationFounded:
            return std::format("{}  {} was founded", when, AccountName(event.account));
        case EventKind::GangFormed: {
            const auto gang = std::ranges::find(state_.gangs, event.account, &Gang::id);
            return std::format("{}  The {} formed", when,
                               gang != state_.gangs.end() ? std::string_view{gang->name}
                                                          : std::string_view{"(unknown gang)"});
        }
        case EventKind::Robbed:
            return std::format("{}  {} robbed {} (took {}) at {}", when, who,
                               NameOf(event.otherRobot), event.credits,
                               DescribeRoom(event.room));
        case EventKind::Arrested:
            // B15: an arrest names its officer: `robot` is the officer,
            // `otherRobot` the criminal. An entry migrated from a version
            // 19 save (step B15e swaps it over) knows no officer, so that
            // one keeps the old indefinite line rather than naming nobody.
            return event.robot.IsValid()
                       ? std::format("{}  {} arrested {} for robbing at {}",
                                     when, who, NameOf(event.otherRobot),
                                     DescribeRoom(event.room))
                       : std::format("{}  An officer arrested {} for robbing at {}",
                                     when, NameOf(event.otherRobot),
                                     DescribeRoom(event.room));
        case EventKind::Fought:
            return std::format("{}  Members of rival gangs fought at {}", when,
                               DescribeRoom(event.room));
        case EventKind::WeatherChanged:
            return event.change == 0
                       ? std::format("{}  The rain stopped", when)
                       : std::format("{}  {} for {} min", when,
                                     RainName(static_cast<std::uint32_t>(event.change)),
                                     event.minutes);
    }
    return std::format("{}  {}: unknown event", when, who);
}

}  // namespace mechapunk::sim
