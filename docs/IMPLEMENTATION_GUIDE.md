# Mechapunk implementation guide

Everything needed to continue the game without earlier conversations: how
to build and check work, the rules that must never break, where things
live, and a concrete plan for every remaining task. Read sections 1 to 4
before changing code; section 5 holds the task plans.

State as of 2026-11-18: save version 16, game version 0.16.0, 103 tests
passing (`sim_tests` and `layout_tests`), last commit "Give the central
bank a rate".

## 1. Build, run, check

Windows, LLVM-MinGW clang, Conan, CMake. From the repo root:

```bash
conan install . --output-folder=build --build=missing --build=editable -pr:a mingw   # once
cmake --build build/build/Release                                                    # everything
cmake --build build/build/Release --target sim_tests mechapunk-sim                   # sim only, faster
ctest --test-dir build/build/Release                                                 # all tests
build/build/Release/tests/sim_tests.exe --gtest_brief=1 --gtest_filter=Economy.*     # some tests
```

- The build tree is `build/build/Release`. A running `mechapunk.exe` or
  `mechapunk-sim.exe` locks its exe: the link then fails with "Permission
  denied". Stop it first.
- Headless runs, the main tool for checking simulation changes:
  `build/build/Release/mechapunk-sim.exe --seed 1 --robots 10000 --days 40 --quiet`.
  It prints a block per day: walks, visits, careers, metro, weather, then
  `economy:`, `wealth:`, `city hall:` and `homes:` lines (see
  `tools/sim_runner/main.cpp`). 40 days at 10000 robots take about 6 to 7
  minutes: run them in the background and read the output file later.
  `--all` prints every event; `--minutes N --save FILE` writes a save.
- The game: `build/build/Release/mechapunk.exe --save FILE` continues a save;
  `--new --save FILE` starts a city; `--seed`, `--robots`, `--follow ROBOT_ID`.
  Keys: 1-5 speed (x1 real time, x2, x10, x100, x1000), Space pause, T
  statistics, F follow, Q/E floors, F12 screenshot.
- `tools/drive-game.ps1` sends clicks, keys and F12 to the game window.
  **Only use it when the user says the machine is free**: it activates the
  game window, and clicks can land in other applications if the game is not
  in front. Never capture the screen: it may show the user's private
  windows.
- Floor plans can be checked without the game: write a throwaway GoogleTest
  that calls `mechapunk::PlanFloor(building, floor)` and dumps boxes, props
  and spots as JSON, then draw them with Python and Pillow (`python -I`).
  Remove the throwaway test afterwards.

## 2. Rules that must never break

1. **Determinism.** The same `WorldState` gives the same future on every
   machine. All randomness in the simulation comes from `state_.random`
   (a saved `Pcg32`) or from `Pcg32::FromSeed(seed, stream)` with a fixed
   stream number during generation. Robots act in id order. Never let the
   iteration order of an `unordered_map`/`unordered_set` decide an outcome
   or a random draw; iterate vectors, sorted maps, or `state_.robots`.
2. **Money adds up.** Credits only move with `World::Pay`, `PayOrBorrow`,
   `PayBill`, `Sell`, `Lend`, `Withdraw`; money is created or destroyed only
   by `World::ChangeMoney`, which logs an `Emission`. `ValidateEconomy`
   checks that robots' and organizations' credits plus the supply market
   equal `economy.created - economy.destroyed`. **Savings are not money**:
   `Robot::savings` is what the bank owes; the cash sits in the bank's
   credits. Interest on savings raises `savings` without moving credits.
   In tests, change a robot's credits only with `sim::SetCredits`.
3. **Saves never break.** Any change to `WorldState` or a type inside it
   (a new field, a renamed enum value, a new enum value) is a save format
   change. Checklist:
   1. Bump `kCurrentSaveVersion` and `kGameVersion` in
      `sim/include/mechapunk/sim/Save.hpp`.
   2. Write `VNToVN+1` in `sim/src/Migrations.cpp` (declare it in
      `Migrations.hpp`, register it in `kMigrations` in `Save.cpp`). It edits
      the untyped JSON and never calls game code. Reading uses
      `error_on_missing_keys`, so **every new key must be added**, including
      inside every element of `world.history` (DailyStats and its
      `counted` DayCounters) and every organization's `today`/`yesterday`
      Books. Frozen: never edit a released migration.
   3. New enum values get a name in the `glz::meta` specializations at the
      top of `Save.cpp`.
   4. Make a new fixture: `mechapunk-sim --seed 33 --robots 100 --minutes 28370
      --save tests/fixtures/saves/vN-city.json`, add a row and a sentence to
      `tests/fixtures/saves/README.md`.
   5. In `tests/SaveTests.cpp`, point `CurrentVersionFixtureLoadsAndRoundTrips`
      at the new fixture (add checks for the new data) and add
      `VersionN-1FixtureMigratesAndRuns` for the previous fixture. Never
      regenerate an old fixture.
