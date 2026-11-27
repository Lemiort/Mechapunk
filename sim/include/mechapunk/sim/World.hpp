#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mechapunk/sim/Careers.hpp"
#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/Construction.hpp"
#include "mechapunk/sim/City.hpp"
#include "mechapunk/sim/Gangs.hpp"
#include "mechapunk/sim/Settings.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Economy.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Ids.hpp"
#include "mechapunk/sim/Metro.hpp"
#include "mechapunk/sim/Random.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/Venues.hpp"
#include "mechapunk/sim/Weather.hpp"

namespace mechapunk::sim {

enum class RobotActivity : std::uint8_t {
    Resting,   // In its apartment.
    Walking,   // On the way from `location` to `destination`.
    Working,   // In its workspace.
    Visiting,  // In a venue.
};

// Lasting damage at which a robot is scrapped. A robot's condition can
// never be better than kMaxWear minus its wear.
inline constexpr std::int32_t kMaxWear = 1000;

// The wear a robot has gathered by `days` old, never fixed.
[[nodiscard]] std::int32_t WearAtAge(std::uint32_t days) noexcept;

struct Robot {
    EntityId id;
    std::string name;
    EntityId apartment;  // Room the robot lives in.
    EntityId workspace;  // Room the robot works in; none while out of work.
    RobotActivity activity{RobotActivity::Resting};
    // Room the robot is in, or has just left while walking.
    EntityId location;
    // Room the robot is walking to; none unless walking.
    EntityId destination;
    EntityId patrol;  // Venue an officer is patrolling, none at the station.
    Tick departedAt{};
    // Tick of the next scheduled change: leaving, arriving, ending a shift
    // or ending a visit.
    Tick nextActionAt{};
    // Start of the shift the robot is heading to, working, or last worked.
    Tick shiftStartsAt{};
    // Personal habit: minutes the robot aims to arrive before its shift.
    std::uint32_t punctualityMinutes{};
    // Bit d set: no shift starts on day of the week d (0 = Monday).
    std::uint8_t daysOff{};
    // When the robot leaves home for its next shift; planned on getting home.
    Tick commuteAt{};
    Needs needs;
    std::int64_t credits{};
    // Rent, or for an apartment it owns the upkeep, not yet paid; collected
    // from later credits first.
    std::int64_t rentOwed{};
    // The bank it saves at and borrows from, and what it has saved there:
    // credits the bank holds and owes it.
    EntityId bank;
    std::int64_t savings{};
    // Spent on outings since midnight, against its daily budget.
    std::int64_t spentToday{};
    // Bit n set: likes Interest n.
    std::uint8_t interests{};
    // Robots it has talked to, sorted by their id.
    std::vector<Bond> bonds;
    // Moments it remembers, oldest first.
    std::vector<Memory> memories;
    // 0 (junior) to kMaxJobLevel (lead); higher levels earn more.
    std::uint8_t jobLevel{};
    // How the robot has done at work since it was hired or last promoted.
    std::int32_t performance{};
    // Days since the robot was built.
    std::uint32_t age{};
    // Lasting damage from 0 to kMaxWear; clinics take some of it away.
    std::int32_t wear{};
    // Which gang the robot belongs to, none if zero; arrests for robbing.
    EntityId gang;
    std::uint32_t arrests{};
    // A metro ride on the walk under way: on the platform at rideFrom from
    // boardsAt, on the trains of the Metro timetable, changing lines at
    // rideVia (rideTo when it rides one line), off at rideTo by alightsAt.
    // Stations are indices into City::stations. All zero when walking the
    // whole way.
    std::uint32_t rideFrom{};
    std::uint32_t rideVia{};
    std::uint32_t rideTo{};
    Tick boardsAt{};
    Tick alightsAt{};
};

// How to get from one room to another: on foot, or partly by metro.
struct Trip {
    std::uint32_t minutes{};  // Door to door.
    bool metro{false};
    std::uint32_t fromStation{};  // Indices into City::stations.
    std::uint32_t viaStation{};   // Where it changes lines; toStation if not.
    std::uint32_t toStation{};
    std::uint32_t minutesToStation{};    // Walk and stairs to the station.
    // From reaching the platform to getting off the last train: waiting,
    // riding and changing.
    std::uint32_t minutesUnderground{};
};

// Fare of a metro ride; a robot that cannot pay it all pays what it has.
inline constexpr std::uint32_t kMetroFare = 4;

// A robot that was scrapped, kept while others still remember it.
struct DepartedRobot {
    EntityId id;
    std::string name;
    Tick scrappedAt{};
};

// Everything that defines a running world. This is exactly what a save
// contains; caches such as routes and room lookups are rebuilt after
// loading. Robots are kept sorted by id.
struct WorldState {
    Tick tick{};
    std::uint64_t seed{};
    EntityIdAllocator ids;
    Pcg32 random;
    City city;
    std::vector<Robot> robots;
    // Scrapped robots someone remembers, sorted by id.
    std::vector<DepartedRobot> departed;
    // Gangs, sorted by id.
    std::vector<Gang> gangs;
    Weather weather;
    // The knobs this city runs with (Settings.hpp), saved with it.
    SimSettings settings;
    Economy economy;
    // Counted since midnight.
    DayCounters today;
    // One entry per day, oldest first, at most kHistoryDays.
    std::vector<DailyStats> history;
    // The city's story: notable events, oldest first, at most kChronicleCap.
    std::vector<ChronicleEntry> chronicle;
    // Buildings under construction, sorted by building id.
    std::vector<Project> projects;
};

struct WorldParams {
    std::uint64_t seed{1};
    std::uint32_t robots{100};
    // The knobs of the simulation (Settings.hpp); the defaults are the game's.
    SimSettings settings{};
};

// A walk in progress, between two rooms.
struct Walk {
    EntityId from;
    EntityId to;
    Tick departedAt{};
    Tick arrivesAt{};
};

enum class EventKind : std::uint8_t {
    // room: left; otherRoom: heading to; minutes: the trip; metro and
    // stations when riding; credits: the fare paid.
    Departed,
    ClockedIn,       // room: workspace; minutes: late by.
    ShiftEnded,      // room: workspace; credits: wage earned.
    ArrivedAtVenue,  // room: venue; credits: price; minutes: planned stay.
    ArrivedHome,     // room: apartment.
    RentPaid,        // room: apartment; credits: paid.
    RentMissed,      // room: apartment; credits: now owed in total.
    // room: where; otherRobot: who with; topic; change: affinity, an
    // argument when negative. Each of the two robots gets one.
    Talked,
    // memory: what; otherRobot and room: who with and where, if anyone.
    Remembered,
    // The robot wore out and was scrapped; room: the apartment it left.
    Scrapped,
    // A newcomer moved to the city; room: its apartment.
    Arrived,
    // Rain started, changed or stopped; no robot. change: the rain now
    // (0 when dry); minutes: how long this spell lasts.
    WeatherChanged,
    // A robot borrowed; credits: how much.
    Borrowed,
    // A robot bought an apartment (room); credits: the price.
    BoughtProperty,
    // A robot sold an apartment (room); credits: what it got.
    SoldProperty,
    // A robot got a scrapped robot's things; otherRobot: whose.
    Inherited,
    // The central bank created money (credits > 0) or destroyed it; no
    // robot; account: whose account.
    MoneySupplyChanged,
    // A ground-floor unit (room) opened as a venue or workspace, or closed;
    // no robot; account: its owner.
    UnitOpened,
    UnitClosed,
    // A gang member robbed another robot; credits: what it took.
    Robbed,
    // An officer arrested a robber; room: the venue; otherRobot: victim.
    Arrested,
    // Members of rival gangs fought in a room; otherRobot: the opponent.
    Fought,
    // A robot joined a gang (room: its home); otherRobot: the friend who
    // invited it, if any.
    JoinedGang,
    // A robot left its gang or was forced out at 3 arrests.
    LeftGang,
};

// Something that happened during a step, for logs and observers. Events are
// not part of the saved state.
struct SimEvent {
    Tick tick{};
    EventKind kind{};
    EntityId robot;
    EntityId room;
    EntityId otherRoom;
    std::uint32_t minutes{};
    std::int64_t credits{};
    // Other robots already in the room on arrival.
    std::uint32_t others{};
    EntityId otherRobot;
    Interest topic{};
    std::int32_t change{};
    MemoryKind memory{};
    bool metro{false};
    std::uint32_t fromStation{};
    std::uint32_t viaStation{};
    std::uint32_t toStation{};
    EntityId account;
    EmissionReason emission{};
};

// Deterministic simulation of the city. The same WorldState always produces
// the same sequence of states and events, on every platform: all state is
// integral and randomness comes from the saved Pcg32.
//
// Each step is one game minute. Robots work fixed shifts; after work they
// weigh their needs, credits, walking time, opening hours and who is there
// to pick a venue or go home, and they head home in time to rest before the
// next shift. Robots sharing a venue or workspace talk, which makes friends
// and rivals over time.
class World {
public:
    using EventSink = std::function<void(const SimEvent&)>;

