#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk::sim {

namespace {
constexpr std::uint64_t kEconomyStream = 7;
// Of every credit a sector takes, this much buys supplies from industry.
constexpr std::int64_t kMinSupplyPercent = 20;
constexpr std::int64_t kMaxSupplyPercent = 95;
constexpr std::int64_t kUtilitySupplyPercent = 95;
constexpr std::int64_t kTransitSupplyPercent = 95;
constexpr std::int64_t kHousingSupplyPercent = 50;
// A robot with a job may owe up to this many shifts' wages.
constexpr std::int64_t kRobotDebtShifts = 5;
// Each night, the chance in percent that a robot able to buy an apartment
// does, and the nights of rent it keeps after paying.
constexpr std::uint32_t kBuyPercent = 3;
constexpr std::int64_t kBuyReserveNights = 20;
// A mortgage needs this much of the price down, and lends at most this many
// shifts' pay.
constexpr std::int64_t kDepositPercent = 20;
constexpr std::int64_t kMortgageShifts = 60;
// Venue prices move a step a night between these, in percent of base;
// a venue counts as busy when it had kBusyTurns times its places in guests.
constexpr std::uint32_t kPriceStep = 1;
constexpr std::uint32_t kMinPricePercent = 50;
constexpr std::uint32_t kMaxPricePercent = 300;
constexpr std::uint32_t kBusyTurns = 3;
// Market rents move a point a night while fewer homes than the tight share
// stand empty, or more than the loose share; within these bounds.
constexpr std::int64_t kTightVacancyPercent = 5;
constexpr std::int64_t kLooseVacancyPercent = 15;
constexpr std::int64_t kMinRentPercent = 50;
constexpr std::int64_t kMaxRentPercent = 300;
// A venue closes after this many nights in a row with fewer guests than
// half its places, its prices already cut to kQuietPricePercent or less; a
// workspace after as many under half staffed, while under the low share of
// robots, in percent, is out of work.
constexpr std::uint32_t kCloseAfterNights = 10;
constexpr std::uint32_t kQuietPricePercent = 80;
constexpr std::int64_t kLowUnemploymentPercent = 2;
// Each night an empty unit may open, with this chance in percent: as a
// workspace in a workplace while over the high share of robots is out of
// work, otherwise as the venue the city is most short of.
constexpr std::uint32_t kOpenUnitPercent = 10;
constexpr std::int64_t kHighUnemploymentPercent = 5;
// Venues of a kind count as in demand while their prices average this, in
// percent of usual.
constexpr std::int64_t kInDemandPercent = 105;
constexpr std::uint32_t kMinWorkspaceStaff = 5;
constexpr std::uint32_t kMaxWorkspaceStaff = 10;
// Each night, the chance a robot sells an apartment nobody lives in.
constexpr std::uint32_t kSellVacantPercent = 20;
constexpr std::uint32_t kMinPayPercent = 50;
constexpr std::uint32_t kMaxPayPercent = 150;
// What a bank keeps before lending or paying out, plus a share of what it
// has lent, in percent.
constexpr std::int64_t kBankFloor = 1000;
constexpr std::int64_t kBankReservePercent = 10;
constexpr std::int64_t kCorporationFloor = 200;
// Of what a corporation has beyond twice its reserve, the share paid out
// each night.
constexpr std::int64_t kDividendPercent = 50;
// A robot's daily budget for outings: this share of its average day's pay,
// plus 1/kSavingsSpendDays of what it has saved beyond kSavingsTargetNights
// of its bills (savings put by for a rainy day), plus a little.
constexpr std::int64_t kBudgetPercent = 80;
constexpr std::int64_t kSavingsSpendDays = 10;
constexpr std::int64_t kSavingsTargetNights = 30;
constexpr std::int64_t kBaseBudget = 10;
constexpr std::int64_t kWorkDaysPerWeek = 5;
// Robots with more saved than this spend a quarter of what is left of
// their budget on every visit, on better drinks, parts and service.
constexpr std::int64_t kComfortableSavings = 1000;

constexpr std::array<std::string_view, 12> kIndustryNames{
    "Kessler", "Omni", "Vortek", "Hexa", "Ironclad", "Synthex",
    "Chrome", "Atlas", "Raijin", "Brassworks", "Quantum", "Meridian"};
constexpr std::array<std::string_view, 8> kIndustryKinds{
    "Dynamics", "Forge", "Works", "Systems", "Industries", "Fabrication",
    "Logistics", "Robotics"};
constexpr std::array<std::string_view, 6> kChainNames{
    "Neon", "Volt", "Rust", "Static", "Glow", "Byte"};
constexpr std::array<std::string_view, 3> kBankNames{
    "First Chrome Bank", "Ironside Savings", "Neon Credit Union"};

std::string_view ChainKind(RoomKind kind) {
    switch (kind) {
        case RoomKind::Bar: return "Taps";
        case RoomKind::Diner: return "Noodle Co.";
        case RoomKind::ChargingBay: return "Charge";
        case RoomKind::RepairShop: return "Repairs";
        case RoomKind::Arcade: return "Arcades";
        case RoomKind::Shop: return "Goods";
        case RoomKind::Clinic: return "Clinics";
        case RoomKind::BlackMarket: return "Syndicate";
        default: return "Holdings";
    }
}
}  // namespace

std::string_view SectorName(Sector sector) noexcept {
    switch (sector) {
        case Sector::Industry: return "industry";
        case Sector::Venues: return "venues";
        case Sector::Housing: return "housing";
        case Sector::Utilities: return "utilities";
        case Sector::Transit: return "transit";
        case Sector::Banking: return "banking";
        case Sector::Government: return "government";
    }
    return "";
}

std::string_view EmissionReasonName(EmissionReason reason) noexcept {
    switch (reason) {
        case EmissionReason::Founding: return "founding";
        case EmissionReason::NewcomerSavings: return "newcomer's savings";
        case EmissionReason::BankRescue: return "bank rescue";
        case EmissionReason::Repaid: return "repaid";
        case EmissionReason::Interest: return "interest";
    }
    return "";
}

std::int64_t GiniPerMille(std::vector<std::int64_t> wealth) {
    // Debts count as having nothing, so the measure stays within 0..1000.
    for (std::int64_t& each : wealth) {
        each = std::max<std::int64_t>(each, 0);
    }
    std::ranges::sort(wealth);
    std::int64_t total = 0;
    std::int64_t weighted = 0;
    const auto count = static_cast<std::int64_t>(wealth.size());
    for (std::int64_t index = 0; index < count; ++index) {
        total += wealth[static_cast<std::size_t>(index)];
        weighted += (index + 1) * wealth[static_cast<std::size_t>(index)];
    }
    if (total == 0 || count == 0) {
        return 0;
    }
    // G = 2 sum(i x_i) / (n sum x) - (n + 1) / n
    const long double gini = (2.0L * static_cast<long double>(weighted)) /
                                 (static_cast<long double>(count) * static_cast<long double>(total)) -
                             (static_cast<long double>(count + 1) / static_cast<long double>(count));
    return std::clamp<std::int64_t>(static_cast<std::int64_t>(gini * 1000.0L), 0, 1000);
}

