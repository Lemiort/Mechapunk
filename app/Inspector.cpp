#include "Inspector.hpp"
#include "Links.hpp"
#include "InspectorInternal.hpp"
#include "GangLists.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {
constexpr float kPanelWidth = 430.0F;
constexpr float kMargin = 10.0F;
constexpr float kNeedLabelWidth = 90.0F;
constexpr std::int32_t kLowNeed = 250;
constexpr std::int32_t kFailingWear = 750;
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

void Title(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTitle);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
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
        const std::string name = Name(frame, bond->robot);
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

// How worn the robot is: wear caps its condition, and at the end of the bar
// it is scrapped.
void DrawWear(const RobotFrame& robot) {
    ImGui::TextUnformatted("Wear");
    ImGui::SameLine(kNeedLabelWidth);
    const bool failing = robot.wear > kFailingWear;
    if (failing) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLowBar);
    }
    const std::string text = std::format("{}, {} days old", robot.wear, robot.age);
    ImGui::ProgressBar(static_cast<float>(robot.wear) /
                           static_cast<float>(sim::kMaxWear),
                       ImVec2{-1.0F, 0.0F}, text.c_str());
    if (failing) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip(
        "Lasting damage. Condition can't go above %d; clinics take some wear "
        "away, and at %d the robot is scrapped.",
        sim::kMaxWear - robot.wear, sim::kMaxWear);
}

// How the robot is doing at work: a bar toward the next promotion, red
// when it is on thin ice.
void DrawPerformance(const RobotFrame& robot) {
    if (robot.performance < 0) {
        const float risk = static_cast<float>(-robot.performance) /
                           static_cast<float>(-sim::kFiringPerformance);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLowBar);
        ImGui::ProgressBar(risk, ImVec2{-1.0F, 0.0F},
                           std::format("doing poorly ({})", robot.performance).c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Fired at %d", sim::kFiringPerformance);
        return;
    }
    if (robot.jobLevel >= sim::kMaxJobLevel) {
        ImGui::TextDisabled("Top of the ladder");
        return;
    }
    const std::int32_t target = sim::PromotionTarget(robot.jobLevel);
    ImGui::ProgressBar(static_cast<float>(robot.performance) /
                           static_cast<float>(target),
                       ImVec2{-1.0F, 0.0F},
                       std::format("{} / {} to promotion", robot.performance, target)
                           .c_str());
}

// What the robot remembers, newest first. A memory of a robot still in the
// city links to it, else a memory of a place links to the room.
std::optional<InspectorRequest> DrawMemories(const RobotSocial& social, const WorldFrame& frame,
                                             const CityView& city) {
    std::optional<InspectorRequest> result;
    if (social.memories.empty()) {
        ImGui::TextDisabled("Nothing memorable yet");
        return result;
    }
    std::size_t index = 0;
    for (const sim::Memory& memory : social.memories | std::views::reverse) {
        ++index;
        const std::string text = std::format(
            "{}  {}", sim::FormatTime(memory.tick),
            sim::MemoryText(memory.kind,
                            memory.robot.IsValid() ? Name(frame, memory.robot)
                                                   : std::string{},
                            memory.room.IsValid() ? RoomLabel(city, memory.room)
                                                  : std::string{}));
        std::optional<InspectorRequest> target;
        if (memory.robot.IsValid() && SimulationHost::FindRobot(frame, memory.robot) != nullptr) {
            target = InspectorRequest{.selection = ShowRobot(frame, city, memory.robot)};
        } else if (memory.room.IsValid() && city.Rooms().Find(memory.room)) {
            target = InspectorRequest{.selection = ShowRoom(city, memory.room)};
        }
        if (!target) {
            ImGui::TextWrapped("%s", text.c_str());
            continue;
        }
        if (auto link = LinkTo(std::format("{}##memory{}", text, index), *target)) {
            result = link;
        }
    }
    return result;
}