4. **No teleporting robots.** Robots walk through doors, corridors, stairs,
   streets and metro platforms; `CityView::RobotPosition` builds the path.
   Hiding robots nobody can see is fine; jumping is not.
5. **Validation.** `ValidateWorldState` (World.cpp, plus `ValidateEconomy`
   and `ValidateSocial`) runs on every load. Keep it in step with new state.
   A robot `Visiting` must be in a venue, `Working` in its job room, etc.
6. **Workflow.** Game work is committed straight to `master` and pushed.
   NEE (`external/NEE`) changes only through GitHub pull requests that the
   user merges. Commit messages: a short imperative title, a body that
   explains, and the attribution line the session gives.
7. **Style.** clang-format/clang-tidy as configured. Comments are full,
   plain sentences explaining why. In NEE: no raw nullable pointers where a
   reference works; `gsl_lite::not_null` instead of runtime null checks.
   UI: plain Dear ImGui calls. The user wants **more detail, never less**
   (no flat facade textures instead of window boxes, for example).

## 3. Architecture map

### Simulation library (`sim/`, namespace `mechapunk::sim`)

| File | What lives there |
| --- | --- |
| `World.hpp/.cpp` | `Robot`, `WorldState`, `EventKind`/`SimEvent`, the `World` class, `Step`, robot decisions (`Act`, `Arrive`, `Decide`, `StartWalk`), rent (`CollectRent`), `Describe`, `ValidateWorldState`, generation (`World::Generate`, `AssignDorm`, `AssignJob`) |
| `City.hpp/.cpp` | Streets, stations, buildings, rooms, `RoomKind`, `ApartmentClass`, `GenerateCity`, `RoomIndex`, `RoutePlanner` |
| `Venues.hpp/.cpp` | `VenueRules` per venue kind (hours, price, effects, capacity, `StaffRules` shifts), `StaffPerShift`, `JobsAt`, `ShiftSlot` |
| `Careers.hpp/.cpp` | Wages, promotions, firing, `ReviewJobs`, `ReviewHomes` (eviction, moving up, friends moving in together), `JobShift`, `JobKey`, `OpeningsFor` |
| `Economy.hpp/.cpp` | Organizations, loans, emissions, `DayCounters`, `DailyStats`, founding, all money movement, `SettleEconomy`, `TradeProperty` (sales, mortgages), `ReviewUnits` (venues open/close), `SetPrices`, `RunCityHall`, `SetPolicyRate`, `RecordStatistics`, `ValidateEconomy` |
| `Social.hpp/.cpp` | Interests, bonds, memories (`MemoryKind`, `MemoryText`), talks, `Remember` (also counts daily statistics) |
| `LifeCycle.cpp` | Aging, wear, `Scrap` (with `Bequeath`), `AddNewcomer` |
| `Transit.cpp`, `Metro.hpp/.cpp` | Trip planning, the metro network, timetable and trains |
| `Weather.*`, `Random.*`, `GameTime.*`, `Ids.hpp` | As named |
| `Save.hpp/.cpp`, `Migrations.*` | glaze JSON, enum names, migrations |

Order of the midnight step (`World::Step` when `MinuteOfDay == 0`):
`CollectRent` (rent, upkeep, property tax) → `AgeRobots` (scrapping,
newcomers) → `ReviewHomes` → `ReviewJobs` → `FadeBonds` → (needs and talks
every 10 minutes) → `ForgetDeparted` → `TradeProperty` → `SettleEconomy`.
`SettleEconomy`: supply market to industry → interest and repayments →
supply price → employers' pay → `RunCityHall` (profit tax, support, public
works) → `SetPolicyRate` (savings interest, rate) → dividends → deposits →
`ReviewUnits` → `SetPrices` → `RecordStatistics` → books roll over.