void World::FoundEconomy(std::uint64_t seed) {
    Pcg32 random = Pcg32::FromSeed(seed, kEconomyStream);
    Economy& economy = state_.economy;
    const auto found = [&](std::string name, OrganizationKind kind, Sector sector) -> Organization& {
        economy.organizations.push_back(
            {.id = state_.ids.Allocate(), .name = std::move(name), .kind = kind, .sector = sector});
        return economy.organizations.back();
    };
    found("Central Reserve", OrganizationKind::CentralBank, Sector::Banking);
    std::vector<std::size_t> banks;
    for (const std::string_view name : kBankNames) {
        banks.push_back(economy.organizations.size());
        found(std::string{name}, OrganizationKind::Bank, Sector::Banking);
    }
    found("Habitat Trust", OrganizationKind::Corporation, Sector::Housing);
    found("City Hall", OrganizationKind::Government, Sector::Government);
    found("Gridline Power & Water", OrganizationKind::Corporation, Sector::Utilities);
    found("Metro Authority", OrganizationKind::Corporation, Sector::Transit);

    // Industry: corporations of about four workplaces each.
    std::vector<Building*> workplaces;
    for (Building& building : state_.city.buildings) {
        if (building.kind == BuildingKind::Workplace) {
            workplaces.push_back(&building);
        }
    }
    const std::size_t corporations =
        std::clamp<std::size_t>(workplaces.size() / 4, 1, kIndustryNames.size() * 2);
    const std::size_t firstIndustry = economy.organizations.size();
    for (std::size_t index = 0; index < corporations; ++index) {
        found(std::format("{} {}", kIndustryNames.at(index % kIndustryNames.size()),
                          kIndustryKinds.at((index + (index / kIndustryNames.size())) %
                                            kIndustryKinds.size())),
              OrganizationKind::Corporation, Sector::Industry);
    }
    for (std::size_t index = 0; index < workplaces.size(); ++index) {
        // A random corporation for each, but every corporation gets one.
        const std::size_t owner =
            index < corporations
                ? index
                : random.UniformInt(0, static_cast<std::uint32_t>(corporations - 1));
        Organization& corporation = economy.organizations[firstIndustry + owner];
        for (Room& room : workplaces[index]->rooms) {
            room.owner = corporation.id;
            // Three days of base wages to start with.
            corporation.credits += 3 * static_cast<std::int64_t>(room.capacity) *
                                   workplaces[index]->shift.wage;
        }
    }

    // Venues: up to three chains of each kind.
    std::unordered_map<std::uint8_t, std::vector<Room*>> venues;
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (IsVenue(room.kind)) {
                venues[static_cast<std::uint8_t>(room.kind)].push_back(&room);
            }
        }
    }
    std::size_t chain = 0;
    for (const RoomKind kind : VenueKinds()) {
        const auto rooms = venues.find(static_cast<std::uint8_t>(kind));
        if (rooms == venues.end()) {
            continue;
        }
        const std::size_t chains = std::clamp<std::size_t>(rooms->second.size() / 12, 1, 3);
        const std::size_t first = economy.organizations.size();
        for (std::size_t index = 0; index < chains; ++index) {
            found(std::format("{} {}", kChainNames.at(chain++ % kChainNames.size()), ChainKind(kind)),
                  OrganizationKind::Corporation, Sector::Venues);
        }
        for (std::size_t index = 0; index < rooms->second.size(); ++index) {
            Organization& owner = economy.organizations[first + (index % chains)];
            rooms->second[index]->owner = owner.id;
            owner.credits += 100;
        }
    }

    // The city's housing office owns the dorms everyone starts in; the
    // trust owns every other apartment, to let or sell.
    const EntityId trust = economy.organizations.at(banks.back() + 1).id;
    const EntityId office = economy.organizations.at(banks.back() + 2).id;
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.kind == RoomKind::Apartment) {
                room.owner = room.apartmentClass == ApartmentClass::Dorm ? office : trust;
            }
        }
    }

    // Starting money: robots' savings, and capital for every organization.
    const auto population = static_cast<std::int64_t>(state_.robots.size());
    for (Organization& organization : economy.organizations) {
        switch (organization.sector) {
            case Sector::Housing:
            case Sector::Utilities:
            case Sector::Transit:
            case Sector::Government:
                organization.credits += 1000 + (population / 2);
                break;
            case Sector::Banking:
                organization.credits = organization.kind == OrganizationKind::Bank
                                           ? kBankFloor + (population * 4)
                                           : 0;
                break;
            default:
                break;
        }
    }
    std::int64_t savings = 0;
    for (Robot& robot : state_.robots) {
        savings += robot.credits;
        robot.bank = economy.organizations.at(banks.at(
            random.UniformInt(0, static_cast<std::uint32_t>(banks.size() - 1)))).id;
    }
    const EntityId central = economy.organizations.front().id;
    for (Organization& organization : economy.organizations) {
        organization.bank = organization.kind == OrganizationKind::Corporation ||
                                    organization.kind == OrganizationKind::Government
                                ? economy.organizations.at(banks.at(random.UniformInt(
                                      0, static_cast<std::uint32_t>(banks.size() - 1)))).id
                                : (organization.kind == OrganizationKind::Bank ? central : EntityId{});
        economy.created += organization.credits;
        if (organization.credits > 0) {
            economy.emissions.push_back({.tick = state_.tick,
                                         .account = organization.id,
                                         .amount = organization.credits,
                                         .reason = EmissionReason::Founding});
        }
    }
    economy.created += savings;
    economy.emissions.push_back(
        {.tick = state_.tick, .account = {}, .amount = savings, .reason = EmissionReason::Founding});

    // Owners: one to three robots or banks for every corporation and bank.
    for (Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank ||
            organization.kind == OrganizationKind::Government) {
            continue;  // Public: nobody's to own.
        }
        const std::uint32_t owners = random.UniformInt(1, 3);
        for (std::uint32_t index = 0; index < owners; ++index) {
            EntityId owner;
            if (organization.kind == OrganizationKind::Corporation &&
                random.UniformInt(1, 100) <= 25) {
                owner = economy.organizations.at(banks.at(
                    random.UniformInt(0, static_cast<std::uint32_t>(banks.size() - 1)))).id;
            } else if (!state_.robots.empty()) {
                owner = state_.robots.at(random.UniformInt(
                    0, static_cast<std::uint32_t>(state_.robots.size() - 1))).id;
            }
            if (!owner.IsValid() || std::ranges::contains(organization.owners, owner, &Share::owner)) {
                continue;
            }
            organization.owners.push_back({.owner = owner, .shares = random.UniformInt(10, 100)});
        }
        std::ranges::sort(organization.owners, {}, [](const Share& share) { return share.owner.value; });
    }
    // The metro belongs to the city.
    for (Organization& organization : economy.organizations) {
        if (organization.sector == Sector::Transit) {
            organization.owners = {{.owner = office, .shares = 100}};
        }
    }
}

void World::IndexEconomy() {
    loanOf_.clear();
    for (std::size_t index = 0; index < state_.economy.loans.size(); ++index) {
        loanOf_[state_.economy.loans[index].borrower.value] = index;
    }
    for (const Organization& organization : state_.economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank) {
            centralBank_ = organization.id;
        }
        if (organization.kind == OrganizationKind::Government) {
            cityHall_ = organization.id;
        }
        switch (organization.sector) {
            case Sector::Housing: housing_ = organization.id; break;
            case Sector::Utilities: utilities_ = organization.id; break;
            case Sector::Transit: transit_ = organization.id; break;
            default: break;
        }
    }
}

const Organization* World::FindOrganization(EntityId id) const noexcept {
    const auto& organizations = state_.economy.organizations;
    const auto found = std::ranges::lower_bound(organizations, id.value, {},
                                                [](const Organization& each) { return each.id.value; });
    return found != organizations.end() && found->id == id ? &*found : nullptr;
}

Organization* World::MutableOrganization(EntityId id) noexcept {
    return const_cast<Organization*>(std::as_const(*this).FindOrganization(id));
}

