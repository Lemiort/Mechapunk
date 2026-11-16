#include "Inspector.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
constexpr float kTitleSize = 28.0F;
constexpr float kTextSize = 20.0F;
constexpr std::size_t kNeedBarWidth = 20;
constexpr std::size_t kRecentEvents = 8;
constexpr std::size_t kListedOccupants = 12;
constexpr glm::vec4 kTitle{1.0F, 0.85F, 0.3F, 1.0F};
constexpr glm::vec4 kHeading{0.55F, 0.85F, 1.0F, 1.0F};
constexpr glm::vec4 kDim{0.6F, 0.62F, 0.7F, 1.0F};
constexpr glm::vec4 kWarning{1.0F, 0.45F, 0.35F, 1.0F};

class Panel {
public:
    explicit Panel(std::size_t maxChars) : maxChars_(maxChars) {}

    void Title(std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        lines_.push_back({.text = std::move(text),
                          .size = kTitleSize,
                          .color = kTitle});
    }

    void Line(std::string_view text, glm::vec4 color = {0.85F, 0.88F, 0.95F,
                                                        1.0F}) {
        // Wrap at spaces; continuation lines are indented.
        std::string_view rest = text;
        bool first = true;
        while (!rest.empty()) {
            const std::size_t room = first ? maxChars_ : maxChars_ - 2;
            std::size_t cut = rest.size();
            if (rest.size() > room) {
                cut = rest.rfind(' ', room);
                if (cut == std::string_view::npos || cut == 0) {
                    cut = room;
                }
            }
            lines_.push_back({.text = std::string(first ? "" : "  ") +
                                      std::string(rest.substr(0, cut)),
                              .size = kTextSize,
                              .color = color});
            rest.remove_prefix(cut);
            while (!rest.empty() && rest.front() == ' ') {
                rest.remove_prefix(1);
            }
            first = false;
        }
    }

    void Gap() { lines_.push_back({.text = "", .size = kTextSize * 0.5F}); }

    [[nodiscard]] std::vector<InspectorLine> Take() { return std::move(lines_); }

private:
    std::size_t maxChars_;
    std::vector<InspectorLine> lines_;
};

std::string FloorName(std::uint32_t floor) {
    return floor == 0 ? "ground floor" : std::format("floor {}", floor);
}

std::string RoomLabel(const CityView& city, sim::EntityId room) {
    const auto found = city.Rooms().Find(room);
    if (!found) {
        return "somewhere";
    }
    if (const sim::VenueRules* rules = sim::FindVenueRules(found->room->kind)) {
        return std::format("{} ({}, {})", found->room->name, rules->label,
                           found->building->name);
    }
    return std::format("{}, {}", found->room->name, found->building->name);
}

std::string_view KindLabel(sim::RoomKind kind) {
    if (const sim::VenueRules* rules = sim::FindVenueRules(kind)) {
        return rules->label;
    }
    return kind == sim::RoomKind::Apartment ? "apartment" : "workspace";
}

std::string Hours(std::uint32_t opens, std::uint32_t closes) {
    if (opens == closes) {
        return "open around the clock";
    }
    return std::format("open {}-{}", sim::FormatClock(opens),
                       sim::FormatClock(closes));
}

std::string Activity(const CityView& city, const RobotFrame& robot) {
    const std::string until = sim::FormatClock(sim::MinuteOfDay(robot.nextActionAt));
    switch (robot.activity) {
        case sim::RobotActivity::Resting:
            return std::format("Resting at home, leaves for work at {}", until);
        case sim::RobotActivity::Working:
            return std::format("Working at {} until {}",
                               RoomLabel(city, robot.location), until);
        case sim::RobotActivity::Visiting:
            return std::format("At {} until {}",
                               RoomLabel(city, robot.location), until);
        case sim::RobotActivity::Walking:
            return std::format("Walking to {}, arrives {}",
                               RoomLabel(city, robot.destination), until);
    }
    return "Unknown";
}

std::string NeedBar(std::string_view name, std::int32_t value) {
    const auto filled = static_cast<std::size_t>(
        std::clamp(value, 0, sim::Needs::kMax) * kNeedBarWidth /
        sim::Needs::kMax);
    return std::format("{:<10}[{}{}] {}", name, std::string(filled, '#'),
                       std::string(kNeedBarWidth - filled, '.'), value);
}

// Robots in each room right now, in id order.
std::map<std::uint64_t, std::vector<const RobotFrame*>> Occupants(
    const WorldFrame& frame) {
    std::map<std::uint64_t, std::vector<const RobotFrame*>> occupants;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            occupants[robot.location.value].push_back(&robot);
        }
    }
    return occupants;
}

std::string_view Name(const SimulationHost& host, sim::EntityId robot) {
    const RobotInfo* info = host.FindRobotInfo(robot);
    return info != nullptr ? std::string_view{info->name} : "unknown robot";
}