Derived, unsaved caches in `World`: `routes_`, `rooms_` (rebuild with
`rooms_ = RoomIndex(state_.city)` after any room kind changes), `metro_`,
`reserved_`/`guests_` (venue occupancy; only guest trips count, see
`IsGuestTrip`), `residents_` (`CountResidents`), `loanOf_` (`IndexEconomy`),
`cityVersion_` (bump when rooms change so the renderer reloads them).

### Game (`app/`, namespace `mechapunk`)

| File | What lives there |
| --- | --- |
| `SimulationHost` | Runs the World on a worker thread; publishes an immutable `WorldFrame` after each step: `RobotFrame`s, shared `RobotSocial`, `RobotDirectory`, `EconomyFrame` (organizations, loans, emissions, history, room owners; refreshed every 10 minutes), and `city` + `cityVersion` when rooms changed. |
| `CityView` | Everything drawn: city, static batches, stations, trains, robots (`RobotPosition`), interiors of the open floor, signs and lights (`UpdateLights`), picking. `UpdateCity` takes changed rooms. |
| `Interior` | `PlanFloor`: floor plans (walls, props, rooms with spots and entries) for every floor kind; `WayOut` paths. |
| `Props` | Box models of every prop and the robot figure. |
| `Inspector` | Inspector window for the selected building, room or robot. |
| `Statistics` | Statistics window (T): Overview, Economy, Society, Property, Organizations, Central bank tabs, plots from `DailyStats`. |
| `main.cpp` | Window, input, camera, panels, applying frames. |

Adding data the UI needs: add it to `RobotFrame` or `EconomyFrame` in
`SimulationHost.hpp` and fill it in `SimulationHost::Publish`.

## 4. Pitfalls met before

- Edits by exact string anchors fail when the anchor appears twice; use a
  longer anchor. Some files mix LF and CRLF; `git checkout -- FILE` if only
  line endings changed.
- Robots going to work at a venue must not count as its guests
  (`IsGuestTrip`), and nobody goes out to the venue it works at.
- A room can only change kind when nobody is in it or on the way to it.
- Tests that script one or two robots are fragile: many systems act on
  them (mortgages, hiring, bonds fading, layoffs). Prefer population-level
  tests (run a city for N days, check a property of all robots), or set up
  the exact state a minute before midnight and run one minute.
- Integer money: small percentages round to zero for small amounts. Keep
  it in mind for interest, taxes and tests.
- Small towns (60 to 400 robots) behave differently from 10000: some
  classes of home or kinds of venue may be missing.
- Economy changes need a 30 to 40 day run at 10000 robots before trusting
  them. Watch: `created` per day (should stay small), `wages at N%`
  (industry pay), corporate and bank debts, Gini, broke robots, homes.

## 5. Remaining tasks, with plans

Suggested order: A (police and transit staff), B (gangs), C (chronicle),
D (construction), then E to H as wanted. Each task is one or more commits;
each saved change follows rule 3.

### A. City staff: police and metro drivers (City Hall payroll)

> **Status: done** (commit 8af52a3 plus the fixes after it). Officers patrol
> on the hour (`World::Patrol`), stay `kPatrolStayMinutes` at the venue and
> only set out when they can be back before the shift ends. Drivers are
> planned by `Metro::PlanDriverRun` and drawn by `CityView::DriverPlacement`:
> they walk from the crew room to the platform, board through the front door,
> ride at `TrainState::cab` (which moves smoothly while the train reverses),
> get off at the last stop at their first station before the shift ends and
> walk back. Drivers ranked beyond the line's trains, or with no train in
> their shift, stay in the crew room. Old saves (before v17) have no depots,
> so they never get drivers.

Why: gangs need police as their rivals; the user asked for metro drivers
(optional). Both are City Hall jobs paid from taxes.

**Police**
- Add `RoomKind::PoliceStation` (save enum name `"policeStation"`). A
  police station is a ground-floor unit: let City Hall open one in an empty
  unit (`ReviewUnits`) when the city has fewer than one station per 2000
  robots, and generate one per 3000 robots in new cities (in `MakeHabitat`'s
  store list or by converting a store after generation). Owner: City Hall.
- Jobs: give it `StaffRules` like a venue (three 8-hour shifts, wage 120)
  through a small `FindStaffRules(kind)` that covers venues and stations, and
  make `JobsAt`, `OpeningsFor`, `AssignJob`, `JobShift`, `JobKey` use it.
  `PayWage` already pays from the room's owner (City Hall).