Robot* World::MutableRobot(EntityId id) noexcept {
    return const_cast<Robot*>(FindRobot(id));
}

Room& World::MutableRoom(EntityId id) {
    const auto room = rooms_.Find(id);
    if (!room) {
        throw std::logic_error("No such room");
    }
    // The index points into state_.city, which this world owns and may
    // change; only the room's owner changes, never what the index keys on.
    return const_cast<Room&>(*room->room);
}

const Loan* World::LoanOf(EntityId borrower) const noexcept {
    const auto found = loanOf_.find(borrower.value);
    return found != loanOf_.end() ? &state_.economy.loans[found->second] : nullptr;
}

std::string_view World::AccountName(EntityId account) const {
    if (const Organization* organization = FindOrganization(account)) {
        return organization->name;
    }
    return NameOf(account);
}

std::int64_t& World::Balance(EntityId account) {
    if (Organization* organization = MutableOrganization(account)) {
        return organization->credits;
    }
    if (Robot* robot = MutableRobot(account)) {
        return robot->credits;
    }
    throw std::logic_error(std::format("No account {}", account.value));
}

Books* World::BooksOf(EntityId account) {
    Organization* organization = MutableOrganization(account);
    return organization != nullptr ? &organization->today : nullptr;
}

void World::Pay(EntityId from, EntityId to, std::int64_t amount) {
    if (amount <= 0 || from == to) {
        return;
    }
    std::int64_t& payer = Balance(from);
    if (payer < amount) {
        throw std::logic_error(std::format("Account {} cannot pay {}", from.value, amount));
    }
    payer -= amount;
    Balance(to) += amount;
}

void World::ChangeMoney(EntityId account, std::int64_t amount, EmissionReason reason) {
    Economy& economy = state_.economy;
    Balance(account) += amount;
    if (amount >= 0) {
        economy.created += amount;
        state_.today.created += amount;
    } else {
        economy.destroyed -= amount;
        state_.today.destroyed -= amount;
    }
    economy.emissions.push_back(
        {.tick = state_.tick, .account = account, .amount = amount, .reason = reason});
    if (economy.emissions.size() > kEmissionLogSize) {
        economy.emissions.erase(economy.emissions.begin());
    }
    Emit({.tick = state_.tick,
          .kind = EventKind::MoneySupplyChanged,
          .credits = amount,
          .account = account,
          .emission = reason});
}

void World::Lend(EntityId lender, EntityId borrower, std::int64_t amount) {
    if (amount <= 0) {
        return;
    }
    Organization* bank = MutableOrganization(lender);
    if (bank == nullptr) {
        throw std::logic_error("Lending without a lender");
    }
    if (bank->kind == OrganizationKind::CentralBank) {
        // Money the city did not have.
        ChangeMoney(lender, amount, EmissionReason::BankRescue);
    } else if (bank->credits - amount < kBankFloor) {
        Lend(bank->bank, bank->id, amount + kBankFloor - bank->credits + (amount / 2));
        bank = MutableOrganization(lender);
    }
    Pay(lender, borrower, amount);
    auto& loans = state_.economy.loans;
    if (const auto found = loanOf_.find(borrower.value); found != loanOf_.end()) {
        loans[found->second].owed += amount;
    } else {
        loanOf_[borrower.value] = loans.size();
        loans.push_back({.id = state_.ids.Allocate(),
                         .lender = lender,
                         .borrower = borrower,
                         .owed = amount,
                         .takenAt = state_.tick});
    }
    state_.today.loansTaken += 1;
    state_.today.lent += amount;
}

bool World::MayBorrow(const Robot& robot, std::int64_t amount) const {
    if (!robot.workspace.IsValid() || !robot.bank.IsValid()) {
        return false;
    }
    const Loan* loan = LoanOf(robot.id);
    const std::int64_t owed = loan != nullptr ? loan->owed : 0;
    return owed + amount <= kRobotDebtShifts * WageOf(robot);
}

std::int64_t World::PocketOf(const Robot& robot) const {
    return std::max<std::int64_t>(60, 6 * RentOf(robot));
}

void World::Withdraw(Robot& robot, std::int64_t amount) {
    amount = std::min(amount, robot.savings);
    if (amount <= 0) {
        return;
    }
    Organization* bank = MutableOrganization(robot.bank);
    if (bank->credits - amount < 0) {
        // A run on the bank: the central bank lends it what it lacks.
        Lend(bank->bank, bank->id, amount - bank->credits + kBankFloor);
    }
    robot.savings -= amount;
    Pay(robot.bank, robot.id, amount);
}

std::int64_t World::PayBill(Robot& robot, EntityId to, std::int64_t amount) {
    if (amount <= 0) {
        return 0;
    }
    if (robot.credits < amount) {
        Withdraw(robot, amount - robot.credits);
    }
    if (robot.credits < amount && MayBorrow(robot, amount - robot.credits)) {
        const bool first = LoanOf(robot.id) == nullptr;
        const std::int64_t borrowed = amount - robot.credits;
        Lend(robot.bank, robot.id, borrowed);
        Emit({.tick = state_.tick, .kind = EventKind::Borrowed, .robot = robot.id, .credits = borrowed});
        if (first) {
            Remember(robot, MemoryKind::TookLoan, {}, {});
        }
    }
    const std::int64_t paid = std::min(robot.credits, amount);
    Pay(robot.id, to, paid);
    robot.rentOwed += amount - paid;
    return paid;
}

void World::PayOrBorrow(Organization& payer, EntityId to, std::int64_t amount) {
    if (amount <= 0) {
        return;
    }
    const EntityId id = payer.id;
    if (payer.credits < amount) {
        Lend(payer.bank, id, amount - payer.credits);
    }
    Pay(id, to, amount);
}

void World::PaySupplies(Organization& buyer, std::int64_t revenue) {
    std::int64_t percent = 0;
    switch (buyer.sector) {
        case Sector::Venues: percent = state_.economy.supplyPercent; break;
        case Sector::Utilities: percent = kUtilitySupplyPercent; break;
        case Sector::Transit: percent = kTransitSupplyPercent; break;
        case Sector::Housing: percent = kHousingSupplyPercent; break;
        default: return;
    }
    const std::int64_t supplies = std::min(buyer.credits, revenue * percent / 100);
    if (supplies <= 0) {
        return;
    }
    buyer.credits -= supplies;
    buyer.today.supplies += supplies;
    state_.economy.supplyMarket += supplies;
    state_.today.supplies += supplies;
}

std::int64_t World::LuxuryOf(const Robot& robot, std::int64_t price) const {
    if (robot.savings <= kComfortableSavings) {
        return 0;
    }
    return std::max<std::int64_t>(0, BudgetOf(robot) - robot.spentToday - price) / 4;
}

std::int64_t World::PayOf(const Robot& robot) const {
    const auto job = rooms_.Find(robot.workspace);
    const Organization* employer = job ? FindOrganization(job->room->owner) : nullptr;
    const std::int64_t percent = employer != nullptr ? employer->payPercent : 100;
    return static_cast<std::int64_t>(WageOf(robot)) * percent / 100;
}

std::int64_t World::NetPayOf(const Robot& robot) const {
    const std::int64_t wage = PayOf(robot);
    return wage - (wage * kWageTaxPercent / 100);
}

std::int64_t World::BudgetOf(const Robot& robot) const {
    // Out of work, a robot lives on City Hall's support.
    const std::int64_t pay =
        robot.workspace.IsValid() ? NetPayOf(robot) * kWorkDaysPerWeek / kDaysPerWeek : kBenefit;
    const std::int64_t target = kSavingsTargetNights * RentOf(robot);
    const std::int64_t spare = std::max<std::int64_t>(0, robot.savings - target);
    return (pay * kBudgetPercent / 100) + (spare / kSavingsSpendDays) + kBaseBudget;
}

