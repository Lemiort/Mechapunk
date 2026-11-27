#include "Statistics.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "GangLists.hpp"
#include "Links.hpp"
#include "ProjectLists.hpp"
#include "StatisticsInternal.hpp"

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Gangs.hpp"

#include "SimulationHost.hpp"

namespace mechapunk {

namespace {

constexpr ImVec4 kGood{0.45F, 0.9F, 0.55F, 1.0F};
constexpr ImVec4 kBad{1.0F, 0.4F, 0.35F, 1.0F};

std::string PerMille(std::int64_t value) {
    return std::format("{:.1f}%", static_cast<double>(value) / 10.0);
}

std::string Share(std::int64_t part, std::int64_t whole) {
    return whole > 0 ? std::format("{:.1f}%", 100.0 * static_cast<double>(part) /
                                                  static_cast<double>(whole))
                     : std::string{"-"};
}

// `Plot` and `PlotCount` live in `StatisticsInternal.hpp` (step D5e2b: the
// Construction section's plots read them too); the two wrappers below and the
// tabs of this file call them.
void PlotCredits(const char* label, const std::vector<sim::DailyStats>& history,
                 const Stat& stat) {
    Plot(label, history, stat,
         [](double value) { return Credits(static_cast<std::int64_t>(value)); });
}

void PlotPerMille(const char* label, const std::vector<sim::DailyStats>& history,
                  const Stat& stat) {
    Plot(label, history, stat,
         [](double value) { return PerMille(static_cast<std::int64_t>(value)); });
}

std::int64_t MoneyInAccounts(const WorldFrame& frame) {
    std::int64_t money = frame.economy->supplyMarket;
    for (const RobotFrame& robot : frame.robots) {
        money += robot.credits;
    }
    for (const sim::Organization& organization : frame.economy->organizations) {
        money += organization.credits;
    }
    return money;
}

void DrawOverview(const WorldFrame& frame, const std::vector<sim::DailyStats>& history) {
    const EconomyFrame& economy = *frame.economy;
    std::int64_t savings = 0;
    std::int64_t credits = 0;
    std::uint32_t employed = 0;
    for (const RobotFrame& robot : frame.robots) {
        savings += robot.savings;
        credits += robot.credits;
        employed += robot.workspace.IsValid() ? 1 : 0;
    }
    const auto population = static_cast<std::int64_t>(frame.robots.size());
    ImGui::SeparatorText("Now");
    if (BeginFacts("now")) {
        Row("Robots", std::format("{} ({} with a job, {})", population, employed,
                                  Share(employed, population)));
        Row("Money in the city", Credits(economy.created - economy.destroyed));
        Row("Robots' credits and savings", std::format("{} + {}", Credits(credits), Credits(savings)));
        Row("Created by the central bank today", Credits(economy.today.created));
        Row("Wages paid today", Credits(economy.today.wages));
        Row("Spent at venues today", Credits(economy.today.venueSales));
        ImGui::EndTable();
    }
    if (history.empty()) {
        ImGui::TextDisabled("Statistics start at the end of the first day.");
        return;
    }
    const sim::DailyStats& day = history.back();
    ImGui::SeparatorText(std::format("End of day {}", day.day).c_str());
    if (BeginFacts("yesterday")) {
        Row("Median wealth", Credits(day.medianWealth));
        Row("Wealth inequality (Gini)", PerMille(day.giniWealth));
        Row("Richest tenth holds", PerMille(day.topTenthShare));
        Row("Homeowners", Share(day.homeowners, day.population));
        Row("Out of work", std::format("{} ({})", day.unemployed, Share(day.unemployed, day.population)));
        Row("In debt", std::format("{} robots, {}", day.indebtedRobots, Credits(day.robotDebt)));
        Row("Without a friend", Share(day.lonely, day.population));
        Row("Average charge / condition / social / fun",
            std::format("{} / {} / {} / {}", day.averageCharge, day.averageCondition,
                        day.averageSocial, day.averageFun));
        ImGui::EndTable();
    }
    PlotCredits("Median wealth", history, [](const auto& d) { return d.medianWealth; });
    PlotPerMille("Gini", history, [](const auto& d) { return d.giniWealth; });
    PlotCount("Out of work", history, [](const auto& d) { return d.unemployed; });
}

void DrawEconomy(const std::vector<sim::DailyStats>& history) {
    if (history.empty()) {
        ImGui::TextDisabled("Statistics start at the end of the first day.");
        return;
    }
    const sim::DailyStats& day = history.back();
    const sim::DayCounters& counted = day.counted;
    ImGui::SeparatorText("Money");
    PlotCredits("Money in the city", history, [](const auto& d) { return d.moneySupply; });
    PlotCredits("Created that day", history, [](const auto& d) { return d.counted.created; });
    PlotCredits("Robots' credits", history, [](const auto& d) { return d.robotMoney; });
    PlotCredits("Robots' savings", history, [](const auto& d) { return d.robotSavings; });
    PlotCredits("Corporations' credits", history, [](const auto& d) { return d.corporationMoney; });
    PlotCredits("Banks' credits", history, [](const auto& d) { return d.bankMoney; });
    ImGui::SeparatorText("Debts");
    PlotCount("Central bank rate, 1/10000 a night", history,
              [](const auto& d) { return d.policyRate; });
    PlotCredits("Interest paid on savings", history,
                [](const auto& d) { return d.counted.savingsInterest; });
    PlotCredits("Robots owe", history, [](const auto& d) { return d.robotDebt; });
    PlotCredits("Corporations owe", history, [](const auto& d) { return d.corporationDebt; });
    PlotCredits("Banks owe the central bank", history, [](const auto& d) { return d.bankDebt; });
    ImGui::SeparatorText(std::format("Flows on day {}", day.day).c_str());
    if (BeginFacts("flows")) {
        Row("Wages", std::format("{} at {}% of base pay", Credits(counted.wages), day.averagePayPercent));
        Row("Venue sales", Credits(counted.venueSales));
        Row("Rent", Credits(counted.rent));
        Row("Upkeep", Credits(counted.upkeep));
        Row("Fares", Credits(counted.fares));
        Row("Supplies bought from industry", Credits(counted.supplies));
        Row("Dividends", Credits(counted.dividends));
        Row("Interest added to loans", Credits(counted.interest));
        Row("Lent", std::format("{} in {} loans", Credits(counted.lent), counted.loansTaken));
        Row("Written off", Credits(counted.writtenOff));
        ImGui::EndTable();
    }
    ImGui::SeparatorText("City Hall");
    if (BeginFacts("city")) {
        Row("Wage / property / profit tax",
            std::format("{} / {} / {}", Credits(counted.wageTax), Credits(counted.propertyTax),
                        Credits(counted.profitTax)));
        Row("Support for robots out of work", Credits(counted.benefits));
        Row("Public works", Credits(counted.publicSpending));
        Row("Treasury / owed", std::format("{} / {}", Credits(day.governmentMoney),
                                           Credits(day.governmentDebt)));
        ImGui::EndTable();
    }
    PlotCredits("Taxes", history, [](const auto& d) {
        return d.counted.wageTax + d.counted.propertyTax + d.counted.profitTax;
    });
    PlotCredits("Support and public works", history,
                [](const auto& d) { return d.counted.benefits + d.counted.publicSpending; });
    PlotCredits("Treasury", history, [](const auto& d) { return d.governmentMoney; });
    PlotCredits("Wages", history, [](const auto& d) { return d.counted.wages; });
    PlotCredits("Venue sales", history, [](const auto& d) { return d.counted.venueSales; });
    PlotCount("Industry pay, % of base", history, [](const auto& d) { return d.averagePayPercent; });
    PlotCount("Price of supplies, % of venue takings", history,
              [](const auto& d) { return d.supplyPercent; });
    PlotCount("Venue prices, % of usual", history,
              [](const auto& d) { return d.venuePricePercent; });
    PlotCount("Venues open", history, [](const auto& d) { return d.openVenues; });
    PlotCount("Empty units", history, [](const auto& d) { return d.emptyUnits; });
    ImGui::SeparatorText("Wealth");
    if (BeginFacts("wealth")) {
        Row("Credits: median / mean", std::format("{} / {}", Credits(day.medianCredits), Credits(day.meanCredits)));
        Row("Wealth: median / mean", std::format("{} / {}", Credits(day.medianWealth), Credits(day.meanWealth)));
        Row("Gini", PerMille(day.giniWealth));
        Row("Richest tenth / poorer half hold",
            std::format("{} / {}", PerMille(day.topTenthShare), PerMille(day.bottomHalfShare)));
        Row("Broke (less than a night's rent)", std::format("{}", day.broke));
        Row("Behind on bills", std::format("{}", day.behindOnBills));
        ImGui::EndTable();
    }
    PlotPerMille("Gini", history, [](const auto& d) { return d.giniWealth; });
    PlotPerMille("Richest tenth's share", history, [](const auto& d) { return d.topTenthShare; });
    PlotCount("Broke", history, [](const auto& d) { return d.broke; });
}

void DrawSociety(const std::vector<sim::DailyStats>& history) {
    if (history.empty()) {
        ImGui::TextDisabled("Statistics start at the end of the first day.");
        return;
    }
    const sim::DailyStats& day = history.back();
    const sim::DayCounters& counted = day.counted;
    ImGui::SeparatorText("People");
    if (BeginFacts("people")) {
        Row("Robots", std::format("{} ({} arrived, {} scrapped that day)", day.population,
                                  counted.arrivals, counted.scrapped));
        Row("Average age / wear", std::format("{} days / {}", day.averageAge, day.averageWear));
        Row("Junior / regular / senior / lead",
            std::format("{} / {} / {} / {}", day.jobLevels[0], day.jobLevels[1], day.jobLevels[2],
                        day.jobLevels[3]));
        Row("Out of work", std::format("{}", day.unemployed));
        Row("Promoted / lost a job / found one",
            std::format("{} / {} / {}", counted.promotions, counted.jobsLost, counted.jobsFound));
        Row("Moved up / evicted", std::format("{} / {}", counted.movedUp, counted.evictions));
        ImGui::EndTable();
    }
    PlotCount("Robots", history, [](const auto& d) { return d.population; });
    PlotCount("Out of work", history, [](const auto& d) { return d.unemployed; });
    ImGui::SeparatorText("Wellbeing");
    PlotCount("Average charge", history, [](const auto& d) { return d.averageCharge; });
    PlotCount("Average condition", history, [](const auto& d) { return d.averageCondition; });
    PlotCount("Average social", history, [](const auto& d) { return d.averageSocial; });
    PlotCount("Average fun", history, [](const auto& d) { return d.averageFun; });
    ImGui::SeparatorText("Relationships");
    if (BeginFacts("relationships")) {
        Row("With a friend / a close friend / a rival",
            std::format("{} / {} / {}", day.withFriend, day.withCloseFriend, day.withRival));
        Row("Without a friend", std::format("{}", day.lonely));
        Row("Talks / arguments", std::format("{} / {}", counted.talks, counted.arguments));
        Row("New friendships / rivalries",
            std::format("{} / {}", counted.newFriendships, counted.newRivalries));
        ImGui::EndTable();
    }
    PlotCount("With a friend", history, [](const auto& d) { return d.withFriend; });
    PlotCount("With a rival", history, [](const auto& d) { return d.withRival; });
    ImGui::SeparatorText("Getting about");
    if (BeginFacts("travel")) {
        Row("Walks / by metro", std::format("{} / {}", counted.walks, counted.metroRides));
        Row("Venue visits", std::format("{}", counted.visits));
        Row("Late for work", std::format("{}", counted.lateArrivals));
        Row("Dodgy parts", std::format("{}", counted.badParts));
        ImGui::EndTable();
    }
    PlotCount("Metro rides", history, [](const auto& d) { return d.counted.metroRides; });
    PlotCount("Venue visits", history, [](const auto& d) { return d.counted.visits; });
}

// Draws the Property tab. The Construction section's links report what the
// player clicked through `request`, as the Crime tab's links do.
void DrawProperty(const std::vector<sim::DailyStats>& history, const WorldFrame& frame,
                  const CityView& city, std::optional<InspectorRequest>* request) {
    if (history.empty()) {
        ImGui::TextDisabled("Statistics start at the end of the first day.");
        return;
    }
    const sim::DailyStats& day = history.back();
    if (BeginFacts("property")) {
        Row("Homeowners", std::format("{} ({})", day.homeowners, Share(day.homeowners, day.population)));
        Row("Tenants", std::format("{}", day.tenants));
        Row("Landlords", std::format("{}", day.landlords));
        Row("Let by organizations", std::format("{}", day.corporateApartments));
        Row("Vacant", std::format("{}", day.vacantApartments));
        Row("Shareholders", std::format("{}", day.shareholders));
        Row("In dorms / studios / flats / two-bedroom / penthouses",
            std::format("{} / {} / {} / {} / {}", day.residentsByClass[0], day.residentsByClass[1],
                        day.residentsByClass[2], day.residentsByClass[3], day.residentsByClass[4]));
        Row("Sharing a home", std::format("{} ({} moved in together that day)", day.sharing,
                                          day.counted.movedInTogether));
        Row("Sales that day", std::format("{} for {}", day.counted.propertySales,
                                          Credits(day.counted.propertySalesValue)));
        ImGui::EndTable();
    }
    PlotCount("Homeowners", history, [](const auto& d) { return d.homeowners; });
    PlotCount("In dorms", history, [](const auto& d) { return d.residentsByClass[0]; });
    PlotCount("In studios", history, [](const auto& d) { return d.residentsByClass[1]; });
    PlotCount("In one-bedroom flats", history, [](const auto& d) { return d.residentsByClass[2]; });
    PlotCount("In two-bedroom flats", history, [](const auto& d) { return d.residentsByClass[3]; });
    PlotCount("In penthouses", history, [](const auto& d) { return d.residentsByClass[4]; });
    PlotCount("Sharing a home", history, [](const auto& d) { return d.sharing; });
    ImGui::SeparatorText("Rents asked");
    PlotCredits("Dorm room", history, [](const auto& d) { return d.averageRent[0]; });
    PlotCredits("Studio", history, [](const auto& d) { return d.averageRent[1]; });
    PlotCredits("One-bedroom flat", history, [](const auto& d) { return d.averageRent[2]; });
    PlotCredits("Two-bedroom flat", history, [](const auto& d) { return d.averageRent[3]; });
    PlotCredits("Penthouse", history, [](const auto& d) { return d.averageRent[4]; });
    PlotCount("Tenants", history, [](const auto& d) { return d.tenants; });
    PlotCount("Landlords", history, [](const auto& d) { return d.landlords; });
    PlotCount("Let by corporations", history, [](const auto& d) { return d.corporateApartments; });
    PlotCount("Sales", history, [](const auto& d) { return d.counted.propertySales; });

    // Buildings by style, habitats and workplaces apart, counted from the
    // city the view holds: the style is saved with each building, so there is
    // no counter of its own (task I, step I7).
    ImGui::SeparatorText("Buildings by style (sites going up left out)");
    constexpr std::size_t kStyles = static_cast<std::size_t>(sim::BuildingStyle::Tower) + 1;
    std::array<std::array<std::uint32_t, 2>, kStyles> byStyle{};
    for (const sim::Building& building : city.City().buildings) {
        // A site is a scaffold, not yet the style it carries.
        if (ProjectOn(frame, building.id) != nullptr) {
            continue;
        }
        ++byStyle.at(static_cast<std::size_t>(building.style))
              .at(building.kind == sim::BuildingKind::Habitat ? 0 : 1);
    }
    if (ImGui::BeginTable("styles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Style");
        ImGui::TableSetupColumn("Habitats");
        ImGui::TableSetupColumn("Workplaces");
        ImGui::TableHeadersRow();
        for (std::size_t style = 0; style != kStyles; ++style) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                std::string{sim::StyleName(static_cast<sim::BuildingStyle>(style))}.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", byStyle.at(style).at(0));
            ImGui::TableNextColumn();
            ImGui::Text("%u", byStyle.at(style).at(1));
        }
        ImGui::EndTable();
    }

