// The organization's page in the inspector: what it is, its books and
// owners, every room it owns by building, everyone working for it, and for
// banks the loans they gave and who saves with them.

#include "InspectorInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Links.hpp"
#include "OrganizationLists.hpp"
#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {

// Long lists scroll inside a table this many rows high, and only the rows on
// screen are drawn (ImGuiListClipper): the Habitat Trust owns thousands.
constexpr int kVisibleRows = 12;

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

std::string Credits(std::int64_t value) {
    return std::format("{} cr", value);
}

std::string KindOf(const sim::Organization& organization) {
    switch (organization.kind) {
        case sim::OrganizationKind::CentralBank: return "central bank";
        case sim::OrganizationKind::Bank: return "bank";
        case sim::OrganizationKind::Government: return "the city's government";
        case sim::OrganizationKind::Corporation: break;
    }
    return std::format("corporation, {}", SectorLabel(organization.sector));
}

// Height of a scrolling list of `rows` rows plus its header.
ImVec2 ListSize(std::size_t rows) {
    const auto shown = static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

// What a worker is doing now, in a few words.
const char* Doing(const RobotFrame& robot) {
    switch (robot.activity) {
        case sim::RobotActivity::Resting: return "at home";
        case sim::RobotActivity::Working: return "working";
        case sim::RobotActivity::Visiting: return "out";
        case sim::RobotActivity::Walking: return "walking";
    }
    return "";
}

// A room's rent, or a venue's price for a visit.
std::string PriceOf(const sim::Room& room) {
    if (const sim::VenueRules* rules = sim::FindVenueRules(room.kind)) {
        const auto price = std::max<std::uint64_t>(
            1, std::uint64_t{rules->price} * room.pricePercent / 100);
        return std::format("{} cr a visit", price);
    }
    if (room.kind == sim::RoomKind::Apartment) {
        return std::format("{} cr a night", room.rent);
    }
    return "";
}

void Fact(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
}

}  // namespace

