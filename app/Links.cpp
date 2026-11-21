#include "Links.hpp"

#include <format>
#include <unordered_map>

#include "mechapunk/sim/Gangs.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {

// Gang colour as an ImVec4.
inline ImVec4 GangColour(std::uint32_t colour) {
    std::uint8_t r = (colour >> 16U) & 0xFF;
    std::uint8_t g = (colour >> 8U) & 0xFF;
    std::uint8_t b = colour & 0xFF;
    return ImVec4{static_cast<float>(r) / 255.0F,
                  static_cast<float>(g) / 255.0F,
                  static_cast<float>(b) / 255.0F,
                  1.0F};
}

// The colour of every clickable name, so the player sees what is a link.
constexpr ImVec4 kLink{0.4F, 0.7F, 1.0F, 1.0F};

std::string BuildingName(const CityView& city, sim::EntityId building) {
    if (const sim::Building* found = city.City().FindBuilding(building)) {
        return found->name;
    }
    return "somewhere";
}

// Every link: light blue text with the same tooltip. Returns whether it
// was clicked.
// How many times each label was drawn this frame. A page may link the same
// object twice (a robot's room in "at ..." and in "Work: ..."; a bank that
// is both its savings bank and its lender), and two items with one id
// conflict in ImGui. Each repeat gets its own id: the label's number of
// earlier uses, which stays the same from frame to frame.
int RepeatOf(const std::string& label) {
    static int frame = -1;
    static std::unordered_map<std::string, int> uses;
    if (ImGui::GetFrameCount() != frame) {
        frame = ImGui::GetFrameCount();
        uses.clear();
    }
    return uses[label]++;
}

// As wide as its text, so a link can follow other text on the same line.
bool Link(const std::string& label, ImGuiSelectableFlags flags = 0) {
    ImGui::PushID(RepeatOf(label));
    ImGui::PushStyleColor(ImGuiCol_Text, kLink);
    const ImVec2 size = (flags & ImGuiSelectableFlags_SpanAllColumns) != 0
                            ? ImVec2{0.0F, 0.0F}
                            : ImGui::CalcTextSize(label.c_str(), nullptr, true);
    const bool clicked = ImGui::Selectable(label.c_str(), false, flags, size);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Show in the inspector");
    ImGui::PopID();
    return clicked;
}

}  // namespace

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
std::optional<InspectorRequest> RoomLink(const CityView& city, sim::EntityId room,
                                         std::string_view prefix) {
    const std::string label = std::format("{}{}##room{}", prefix,
                                          RoomLabel(city, room), room.value);
    if (Link(label)) {
        return InspectorRequest{.selection = ShowRoom(city, room)};
    }
    return std::nullopt;
}

// A building's name: a click opens the building at its ground floor.
std::optional<InspectorRequest> BuildingLink(const CityView& city,
                                             sim::EntityId building,
                                             std::string_view prefix) {
    const std::string label =
        std::format("{}{}##building{}", prefix, BuildingName(city, building),
                    building.value);
    if (Link(label)) {
        return InspectorRequest{.selection = {.building = building, .floor = 0}};
    }
    return std::nullopt;
}

// An organization's name: a click shows it in the inspector.
std::optional<InspectorRequest> OrganizationLink(const WorldFrame& frame,
                                                 sim::EntityId organization,
                                                 std::string_view prefix) {
    const std::string label =
        std::format("{}{}##organization{}", prefix,
                    SimulationHost::AccountName(frame, organization),
                    organization.value);
    if (Link(label)) {
        return InspectorRequest{.selection = {.organization = organization}};
    }
    return std::nullopt;
}