- On shift, an officer patrols: instead of standing in the station, every
  hour it walks to a random venue within 10 minutes and back. Simplest
  correct way: a new `RobotActivity::Patrolling` is a bigger change; instead
  keep `Working` and add `Robot::patrol` (EntityId of the venue being
  visited, none at the station). Render the walk like any walk (rule 4).
- Interior: a front desk, desks with chairs, a holding cell (bars as thin
  boxes). Sign colour: blue and white.

**Metro drivers** (no teleporting)
- Each line needs one driver per train per shift. Add a `RoomKind::CrewRoom`
  in a new `Metro Depot` workplace building generated near each line's first
  station (one crew room per line, capacity trains x shifts), owned by the
  Metro Authority, which City Hall owns.
- A driver commutes to the crew room, walks from there to the line's first
  station platform (`StationRoute`, stairs, platform spot as riders do),
  waits for the next train of its shift, boards through the cab door and
  stays in the cab (front of the leading car: `TrainPoseOf` position plus
  forward x 12 m) until the shift ends at a stop at that station, then walks
  back. Which train: drivers of a line on shift, sorted by id, take trains
  0, 1, 2... of the line (`Metro::TrainAt(line, train, time)`).
- The simulation needs no train positions: the driver's job is in the crew
  room; only `CityView::RobotPosition` draws it on the platform or in the
  cab during its shift. Keep this rule: the sim decides where a robot is
  (room), the view decides how it gets there.

Tests: officers and drivers are hired and paid by City Hall (`EconomyTests`),
a police station opens in an empty unit when there is none, money adds up.

### B. Gangs

The user's spec: gangs can be rivals or friends of each other, always
rivals of the police; members rob robots who are not in their gang. Unions
later.

**Data (save change)**
```cpp
enum class Stance : std::uint8_t { Friendly, Neutral, Rival };
struct GangRelation { EntityId gang; Stance stance{Stance::Neutral}; };
struct Gang {
    EntityId id;           // From state.ids.
    std::string name;      // "Chrome Jackals", "Rust Saints", ...
    std::uint32_t color;   // 0xRRGGBB, for jackets/accents in the view.
    EntityId turf;         // A habitat building it claims, or none.
    std::vector<GangRelation> relations;  // Sorted by gang id.
    std::int64_t credits{};               // The gang's kitty (an account!).
};
// WorldState: std::vector<Gang> gangs;  (sorted by id)
// Robot: EntityId gang;  // none unless a member
```
A gang with credits is an account: `World::Balance` must find gangs too (or
keep the kitty in members' credits and skip `Gang::credits`, simpler). Add
`ValidateEconomy`/`ValidateWorldState` checks: members exist, relations
point at gangs, stance with itself not stored.

**Founding:** in a new city, 3 to 5 gangs (one per 1500 robots, at least
2) from a new random stream, each with a turf habitat near the city's
edge. Relations: random, at least one rival pair. Migration for old saves:
no gangs (`gangs: []`, every robot `gang: 0`); gangs then form at runtime
(next point).

**Joining and leaving (nightly, in a new `ReviewGangs` after `ReviewJobs`):**
- Who: robots out of work, in debt or broke, young (age under 120 days),
  living in dorms; chance about 2% a night each, 4x if a close friend is a
  member, 0 if a close friend is an officer. They join their friend's gang,
  else the gang whose turf is nearest home.
- Leave: 1% a night when employed and not in debt; always when arrested
  three times (`Robot::arrests`). Memories: `JoinedGang`, `LeftGang`.

**Robbery:** when a gang member is in a room or on the street at night
(20:00-04:00) with a non-member who is not friendly, the member may rob
them: in `Socialize` (every 10 minutes, pairs in the same room) roll 3% per
pair where the robber's gang is not friendly with the victim's gang. Take
`min(victim.credits, 20 + victim.credits / 4)` with `Pay(victim, robber)`.
Events `Robbed` (victim, otherRobot robber, credits) and memories `WasRobbed`
for the victim (bond to robber -60, so rivalry), `RobbedSomeone` for the
robber. Never rob in a room where an officer on shift is; if an officer is
in the room, the robber is arrested instead.

**Arrests:** an officer in the same room as a robbery, or patrolling into a
venue with a member who robbed there in the last hour, arrests the robber:
fine `min(credits + savings, 100)` paid to City Hall (`PayBill`), the loot
returned to the victim if still held, `arrests++`, memory `Arrested`.
Gang members and officers are always rivals: in `Talk`, member with
officer changes affinity by -20.

**Gang war:** rival gangs' members in the same room argue (affinity -30)
and may fight (1%): both lose condition 100 and wear 20; event `Fight`.

**Statistics:** `DayCounters`: robberies, robbedCredits, arrests, fights,
joined, left; `DailyStats`: gangMembers, per gang member counts (a
vector sized by gangs, fine in JSON). Statistics window: a "Crime" tab.
Inspector: a robot's gang and its arrests; a gang list with members.
View: gang members get the gang's colour as their accent (`Accent(robot)`
in `CityView.cpp` line ~167, pass the colour through `RobotFrame`).