    // The Construction section sits in its own file, as the Chronicle tab
    // does: the by-day plots of the construction counters, the sites going
    // up now, their facts and their links.
    DrawConstruction(history, frame, city, request);
}

std::optional<InspectorRequest> DrawOrganizations(StatisticsView& view, const WorldFrame& frame) {
    const EconomyFrame& economy = *frame.economy;
    std::optional<InspectorRequest> request;
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("organizations", 8, kFlags, ImVec2{0.0F, 260.0F})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        for (const char* column : {"Sector", "Credits", "Owes", "Revenue", "Wages", "Dividends",
                                   "Pay"}) {
            ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, 1.4F);
        }
        ImGui::TableHeadersRow();
        for (const sim::Organization& organization : economy.organizations) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // Opens the organization in the inspector.
            if (auto link = OrganizationLink(frame, organization.id, "")) {
                request = *link;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(organization.kind == sim::OrganizationKind::CentralBank
                                       ? "central bank"
                                       : std::string{sim::SectorName(organization.sector)}.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(Credits(organization.credits).c_str());
            ImGui::TableSetColumnIndex(3);
            const sim::Loan* loan = economy.LoanOf(organization.id);
            ImGui::TextUnformatted(loan != nullptr ? Credits(loan->owed).c_str() : "");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(Credits(organization.yesterday.revenue).c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(Credits(organization.yesterday.wages).c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(Credits(organization.yesterday.dividends).c_str());
            ImGui::TableSetColumnIndex(7);
            if (organization.sector == sim::Sector::Industry) {
                ImGui::Text("%u%%", organization.payPercent);
            }
        }
        ImGui::EndTable();
    }
    return request;
}

void DrawCentralBank(const WorldFrame& frame, const std::vector<sim::DailyStats>& history,
                     std::optional<InspectorRequest>* request) {
    const EconomyFrame& economy = *frame.economy;
    const std::int64_t expected = economy.created - economy.destroyed;
    const std::int64_t held = MoneyInAccounts(frame);
    if (BeginFacts("central")) {
        Row("Created since the city began", Credits(economy.created));
        Row("Destroyed (paid back)", Credits(economy.destroyed));
        Row("Should be in circulation", Credits(expected));
        Row("Held in all accounts", Credits(held));
        Row("Created today", Credits(economy.today.created));
        ImGui::EndTable();
    }
    if (held == expected) {
        ImGui::TextColored(kGood, "The money adds up.");
    } else {
        ImGui::TextColored(kBad, "The money is off by %s!", Credits(held - expected).c_str());
    }
    PlotCredits("Created per day", history, [](const auto& d) { return d.counted.created; });
    PlotCredits("Destroyed per day", history, [](const auto& d) { return d.counted.destroyed; });
    ImGui::SeparatorText("Log, newest first");
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("emissions", 4, kFlags, ImVec2{0.0F, 260.0F})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char* column : {"When", "Account", "Amount", "Why"}) {
            ImGui::TableSetupColumn(column);
        }
        ImGui::TableHeadersRow();
        for (auto emission = economy.emissions.rbegin(); emission != economy.emissions.rend();
             ++emission) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sim::FormatTime(emission->tick).c_str());
            ImGui::TableSetColumnIndex(1);
            if (auto link = AccountLink(frame, emission->account, "")) {
                *request = *link;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(emission->amount >= 0 ? kBad : kGood, "%s%s",
                               emission->amount >= 0 ? "+" : "", Credits(emission->amount).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(std::string{sim::EmissionReasonName(emission->reason)}.c_str());
        }
        ImGui::EndTable();
    }
}