    // Minutes at home a robot keeps free before leaving for its next shift.
    static constexpr Tick kRestMinutes = 6 * kMinutesPerHour;
    // Every night ends at home by this minute of the day, 03:30.
    static constexpr Tick kBedtimeMinute = (3 * kMinutesPerHour) + 30;

    // Builds a new city and population from a seed.
    [[nodiscard]] static World Generate(const WorldParams& params);
    // Validates a loaded or hand-made state and rebuilds derived data.
    [[nodiscard]] static std::expected<World, std::string> FromState(
        WorldState state);
    // The settings this world runs with: part of its state, so saved.
    [[nodiscard]] const SimSettings& Settings() const noexcept { return state_.settings; }
    void SetSettings(const SimSettings& settings) noexcept { state_.settings = settings; }

    // Derived lookups point into the state, so a World can be moved but not
    // copied.
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World() = default;

    [[nodiscard]] const WorldState& State() const noexcept { return state_; }
    [[nodiscard]] Tick CurrentTick() const noexcept { return state_.tick; }
    [[nodiscard]] const Robot* FindRobot(EntityId id) const noexcept;
    // Name of a living robot or a remembered scrapped one.
    [[nodiscard]] std::string_view NameOf(EntityId robot) const;
    // Changes whenever robots are scrapped, arrive, or are forgotten.
    [[nodiscard]] std::uint64_t PopulationVersion() const noexcept {
        return populationVersion_;
    }
    [[nodiscard]] const RoomIndex& Rooms() const noexcept { return rooms_; }
    [[nodiscard]] const Metro& GetMetro() const noexcept { return metro_; }
    // Changes whenever a room changes kind, so views of the city can update.
    [[nodiscard]] std::uint64_t CityVersion() const noexcept { return cityVersion_; }
    [[nodiscard]] const Organization* FindOrganization(EntityId id) const noexcept;
    // What a robot or organization owes, or nothing.
    [[nodiscard]] const Loan* LoanOf(EntityId borrower) const noexcept;
    // Name of a robot, a remembered robot or an organization.
    [[nodiscard]] std::string_view AccountName(EntityId account) const;
    // Robots in a venue right now, not counting those still on their way.
    [[nodiscard]] std::uint32_t GuestsIn(EntityId room) const;