void World::Sell(Robot& buyer, EntityId seller, std::int64_t price) {
    const std::int64_t paid = std::min(buyer.credits, price);
    if (paid <= 0) {
        return;
    }
    Pay(buyer.id, seller, paid);
    if (Organization* organization = MutableOrganization(seller)) {
        organization->today.revenue += paid;
        PaySupplies(*organization, paid);
    }
}

void World::PayWage(Robot& robot) {
    const auto workspace = rooms_.Find(robot.workspace);
    Organization* employer = workspace ? MutableOrganization(workspace->room->owner) : nullptr;
    if (employer == nullptr) {
        throw std::logic_error("A workspace without an owner");
    }
    // The employer pays the wage, City Hall's share of it straight to City
    // Hall.
    const std::int64_t wage = PayOf(robot);
    const std::int64_t tax = wage * kWageTaxPercent / 100;
    employer->today.wages += wage;
    PayOrBorrow(*employer, robot.id, wage - tax);
    PayOrBorrow(*MutableOrganization(employer->id), cityHall_, tax);
    if (Organization* city = MutableOrganization(cityHall_)) {
        city->today.revenue += tax;
    }
    state_.today.wages += wage;
    state_.today.wageTax += tax;
    Emit({.tick = state_.tick,
          .kind = EventKind::ShiftEnded,
          .robot = robot.id,
          .room = robot.workspace,
          .credits = wage - tax});
}

std::int64_t World::PriceOf(const Room& apartment) const {
    return RentFor(apartment) * kPriceNights;
}

std::int64_t World::RentFor(const Room& apartment) const {
    const auto kind = static_cast<std::size_t>(apartment.apartmentClass);
    if (apartment.apartmentClass == ApartmentClass::Dorm || kind < 2) {
        return apartment.rent;
    }
    return std::max<std::int64_t>(
        1, static_cast<std::int64_t>(apartment.rent) * state_.economy.rentPercent.at(kind - 2) / 100);
}

std::int64_t World::PriceAt(const Room& venue) const {
    const VenueRules* rules = FindVenueRules(venue.kind);
    return rules != nullptr
               ? std::max<std::int64_t>(1, static_cast<std::int64_t>(rules->price) * venue.pricePercent / 100)
               : 0;
}

void World::CloseUnit(Room& room) {
    const RoomKind was = room.kind;
    // Its staff are laid off.
    for (Robot& robot : state_.robots) {
        if (robot.workspace != room.id) {
            continue;
        }
        robot.workspace = {};
        robot.jobLevel = 0;
        robot.performance = 0;
        robot.commuteAt = kNoCommute;
        Remember(robot, MemoryKind::LostJob, {}, room.id);
        if (robot.activity == RobotActivity::Resting) {
            PlanAtHome(robot);
        }
    }
    room.kind = RoomKind::VacantStore;
    room.name = "Empty Unit";
    room.pricePercent = 100;
    room.visits = 0;
    room.quietNights = 0;
    ++cityVersion_;
    ++state_.today.venuesClosed;
    Emit({.tick = state_.tick, .kind = EventKind::UnitClosed, .room = room.id,
          .account = room.owner, .change = static_cast<std::int32_t>(was)});
}

void World::OpenUnit(Room& room, RoomKind kind) {
    // The chain with most venues of the kind takes it on, or for a
    // workspace the corporation owning the rest of its workplace.
    std::unordered_map<std::uint64_t, std::uint32_t> held;
    const auto building = rooms_.Find(room.id);
    if (kind == RoomKind::Workspace) {
        for (const Room& other : building->building->rooms) {
            if (other.kind == RoomKind::Workspace) {
                ++held[other.owner.value];
            }
        }
    } else {
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            ++held[venue.room->owner.value];
        }
    }
    EntityId owner;
    std::uint32_t most = 0;
    for (const Organization& organization : state_.economy.organizations) {
        const auto found = held.find(organization.id.value);
        const std::uint32_t count = found != held.end() ? found->second : 0;
        const Sector wanted = kind == RoomKind::Workspace ? Sector::Industry : Sector::Venues;
        if (organization.sector == wanted && (!owner.IsValid() || count > most)) {
            owner = organization.id;
            most = count;
        }
    }
    if (owner.IsValid()) {
        room.owner = owner;
    }
    room.kind = kind;
    if (const VenueRules* rules = FindVenueRules(kind)) {
        room.name = std::string{VenueNames(kind)[state_.random.UniformInt(
            0, static_cast<std::uint32_t>(VenueNames(kind).size() - 1))]};
        room.capacity = state_.random.UniformInt(rules->minCapacity, rules->maxCapacity);
    } else {
        room.name = "Workshop";
        room.capacity = state_.random.UniformInt(kMinWorkspaceStaff, kMaxWorkspaceStaff);
    }
    room.pricePercent = 100;
    room.visits = 0;
    room.quietNights = 0;
    ++cityVersion_;
    ++state_.today.venuesOpened;
    Emit({.tick = state_.tick, .kind = EventKind::UnitOpened, .room = room.id,
          .account = room.owner});
}

void World::ReviewUnits() {
    const std::uint64_t version = cityVersion_;
    // Rooms someone is in or on the way to stay as they are tonight.
    std::unordered_set<std::uint64_t> busy;
    std::unordered_map<std::uint64_t, std::uint32_t> staff;
    std::int64_t unemployed = 0;
    for (const Robot& robot : state_.robots) {
        busy.insert(robot.location.value);
        busy.insert(robot.destination.value);
        if (robot.workspace.IsValid()) {
            ++staff[robot.workspace.value];
        } else {
            ++unemployed;
        }
    }
    const std::int64_t population = std::max<std::int64_t>(1, static_cast<std::int64_t>(state_.robots.size()));
    const std::int64_t unemployment = unemployed * 100 / population;
    // How much each kind of venue is in demand: its average price, and
    // whether the city has any.
    std::map<RoomKind, std::pair<std::int64_t, std::int64_t>> demand;
    for (const RoomKind kind : VenueKinds()) {
        demand[kind] = {0, 0};
        for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
            demand[kind].first += venue.room->pricePercent;
            ++demand[kind].second;
        }
    }
    const auto mostWanted = [&]() -> std::optional<RoomKind> {
        std::optional<RoomKind> best;
        std::int64_t bestScore = 0;
        for (const auto& [kind, sum] : demand) {
            // A kind the city lacks altogether is wanted most.
            const std::int64_t score = sum.second == 0 ? 1000 : sum.first / sum.second;
            if (score >= kInDemandPercent && score > bestScore) {
                best = kind;
                bestScore = score;
            }
        }
        return best;
    };
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.floor != 0) {
                continue;
            }
            if (IsVenue(room.kind)) {
                const bool quiet = room.visits * 2 < room.capacity && room.pricePercent <= kQuietPricePercent;
                room.quietNights = quiet ? room.quietNights + 1 : 0;
                if (room.quietNights >= kCloseAfterNights && !busy.contains(room.id.value)) {
                    CloseUnit(room);
                }
            } else if (room.kind == RoomKind::Workspace) {
                room.quietNights = staff[room.id.value] * 2 < room.capacity ? room.quietNights + 1 : 0;
                if (room.quietNights >= kCloseAfterNights && unemployment < kLowUnemploymentPercent &&
                    !busy.contains(room.id.value)) {
                    CloseUnit(room);
                }
            } else if (room.kind == RoomKind::VacantStore) {
                ++room.quietNights;
                if (state_.random.UniformInt(1, 100) > kOpenUnitPercent || busy.contains(room.id.value)) {
                    continue;
                }
                if (building.kind == BuildingKind::Workplace &&
                    unemployment >= kHighUnemploymentPercent) {
                    OpenUnit(room, RoomKind::Workspace);
                } else if (const auto kind = mostWanted()) {
                    OpenUnit(room, *kind);
                    demand[*kind].first += 100;
                    ++demand[*kind].second;
                }
            }
        }
    }
    if (cityVersion_ != version) {
        // Kinds changed: the index of rooms by kind must follow.
        rooms_ = RoomIndex(state_.city);
    }
}

