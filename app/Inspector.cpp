#include "Inspector.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <ranges>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
constexpr float kPanelWidth = 430.0F;
constexpr float kMargin = 10.0F;
constexpr float kNeedLabelWidth = 90.0F;
constexpr std::int32_t kLowNeed = 250;
constexpr std::size_t kRecentEvents = 30;
constexpr ImVec4 kTitle{1.0F, 0.85F, 0.3F, 1.0F};
constexpr ImVec4 kWarning{1.0F, 0.45F, 0.35F, 1.0F};
constexpr ImVec4 kLowBar{0.85F, 0.25F, 0.2F, 1.0F};
constexpr ImVec4 kFriendBar{0.3F, 0.75F, 0.45F, 1.0F};
constexpr ImVec4 kRivalBar{0.85F, 0.3F, 0.25F, 1.0F};
constexpr float kFeelingWidth = 120.0F;
constexpr float kAffinityWidth = 90.0F;
// Rows of relationships shown before the table scrolls.
constexpr float kVisibleBonds = 6.0F;
constexpr float kMinRecentHeight = 160.0F;

std::string FloorName(std::uint32_t floor) {
    return floor == 0 ? "ground floor" : std::format("floor {}", floor);
}

std::string KindLabel(sim::RoomKind kind) {
    if (const sim::VenueRules* rules = sim::FindVenueRules(kind)) {
        return std::string{rules->label};
    }
    return kind == sim::RoomKind::Apartment ? "apartment" : "workspace";
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

std::string Clock(sim::Tick tick) {
    return sim::FormatClock(sim::MinuteOfDay(tick));
}

std::string Name(const SimulationHost& host, sim::EntityId robot) {
    const RobotInfo* info = host.FindRobotInfo(robot);
    return info != nullptr ? info->name : "unknown robot";
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

// The open building and floor that show a room, with the room selected.
Selection ShowRoom(const CityView& city, sim::EntityId room) {
    const auto found = city.Rooms().Find(room);
    if (!found) {
        return {};
    }
    return {.building = found->building->id,
            .floor = found->room->floor,
            .room = room,
            .robot = {}};
}

// A clickable room name; returns where clicking it leads.
std::optional<Selection> RoomLink(const CityView& city, sim::EntityId room,
                                  std::string_view prefix) {
    const std::string label = std::format("{}{}##room{}", prefix,
                                          RoomLabel(city, room), room.value);
    std::optional<Selection> result;
    if (ImGui::Selectable(label.c_str())) {
        result = ShowRoom(city, room);
    }
    ImGui::SetItemTooltip("Show this room");
    return result;
}

// Selects a robot and shows the room it is in, if it is in one.
Selection ShowRobot(const WorldFrame& frame, const CityView& city,
                    sim::EntityId robot) {
    const RobotFrame* found = SimulationHost::FindRobot(frame, robot);
    if (found == nullptr || found->activity == sim::RobotActivity::Walking) {
        return {.robot = robot};
    }
    Selection next = ShowRoom(city, found->location);
    next.room = {};
    next.robot = robot;
    return next;
}

void Title(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTitle);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(const std::string& name,
                                          const Selection& target,
                                          ImGuiSelectableFlags flags = 0) {
    if (!ImGui::Selectable(name.c_str(), false,
                           flags | ImGuiSelectableFlags_AllowDoubleClick)) {
        return std::nullopt;
    }
    return InspectorRequest{
        .selection = target,
        .jumpToRobot = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
}

void NeedBar(const char* name, std::int32_t value) {
    ImGui::TextUnformatted(name);
    ImGui::SameLine(kNeedLabelWidth);
    const bool low = value < kLowNeed;
    if (low) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLowBar);
    }
    const std::string text = std::to_string(value);
    ImGui::ProgressBar(static_cast<float>(value) /
                           static_cast<float>(sim::Needs::kMax),
                       ImVec2{-1.0F, 0.0F}, text.c_str());
    if (low) {
        ImGui::PopStyleColor();
    }
}

// Who the robot knows, warmest first; clicking a name selects that robot.
std::optional<InspectorRequest> DrawBonds(const RobotSocial& social,
                                   const WorldFrame& frame,
                                   const CityView& city,
                                   const SimulationHost& host) {
    std::optional<InspectorRequest> result;
    std::vector<const sim::Bond*> bonds;
    for (const sim::Bond& bond : social.bonds) {
        bonds.push_back(&bond);
    }
    std::ranges::sort(bonds, std::greater{}, &sim::Bond::affinity);
    if (bonds.empty()) {
        ImGui::TextDisabled("Has not got to know anyone yet");
        return result;
    }
    const float rows =
        std::min(static_cast<float>(bonds.size()), kVisibleBonds) + 1.0F;
    const ImVec2 size{0.0F, rows * ImGui::GetFrameHeightWithSpacing()};
    if (!ImGui::BeginTable("bonds", 3,
                           ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_ScrollY,
                           size)) {
        return result;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Robot");
    ImGui::TableSetupColumn("Feels", ImGuiTableColumnFlags_WidthFixed,
                            kFeelingWidth);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                            kAffinityWidth);
    ImGui::TableHeadersRow();
    for (const sim::Bond* bond : bonds) {
        ImGui::PushID(static_cast<int>(bond->robot.value));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string name = Name(host, bond->robot);
        if (auto link = RobotLink(name, ShowRobot(frame, city, bond->robot))) {
            result = link;
        }
        ImGui::SetItemTooltip("%u talks, last %s. Double-click to look at.",
                              bond->talks,
                              sim::FormatTime(bond->lastTalk).c_str());
        ImGui::TableNextColumn();
        const std::string_view level = sim::BondLevelName(bond->level);
        if (bond->level == sim::BondLevel::Rival) {
            ImGui::TextColored(kWarning, "%.*s",
                               static_cast<int>(level.size()), level.data());
        } else if (bond->level == sim::BondLevel::Acquaintance) {
            ImGui::TextDisabled("%.*s", static_cast<int>(level.size()),
                                level.data());
        } else {
            ImGui::Text("%.*s", static_cast<int>(level.size()), level.data());
        }
        ImGui::TableNextColumn();
        // Liking fills the bar green, dislike red.
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              bond->affinity < 0 ? kRivalBar : kFriendBar);
        const std::string value = std::to_string(bond->affinity);
        ImGui::ProgressBar(static_cast<float>(std::abs(bond->affinity)) /
                               static_cast<float>(sim::Bond::kMaxAffinity),
                           ImVec2{-1.0F, 0.0F}, value.c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndTable();
    return result;
}

// What the robot remembers, newest first.
void DrawMemories(const RobotSocial& social, const CityView& city,
                  const SimulationHost& host) {
    if (social.memories.empty()) {
        ImGui::TextDisabled("Nothing memorable yet");
        return;
    }
    for (const sim::Memory& memory : social.memories | std::views::reverse) {
        std::string text = std::format(
            "{}  {}", sim::FormatTime(memory.tick),
            sim::MemoryText(memory.kind, memory.robot.IsValid()
                                             ? Name(host, memory.robot)
                                             : std::string{}));
        const auto room = city.Rooms().Find(memory.room);
        if (room && sim::IsVenue(room->room->kind)) {
            text += std::format(" at {}", room->room->name);
        }
        ImGui::TextWrapped("%s", text.c_str());
    }
}

std::optional<InspectorRequest> DrawRobot(const RobotFrame& robot,
                                          const WorldFrame& frame,
                                          const CityView& city,
                                          const SimulationHost& host,
                                          bool& following) {
    std::optional<InspectorRequest> result;
    const auto follow = [&](std::optional<Selection> link) {
        if (link) {
            result = InspectorRequest{.selection = *link};
        }
    };
    Title(Name(host, robot.id));
    if (const RobotInfo* info = host.FindRobotInfo(robot.id)) {
        ImGui::TextDisabled("Likes %s",
                            sim::InterestList(info->interests).c_str());
    }
    if (ImGui::SmallButton("Look at")) {
        result = InspectorRequest{.selection = ShowRobot(frame, city, robot.id),
                                  .jumpToRobot = true};
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow (F)", &following);
    ImGui::SetItemTooltip("Keep the camera on this robot wherever it goes");

    switch (robot.activity) {
        case sim::RobotActivity::Resting: {
            const bool goingOut = robot.nextActionAt < robot.commuteAt;
            ImGui::Text("Resting at home, %s at %s",
                        goingOut ? "going out" : "leaves for work",
                        Clock(robot.nextActionAt).c_str());
            break;
        }
        case sim::RobotActivity::Working:
        case sim::RobotActivity::Visiting:
            ImGui::Text("%s until %s",
                        robot.activity == sim::RobotActivity::Working
                            ? "Working"
                            : "Out",
                        Clock(robot.nextActionAt).c_str());
            follow(RoomLink(city, robot.location, "at "));
            break;
        case sim::RobotActivity::Walking:
            ImGui::Text("Walking, arrives %s",
                        Clock(robot.nextActionAt).c_str());
            follow(RoomLink(city, robot.destination, "to "));
            break;
    }

    ImGui::SeparatorText("Needs");
    NeedBar("Charge", robot.needs.charge);
    NeedBar("Condition", robot.needs.condition);
    NeedBar("Social", robot.needs.social);
    NeedBar("Fun", robot.needs.fun);
    ImGui::Text("Credits: %lld", static_cast<long long>(robot.credits));
    if (robot.rentOwed > 0) {
        ImGui::TextColored(kWarning, "Behind on rent: owes %lld cr",
                           static_cast<long long>(robot.rentOwed));
    }

    if (const RobotInfo* info = host.FindRobotInfo(robot.id)) {
        ImGui::SeparatorText("Home and work");
        follow(RoomLink(city, info->apartment, "Home: "));
        if (const auto home = city.Rooms().Find(info->apartment)) {
            ImGui::TextDisabled("%u cr a night", home->room->rent);
        }
        follow(RoomLink(city, info->workspace, "Work: "));
        if (const auto work = city.Rooms().Find(info->workspace)) {
            const sim::Shift& shift = work->building->shift;
            const std::string hours = std::format(
                "{}-{}, {} cr a shift", sim::FormatClock(shift.startMinute),
                sim::FormatClock(shift.startMinute + shift.lengthMinutes),
                shift.wage);
            ImGui::TextDisabled("%s", hours.c_str());
        }
        std::string daysOff;
        for (std::uint32_t day = 0; day < sim::kDaysPerWeek; ++day) {
            if (((info->daysOff >> day) & 1U) != 0) {
                daysOff += std::format("{}{}", daysOff.empty() ? "" : ", ",
                                       sim::WeekdayName(day));
            }
        }
        ImGui::TextDisabled("Days off: %s", daysOff.c_str());
    }

    if (const RobotSocial* social = SimulationHost::FindSocial(frame, robot.id)) {
        ImGui::SeparatorText("Relationships");
        if (auto link = DrawBonds(*social, frame, city, host)) {
            result = link;
        }
        ImGui::SeparatorText("Memories");
        DrawMemories(*social, city, host);
    }

    ImGui::SeparatorText("Recent");
    const float recentHeight =
        std::max(ImGui::GetContentRegionAvail().y, kMinRecentHeight);
    if (ImGui::BeginChild("recent", ImVec2{0.0F, recentHeight},
                          ImGuiChildFlags_Borders)) {
        for (const std::string& event :
             host.RecentEvents(robot.id, kRecentEvents)) {
            ImGui::TextWrapped("%s", event.c_str());
        }
        // Follow new events unless the player scrolled up.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0F);
        }
    }
    ImGui::EndChild();
    return result;
}

std::optional<InspectorRequest> DrawRoom(const Selection& selection,
                                         const WorldFrame& frame,
                                         const CityView& city,
                                         const SimulationHost& host) {
    std::optional<InspectorRequest> result;
    const auto found = city.Rooms().Find(selection.room);
    if (!found) {
        return result;
    }
    const sim::Room& room = *found->room;
    Title(room.name);
    ImGui::Text("%s in %s, %s", KindLabel(room.kind).c_str(),
                found->building->name.c_str(), FloorName(room.floor).c_str());
    if (const sim::VenueRules* rules = sim::FindVenueRules(room.kind)) {
        if (rules->opensAt == rules->closesAt) {
            ImGui::TextDisabled("Open around the clock, %u cr a visit",
                                rules->price);
        } else {
            ImGui::TextDisabled("Open %s-%s, %u cr a visit",
                                sim::FormatClock(rules->opensAt).c_str(),
                                sim::FormatClock(rules->closesAt).c_str(),
                                rules->price);
        }
    } else if (room.kind == sim::RoomKind::Apartment) {
        ImGui::TextDisabled("%u cr a night", room.rent);
    }

    const auto occupants = Occupants(frame);
    const auto inside = occupants.find(room.id.value);
    const std::size_t count =
        inside != occupants.end() ? inside->second.size() : 0;
    const std::string occupancy = std::format("{} of {}", count, room.capacity);
    ImGui::ProgressBar(static_cast<float>(count) /
                           static_cast<float>(std::max(room.capacity, 1U)),
                       ImVec2{-1.0F, 0.0F}, occupancy.c_str());

    if (count > 0 &&
        ImGui::BeginTable("occupants", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Robot");
        ImGui::TableSetupColumn("Until", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const RobotFrame* robot : inside->second) {
            ImGui::PushID(static_cast<int>(robot->id.value));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string name = Name(host, robot->id);
            Selection next = selection;
            next.robot = robot->id;
            next.room = {};
            if (auto link = RobotLink(name, next,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                result = link;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Clock(robot->nextActionAt).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return result;
}

std::optional<InspectorRequest> DrawBuilding(const Selection& selection,
                                             const WorldFrame& frame,
                                             const CityView& city,
                                             const SimulationHost& host) {
    std::optional<InspectorRequest> result;
    const sim::Building* building =
        city.City().FindBuilding(selection.building);
    if (building == nullptr) {
        return result;
    }
    Title(building->name);
    const bool habitat = building->kind == sim::BuildingKind::Habitat;
    ImGui::Text("%s, %u floors", habitat ? "Habitat" : "Workplace",
                building->floors);
    if (!habitat) {
        ImGui::TextDisabled(
            "Shift %s-%s, %u cr",
            sim::FormatClock(building->shift.startMinute).c_str(),
            sim::FormatClock(building->shift.startMinute +
                             building->shift.lengthMinutes)
                .c_str(),
            building->shift.wage);
    }

    auto floor = static_cast<int>(selection.floor);
    const std::string floorName = FloorName(selection.floor);
    if (ImGui::SliderInt("Floor", &floor, 0,
                         static_cast<int>(building->floors) - 1,
                         floorName.c_str())) {
        Selection next = selection;
        next.floor = static_cast<std::uint32_t>(floor);
        next.room = {};
        result = InspectorRequest{.selection = next};
    }
    ImGui::SetItemTooltip("Q / E change floors too");

    const auto occupants = Occupants(frame);
    if (ImGui::BeginTable("rooms", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Room");
        ImGui::TableSetupColumn("Who");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const sim::Room& room : building->rooms) {
            if (room.floor != selection.floor) {
                continue;
            }
            const auto inside = occupants.find(room.id.value);
            const std::size_t count =
                inside != occupants.end() ? inside->second.size() : 0;
            ImGui::PushID(static_cast<int>(room.id.value));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(room.name.c_str())) {
                Selection next = selection;
                next.room = room.id;
                next.robot = {};
                result = InspectorRequest{.selection = next};
            }
            ImGui::TableNextColumn();
            if (room.kind == sim::RoomKind::Apartment) {
                if (count == 0) {
                    ImGui::TextDisabled("nobody home");
                } else {
                    const RobotFrame* robot = inside->second.front();
                    const std::string name = Name(host, robot->id);
                    Selection next = selection;
                    next.robot = robot->id;
                    next.room = {};
                    if (auto link = RobotLink(name, next)) {
                        result = link;
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%u cr", room.rent);
            } else {
                ImGui::Text("%zu of %u", count, room.capacity);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", KindLabel(room.kind).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return result;
}
}  // namespace

std::optional<InspectorRequest> DrawInspector(const Selection& selection,
                                              const WorldFrame& frame,
                                              const CityView& city,
                                              const SimulationHost& host,
                                              bool& following) {
    if (!selection.building.IsValid() && !selection.robot.IsValid() &&
        !selection.room.IsValid()) {
        return std::nullopt;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2{viewport.WorkPos.x + viewport.WorkSize.x - kMargin,
               viewport.WorkPos.y + kMargin},
        ImGuiCond_FirstUseEver, ImVec2{1.0F, 0.0F});
    ImGui::SetNextWindowSize(
        ImVec2{kPanelWidth, viewport.WorkSize.y - (4.0F * kMargin)},
        ImGuiCond_FirstUseEver);

    bool open = true;
    std::optional<InspectorRequest> result;
    if (ImGui::Begin("Inspector", &open)) {
        if ((selection.robot.IsValid() || selection.room.IsValid()) &&
            selection.building.IsValid()) {
            if (ImGui::SmallButton("< Back to floor")) {
                Selection next = selection;
                next.robot = {};
                next.room = {};
                result = InspectorRequest{.selection = next};
            }
        }
        std::optional<InspectorRequest> clicked;
        if (selection.robot.IsValid()) {
            if (const RobotFrame* robot =
                    SimulationHost::FindRobot(frame, selection.robot)) {
                clicked = DrawRobot(*robot, frame, city, host, following);
            }
        } else if (selection.room.IsValid()) {
            clicked = DrawRoom(selection, frame, city, host);
        } else {
            clicked = DrawBuilding(selection, frame, city, host);
        }
        if (clicked) {
            result = clicked;
        }
    }
    ImGui::End();
    if (!open) {
        return InspectorRequest{};
    }
    return result;
}

}  // namespace mechapunk