Tests: gangs founded with relations; robberies move credits and money adds
up; no robbery with an officer present; arrests fine the robber; members of
friendly gangs don't rob each other; save migration.

### C. Chronicle (saved city history)

```cpp
struct ChronicleEntry { Tick tick; EventKind kind; EntityId robot; EntityId otherRobot;
                        EntityId room; std::int64_t credits; MemoryKind memory; };
// WorldState: std::vector<ChronicleEntry> chronicle;  // newest last, at most 5000
```
Record in `World::Emit` (or in the specific places) only notable events:
scrapped, arrived, close friendships, rivalries, firings, promotions to
lead, evictions, home purchases, venues opening/closing, gang events,
money created over 10000 in one emission. Text comes from
`World::Describe` (needs a `SimEvent`; build one from the entry). UI: a
"Chronicle" tab in the Statistics window, newest first, robot names
clickable (`InspectorRequest` like the Organizations tab). Robot inspector:
"History" section with its own entries.

### D. Construction

- Land: free corners are parks (`CityView` draws them; the sim does not
  know them). Make them sim state: `City::lots` (node and corner index)
  so the sim can build there; migration fills lots from the grid exactly as
  `CityView` computes free corners (four corners per node minus taken).
- Trigger (nightly): vacancy of homes under 5% for a week → the Habitat
  Trust starts a habitat; jobs per robot under 1.05 → an industry
  corporation starts a workplace. Projects: `struct Project { EntityId
  building; std::uint32_t daysLeft; EntityId owner; }` in `WorldState`.
- A project employs builders (jobs in a site office room) and pays
  supplies; when done, the building's rooms become usable. Rendering: a
  building under construction is a scaffold (thin boxes) growing floor by
  floor; `CityView` must add and rebuild static batches for a new building
  (today they are built once: write `AddBuilding`/`RemoveBuilding` that edit
  `staticBatches_`, `shapes_`, `buildingBlocks_`, facades and signs).
  `cityVersion_` already tells the view about changes.

### E. Simulation level of detail

Not urgent (10000 robots: about 1.5 s per simulated day). If needed: update
needs for robots at home or at work every 30 minutes instead of 10, and
cache `TravelMinutes` per building pair. Saves (about 40 MB) could store
history and emissions more compactly.

### F. See-through windows (user asked for detail)

Lit windows should show the room behind or a cube map inside. Start by
making window panes glass (transparent, already supported by
`SubmitTransparentMesh`) only on the open building, and light window boxes
by real occupancy: a window glows when its apartment's residents are home
and awake (`EconomyFrame::owners` plus `RobotFrame::apartment`).

### G. Unions (after gangs)

Workers of one employer form a union when pay falls under 90% for a week;
union strikes (no shifts) for a day when pay cuts continue; employers then
raise pay. Fits `SettleEconomy`'s pay step.

### H. Engine (NEE, by pull request only)

Instance handles for per-frame meshes, input mapping and camera controllers,
node hierarchy on model import, more lights (clustered or instanced
volumes), rotating autosaves in the game. See `ROADMAP.md`.

## 6. Economy constants worth knowing

All in `sim/src/Economy.cpp` (anonymous namespace) or
`sim/include/mechapunk/sim/Economy.hpp`: upkeep 50% of rent; price of a home
100 nights of rent; supply shares (venues: market price `supplyPercent`,
utilities and transit 95%, housing 50%); budget 80% of pay after tax plus a
tenth of savings beyond 30 nights of bills; mortgage 20% down, up to 60
shifts' pay, interest at most a third of pay; venue prices +-1% a night
(50% to 300%); market rents +-1% a night by vacancy (5% / 15%); units close
after 10 quiet nights; taxes 10% wages, 5% of rent as property tax, 20% of
profits; support 40 a night; policy rate 2 to 50 in 10000 a night.