void World::RunCityHall() {
    Organization* city = MutableOrganization(cityHall_);
    if (city == nullptr) {
        return;
    }
    // A fifth of every corporation's and bank's profit today.
    for (Organization& organization : state_.economy.organizations) {
        if (organization.kind != OrganizationKind::Corporation &&
            organization.kind != OrganizationKind::Bank) {
            continue;
        }
        const Books& books = organization.today;
        const std::int64_t profit =
            books.revenue - (books.wages + books.supplies + books.upkeep + books.rent + books.taxes);
        const std::int64_t tax = std::min(organization.credits, std::max<std::int64_t>(0, profit) *
                                                                    kProfitTaxPercent / 100);
        if (tax <= 0) {
            continue;
        }
        Pay(organization.id, cityHall_, tax);
        organization.today.taxes += tax;
        city->today.revenue += tax;
        state_.today.profitTax += tax;
    }
    // Support for every robot out of work, borrowed if the treasury is bare.
    for (Robot& robot : state_.robots) {
        if (!robot.workspace.IsValid()) {
            PayOrBorrow(*city, robot.id, kBenefit);
            city->today.wages += kBenefit;
            state_.today.benefits += kBenefit;
        }
    }
    // Public works: half of what City Hall has beyond twice its reserve
    // buys supplies from industry, unless it is in debt.
    const std::int64_t reserve = std::max<std::int64_t>(
        kCorporationFloor * 5, 3 * (city->today.wages + city->today.upkeep));
    const std::int64_t spare = city->credits - (2 * reserve);
    if (spare > 0 && LoanOf(cityHall_) == nullptr) {
        const std::int64_t works = spare / 2;
        city->credits -= works;
        city->today.supplies += works;
        state_.economy.supplyMarket += works;
        state_.today.supplies += works;
        state_.today.publicSpending += works;
    }
}

void World::SetPrices() {
    // A venue that had three times its places in guests asks more; one
    // that had fewer guests than places, less.
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (!IsVenue(room.kind)) {
                continue;
            }
            if (room.visits >= kBusyTurns * room.capacity) {
                room.pricePercent = std::min(kMaxPricePercent, room.pricePercent + kPriceStep);
            } else if (room.visits < room.capacity) {
                room.pricePercent = std::max(kMinPricePercent, room.pricePercent - kPriceStep);
            }
            room.visits = 0;
        }
    }
    // Market rents follow how many homes of each class stand empty.
    std::array<std::int64_t, 4> homes{};
    std::array<std::int64_t, 4> empty{};
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const auto kind = static_cast<std::size_t>(apartment.room->apartmentClass);
        if (kind < 2) {
            continue;
        }
        ++homes.at(kind - 2);
        empty.at(kind - 2) += ResidentsOf(apartment.room->id) == 0 ? 1 : 0;
    }
    for (std::size_t kind = 0; kind < homes.size(); ++kind) {
        if (homes[kind] == 0) {
            continue;
        }
        std::int64_t& rent = state_.economy.rentPercent[kind];
        const std::int64_t vacancy = empty[kind] * 100 / homes[kind];
        if (vacancy < kTightVacancyPercent) {
            rent = std::min(kMaxRentPercent, rent + 1);
        } else if (vacancy > kLooseVacancyPercent) {
            rent = std::max(kMinRentPercent, rent - 1);
        }
    }
}

std::int64_t World::UpkeepOf(const Room& apartment) const {
    return RentFor(apartment) * kUpkeepPercent / 100;
}

void World::Transfer(EntityId apartment, EntityId buyer, std::int64_t price) {
    Room& room = MutableRoom(apartment);
    const EntityId seller = room.owner;
    if (Organization* organization = MutableOrganization(buyer)) {
        PayOrBorrow(*organization, seller, price);
    } else {
        Pay(buyer, seller, price);
    }
    MutableRoom(apartment).owner = buyer;
    state_.today.propertySales += 1;
    state_.today.propertySalesValue += price;
    if (Robot* robot = MutableRobot(buyer)) {
        Emit({.tick = state_.tick, .kind = EventKind::BoughtProperty, .robot = buyer,
              .room = apartment, .credits = price});
        Remember(*robot, MemoryKind::BoughtProperty, {}, apartment);
    }
    if (FindRobot(seller) != nullptr) {
        Emit({.tick = state_.tick, .kind = EventKind::SoldProperty, .robot = seller,
              .room = apartment, .credits = price});
    }
}

void World::Bequeath(Robot& robot) {
    // Its best friend gets everything, or failing that its bank.
    EntityId heir = robot.bank;
    std::int32_t closest = 0;
    for (const Bond& bond : robot.bonds) {
        if ((bond.level == BondLevel::Friend || bond.level == BondLevel::CloseFriend) &&
            bond.affinity > closest && FindRobot(bond.robot) != nullptr) {
            closest = bond.affinity;
            heir = bond.robot;
        }
    }
    // Its savings come out of the bank; debts are paid first, and what
    // cannot be is lost to the lender.
    Withdraw(robot, robot.savings);
    if (const auto found = loanOf_.find(robot.id.value); found != loanOf_.end()) {
        Loan& loan = state_.economy.loans[found->second];
        const std::int64_t paid = std::min(loan.owed, robot.credits);
        Pay(robot.id, loan.lender, paid);
        state_.today.writtenOff += loan.owed - paid;
        loan.owed = 0;
    }
    Pay(robot.id, heir, robot.credits);
    for (Building& building : state_.city.buildings) {
        for (Room& room : building.rooms) {
            if (room.owner == robot.id) {
                room.owner = heir;
            }
        }
    }
    for (Organization& organization : state_.economy.organizations) {
        auto& owners = organization.owners;
        const auto share = std::ranges::find(owners, robot.id, &Share::owner);
        if (share == owners.end()) {
            continue;
        }
        const std::uint32_t shares = share->shares;
        owners.erase(share);
        if (heir == organization.id) {
            continue;  // A bank does not own itself.
        }
        if (const auto existing = std::ranges::find(owners, heir, &Share::owner);
            existing != owners.end()) {
            existing->shares += shares;
        } else {
            owners.push_back({.owner = heir, .shares = shares});
            std::ranges::sort(owners, {}, [](const Share& each) { return each.owner.value; });
        }
    }
    if (Robot* friendRobot = MutableRobot(heir)) {
        Emit({.tick = state_.tick, .kind = EventKind::Inherited, .robot = heir, .otherRobot = robot.id});
        Remember(*friendRobot, MemoryKind::Inherited, robot.id, {});
    }
}

