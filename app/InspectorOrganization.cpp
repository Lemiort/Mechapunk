#include "InspectorInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "Links.hpp"
#include "OrganizationLists.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Venues.hpp"

namespace mechapunk {

namespace {

// Format an integer with space-separated thousands.
std::string Credits(std::int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string grouped;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index > 0 && (digits.size() - index) % 3 == 0) {
            grouped += ' ';
        }
        grouped += digits[index];
    }
    return std::format("{}{} cr", value < 0 ? "-" : "", grouped);
}

// Format a percentage.
std::string Percent(std::uint64_t part, std::int64_t total) {
    return total > 0
        ? std::format("{:.1f}%", 100.0 * static_cast<double>(part) / static_cast<double>(total))
        : std::string{"-"};
}

// Format shift start/end as clock times.
std::string ShiftTimes(sim::Shift shift) {
    return std::format(
        "{}, {}", Clock(shift.startMinute),
        Clock((shift.startMinute + shift.lengthMinutes) % sim::kMinutesPerDay));
}

}  // namespace

// The organization's page in the inspector: facts, owners, property, workers,
// and for banks loans given.
std::optional<InspectorRequest> DrawOrganization(
    const Selection& selection, const WorldFrame& frame,
    const CityView& city, const SimulationHost& host) {
    std::optional<InspectorRequest> result;

    // Title: the organization's name.
    std::string orgName = SimulationHost::AccountName(frame, selection.organization);
    Title(orgName);

    if (frame.economy == nullptr ||
        frame.economy->FindOrganization(selection.organization) == nullptr) {
        ImGui::TextDisabled("The organization is gone.");
        return result;
    }

    const sim::Organization& org = *frame.economy->FindOrganization(
        selection.organization);

    // Kind and sector label.
    if (org.kind == sim::OrganizationKind::CentralBank) {
        ImGui::TextDisabled("central bank, %s", SectorLabel(org.sector).c_str());
    } else {
        std::string kind;
        switch (org.kind) {
            case sim::OrganizationKind::Corporation: kind = "corporation"; break;
            case sim::OrganizationKind::Bank:        kind = "bank";        break;
            case sim::OrganizationKind::Government:  kind = "city hall";   break;
            default:                                 kind = "unknown";      break;
        }
        ImGui::TextDisabled("%s, %s", kind.c_str(), SectorLabel(org.sector).c_str());
    }

    // Facts section.
    if (ImGui::BeginTable("facts", 2,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_RowBg)) {
        // Credits.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Credits");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(Credits(org.credits).c_str());

        // What it owes (its loan, to its lender bank).
        const sim::Loan* orgLoan = frame.economy->LoanOf(selection.organization);
        if (orgLoan != nullptr) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Owes");
            ImGui::TableSetColumnIndex(1);
            // Use AccountLink to the lender.
            const std::string lenderName = SimulationHost::AccountName(frame, orgLoan->lender);
            if (auto link = OrganizationLink(frame, orgLoan->lender, "")) {
                result = *link;
                ImGui::SameLine();
            } else {
                ImGui::TextDisabled("%s to %s", Credits(orgLoan->owed).c_str(), lenderName.c_str());
            }
        }

        // Pay percent for industry.
        if (org.sector == sim::Sector::Industry) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Pay");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u%%", org.payPercent);
        }

        // Yesterday's revenue / wages.
        const sim::Books& books = org.yesterday;
        if (ImGui::BeginTable("books", 2,
                                  ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_RowBg)) {
            std::string label = "Revenue";
            std::string value = Credits(books.revenue);
            Row(label.c_str(), value.c_str());

            label = "Wages";
            value = Credits(books.wages);
            Row(label.c_str(), value.c_str());

            label = "Supplies";
            value = Credits(books.supplies);
            Row(label.c_str(), value.c_str());

            label = "Upkeep";
            value = Credits(books.upkeep);
            Row(label.c_str(), value.c_str());

            label = "Dividends";
            value = Credits(books.dividends);
            Row(label.c_str(), value.c_str());

            label = "Repaid";
            value = Credits(books.repaid);
            Row(label.c_str(), value.c_str());

            ImGui::EndTable();
        }

        ImGui::EndTable();
    }

    // Owners section.
    ImGui::SeparatorText("Owners");
    if (!org.owners.empty()) {
        std::uint64_t totalShares = 0;
        for (const auto& share : org.owners) {
            totalShares += share.shares;
        }

        // List owners as AccountLink with their share percentage.
        for (const sim::Share& share : org.owners) {
            std::string ownerName = SimulationHost::AccountName(frame, share.owner);
            std::string label =
                std::format("{} ({})##{}", ownerName,
                            Percent(share.shares, totalShares),
                            share.owner.value);

            // Use AccountLink (a robot or organization is clickable).
            if (auto link = AccountLink(frame, city, share.owner, "")) {
                result = *link;
            } else {
                // Scrapped account: plain text.
                ImGui::TextDisabled("%s (%s)", ownerName.c_str(),
                                    Percent(share.shares, totalShares).c_str());
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Show in the inspector");
            }
        }
    } else {
        ImGui::TextDisabled("Nobody owns this organization yet.");
    }

    // Property section: N rooms in M buildings.
    std::vector<OwnedRoom> property = RoomsOwnedBy(
        *frame.economy, city.Rooms(), selection.organization);
    const std::uint32_t roomCount = static_cast<std::uint32_t>(property.size());

    // Group by building id to count buildings and rooms per building.
    std::map<std::uint64_t, std::vector<const OwnedRoom*>> byBuilding;
    for (const auto& room : property) {
        byBuilding[room.building.value].push_back(&room);
    }
    const std::uint32_t buildingCount = static_cast<std::uint32_t>(byBuilding.size());

    ImGui::TextDisabled("%u rooms in %u buildings", roomCount, buildingCount);

    // One TreeNode per building: label with BuildingLink text and room count.
    for (const auto& [buildingIdValue, rooms] : byBuilding) {
        const sim::Building* building =
            city.City().FindBuilding(sim::EntityId{buildingIdValue});
        std::string buildLabel =
            building != nullptr ? building->name : "somewhere";

        ImGui::PushID(static_cast<int>(buildingIdValue));

        if (ImGui::TreeNode(
                std::format("{} ({})##{}", buildLabel, rooms.size(),
                            buildingIdValue).c_str())) {
            // Sort rooms by floor then id for stable display order.
            std::vector<OwnedRoom> sortedRooms;
            for (const OwnedRoom* roomPtr : rooms) {
                sortedRooms.push_back(*roomPtr);
            }
            std::sort(sortedRooms.begin(), sortedRooms.end(),
                      [](const OwnedRoom& a, const OwnedRoom& b) {
                          if (a.floor != b.floor) return a.floor < b.floor;
                          return a.room < b.room;
                      });

            // Table: room (RoomLink), kind (KindLabel), floor, rent/price,
            // occupants. Use Clipper for large lists (>50 rows).
            if (ImGui::BeginTable("rooms", 5, ImGuiTableFlags_RowBg |
                                                       ImGuiTableFlags_BordersInnerV |
                                                       ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Room");
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Floor",
                                        ImGuiTableColumnFlags_WidthFixed, 100.0F);
                ImGui::TableSetupColumn(
                    "Price", ImGuiTableColumnFlags_WidthFixed, 120.0F);
                ImGui::TableSetupColumn("Occupants",
                                        ImGuiTableColumnFlags_WidthFixed, 80.0F);
                ImGui::TableHeadersRow();

                // Clipper for large lists (>50 rows).
                const int totalRows = static_cast<int>(sortedRooms.size());
                if (totalRows > 50) {
                    ImGuiListClipper clipper;
                    clipper.Begin(totalRows);
                    while (clipper.Step()) {
                        for (int index = clipper.DisplayStart;
                             index < clipper.DisplayEnd; ++index) {
                            const OwnedRoom& room = sortedRooms[index];
                            ImGui::TableNextRow();

                            // RoomLink for the room name.
                            if (auto link = RoomLink(city, room.room, "")) {
                                result = *link;
                            }

                            // KindLabel for kind.
                            const auto foundRoom = city.Rooms().Find(room.room);
                            ImGui::TableNextColumn();
                            if (foundRoom) {
                                std::string kindText = KindLabel(foundRoom->room->kind);
                                ImGui::TextUnformatted(kindText.c_str());
                            } else {
                                ImGui::TextDisabled("gone");
                            }

                            // Floor.
                            ImGui::TableNextColumn();
                            if (foundRoom) {
                                std::string floorText =
                                    FloorName(foundRoom->room->floor);
                                ImGui::TextUnformatted(floorText.c_str());
                            } else {
                                ImGui::TextDisabled("-");
                            }

                            // Rent or price.
                            ImGui::TableNextColumn();
                            if (foundRoom) {
                                const auto* venueRules = sim::FindVenueRules(
                                    foundRoom->room->kind);
                                std::string priceText;
                                if (venueRules != nullptr) {
                                    const uint64_t price = static_cast<uint64_t>(
                                        std::max<std::uint64_t>(
                                            1,
                                            static_cast<uint64_t>(
                                                venueRules->price) *
                                            foundRoom->room->pricePercent /
                                            100));
                                    priceText = std::format(
                                        "{} cr a visit", price);
                                } else if (foundRoom->room->kind ==
                                           sim::RoomKind::Apartment) {
                                    priceText = std::format(
                                        "{} cr/night", foundRoom->room->rent);
                                } else {
                                    priceText = "-";
                                }
                                ImGui::TextUnformatted(priceText.c_str());
                            } else {
                                ImGui::TextDisabled("-");
                            }

                            // Occupants.
                            ImGui::TableNextColumn();
                            const auto& occupantsMap = Occupy(frame);
                            const auto occIt =
                                occupantsMap.find(room.room.value);
                            if (occIt != occupantsMap.end()) {
                                std::string occText =
                                    std::format("{}/{}",
                                                occIt->second.size(),
                                                foundRoom
                                                    ? foundRoom->room->capacity
                                                    : 0u);
                                ImGui::TextUnformatted(occText.c_str());
                            } else {
                                ImGui::TextDisabled("empty");
                            }

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Show in the inspector");
                            }
                        }
                    }
                    clipper.End();
                }
                ImGui::EndTable();
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Workers section: N workers, table with name (RobotLink), job room
    // (RoomLink), shift times, activity. Clipped for large lists.
    std::vector<const RobotFrame*> workers =
        WorkersOf(frame, selection.organization);
    const std::uint32_t workerCount = static_cast<std::uint32_t>(workers.size());

    ImGui::TextDisabled("%u workers", workerCount);

    if (!workers.empty()) {
        // Table with clipper for large lists (>50 rows).
        if (workerCount > 50) {
            const ImVec2 size = ImVec2{
                0.0F,
                std::min(static_cast<float>(workers.size()), 16.0F) *
                    ImGui::GetFrameHeightWithSpacing()};

            if (ImGui::BeginTable("workers", 4,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY,
                                  size)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Job Room");
                ImGui::TableSetupColumn("Shift",
                                        ImGuiTableColumnFlags_WidthFixed, 100.0F);
                ImGui::TableSetupColumn(
                    "Activity", ImGuiTableColumnFlags_WidthFixed, 130.0F);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(workers.size()));
                while (clipper.Step()) {
                    for (int index = clipper.DisplayStart;
                         index < clipper.DisplayEnd; ++index) {
                        const RobotFrame* robot = workers[index];
                        ImGui::TableNextRow();

                        // Name with RobotLink. Double-click jumps to the
                        // robot.
                        std::string nameText = Name(frame, robot->id);
                        Selection sel{.robot = robot->id};
                        if (auto link = RobotLink(nameText, sel)) {
                            result = *link;
                        }

                        // Job Room with RoomLink.
                        ImGui::TableNextColumn();
                        const auto jobRoomRef =
                            city.Rooms().Find(robot->workspace);
                        if (jobRoomRef) {
                            std::string roomLabelText =
                                std::format("{} ({})",
                                            jobRoomRef->room->name,
                                            jobRoomRef->building->name);
                            Selection roomSel{.building =
                                                  jobRoomRef->building->id,
                                              .floor =
                                                  jobRoomRef->room->floor};
                            if (auto link = RoomLink(city, robot->workspace,
                                                     "")) {
                                result = *link;
                            } else {
                                ImGui::TextUnformatted(roomLabelText.c_str());
                            }
                        } else {
                            ImGui::TextDisabled("gone");
                        }

                        // Shift times.
                        ImGui::TableNextColumn();
                        if (jobRoomRef) {
                            sim::Shift shift = sim::JobShift(
                                *jobRoomRef, robot->id);
                            std::string shiftText = ShiftTimes(shift);
                            ImGui::TextUnformatted(shiftText.c_str());
                        } else {
                            ImGui::TextDisabled("out of work");
                        }

                        // What is the robot doing now.
                        ImGui::TableNextColumn();
                        switch (robot->activity) {
                            case sim::RobotActivity::Resting:
                                if (robot->nextActionAt == sim::kNoCommute) {
                                    ImGui::TextUnformatted("resting at home");
                                } else {
                                    ImGui::Text(
                                        "resting until %s",
                                        Clock(robot->nextActionAt).c_str());
                                }
                                break;
                            case sim::RobotActivity::Working:
                                ImGui::Text("working until %s",
                                            Clock(robot->nextActionAt).c_str());
                                break;
                            case sim::RobotActivity::Visiting:
                                ImGui::Text("out until %s",
                                            Clock(robot->nextActionAt).c_str());
                                break;
                            default: {  // Walking.
                                const auto& stations = city.City().stations;
                                if (robot->destination.value < static_cast<std::uint64_t>(stations.size())) {
                                    ImGui::Text("walking to %s",
                                                stations[robot->destination.value]
                                                    .name.c_str());
                                } else {
                                    ImGui::TextUnformatted("walking");
                                }
                                break;
                            }
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Show in the inspector");
                        }
                    }
                }
                clipper.End();
                ImGui::EndTable();
            }
        }
    } else {
        ImGui::TextDisabled("No workers yet.");
    }

    // For banks: loans given and robots saving.
    if (org.kind == sim::OrganizationKind::Bank) {
        ImGui::SeparatorText("Loans");

        std::vector<const sim::Loan*> loans = LoansFrom(
            *frame.economy, selection.organization);
        if (!loans.empty()) {
            if (ImGui::BeginTable("loans", 3,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Borrower");
                ImGui::TableSetupColumn(
                    "Owed", ImGuiTableColumnFlags_WidthFixed, 150.0F);
                ImGui::TableSetupColumn(
                    "Taken", ImGuiTableColumnFlags_WidthFixed, 80.0F);
                ImGui::TableHeadersRow();

                for (const sim::Loan* loan : loans) {
                    ImGui::PushID(static_cast<int>(loan->borrower.value));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // AccountLink to the borrower.
                    if (auto link =
                            AccountLink(frame, city, loan->borrower, "")) {
                        result = *link;
                    } else {
                        const std::string nameText =
                            SimulationHost::AccountName(frame, loan->borrower);
                        ImGui::TextDisabled("%s", nameText.c_str());
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Credits(loan->owed).c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(std::string{sim::FormatTime(loan->takenAt)}.c_str());

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show in the inspector");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("No loans given yet.");
        }

        // Robots saving with this bank.
        std::uint32_t savingsCount = 0;
        for (const auto& robot : frame.robots) {
            if (robot.bank == selection.organization && robot.savings > 0) {
                ++savingsCount;
            }
        }

        ImGui::SeparatorText("Savings");
        ImGui::TextDisabled("%u robots saving here", savingsCount);
    }

    return result;
}

}  // namespace mechapunk