void DescribeRobot(Panel& panel, const RobotFrame& robot,
                   const CityView& city, const SimulationHost& host) {
    const RobotInfo* info = host.FindRobotInfo(robot.id);
    panel.Title(std::string{Name(host, robot.id)});
    panel.Line(Activity(city, robot));
    panel.Gap();
    const auto need = [&](std::string_view name, std::int32_t value) {
        panel.Line(NeedBar(name, value), value < 250 ? kWarning : glm::vec4{
                                                          0.85F, 0.88F, 0.95F,
                                                          1.0F});
    };
    need("Charge", robot.needs.charge);
    need("Condition", robot.needs.condition);
    need("Social", robot.needs.social);
    need("Fun", robot.needs.fun);
    panel.Line(std::format("Credits   {}", robot.credits));
    panel.Gap();
    if (info != nullptr) {
        panel.Line(std::format("Home: {}", RoomLabel(city, info->apartment)),
                   kDim);
        const auto work = city.Rooms().Find(info->workspace);
        if (work) {
            const sim::Shift& shift = work->building->shift;
            panel.Line(
                std::format("Work: {}, {}-{}, {} cr a shift",
                            RoomLabel(city, info->workspace),
                            sim::FormatClock(shift.startMinute),
                            sim::FormatClock(shift.startMinute +
                                             shift.lengthMinutes),
                            shift.wage),
                kDim);
        }
    }
    panel.Gap();
    panel.Line("Recent", kHeading);
    for (const std::string& event : host.RecentEvents(robot.id, kRecentEvents)) {
        panel.Line(event);
    }
}

void DescribeRoom(Panel& panel, sim::EntityId roomId, const WorldFrame& frame,
                  const CityView& city, const SimulationHost& host) {
    const auto found = city.Rooms().Find(roomId);
    if (!found) {
        return;
    }
    const sim::Room& room = *found->room;
    panel.Title(room.name);
    panel.Line(std::format("{} in {}, {}", KindLabel(room.kind),
                           found->building->name, FloorName(room.floor)));
    if (const sim::VenueRules* rules = sim::FindVenueRules(room.kind)) {
        panel.Line(std::format("{}, {} cr a visit",
                               Hours(rules->opensAt, rules->closesAt),
                               rules->price),
                   kDim);
    }
    const auto occupants = Occupants(frame);
    const auto inside = occupants.find(room.id.value);
    const std::size_t count =
        inside != occupants.end() ? inside->second.size() : 0;
    panel.Line(std::format("{} of {} here now", count, room.capacity));
    if (inside != occupants.end()) {
        panel.Gap();
        std::size_t listed = 0;
        for (const RobotFrame* robot : inside->second) {
            if (++listed > kListedOccupants) {
                panel.Line(std::format("...and {} more",
                                       count - kListedOccupants),
                           kDim);
                break;
            }
            panel.Line(std::format("{}, until {}", Name(host, robot->id),
                                   sim::FormatClock(sim::MinuteOfDay(
                                       robot->nextActionAt))));
        }
    }
    panel.Gap();
    panel.Line("Click a robot inside to inspect it", kDim);
}

void DescribeBuilding(Panel& panel, const Selection& selection,
                      const WorldFrame& frame, const CityView& city,
                      const SimulationHost& host) {
    const sim::Building* building = city.City().FindBuilding(selection.building);
    if (building == nullptr) {
        return;
    }
    panel.Title(building->name);
    const bool habitat = building->kind == sim::BuildingKind::Habitat;
    panel.Line(std::format("{}, {} floors", habitat ? "habitat" : "workplace",
                           building->floors));
    if (!habitat) {
        panel.Line(std::format("shift {}-{}, {} cr",
                               sim::FormatClock(building->shift.startMinute),
                               sim::FormatClock(building->shift.startMinute +
                                                building->shift.lengthMinutes),
                               building->shift.wage),
                   kDim);
    }
    panel.Gap();
    panel.Line(std::format("Viewing {}  (Q/E: floor down/up)",
                           FloorName(selection.floor)),
               kHeading);
    const auto occupants = Occupants(frame);
    for (const sim::Room& room : building->rooms) {
        if (room.floor != selection.floor) {
            continue;
        }
        const auto inside = occupants.find(room.id.value);
        const std::size_t count =
            inside != occupants.end() ? inside->second.size() : 0;
        if (room.kind == sim::RoomKind::Apartment) {
            panel.Line(std::format(
                "{}: {}", room.name,
                count > 0 ? std::string{Name(host, inside->second.front()->id)}
                          : std::string{"nobody home"}),
                count > 0 ? glm::vec4{0.85F, 0.88F, 0.95F, 1.0F} : kDim);
        } else {
            panel.Line(std::format("{} ({}): {} of {}", room.name,
                                   KindLabel(room.kind), count, room.capacity));
        }
    }
}
}  // namespace

std::vector<InspectorLine> DescribeSelection(const Selection& selection,
                                             const WorldFrame& frame,
                                             const CityView& city,
                                             const SimulationHost& host,
                                             std::size_t maxChars) {
    Panel panel(std::max<std::size_t>(maxChars, 20));
    if (selection.robot.IsValid()) {
        if (const RobotFrame* robot =
                SimulationHost::FindRobot(frame, selection.robot)) {
            DescribeRobot(panel, *robot, city, host);
        }
    } else if (selection.room.IsValid()) {
        DescribeRoom(panel, selection.room, frame, city, host);
    } else if (selection.building.IsValid()) {
        DescribeBuilding(panel, selection, frame, city, host);
    }
    return panel.Take();
}

}  // namespace mechapunk