std::int64_t World::ReserveOf(const Organization& organization) const {
    if (organization.kind == OrganizationKind::Bank) {
        std::int64_t held = 0;
        for (const Loan& loan : state_.economy.loans) {
            held += loan.lender == organization.id ? loan.owed : 0;
        }
        for (const Robot& robot : state_.robots) {
            held += robot.bank == organization.id ? robot.savings : 0;
        }
        return kBankFloor + (held * kBankReservePercent / 100);
    }
    const auto costs = [](const Books& books) {
        return books.wages + books.supplies + books.upkeep + books.rent + books.taxes;
    };
    return std::max(kCorporationFloor,
                    std::max(costs(organization.today), costs(organization.yesterday)));
}

void World::TradeProperty() {
    std::unordered_set<std::uint64_t> lived;
    for (const Robot& robot : state_.robots) {
        lived.insert(robot.apartment.value);
    }
    // Robots with apartments nobody lives in sell them to the trust.
    Organization* trust = MutableOrganization(housing_);
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const Room& room = *apartment.room;
        if (lived.contains(room.id.value) || FindRobot(room.owner) == nullptr ||
            state_.random.UniformInt(1, 100) > kSellVacantPercent || trust == nullptr) {
            continue;
        }
        Transfer(room.id, housing_, PriceOf(room) * kSellPercent / 100);
    }
    // Owners who cannot keep up with their bills sell their home and rent
    // it back.
    for (Robot& robot : state_.robots) {
        const auto home = rooms_.Find(robot.apartment);
        if (!home || home->room->owner != robot.id) {
            continue;
        }
        const std::int64_t upkeep = UpkeepOf(*home->room);
        if (upkeep <= 0 || robot.rentOwed < kEvictionNights * upkeep) {
            continue;
        }
        Transfer(robot.apartment, housing_, PriceOf(*home->room) * kSellPercent / 100);
        Remember(robot, MemoryKind::SoldHome, {}, robot.apartment);
        // The sale pays its debts.
        const std::int64_t bills = std::min(robot.rentOwed, robot.credits);
        Pay(robot.id, utilities_, bills);
        robot.rentOwed -= bills;
        if (const auto found = loanOf_.find(robot.id.value); found != loanOf_.end()) {
            Loan& loan = state_.economy.loans[found->second];
            const std::int64_t repaid = std::min(loan.owed, robot.credits);
            Pay(robot.id, loan.lender, repaid);
            loan.owed -= repaid;
        }
    }
    // Robots with a job and a fifth of the price saved buy their first
    // home with a mortgage: an empty studio or flat of the trust's, and
    // they move in.
    std::unordered_set<std::uint64_t> taken = lived;
    for (Robot& robot : state_.robots) {
        const auto home = rooms_.Find(robot.apartment);
        if (!home || home->room->owner == robot.id || !robot.workspace.IsValid() ||
            robot.activity == RobotActivity::Walking || LoanOf(robot.id) != nullptr ||
            robot.rentOwed > 0 || state_.random.UniformInt(1, 100) > kBuyPercent) {
            continue;
        }
        const std::int64_t means = robot.credits + robot.savings;
        const std::int64_t mortgage = kMortgageShifts * PayOf(robot);
        // The dearest it can put a fifth down on, keeping a month's bills.
        const Room* best = nullptr;
        for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
            const Room& room = *apartment.room;
            if (room.owner != housing_ || taken.contains(room.id.value) ||
                Sleeps(room.apartmentClass) != 1 || RentFor(room) <= RentFor(*home->room)) {
                continue;
            }
            const std::int64_t price = PriceOf(room);
            const std::int64_t deposit = price * kDepositPercent / 100;
            if (means < deposit + (kBuyReserveNights * UpkeepOf(room)) || price - deposit > mortgage ||
                (best != nullptr && RentFor(room) <= RentFor(*best))) {
                continue;
            }
            best = &room;
        }
        if (best == nullptr) {
            continue;
        }
        const std::int64_t price = PriceOf(*best);
        const std::int64_t own = std::min(means - (kBuyReserveNights * UpkeepOf(*best)), price);
        // A bank lends only what it can spare beyond its reserve.
        const Organization* bank = FindOrganization(robot.bank);
        if (bank == nullptr || bank->credits - (price - own) < ReserveOf(*bank)) {
            continue;
        }
        Withdraw(robot, own - robot.credits);
        Lend(robot.bank, robot.id, price - std::min(own, robot.credits));
        Transfer(best->id, robot.id, price);
        taken.insert(best->id.value);
        robot.apartment = best->id;
        Remember(robot, MemoryKind::MovedUp, {}, best->id);
        if (robot.activity == RobotActivity::Resting) {
            StartWalk(robot, best->id);  // Moving into its own home tonight.
        }
    }
    CountResidents();

    // Robots with savings buy: first the home they rent from the trust,
    // then a let apartment of the trust's to collect its rent.
    std::vector<EntityId> letByTrust;
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        if (apartment.room->owner == housing_ && lived.contains(apartment.room->id.value)) {
            letByTrust.push_back(apartment.room->id);
        }
    }
    for (Robot& robot : state_.robots) {
        if (LoanOf(robot.id) != nullptr || robot.rentOwed > 0 ||
            state_.random.UniformInt(1, 100) > kBuyPercent) {
            continue;
        }
        const auto home = rooms_.Find(robot.apartment);
        const std::int64_t keep = kBuyReserveNights * (home ? RentFor(*home->room) : 0);
        const std::int64_t means = robot.credits + robot.savings;
        EntityId target;
        if (home && home->room->owner == housing_ && means >= PriceOf(*home->room) + keep) {
            target = robot.apartment;
        } else if (!letByTrust.empty()) {
            const EntityId candidate = letByTrust.at(state_.random.UniformInt(
                0, static_cast<std::uint32_t>(letByTrust.size() - 1)));
            const auto room = rooms_.Find(candidate);
            if (room && room->room->owner == housing_ && means >= PriceOf(*room->room) + keep) {
                target = candidate;
            }
        }
        if (target.IsValid()) {
            const auto room = rooms_.Find(target);
            Withdraw(robot, PriceOf(*room->room) - robot.credits);
            Transfer(target, robot.id, PriceOf(*room->room));
            std::erase(letByTrust, target);
        }
    }
}