// Draws the Crime tab. Links in it report what the player clicked through
// `request`, as the Central bank tab does: a link only returns on the frame
// it is clicked, so a drawing function that drops that value has links the
// player can hover and never use.
void DrawCrime(const std::vector<sim::DailyStats>& history, const WorldFrame& frame,
               const CityView& city, std::optional<InspectorRequest>* request) {
    // One click per frame is all a player can ask for; the last link clicked
    // wins, as it does on the gang page.
    const auto follow = [&](const std::optional<InspectorRequest>& link) {
        if (link) {
            *request = *link;
        }
    };
    if (history.empty()) {
        ImGui::TextDisabled("Statistics start at the end of the first day.");
        return;
    }
    const sim::DailyStats& day = history.back();
    const sim::DayCounters& counted = day.counted;

    ImGui::SeparatorText("Crime");
    if (BeginFacts("crime")) {
        Row("Robberies", std::format("{} ({} cr)", counted.robberies, Credits(counted.robbedCredits)));
        Row("Arrests", std::format("{}", counted.arrests));
        Row("Gang fights", std::format("{}", counted.fights));
        Row("Robots in gangs today", std::to_string(day.gangMembers));
        ImGui::EndTable();
    }

    PlotCount("Robberies", history, [](const auto& d) { return d.counted.robberies; });
    PlotCredits("Stolen credits", history,
                [](const auto& d) { return d.counted.robbedCredits; });
    PlotCount("Arrests", history, [](const auto& d) { return d.counted.arrests; });
    PlotCount("Gang fights", history, [](const auto& d) { return d.counted.fights; });

    ImGui::SeparatorText("Gangs");
    if (frame.gangs == nullptr || frame.gangs->empty()) {
        ImGui::TextDisabled("No gangs yet.");
    } else {
        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;

        // The gang table below reads every gang once per frame through the
        // tested GangLists functions (guide §9.4): `members` gives the Members
        // column and `robberiesToday` the Robberies column, how many of this
        // night's crimes its members remember as robberies. Rows of GangCrimes
        // come newest first, so the walk stops at the first crime before this
        // midnight; `frame.gangs` is sorted by id, so a row keeps its place
        // between frames and the player can click the same gang twice.
        const auto today = sim::StartOfDay(frame.tick);
        std::vector<std::vector<const RobotFrame*>> members;
        std::vector<std::uint32_t> robberiesToday;
        for (const sim::Gang& gang : *frame.gangs) {
            members.push_back(MembersOf(frame, gang.id));
            std::uint32_t robberies = 0;
            for (const GangCrime& crime : GangCrimes(frame, gang.id)) {
                if (crime.tick < today) {
                    break;
                }
                if (crime.kind == CrimeKind::Robbery) {
                    ++robberies;
                }
            }
            robberiesToday.push_back(robberies);
        }

        if (ImGui::BeginTable("gangs", 5, kFlags, ImVec2{0.0F, 350.0F})) {
            for (const char* column :
                {"Gang", "Colour", "Members", "Robberies", "Turf"}) {
                ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, 0.8F);
            }
            ImGui::TableHeadersRow();

            for (std::size_t index = 0; index < frame.gangs->size(); ++index) {
                const sim::Gang& gang = (*frame.gangs)[index];
                ImGui::TableNextRow();
                // Gang name and colour as a link: opens the gang's page.
                ImGui::TableSetColumnIndex(0);
                follow(GangLink(frame, gang.id, ""));

                // Colour again as a square with its hex code. GangSwatch
                // counts how often this frame drew this gang's square, so the
                // one here and the one in front of the name stay two widgets.
                ImGui::TableSetColumnIndex(1);
                const ImVec4 gangColour{static_cast<float>((gang.color >> 16) & 0xFF) / 255.0F,
                                        static_cast<float>((gang.color >> 8) & 0xFF) / 255.0F,
                                        static_cast<float>(gang.color & 0xFF) / 255.0F,
                                        1.0F};
                GangSwatch(std::format("gang##{}", gang.id.value), gangColour,
                           ImVec2{24.0F, 16.0F});
                ImGui::SameLine();
                std::string colourStr = std::format("#{:06X}", gang.color);
                ImGui::TextUnformatted(colourStr.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(std::to_string(members[index].size()).c_str());

                // Robberies by this gang's members since midnight; counted
                // by kind, as the text-prefix count went dead when B9a made
                // `text` the bare act.
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(std::to_string(robberiesToday[index]).c_str());

                // Turf. The members, relations and crimes a gang holds are
                // lists of things, not a figure per gang, so they left this
                // tab for the gang's own page, which its name in column 0
                // opens.
                ImGui::TableSetColumnIndex(4);
                if (gang.turf.value != 0) {
                    follow(BuildingLink(city, gang.turf, ""));
                } else {
                    ImGui::TextDisabled("no turf");
                }
            }
            ImGui::EndTable();
        }

        // What a gang holds - its members, its relations, the crimes its
        // members remember, the rivals standing on its turf - is on the gang's
        // own page in the inspector, and the name in the table above opens it.
        ImGui::TextDisabled(
            "Click a gang's name for its members, relations and crimes.");
    }
}

}  // namespace

std::optional<InspectorRequest> DrawStatistics(StatisticsView& view, const WorldFrame& frame,
                                               const CityView& city) {
    if (!view.open || frame.economy == nullptr) {
        return std::nullopt;
    }
    std::optional<InspectorRequest> request;
    // Beside the City panel, leaving the right side to the inspector.
    ImGui::SetNextWindowPos(ImVec2{360.0F, 10.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{760.0F, 720.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Statistics", &view.open)) {
        static const std::vector<sim::DailyStats> kNone;
        const std::vector<sim::DailyStats>& history =
            frame.economy->history != nullptr ? *frame.economy->history : kNone;
        if (ImGui::BeginTabBar("statistics")) {
            if (ImGui::BeginTabItem("Overview")) {
                DrawOverview(frame, history);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Economy")) {
                DrawEconomy(history);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Society")) {
                DrawSociety(history);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Property")) {
                DrawProperty(history, frame, city, &request);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Organizations")) {
                request = DrawOrganizations(view, frame);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Central bank")) {
                DrawCentralBank(frame, history, &request);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Crime")) {
                DrawCrime(history, frame, city, &request);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Chronicle")) {
                DrawChronicle(view, frame, city, &request);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return request;
}

}  // namespace mechapunk