std::optional<InspectorRequest> DrawRobot(const RobotFrame& robot,
                                          const WorldFrame& frame,
                                          const CityView& city,
                                          const SimulationHost& host,
                                          bool& following) {
    std::optional<InspectorRequest> result;
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            result = *link;
        }
    };
    Title(Name(frame, robot.id));
    if (const RobotInfo* info = SimulationHost::FindRobotInfo(frame, robot.id)) {
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
            if (robot.nextActionAt == sim::kNoCommute) {
                ImGui::TextUnformatted("Resting at home");
                break;
            }
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
        case sim::RobotActivity::Walking: {
            const auto& stations = city.City().stations;
            const sim::Metro& metro = city.Transit();
            const bool rides = robot.alightsAt > robot.boardsAt &&
                               robot.rideFrom < stations.size() &&
                               robot.rideTo < stations.size();
            const std::vector<sim::Leg> legs =
                rides ? metro.Route(robot.rideFrom, robot.rideTo,
                                    static_cast<double>(robot.boardsAt))
                      : std::vector<sim::Leg>{};
            const auto name = [&](std::uint32_t station) -> const std::string& {
                return stations[station].name;
            };
            // A train is named after the end of the line it runs to.
            const auto toward = [&](const sim::Leg& leg) -> const std::string& {
                const auto& line = metro.Lines()[leg.line].stations;
                return name(leg.direction == sim::Direction::Up ? line.back() : line.front());
            };
            const auto now = static_cast<double>(frame.tick);
            std::string doing;
            if (legs.empty()) {
                doing = "Walking";
            } else if (now < static_cast<double>(robot.boardsAt)) {
                doing = std::format("Walking to {} station, metro to {}", name(robot.rideFrom),
                                    name(robot.rideTo));
                if (legs.size() > 1) {
                    doing += std::format(" changing at {}", name(legs.front().toStation));
                }
            } else {
                doing = std::format("Walking from {} station", name(robot.rideTo));
                for (std::size_t index = 0; index < legs.size(); ++index) {
                    const sim::Leg& leg = legs[index];
                    if (now < leg.departs - sim::kDwellMinutes) {
                        doing = std::format("{} at {} for the {} train toward {}",
                                            index == 0 ? "Waiting" : "Changing",
                                            name(leg.fromStation),
                                            sim::FormatClock(static_cast<std::uint32_t>(
                                                std::fmod(leg.departs, sim::kMinutesPerDay))),
                                            toward(leg));
                        break;
                    }
                    if (now < leg.arrives) {
                        doing = std::format("On the train toward {}, off at {}", toward(leg),
                                            name(leg.toStation));
                        break;
                    }
                }
            }
            ImGui::Text("%s, arrives %s", doing.c_str(), Clock(robot.nextActionAt).c_str());
            follow(RoomLink(city, robot.destination, "to "));
            break;
        }
    }

    ImGui::SeparatorText("Needs");
    NeedBar("Charge", robot.needs.charge);
    NeedBar("Condition", robot.needs.condition);
    NeedBar("Social", robot.needs.social);
    NeedBar("Fun", robot.needs.fun);
    DrawWear(robot);
    ImGui::Text("Credits: %lld", static_cast<long long>(robot.credits));
    if (robot.rentOwed > 0) {
        ImGui::TextColored(kWarning, "Behind on rent: owes %lld cr",
                           static_cast<long long>(robot.rentOwed));
    }
    if (frame.economy != nullptr) {
        const EconomyFrame& economy = *frame.economy;
        // Each link draws its own name: text before it, then SameLine.
        ImGui::Text("Savings: %lld cr at", static_cast<long long>(robot.savings));
        ImGui::SameLine();
        follow(AccountLink(frame, city, robot.bank, ""));
        if (const sim::Loan* loan = economy.LoanOf(robot.id)) {
            ImGui::TextColored(kWarning, "Owes %lld cr to", static_cast<long long>(loan->owed));
            ImGui::SameLine();
            follow(AccountLink(frame, city, loan->lender, ""));
        }
        // Its home, apartments it lets, and organizations it owns part of.
        const sim::EntityId landlord = economy.OwnerOf(robot.apartment);
        if (landlord == robot.id) {
            ImGui::TextUnformatted("Owns its home");
        } else if (landlord.IsValid()) {
            ImGui::TextUnformatted("Rents its home from");
            ImGui::SameLine();
            follow(AccountLink(frame, city, landlord, ""));
        }
        std::vector<sim::EntityId> let;
        if (economy.owners != nullptr) {
            for (const auto& [room, owner] : *economy.owners) {
                if (owner == robot.id && room != robot.apartment.value) {
                    let.push_back(sim::EntityId{room});
                }
            }
        }
        std::ranges::sort(let);  // The owners map is unordered.
        if (!let.empty()) {
            ImGui::Text("Lets %zu apartment%s:", let.size(), let.size() == 1 ? "" : "s");
            ImGui::Indent();
            for (const sim::EntityId room : let) {
                follow(RoomLink(city, room, ""));
            }
            ImGui::Unindent();
        }
        bool partOwner = false;
        for (const sim::Organization& organization : economy.organizations) {
            if (!std::ranges::contains(organization.owners, robot.id, &sim::Share::owner)) {
                continue;
            }
            if (!partOwner) {
                ImGui::TextUnformatted("Owns part of:");
                partOwner = true;
            }
            ImGui::Indent();
            follow(OrganizationLink(frame, organization.id, ""));
            ImGui::Unindent();
        }
    }

    // Gang membership.
    if (frame.gangs != nullptr && robot.gang.IsValid()) {
        const sim::Gang* gang = nullptr;
        for (const sim::Gang& g : *frame.gangs) {
            if (g.id == robot.gang) {
                gang = &g;
                break;
            }
        }
        if (gang) {
            ImGui::SeparatorText("Gang");
            follow(GangLink(frame, gang->id, ""));
        }
    }
    if (robot.arrests > 0) {
        // Once, on its own line; robots that left their gang still see it.
        ImGui::Text("Arrests: %u", robot.arrests);
    }

    if (const RobotInfo* info = SimulationHost::FindRobotInfo(frame, robot.id)) {
        ImGui::SeparatorText("Home and work");
        follow(RoomLink(city, robot.apartment, "Home: "));
        if (const auto home = city.Rooms().Find(robot.apartment)) {
            ImGui::TextDisabled("%u cr a night", home->room->rent);
        }
        if (const auto work = city.Rooms().Find(robot.workspace)) {
            follow(RoomLink(city, robot.workspace, "Work: "));
            if (frame.economy != nullptr) {
                follow(AccountLink(frame, city, frame.economy->OwnerOf(robot.workspace),
                                   "Employer: "));
            }
            const sim::Shift shift = sim::JobShift(*work, robot.id);
            // The level's wage at the share of it the employer pays now.
            std::int64_t pay = sim::WageAtLevel(shift.wage, robot.jobLevel);
            if (frame.economy != nullptr) {
                if (const sim::Organization* employer =
                        frame.economy->FindOrganization(frame.economy->OwnerOf(robot.workspace))) {
                    pay = pay * employer->payPercent / 100;
                }
            }
            pay -= pay * sim::kWageTaxPercent / 100;  // After City Hall's share.
            const std::string job = std::format(
                "{}{}, {}-{}, {} cr a shift after tax", sim::JobTitle(robot.jobLevel),
                sim::IsVenue(work->room->kind) ? " on the staff" : "",
                sim::FormatClock(shift.startMinute),
                sim::FormatClock((shift.startMinute + shift.lengthMinutes) % sim::kMinutesPerDay),
                pay);
            ImGui::TextDisabled("%s", job.c_str());
            DrawPerformance(robot);
        } else {
            ImGui::TextColored(kWarning, "Out of work, looking for a job");
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
        follow(DrawMemories(*social, frame, city));
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
    ImGui::Text("%s, %s, in", KindLabel(room.kind).c_str(), FloorName(room.floor).c_str());
    ImGui::SameLine();
    if (auto link = BuildingLink(city, found->building->id, "")) {
        result = *link;
    }
    if (const sim::VenueRules* rules = sim::FindVenueRules(room.kind)) {
        const auto price = static_cast<unsigned>(
            std::max<std::uint64_t>(1, std::uint64_t{rules->price} * room.pricePercent / 100));
        if (rules->opensAt == rules->closesAt) {
            ImGui::TextDisabled("Open around the clock, %u cr a visit", price);
        } else {
            ImGui::TextDisabled("Open %s-%s, %u cr a visit",
                                sim::FormatClock(rules->opensAt).c_str(),
                                sim::FormatClock(rules->closesAt).c_str(), price);
        }
        ImGui::TextDisabled("Prices at %u%% of usual, %u guests today", room.pricePercent,
                            room.visits);
    } else if (room.kind == sim::RoomKind::Apartment) {
        const std::string kind{sim::ApartmentClassName(room.apartmentClass)};
        ImGui::TextDisabled("A %s of %u m2, sleeps %u; %u cr a night, worth %lld cr",
                            kind.c_str(), sim::AreaOf(room.apartmentClass),
                            sim::Sleeps(room.apartmentClass), room.rent,
                            static_cast<long long>(room.rent * sim::kPriceNights));
    }
    if (frame.economy != nullptr) {
        // The owner runs a venue or workspace, and lets an apartment.
        ImGui::TextUnformatted(room.kind == sim::RoomKind::Apartment ? "Owner:" : "Run by:");
        ImGui::SameLine();
        if (auto link = AccountLink(frame, city, frame.economy->OwnerOf(room.id), "")) {
            result = *link;
        }
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
        ImGui::BeginTable("occupants", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Robot");
        ImGui::TableSetupColumn("Until", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Gang", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const RobotFrame* robot : inside->second) {
            ImGui::PushID(static_cast<int>(robot->id.value));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string name = Name(frame, robot->id);
            Selection next = selection;
            next.robot = robot->id;
            next.room = {};
            if (auto link = RobotLink(name, next,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                result = link;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Clock(robot->nextActionAt).c_str());
            // A gang member present in this room, named by its gang's link.
            ImGui::TableNextColumn();
            if (robot->gang.IsValid()) {
                if (auto link = GangLink(frame, robot->gang, "")) {
                    result = link;
                }
            } else {
                ImGui::TextDisabled("-");
            }
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

    // Who owns its rooms: most rooms first.
    if (frame.economy != nullptr) {
        std::map<std::uint64_t, std::uint32_t> owned;
        for (const sim::Room& room : building->rooms) {
            ++owned[frame.economy->OwnerOf(room.id).value];
        }
        std::vector<std::pair<std::uint64_t, std::uint32_t>> owners(owned.begin(), owned.end());
        std::ranges::stable_sort(owners, std::greater{}, &std::pair<std::uint64_t, std::uint32_t>::second);
        ImGui::TextUnformatted("Owned by:");
        ImGui::Indent();
        for (const auto& [owner, rooms] : owners) {
            if (auto link = AccountLink(frame, city, sim::EntityId{owner}, "")) {
                result = *link;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%u room%s", rooms, rooms == 1 ? "" : "s");
        }
        ImGui::Unindent();
    }

    // A building a gang has claimed as its turf.
    if (const auto turfGang = TurfOf(frame, selection.building); turfGang != std::nullopt) {
        ImGui::TextUnformatted("Turf of");
        ImGui::SameLine();
        if (auto link = GangLink(frame, *turfGang, "")) {
            result = *link;
        }
    }

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
            if (auto link = RoomLink(city, room.id, "")) {
                result = *link;
            }
            ImGui::TableNextColumn();
            if (room.kind == sim::RoomKind::Apartment) {
                if (count == 0) {
                    ImGui::TextDisabled("nobody home");
                } else {
                    const RobotFrame* robot = inside->second.front();
                    const std::string name = Name(frame, robot->id);
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
    // Nothing selected at all: no window. A gang alone is a page, even with
    // no building open, which is how a gang clicked in the Statistics window
    // opens one.
    if (!selection.organization.IsValid() && !selection.building.IsValid() &&
        !selection.robot.IsValid() && !selection.room.IsValid() &&
        !selection.gang.IsValid()) {
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
        } else if (selection.organization.IsValid()) {
            clicked = DrawOrganization(selection, frame, city);
        } else if (selection.gang.IsValid()) {
            clicked = DrawGangPage(selection, frame, city);
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