std::optional<InspectorRequest> DrawOrganization(const Selection& selection,
                                                 const WorldFrame& frame,
                                                 const CityView& city) {
    std::optional<InspectorRequest> result;
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            result = *link;
        }
    };
    Title(SimulationHost::AccountName(frame, selection.organization));
    const sim::Organization* found =
        frame.economy != nullptr ? frame.economy->FindOrganization(selection.organization)
                                 : nullptr;
    if (found == nullptr) {
        ImGui::TextDisabled("The organization is gone.");
        return result;
    }
    const sim::Organization& organization = *found;
    const EconomyFrame& economy = *frame.economy;
    ImGui::TextDisabled("%s", KindOf(organization).c_str());

    // Facts and yesterday's books.
    if (ImGui::BeginTable("facts", 2, ImGuiTableFlags_SizingStretchProp)) {
        Fact("Credits", Credits(organization.credits));
        if (const sim::Loan* loan = economy.LoanOf(organization.id)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Owes");
            ImGui::TableNextColumn();
            ImGui::Text("%s to", Credits(loan->owed).c_str());
            ImGui::SameLine();
            follow(AccountLink(frame, city, loan->lender, ""));
        }
        if (organization.sector == sim::Sector::Industry) {
            Fact("Pays", std::format("{}% of full wages", organization.payPercent));
        }
        const sim::Books& books = organization.yesterday;
        Fact("Yesterday: revenue", Credits(books.revenue));
        Fact("Yesterday: wages", Credits(books.wages));
        Fact("Yesterday: supplies", Credits(books.supplies));
        Fact("Yesterday: upkeep", Credits(books.upkeep));
        Fact("Yesterday: dividends", Credits(books.dividends));
        Fact("Yesterday: repaid", Credits(books.repaid));
        ImGui::EndTable();
    }

    // Owners and their shares.
    ImGui::SeparatorText("Owners");
    std::uint64_t shares = 0;
    for (const sim::Share& share : organization.owners) {
        shares += share.shares;
    }
    if (organization.owners.empty()) {
        ImGui::TextDisabled("Nobody owns a share of it.");
    }
    for (const sim::Share& share : organization.owners) {
        follow(AccountLink(frame, city, share.owner, ""));
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f%%", shares > 0 ? 100.0 * static_cast<double>(share.shares) /
                                                       static_cast<double>(shares)
                                                 : 0.0);
    }

    // Who is in each room now, counted once for the property list.
    std::unordered_map<std::uint64_t, std::uint32_t> inside;
    for (const RobotFrame& robot : frame.robots) {
        if (robot.activity != sim::RobotActivity::Walking) {
            ++inside[robot.location.value];
        }
    }

    // Property, by building.
    const std::vector<OwnedRoom> property = RoomsOwnedBy(economy, city.Rooms(), organization.id);
    std::map<std::uint64_t, std::vector<const OwnedRoom*>> byBuilding;
    for (const OwnedRoom& room : property) {
        byBuilding[room.building.value].push_back(&room);
    }
    ImGui::SeparatorText(std::format("Property: {} rooms in {} buildings", property.size(),
                                     byBuilding.size())
                             .c_str());
    for (const auto& [building, rooms] : byBuilding) {
        ImGui::PushID(static_cast<int>(building));
        const bool open = ImGui::TreeNode("rooms", "%zu in", rooms.size());
        ImGui::SameLine();
        follow(BuildingLink(city, sim::EntityId{building}, ""));
        if (open) {
            if (ImGui::BeginTable("rooms", 5, kListFlags, ListSize(rooms.size()))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch, 3.0F);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 2.0F);
                ImGui::TableSetupColumn("Floor", ImGuiTableColumnFlags_WidthStretch, 1.5F);
                ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch, 1.5F);
                ImGui::TableSetupColumn("In now", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rooms.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const OwnedRoom& owned = *rooms[static_cast<std::size_t>(row)];
                        const auto room = city.Rooms().Find(owned.room);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (!room) {
                            ImGui::TextDisabled("gone");
                            continue;
                        }
                        follow(LinkTo(std::format("{}##room{}", room->room->name, owned.room.value),
                                      InspectorRequest{.selection = ShowRoom(city, owned.room)}));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(KindLabel(room->room->kind).c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(FloorName(room->room->floor).c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(PriceOf(*room->room).c_str());
                        ImGui::TableNextColumn();
                        const auto count = inside.find(owned.room.value);
                        ImGui::Text("%u", count != inside.end() ? count->second : 0U);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Everyone whose job room it owns.
    const std::vector<const RobotFrame*> workers = WorkersOf(frame, organization.id);
    ImGui::SeparatorText(std::format("Workers: {}", workers.size()).c_str());
    if (workers.empty()) {
        ImGui::TextDisabled("Nobody works for it.");
    } else if (ImGui::BeginTable("workers", 5, kListFlags, ListSize(workers.size()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Shift", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(workers.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RobotFrame& robot = *workers[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                follow(RobotLink(Name(frame, robot.id), ShowRobot(frame, city, robot.id)));
                ImGui::TableNextColumn();
                const auto job = city.Rooms().Find(robot.workspace);
                follow(RoomLink(city, robot.workspace, ""));
                ImGui::TableNextColumn();
                if (job) {
                    const sim::Shift shift = sim::JobShift(*job, robot.id);
                    ImGui::Text("%s-%s", sim::FormatClock(shift.startMinute).c_str(),
                                sim::FormatClock((shift.startMinute + shift.lengthMinutes) %
                                                 sim::kMinutesPerDay)
                                    .c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string{sim::JobTitle(robot.jobLevel)}.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Doing(robot));
            }
        }
        ImGui::EndTable();
    }

    // Banks: the loans they gave, and who saves with them.
    if (organization.kind == sim::OrganizationKind::Bank ||
        organization.kind == sim::OrganizationKind::CentralBank) {
        const std::vector<const sim::Loan*> loans = LoansFrom(economy, organization.id);
        std::int64_t lent = 0;
        for (const sim::Loan* loan : loans) {
            lent += loan->owed;
        }
        ImGui::SeparatorText(
            std::format("Loans: {}, {} owed to it", loans.size(), Credits(lent)).c_str());
        if (!loans.empty() && ImGui::BeginTable("loans", 3, kListFlags, ListSize(loans.size()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Borrower", ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("Owes", ImGuiTableColumnFlags_WidthStretch, 1.5F);
            ImGui::TableSetupColumn("Since", ImGuiTableColumnFlags_WidthStretch, 2.0F);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(loans.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const sim::Loan& loan = *loans[static_cast<std::size_t>(row)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    follow(AccountLink(frame, city, loan.borrower, ""));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Credits(loan.owed).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(sim::FormatTime(loan.takenAt).c_str());
                }
            }
            ImGui::EndTable();
        }
        std::uint32_t savers = 0;
        std::int64_t saved = 0;
        for (const RobotFrame& robot : frame.robots) {
            if (robot.bank == organization.id && robot.savings > 0) {
                ++savers;
                saved += robot.savings;
            }
        }
        ImGui::SeparatorText("Savings");
        ImGui::Text("%u robots save %s here", savers, Credits(saved).c_str());
    }
    return result;
}

}  // namespace mechapunk