void World::SettleEconomy() {
    Economy& economy = state_.economy;
    // The supply market pays industry for the day's supplies, in proportion
    // to the wages each corporation paid.
    std::int64_t wageBill = 0;
    std::vector<Organization*> industry;
    for (Organization& organization : economy.organizations) {
        if (organization.sector == Sector::Industry) {
            industry.push_back(&organization);
            wageBill += organization.today.wages;
        }
    }
    if (!industry.empty() && economy.supplyMarket > 0) {
        std::int64_t left = economy.supplyMarket;
        for (std::size_t index = 0; index < industry.size(); ++index) {
            Organization& corporation = *industry[index];
            const std::int64_t share =
                index + 1 == industry.size()
                    ? left
                    : (wageBill > 0 ? economy.supplyMarket * corporation.today.wages / wageBill
                                    : economy.supplyMarket / static_cast<std::int64_t>(industry.size()));
            corporation.credits += share;
            corporation.today.revenue += share;
            left -= share;
        }
        economy.supplyMarket = 0;
    }

    // Interest, then repayments from whatever borrowers can spare.
    for (Loan& loan : economy.loans) {
        const std::int64_t interest = (loan.owed * kInterestPerNight + 9999) / 10000;
        loan.owed += interest;
        state_.today.interest += interest;
        if (Organization* lender = MutableOrganization(loan.lender)) {
            lender->today.revenue += interest;
        }
    }
    for (Loan& loan : economy.loans) {
        std::int64_t spare = 0;
        if (const Robot* robot = FindRobot(loan.borrower)) {
            const auto home = rooms_.Find(robot->apartment);
            spare = robot->credits - (2 * (home ? RentFor(*home->room) : 0));
        } else if (const Organization* organization = FindOrganization(loan.borrower)) {
            spare = organization->credits - ReserveOf(*organization);
        }
        const std::int64_t repaid = std::clamp<std::int64_t>(spare, 0, loan.owed);
        if (repaid <= 0) {
            continue;
        }
        if (loan.lender == centralBank_) {
            ChangeMoney(loan.borrower, -repaid, EmissionReason::Repaid);
        } else {
            Pay(loan.borrower, loan.lender, repaid);
        }
        loan.owed -= repaid;
        if (Books* books = BooksOf(loan.borrower)) {
            books->repaid += repaid;
        }
    }
    if (std::erase_if(economy.loans, [](const Loan& loan) { return loan.owed <= 0; }) > 0) {
        IndexEconomy();
    }

    // The price of supplies follows industry's books: it must cover the
    // wages and pay down a fiftieth of industry's debts a day. It moves by up
    // to five points a day, faster the further off it is.
    {
        std::int64_t revenue = 0;
        std::int64_t needed = 0;
        for (const Organization* corporation : industry) {
            revenue += corporation->today.revenue;
            needed += corporation->today.wages;
            if (const Loan* loan = LoanOf(corporation->id)) {
                needed += loan->owed / 50;
            }
        }
        if (needed > 0) {
            const std::int64_t gapPercent = (needed - revenue) * 100 / needed;
            const std::int64_t step = std::clamp<std::int64_t>(std::abs(gapPercent) / 2, 1, 5);
            std::int64_t& price = economy.supplyPercent;
            if (gapPercent > 0) {
                price = std::min(kMaxSupplyPercent, price + step);
            } else if (gapPercent < -2) {
                price = std::max(kMinSupplyPercent, price - step);
            }
        }
    }

    // Employers pay what they can afford: less while losing money, more
    // while earning it.
    for (Organization& employer : economy.organizations) {
        if (employer.sector != Sector::Industry && employer.sector != Sector::Venues) {
            continue;
        }
        Organization* corporation = &employer;
        const Books& books = corporation->today;
        const std::int64_t costs = books.wages + books.supplies;
        if (books.revenue < costs) {
            corporation->payPercent = std::max(kMinPayPercent, corporation->payPercent - 1);
        } else if (LoanOf(corporation->id) == nullptr && books.revenue > costs) {
            corporation->payPercent = std::min(kMaxPayPercent, corporation->payPercent + 1);
        }
    }

    RunCityHall();

    // Dividends from what organizations without debts have to spare.
    for (Organization& organization : economy.organizations) {
        if (organization.kind == OrganizationKind::CentralBank || organization.owners.empty() ||
            LoanOf(organization.id) != nullptr) {
            continue;
        }
        const std::int64_t spare = organization.credits - (2 * ReserveOf(organization));
        if (spare <= 0) {
            continue;
        }
        // Dividends come out of the day's profit, never the capital; a
        // bank's credits are mostly its savers' money besides.
        const Books& books = organization.today;
        const std::int64_t profit =
            books.revenue - (books.wages + books.supplies + books.upkeep + books.rent);
        std::int64_t payout = std::min(spare, profit) * kDividendPercent / 100;
        if (payout <= 0) {
            continue;
        }
        std::int64_t shares = 0;
        for (const Share& share : organization.owners) {
            shares += share.shares;
        }
        const EntityId payer = organization.id;
        for (const Share& share : organization.owners) {
            const std::int64_t dividend = payout * share.shares / shares;
            Pay(payer, share.owner, dividend);
            MutableOrganization(payer)->today.dividends += dividend;
            state_.today.dividends += dividend;
            if (Books* books = BooksOf(share.owner)) {
                books->revenue += dividend;
            }
        }
    }

    // Robots bank what they do not need in their pocket, or take some out,
    // and start a new day's budget.
    for (Robot& robot : state_.robots) {
        robot.spentToday = 0;
        const std::int64_t pocket = PocketOf(robot);
        if (robot.credits > 2 * pocket) {
            const std::int64_t deposit = robot.credits - pocket;
            Pay(robot.id, robot.bank, deposit);
            robot.savings += deposit;
        } else if (robot.credits < pocket) {
            Withdraw(robot, pocket - robot.credits);
        }
    }

    ReviewUnits();
    SetPrices();
    RecordStatistics();
    for (Organization& organization : economy.organizations) {
        organization.yesterday = organization.today;
        organization.today = {};
    }
    state_.today = {};
}