namespace {

// AccountLink with or without the city: with it, a robot's link also opens
// the building it is in.
std::optional<InspectorRequest> AnyAccountLink(const WorldFrame& frame, const CityView* city,
                                               sim::EntityId account,
                                               std::string_view prefix) {
    if (account.IsValid()) {
        if (SimulationHost::FindRobot(frame, account) != nullptr) {
            return RobotLink(
                std::format("{}{}", prefix,
                            SimulationHost::AccountName(frame, account)),
                city != nullptr ? ShowRobot(frame, *city, account)
                                : Selection{.robot = account});
        }
        if (frame.economy != nullptr &&
            frame.economy->FindOrganization(account) != nullptr) {
            return OrganizationLink(frame, account, prefix);
        }
    }
    // A robot that was scrapped since the player met it, or no one at all:
    // there is nothing to link to.
    const std::string name = account.IsValid()
                                 ? SimulationHost::AccountName(frame, account)
                                 : "nobody";
    ImGui::TextDisabled("%.*s%s", static_cast<int>(prefix.size()), prefix.data(),
                        name.c_str());
    return std::nullopt;
}

}  // namespace

std::optional<InspectorRequest> AccountLink(const WorldFrame& frame, const CityView& city,
                                            sim::EntityId account, std::string_view prefix) {
    return AnyAccountLink(frame, &city, account, prefix);
}

std::optional<InspectorRequest> AccountLink(const WorldFrame& frame, sim::EntityId account,
                                            std::string_view prefix) {
    return AnyAccountLink(frame, nullptr, account, prefix);
}

std::optional<InspectorRequest> LinkTo(const std::string& label, const InspectorRequest& target) {
    if (Link(label)) {
        return target;
    }
    return std::nullopt;
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

// A robot's name: a click selects `target`, a double-click also moves the
// camera to the robot.
std::optional<InspectorRequest> RobotLink(const std::string& name,
                                          const Selection& target,
                                          ImGuiSelectableFlags flags) {
    // The suffix keeps same-named robots from sharing an id.
    const std::string label =
        std::format("{}##robot{}", name, target.robot.value);
    if (!Link(label, flags | ImGuiSelectableFlags_AllowDoubleClick)) {
        return std::nullopt;
    }
    return InspectorRequest{
        .selection = target,
        .jumpToRobot = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
}

// A gang's colour as a square with an id that survives being drawn twice:
// the same way a link's id does, by counting this window's earlier squares
// for that gang. Windows that need the colour on their own (the Crime tab's
// colour column, next to a hex code and no name) call this instead of
// ImGui::ColorButton, because a ColorButton("##swatch") inside a PushID of
// the gang id clashes with the square `GangLink` draws for the same gang
// in the same frame, and ImGui then treats the two squares as one widget.
void GangSwatch(std::string_view what, const ImVec4& colour, ImVec2 size) {
    ImGui::PushID(RepeatOf(std::format("swatch {}", what)));
    ImGui::ColorButton("##swatch", colour, ImGuiColorEditFlags_NoTooltip, size);
    ImGui::PopID();
}

// A gang's name with a colour swatch; clicking it shows the gang page.
std::optional<InspectorRequest> GangLink(const WorldFrame& frame,
                                         sim::EntityId gang,
                                         std::string_view prefix) {
    if (frame.gangs == nullptr) {
        ImGui::TextDisabled("%.*s%s", static_cast<int>(prefix.size()),
                            prefix.data(), "unknown gang");
        return std::nullopt;
    }

    // Look up the gang.
    const sim::Gang* found = nullptr;
    for (const auto& g : *frame.gangs) {
        if (g.id == gang) {
            found = &g;
            break;
        }
    }
    if (!found) {
        ImGui::TextDisabled("%.*s%s", static_cast<int>(prefix.size()), prefix.data(),
                            "unknown gang");
        return std::nullopt;
    }

    // A colour square, then the name as a link. Both ids count how many times
    // this frame already drew that gang, so a gang shown twice (its own row
    // and another gang's relations) gets two squares and two links instead of
    // one id twice. The label ends in "##gang<id>", like every other link.
    GangSwatch(std::format("gang##{}", gang.value), GangColour(found->color));
    ImGui::SameLine();
    if (Link(std::format("{}{}##gang{}", prefix, found->name, gang.value))) {
        // The gang's page, and its turf building opened and looked at, so
        // clicking a gang somewhere shows the place it owns. A gang with no
        // turf only selects the page, and the camera stays where it is.
        Selection next{.gang = gang};
        if (found->turf.IsValid()) {
            next.building = found->turf;
            next.floor = 0;
        }
        return InspectorRequest{.selection = next};
    }
    return std::nullopt;
}

}  // namespace mechapunk