    // The walk a robot is on; nothing unless it is walking.
    [[nodiscard]] static std::optional<Walk> WalkOf(const Robot& robot);
    // The fastest way between two rooms: walking, or walking to a station,
    // riding the metro and walking on. Includes stairs.
    [[nodiscard]] Trip PlanTrip(EntityId fromRoom, EntityId toRoom,
                                bool metroAllowed = true);
    // Minutes door to door between two rooms, the fastest way.
    [[nodiscard]] std::uint32_t TravelMinutes(EntityId fromRoom,
                                              EntityId toRoom);

    // Receives every event produced by later steps.
    void SetEventSink(EventSink sink) { sink_ = std::move(sink); }

    // Advances the simulation by one minute.
    void Step();
    void RunFor(Tick minutes);

    // "Day 2 19:41  KX-417 Vera arrived at Neon Wok (noodle diner, Hab Block
    // 3), paid 10 cr, 4 others there"
    [[nodiscard]] std::string Describe(const SimEvent& event) const;
    // "Neon Wok (noodle diner, Hab Block 3)" or "Apartment 4-02, Hab Block 3"
    [[nodiscard]] std::string DescribeRoom(EntityId room) const;

private:
    explicit World(WorldState state);

    void UpdateNeeds();
    void Act(Robot& robot);
    void Arrive(Robot& robot);
    // An officer at the station: out to a venue on the hour, or wait.
    void Patrol(Robot& robot, Tick shiftEnd);
    [[nodiscard]] Tick NextPatrolAt(const Robot& robot) const;
    void Decide(Robot& robot);
    void StartWalk(Robot& robot, EntityId destination);
    void ScheduleNextCommute(Robot& robot);
    [[nodiscard]] Tick LatestHomeTick(const Robot& robot);
    // Robots living in an apartment.
    [[nodiscard]] std::uint32_t ResidentsOf(EntityId apartment) const;
    void CountResidents();
    // Going to a venue as a guest, not to work there.
    [[nodiscard]] bool IsGuestTrip(const Robot& robot, EntityId destination) const;
    void CollectRent();
    // Metro, in Transit.cpp.
    void IndexStations();
    // Work and money, in Careers.cpp.
    [[nodiscard]] std::uint32_t WageOf(const Robot& robot) const;
    // Costs performance for turning up worn down or miserable.
    void ScoreArrivalAtWork(Robot& robot);
    // Scores the shift just worked; promotes or fires.
    void EndShift(Robot& robot);
    // At night: robots out of work may be hired.
    void ReviewJobs();
    // At night: evictions, and robots with savings moving to nicer places.
    void ReviewHomes();
    // Robots aging and wearing out, in LifeCycle.cpp.
    void AgeRobots();
    void Scrap(EntityId id);
    void AddNewcomer();
    void ForgetDeparted();
    // Relationships, in Social.cpp.
    void Socialize();
    void Talk(Robot& first, Robot& second);
    void ChangeBond(Robot& robot, EntityId other, std::int32_t change);
    void UpdateBondLevel(Robot& robot, Bond& bond, EntityId room);
    void Remember(Robot& robot, MemoryKind kind, EntityId other, EntityId room);
    void FadeBonds();
    // How much friends at a venue draw the robot there, rivals keep it away.
    [[nodiscard]] std::int64_t CompanyAt(const Robot& robot,
                                         EntityId venue) const;
    // Sets when a robot at home next acts: an outing on a free day, or
    // leaving for work.
    void PlanAtHome(Robot& robot);
    [[nodiscard]] static bool IsDayOff(const Robot& robot, Tick tick) noexcept;
    // First shift start after `after` that is not on a day off.
    [[nodiscard]] Tick NextShiftStart(const Robot& robot, Tick after) const;
    // Longest time the robot may need between leaving home and its shift.
    [[nodiscard]] Tick MaxCommuteLead(const Robot& robot);
    [[nodiscard]] std::int64_t RentOf(const Robot& robot) const;
    // The shift a robot works, at a workspace or a venue.
    [[nodiscard]] Shift ShiftOf(const Robot& robot) const;
    // Robots holding each job, by JobKey.
    [[nodiscard]] std::unordered_map<std::uint64_t, std::uint32_t> StaffCounts() const;
    // Jobs a robot could take now: workspaces with room, venues short of staff
    // on the shift it would work, and the site office of a building going up,
    // whose other rooms open only when the project is done (task D, steps D2c
    // and D3c).
    [[nodiscard]] std::vector<EntityId> OpeningsFor(
        EntityId robot, const std::unordered_map<std::uint64_t, std::uint32_t>& staff) const;
    // The event log and the saved story. Neither is const: the chronicle
    // lives in `state_`, so emitting an event can change saved state.
    void Emit(const SimEvent& event);
    // Keeps the event in `state_.chronicle` when it is notable (Chronicle.cpp
    // lists which kinds); `Emit` calls it for every event.
    void Chronicle(const SimEvent& event);
    // Gangs, in Gangs.cpp.
    void FoundGangs(Pcg32& random);
    void ReviewGangs();
    // On a gang outing: the venues within a walk of one turf, its gang's own
    // or a rival gang's, picked by lot. Empty means an ordinary outing over
    // the whole city; `Decide` only weighs these venues otherwise (B14b: rival
    // members have to meet to fight).
    [[nodiscard]] std::vector<EntityId> GangOuting(const Robot& robot);
    void HandleOfficerAffinity(Robot& first, Robot& second);
    // One definition of "this robot is an officer": its job room is a police
    // station. ReviewGangs keeps such robots out of any gang.
    [[nodiscard]] bool WorksInPoliceStation(const Robot& robot) const;
    // An officer standing its post right now: an officer working or walking a
    // patrol leg. Socialize's watched rooms and HandleOfficerAffinity both
    // ask this, so they cannot disagree about who counts as an officer - the
    // officer rule B4 asked for went dead because it also asked whether the
    // officer belonged to a gang, which ReviewGangs forbids.
    [[nodiscard]] bool OfficerOnShift(const Robot& robot) const;
    // Crime and gang interactions, in Gangs.cpp. `officerInRoom` maps a room
    // that holds an officer on shift to that officer's place in `state_.robots`
    // (the smaller id when several officers share a room); `Socialize` builds
    // it once per call so every pair can ask, instead of walking the whole
    // population for each pair (the check B7 asked for), and an arrest can
    // name the officer that made it (B15).
    void ConsiderCrime(Robot& first, Robot& second,
                       const std::map<std::uint64_t, std::size_t>& officerInRoom);
    // One arrest: the fine to City Hall, the rap sheet, the day's count, and
    // both memories of who took whom: `Arrested` on the robber naming the
    // officer, `ArrestedSomeone` on the officer naming the robber. Both ways
    // an officer takes someone go through this, so an arrest is one thing
    // written once.
    void Arrest(Robot& robber, Robot& officer);
    // B4's second officer rule: an officer on patrol meeting a member that
    // robbed in the last hour arrests it, wherever the robbery happened - the
    // robbery's own room is watched only about one hour in fifty, so matching
    // on the room left arrests at 4 in 40 days. `Socialize` calls this once
    // per watched room with the robots standing in it and the room's officer;
    // it walks them in id order and takes no random number, so the same room
    // always gives the same arrests by the same officer (rule 1: the same
    // state grows the same future everywhere).
    void PatrolArrests(const std::vector<std::size_t>& present,
                       std::size_t officerIndex);
    // The economy, in Founding.cpp, Money.cpp, Units.cpp, Property.cpp,
    // Settlement.cpp and EconomyStatistics.cpp (see docs/MODULES.md).
    void FoundEconomy(std::uint64_t seed);
    void SellShares(const std::vector<std::pair<EntityId, std::int64_t>>& capital,
                    EntityId bank, EntityId central);
    void IndexEconomy();
    [[nodiscard]] Organization* MutableOrganization(EntityId id) noexcept;
    [[nodiscard]] Robot* MutableRobot(EntityId id) noexcept;
    [[nodiscard]] Room& MutableRoom(EntityId id);
    [[nodiscard]] std::int64_t& Balance(EntityId account);
    [[nodiscard]] Books* BooksOf(EntityId account);
    // Moves credits; the payer must have them.
    void Pay(EntityId from, EntityId to, std::int64_t amount);
    // A robot pays what it can of a bill, borrowing if it may; the rest
    // is added to what it owes. Returns what was paid.
    std::int64_t PayBill(Robot& robot, EntityId to, std::int64_t amount);
    // An organization pays, borrowing whatever it lacks.
    void PayOrBorrow(Organization& payer, EntityId to, std::int64_t amount);
    // A sale to a robot at a venue, the metro or similar: revenue for the
    // seller, part of which buys supplies from industry.
    void Sell(Robot& buyer, EntityId seller, std::int64_t price);
    void PaySupplies(Organization& buyer, std::int64_t revenue);
    // What a robot spends beyond the price on a visit.
    [[nodiscard]] std::int64_t LuxuryOf(const Robot& robot, std::int64_t price) const;
    // A shift's pay: the job level's wage at the employer's share of it,
    // before tax.
    [[nodiscard]] std::int64_t PayOf(const Robot& robot) const;
    // What a robot keeps of that once the wage tax is withheld.
    [[nodiscard]] std::int64_t NetPayOf(const Robot& robot) const;
    // At midnight: taxes on profits, support for robots out of work, and
    // public works with whatever City Hall has to spare.
    void RunCityHall();
    // At midnight: interest on savings, and the central bank's rate.
    void SetPolicyRate();
    // What a robot means to spend on outings in a day.
    [[nodiscard]] std::int64_t BudgetOf(const Robot& robot) const;
    void Lend(EntityId lender, EntityId borrower, std::int64_t amount);
    [[nodiscard]] bool MayBorrow(const Robot& robot, std::int64_t amount) const;
    void ChangeMoney(EntityId account, std::int64_t amount, EmissionReason reason);
    void PayWage(Robot& robot);
    // Takes up to `amount` of a robot's savings out of its bank.
    void Withdraw(Robot& robot, std::int64_t amount);
    // Pocket money kept out of the bank.
    [[nodiscard]] std::int64_t PocketOf(const Robot& robot) const;
    [[nodiscard]] std::int64_t PriceOf(const Room& apartment) const;
    // Rent asked for an apartment now: its base rent at the market rent of
    // its class; dorm rents are set by the housing office.
    [[nodiscard]] std::int64_t RentFor(const Room& apartment) const;
    // The price a venue asks for a visit now.
    [[nodiscard]] std::int64_t PriceAt(const Room& venue) const;
    // At midnight: venues' prices follow how busy they were, and market
    // rents how many homes stand empty.
    void SetPrices();
    // At midnight: venues quiet for long close, and empty ground-floor
    // units open as the venues the city is short of, or as workspaces
    // while many robots are out of work.
    void ReviewUnits();
    void CloseUnit(Room& room);
    void OpenUnit(Room& room, RoomKind kind);
    // At midnight, after the units: a site going up pays its supplies and
    // raises a floor (Construction.cpp).
    void ReviewConstruction();
    [[nodiscard]] std::int64_t UpkeepOf(const Room& apartment) const;
    // Changes an apartment's owner for `price`, paid by the buyer.
    void Transfer(EntityId apartment, EntityId buyer, std::int64_t price);
    // Hands a scrapped robot's credits, apartments and shares on.
    void Bequeath(Robot& robot);
    // At midnight: the supply market, interest, repayments, wages and
    // dividends; then the day's statistics.
    void SettleEconomy();
    void TradeProperty();
    void RecordStatistics();
    [[nodiscard]] std::int64_t ReserveOf(const Organization& organization) const;