void World::RecordStatistics() {
    const WorldState& state = state_;
    DailyStats stats{.day = DayNumber(state.tick - 1), .counted = state.today};
    const auto count = static_cast<std::int64_t>(state.robots.size());
    stats.population = count;
    std::unordered_map<std::uint64_t, std::int64_t> property;
    std::unordered_set<std::uint64_t> lived;
    for (const Robot& robot : state.robots) {
        lived.insert(robot.apartment.value);
    }
    for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
        const Room& room = *apartment.room;
        property[room.owner.value] += PriceOf(room);
        if (!lived.contains(room.id.value)) {
            ++stats.vacantApartments;
        } else if (FindOrganization(room.owner) != nullptr) {
            ++stats.corporateApartments;
        }
    }
    std::unordered_set<std::uint64_t> shareholders;
    for (const Organization& organization : state.economy.organizations) {
        for (const Share& share : organization.owners) {
            if (FindRobot(share.owner) != nullptr) {
                shareholders.insert(share.owner.value);
            }
        }
        switch (organization.kind) {
            case OrganizationKind::Corporation:
                stats.corporationMoney += organization.credits;
                break;
            case OrganizationKind::Government:
                stats.governmentMoney += organization.credits;
                break;
            default:
                stats.bankMoney += organization.credits;
                break;
        }
    }
    stats.shareholders = static_cast<std::int64_t>(shareholders.size());
    for (const Loan& loan : state.economy.loans) {
        if (FindRobot(loan.borrower) != nullptr) {
            stats.robotDebt += loan.owed;
            ++stats.indebtedRobots;
        } else if (loan.lender == centralBank_) {
            stats.bankDebt += loan.owed;
        } else if (loan.borrower == cityHall_) {
            stats.governmentDebt += loan.owed;
        } else {
            stats.corporationDebt += loan.owed;
        }
    }

    std::vector<std::int64_t> credits;
    std::vector<std::int64_t> wealth;
    credits.reserve(state.robots.size());
    wealth.reserve(state.robots.size());
    std::int64_t payPercent = 0;
    std::int64_t industry = 0;
    for (const Organization& organization : state.economy.organizations) {
        if (organization.sector == Sector::Industry) {
            payPercent += organization.payPercent;
            ++industry;
        }
    }
    stats.averagePayPercent = industry > 0 ? payPercent / industry : 100;
    stats.supplyPercent = state.economy.supplyPercent;
    {
        std::array<std::int64_t, 5> rents{};
        std::array<std::int64_t, 5> counts{};
        for (const RoomRef& apartment : rooms_.RoomsOfKind(RoomKind::Apartment)) {
            const auto kind = static_cast<std::size_t>(apartment.room->apartmentClass);
            if (kind >= 1 && kind <= 5) {
                rents.at(kind - 1) += RentFor(*apartment.room);
                ++counts.at(kind - 1);
            }
        }
        for (std::size_t kind = 0; kind < rents.size(); ++kind) {
            stats.averageRent.at(kind) = counts[kind] > 0 ? rents[kind] / counts[kind] : 0;
        }
        std::int64_t percent = 0;
        std::int64_t venues = 0;
        for (const RoomKind kind : VenueKinds()) {
            for (const RoomRef& venue : rooms_.RoomsOfKind(kind)) {
                percent += venue.room->pricePercent;
                ++venues;
            }
        }
        stats.venuePricePercent = venues > 0 ? percent / venues : 100;
        stats.openVenues = venues;
        stats.emptyUnits = static_cast<std::int64_t>(rooms_.RoomsOfKind(RoomKind::VacantStore).size());
    }
    for (const Robot& robot : state.robots) {
        stats.robotMoney += robot.credits;
        stats.robotSavings += robot.savings;
        if (robot.workspace.IsValid()) {
            ++stats.employed;
            ++stats.jobLevels.at(std::min<std::size_t>(robot.jobLevel, stats.jobLevels.size() - 1));
        }
        stats.averageCharge += robot.needs.charge;
        stats.averageCondition += robot.needs.condition;
        stats.averageSocial += robot.needs.social;
        stats.averageFun += robot.needs.fun;
        stats.averageAge += robot.age;
        stats.averageWear += robot.wear;
        bool friendly = false;
        bool close = false;
        bool rival = false;
        for (const Bond& bond : robot.bonds) {
            friendly = friendly || bond.level == BondLevel::Friend || bond.level == BondLevel::CloseFriend;
            close = close || bond.level == BondLevel::CloseFriend;
            rival = rival || bond.level == BondLevel::Rival;
        }
        stats.withFriend += friendly ? 1 : 0;
        stats.withCloseFriend += close ? 1 : 0;
        stats.withRival += rival ? 1 : 0;
        stats.lonely += friendly ? 0 : 1;
        const auto home = rooms_.Find(robot.apartment);
        stats.broke += robot.credits + robot.savings < RentOf(robot) ? 1 : 0;
        if (home && home->room->apartmentClass != ApartmentClass::None) {
            ++stats.residentsByClass.at(static_cast<std::size_t>(home->room->apartmentClass) - 1);
            stats.sharing += ResidentsOf(robot.apartment) > 1 ? 1 : 0;
        }
        stats.behindOnBills += robot.rentOwed > 0 ? 1 : 0;
        if (home && home->room->owner == robot.id) {
            ++stats.homeowners;
        } else {
            ++stats.tenants;
        }
        const auto owned = property.find(robot.id.value);
        const std::int64_t propertyValue = owned != property.end() ? owned->second : 0;
        if (owned != property.end() &&
            propertyValue > (home && home->room->owner == robot.id ? PriceOf(*home->room) : 0)) {
            ++stats.landlords;
        }
        const Loan* loan = LoanOf(robot.id);
        credits.push_back(robot.credits + robot.savings);
        wealth.push_back(robot.credits + robot.savings + propertyValue - robot.rentOwed - (loan != nullptr ? loan->owed : 0));
    }
    stats.unemployed = count - stats.employed;
    if (count > 0) {
        stats.averageCharge /= count;
        stats.averageCondition /= count;
        stats.averageSocial /= count;
        stats.averageFun /= count;
        stats.averageAge /= count;
        stats.averageWear /= count;
        stats.meanCredits = (stats.robotMoney + stats.robotSavings) / count;
        std::int64_t total = 0;
        for (const std::int64_t each : wealth) {
            total += each;
        }
        stats.meanWealth = total / count;
        std::ranges::sort(credits);
        std::vector<std::int64_t> sorted = wealth;
        std::ranges::sort(sorted);
        stats.medianCredits = credits[credits.size() / 2];
        stats.medianWealth = sorted[sorted.size() / 2];
        std::int64_t positive = 0;
        std::int64_t top = 0;
        std::int64_t bottom = 0;
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            const std::int64_t each = std::max<std::int64_t>(sorted[index], 0);
            positive += each;
            top += index >= sorted.size() - std::max<std::size_t>(sorted.size() / 10, 1) ? each : 0;
            bottom += index < sorted.size() / 2 ? each : 0;
        }
        stats.topTenthShare = positive > 0 ? top * 1000 / positive : 0;
        stats.bottomHalfShare = positive > 0 ? bottom * 1000 / positive : 0;
    }
    stats.giniWealth = GiniPerMille(std::move(wealth));
    stats.moneySupply = stats.robotMoney + stats.corporationMoney + stats.bankMoney +
                        stats.governmentMoney + state.economy.supplyMarket;
    stats.createdTotal = state.economy.created;
    stats.destroyedTotal = state.economy.destroyed;
    state_.history.push_back(stats);
    if (state_.history.size() > kHistoryDays) {
        state_.history.erase(state_.history.begin());
    }
}


void SetCredits(WorldState& state, Robot& robot, std::int64_t credits) {
    const std::int64_t change = credits - robot.credits;
    if (change >= 0) {
        state.economy.created += change;
    } else {
        state.economy.destroyed -= change;
    }
    robot.credits = credits;
}

std::expected<void, std::string> ValidateEconomy(const WorldState& state) {
    const Economy& economy = state.economy;
    const auto fail = [](std::string message) { return std::unexpected(std::move(message)); };
    if (!std::ranges::is_sorted(economy.organizations, {},
                                [](const Organization& each) { return each.id.value; })) {
        return fail("Organizations are not sorted by id");
    }
    const auto organization = [&](EntityId id) -> const Organization* {
        const auto found = std::ranges::lower_bound(economy.organizations, id.value, {},
                                                    [](const Organization& each) { return each.id.value; });
        return found != economy.organizations.end() && found->id == id ? &*found : nullptr;
    };
    const auto robot = [&](EntityId id) {
        const auto found = std::ranges::lower_bound(state.robots, id.value, {},
                                                    [](const Robot& each) { return each.id.value; });
        return found != state.robots.end() && found->id == id;
    };
    std::int64_t money = economy.supplyMarket;
    for (const Organization& each : economy.organizations) {
        money += each.credits;
        if (each.credits < 0) {
            return fail(std::format("{} has {} credits", each.name, each.credits));
        }
        for (const Share& share : each.owners) {
            if (!robot(share.owner) && organization(share.owner) == nullptr) {
                return fail(std::format("{} has a missing owner", each.name));
            }
        }
        if (each.kind != OrganizationKind::CentralBank && organization(each.bank) == nullptr) {
            return fail(std::format("{} has no bank", each.name));
        }
    }
    for (const Robot& each : state.robots) {
        money += each.credits;
        const Organization* bank = organization(each.bank);
        if (bank == nullptr || bank->kind != OrganizationKind::Bank || each.savings < 0) {
            return fail(std::format("Robot '{}' has no bank, or negative savings", each.name));
        }
    }
    if (money != economy.created - economy.destroyed) {
        return fail(std::format("The city holds {} credits, but {} were created and {} destroyed",
                                money, economy.created, economy.destroyed));
    }
    for (const Building& building : state.city.buildings) {
        for (const Room& room : building.rooms) {
            if (!robot(room.owner) && organization(room.owner) == nullptr) {
                return fail(std::format("{} in {} has no owner", room.name, building.name));
            }
        }
    }
    std::unordered_set<std::uint64_t> borrowers;
    for (const Loan& loan : economy.loans) {
        const Organization* lender = organization(loan.lender);
        if (lender == nullptr || lender->kind == OrganizationKind::Corporation || loan.owed <= 0 ||
            !borrowers.insert(loan.borrower.value).second ||
            (!robot(loan.borrower) && organization(loan.borrower) == nullptr)) {
            return fail(std::format("Loan {} is not valid", loan.id.value));
        }
    }
    return {};
}

}  // namespace mechapunk::sim