    WorldState state_;
    RoutePlanner routes_;
    RoomIndex rooms_;
    Metro metro_;
    // Robots in or heading to each venue room, for capacity checks.
    std::unordered_map<std::uint64_t, std::uint32_t> reserved_;
    // Robots inside each venue room.
    std::unordered_map<std::uint64_t, std::uint32_t> guests_;
    std::uint64_t populationVersion_{};
    // For every street node, its nearest station and the walk there.
    std::vector<std::uint32_t> nearestStation_;
    std::vector<std::uint32_t> metersToStation_;
    // Robots living in each apartment.
    std::unordered_map<std::uint64_t, std::uint32_t> residents_;
    std::uint64_t cityVersion_{};
    // The loan of each borrower, as an index into economy.loans.
    std::unordered_map<std::uint64_t, std::size_t> loanOf_;
    EntityId centralBank_;
    EntityId cityHall_;
    EntityId housing_;
    EntityId utilities_;
    EntityId transit_;
    EventSink sink_;
};

// The shift of a job: a workspace's building shift, or for venue staff
// the venue's shift the robot works.
[[nodiscard]] Shift JobShift(const RoomRef& job, EntityId robot) noexcept;
// A job and shift, as a key: a robot at a venue counts toward its shift.
[[nodiscard]] std::uint64_t JobKey(const Room& job, EntityId robot) noexcept;

// Checks references and invariants of a state; describes the first problem.
[[nodiscard]] std::expected<void, std::string> ValidateWorldState(
    const WorldState& state);
// For tests and tools: sets a robot's credits, as if the central bank had
// created or destroyed the difference, so the money still adds up.
void SetCredits(WorldState& state, Robot& robot, std::int64_t credits);
// The money adds up, and every owner, bank and loan exists.
[[nodiscard]] std::expected<void, std::string> ValidateEconomy(const WorldState& state);

}  // namespace mechapunk::sim
