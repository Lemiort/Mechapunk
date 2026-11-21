# Mechapunk implementation guide

How to continue the game without earlier conversations. Written for models
with a small context (about 57k tokens): everything is split so one step of
one task needs only this guide, one task file and one module.

State as of 2026-11-19: save version 17, game version 0.17.0, 111 tests
passing (`sim_tests`, `layout_tests` and `ContextBudget`).

## 1. How to work with a small context

1. Read this guide (about 5k tokens).
2. Pick the next open task from section 8 and read its file in
   `docs/tasks/` (at most about 4k tokens). It is cut into **steps**; each
   step names the module to open and ends in one commit.
3. Open only the files of that step's module, as listed in
   `docs/MODULES.md`. Do not read other big files whole: find a function
   with `rg -n "FunctionName" sim app` and read just those lines.
4. Do one step, build, run the checks in section 2, commit, push. Then start
   the next step fresh: the commit and the task file carry everything over.
   Tick the step in the task file (`- [x]`) in the same commit.
5. If a step does not fit (a compile error leads into a third module, say),
   stop, commit what builds, and split the step in the task file.

Never paste whole files into your context to "have a look": that is how a
small context runs out. Never refactor outside the step.

## 2. Build, run, check

Windows, LLVM-MinGW clang, Conan, CMake. From the repo root:

```bash
conan install . --output-folder=build --build=missing --build=editable -pr:a mingw   # once
cmake --build build/build/Release                                                    # everything
cmake --build build/build/Release --target sim_tests mechapunk-sim                   # sim only, faster
ctest --test-dir build/build/Release                                                 # all tests
build/build/Release/tests/sim_tests.exe --gtest_brief=1 --gtest_filter=Economy.*     # some tests
python scripts/context_budget.py                                                     # module sizes
```

- The build tree is `build/build/Release`. A running `mechapunk.exe` or
  `mechapunk-sim.exe` locks its exe and the link fails with "Permission
  denied": stop it first.
- The headless runner is the main check for simulation changes:
  `build/build/Release/mechapunk-sim.exe --seed 1 --robots 10000 --days 40 --quiet`.
  Each day it prints walks, visits, careers, metro and weather, then
  `economy:`, `wealth:`, `city hall:` and `homes:` lines. 40 days at
  10000 robots take about 4 to 7 minutes: run it in the background and read
  its output file. `--all` prints every event, `--minutes N --save FILE`
  writes a save. Never print a whole `--all` log into your context: grep it.
- A pure move of code (a split, a rename of a file) must not change
  anything: run `--seed 7 --robots 2000 --days 6 --all --save X.json`
  before and after and compare both files byte for byte, ignoring the last
  two lines of the log (timing and save path).
- The game: `build/build/Release/mechapunk.exe --save FILE` continues a
  save; `--new --save FILE` starts a city; `--seed`, `--robots`,
  `--follow ROBOT_ID`. Keys: 1-5 speed, Space pause, T statistics, F follow,
  Q/E floors, F12 screenshot.
- **Do not drive or screenshot the game window unless the user says the
  machine is free.** `tools/drive-game.ps1` activates the window and clicks
  can land in other applications; a screen capture may show the user's
  private windows. Interface work is checked by unit tests of the data it
  shows, by building, and by the user in the game.
- Floor plans can be checked without the game: a throwaway GoogleTest that
  calls `mechapunk::PlanFloor(building, floor)` and dumps boxes, props and
  spots as JSON, drawn with Python and Pillow (`python -I`). Remove the
  throwaway test afterwards.

## 3. Rules that must never break

1. **Determinism.** The same `WorldState` gives the same future on every
   machine. All simulation randomness comes from `state_.random` (a saved
   `Pcg32`) or `Pcg32::FromSeed(seed, stream)` with a fixed stream during
   generation. Robots act in id order. Never let the iteration order of an
   `unordered_map`/`unordered_set` decide an outcome or a random draw:
   iterate vectors, sorted maps or `state_.robots`, or sort first.
2. **Money adds up.** Credits move only with `World::Pay`, `PayOrBorrow`,
   `PayBill`, `Sell`, `Lend`, `Withdraw` (all in `Money.cpp`); money is
   created or destroyed only by `World::ChangeMoney`, which logs an
   `Emission`. `ValidateEconomy` checks robots' and organizations'
   credits plus the supply market equal `created - destroyed`. Savings are
   not money: `Robot::savings` is what the bank owes. In tests change
   credits only with `sim::SetCredits`.
3. **Saves never break.** Any change to `WorldState` or a type inside it (a
   field, an enum value) is a save format change:
   1. Bump `kCurrentSaveVersion` and `kGameVersion` in `Save.hpp`.
   2. Write `VNToVN+1` at the end of `sim/src/Migrations.cpp`, declare it
      in `Migrations.hpp`, register it in `kMigrations` in `Save.cpp`. It
      edits the untyped JSON and never calls game code. Reading uses
      `error_on_missing_keys`, so **every new key must be added**. Where a
      new field goes depends on its type:

      | New field in | JSON paths to add it to |
      | --- | --- |
      | `Robot` | every `world.robots[i]` |
      | `WorldState` | `world` |
      | `Economy` | `world.economy` |
      | `DayCounters` | `world.today` and every `world.history[i].counted` |
      | `DailyStats` | every `world.history[i]` |
      | `Books` | every `world.economy.organizations[i].today` and `.yesterday` |

      `V15ToV16` in `Migrations.cpp` is a short example covering
      `DayCounters` and `DailyStats`.
      Never edit a released migration.
   3. New enum values get a name in the `glz::meta` specializations at the
      top of `Save.cpp`.
   4. New fixture: `mechapunk-sim --seed 33 --robots 100 --minutes 28370
      --save tests/fixtures/saves/vN-city.json`, plus a row and a sentence
      in `tests/fixtures/saves/README.md`.
   5. In `tests/SaveTests.cpp`, point `CurrentVersionFixtureLoadsAndRoundTrips`
      at the new fixture (check the new data) and add
      `VersionN-1FixtureMigratesAndRuns` for the previous one. Never
      regenerate an old fixture.
4. **No teleporting robots.** Robots walk through doors, corridors, stairs,
   streets and metro platforms; `CityView::RobotPosition` and
   `DriverPlacement` build every path. Hiding robots nobody can see is
   fine; jumping is not. The simulation decides which room a robot is in;
   the view decides how it gets there.
5. **Validation.** `ValidateWorldState` (`Validation.cpp`, plus
   `ValidateEconomy` and `ValidateSocial`) runs on every load. Keep it in
   step with new state.
6. **Small modules.** No source file over 10k tokens, no module over 25k
   (`scripts/context_budget.py`, run by ctest as `ContextBudget`). Every
   new source file goes into a module in that script. A new feature with
   its own data gets its own files (`Gangs.hpp`, `Gangs.cpp`,
   `GangTests.cpp`) instead of growing `World.hpp` or a big file.
7. **Workflow.** Game work is committed straight to `master` and pushed,
   one commit per step. NEE (`external/NEE`) changes only through GitHub
   pull requests that the user merges. Commit messages: a short imperative
   title, a body that explains, and the attribution line the session gives.
8. **Style.** clang-format/clang-tidy as configured. Comments are full,
   plain sentences explaining why. UI: plain Dear ImGui calls. The user
   wants **more detail, never less**.

## 4. How the simulation runs

`World::Step` advances one minute: robots whose `nextActionAt` has come
act in id order (`Act` in `Behaviour.cpp`), needs change every 10
minutes, and at midnight (`MinuteOfDay == 0`):
`CollectRent` → `AgeRobots` → `ReviewHomes` → `ReviewJobs` → `FadeBonds`
→ `ForgetDeparted` → `TradeProperty` → `SettleEconomy`.
`SettleEconomy`: supply market → interest and repayments → supply price →
employers' pay → `RunCityHall` → `SetPolicyRate` → dividends → deposits →
`ReviewUnits` → `SetPrices` → `RecordStatistics` → books roll over.

Unsaved caches in `World`, rebuilt on load: `routes_`, `rooms_` (rebuild
with `rooms_ = RoomIndex(state_.city)` after a room changes kind),
`metro_`, `reserved_`/`guests_` (venue occupancy; only guest trips count,
see `IsGuestTrip`), `residents_`, `loanOf_`, `cityVersion_` (bump when
rooms change so the view reloads them).

The game (`app/`) runs the World on a worker thread (`SimulationHost`),
which publishes an immutable `WorldFrame` after each step: `RobotFrame`s,
`EconomyFrame` (organizations, loans, emissions, history, the owner of
every room) and the city when rooms changed. The UI reads only frames. Data
the UI needs goes into `RobotFrame` or `EconomyFrame` in
`SimulationHost.hpp` and is filled in `SimulationHost::Publish`.

Which file holds what: `docs/MODULES.md`.

## 5. Pitfalls met before

- Edits by exact string anchors fail when the anchor appears twice; use a
  longer anchor. Some files mix LF and CRLF.
- Robots going to work at a venue are not its guests (`IsGuestTrip`), and
  nobody goes out to the venue it works at.
- A room can only change kind when nobody is in it or on the way to it.
- Tests that script one or two robots are fragile: many systems act on
  them. Prefer population-level tests (run a city for N days, check all
  robots), or set the exact state a minute before midnight and run one
  minute. A test that picks "the first robot such that" must pick one whose
  next event is soon, or days off change everything.
- Thresholds in population tests move when the random stream changes (a new
  random draw anywhere shifts every later draw). Leave margin.
- Integer money: small percentages round to zero on small amounts.
- Small towns (60 to 400 robots) lack some classes of home and venue.
- Economy changes need a 30 to 40 day run at 10000 robots before trusting
  them. Watch: `created` per day (small), `wages at N%`, debts, Gini,
  broke robots, homes.
- Dear ImGui needs unique ids: two robots can share a name, so every
  clickable label ends in `##<kind><id>`.
- A test must never skip itself (`GTEST_SKIP`) because it found no data:
  ctest counts a skip as a pass, and that hid a function that always
  returned an empty list. Make the data the test needs, or assert it exists.
- Every `*Link` function in `app/Links.hpp` draws the name itself and
  returns something only when clicked. Never add an `else` that draws the
  name again; put text before a link and `ImGui::SameLine()` between them.
- In an ImGui table, call `TableNextColumn()` after `TableNextRow()` before
  drawing the first cell. Long lists use `ImGuiListClipper` for every
  length, not only past some size.
- Interface code cannot be seen by a model (no driving the game window), so
  put the logic in plain functions with tests (`app/OrganizationLists.*`
  is the example) and keep the drawing code simple.

## 6. Economy constants worth knowing

In `sim/src/EconomyInternal.hpp`, the economy file that uses them, or
`Economy.hpp`: upkeep 50% of rent; a home costs 100 nights of rent; supply
shares (venues: market `supplyPercent`, utilities and transit 95%, housing
50%); budget 80% of pay after tax plus a tenth of savings beyond 30 nights of
bills; mortgage 20% down, up to 60 shifts' pay, interest at most a third of
pay; venue prices ±1% a night (50% to 300%); market rents ±1% a night by
vacancy (5% / 15%); units close after 10 quiet nights; taxes 10% of wages, 5%
of rent, 20% of profits; support 40 a night; policy rate 2 to 50 in 10000 a
night. Police and drivers: three 8-hour shifts at wage 120, paid by City
Hall and the Metro Authority.

## 7. Done so far

M1 to M5 and most of M6 (see `ROADMAP.md`): the city, robots' days, careers,
social life, the metro with right-hand double tracks and depots, the closed
economy with banks, a central bank, City Hall, property and apartment
classes, venues opening and closing, statistics, police officers who patrol
venues hourly, and metro drivers who walk to their trains and drive them.

## 8. Tasks

Do them in this order; each file lists its steps and which are done.

| Task | File | Modules | Save change |
| --- | --- | --- | --- |
| A. Organizations in the inspector; every name a link (done; user to check in game) | `docs/tasks/A-organizations-and-links.md` | inspector, statistics | no |
| B. Gangs (B1-B10b done, all steps) | `docs/tasks/B-gangs.md` | gangs, inspector, statistics | done (v18) |
| C. Chronicle | `docs/tasks/C-chronicle.md` | saves, world, statistics | yes |
| D. Construction | `docs/tasks/D-construction.md` | saves, generation, property, view-core | yes |
| E. Simulation level of detail | `docs/tasks/E-level-of-detail.md` | world | maybe |
| F. See-through windows | `docs/tasks/F-windows.md` | view-core, view-interiors | no |
| G. Unions | `docs/tasks/G-unions.md` | money, careers | yes |
| H. Engine (NEE, pull requests only) | `docs/tasks/H-engine.md` | external | no |

## 9. Detail pages (the pattern from task A)

The user wants every new thing in the game to be inspectable in detail and
every name to lead somewhere. Each task that adds a kind of thing (a gang, a
chronicle entry, a construction project, a union) ends with a **details**
step that does what task A did for organizations:

1. **A page in the inspector** for the new thing: `Selection` gains its
   id; `DrawInspector` dispatches to `Draw<Thing>` in its own
   `app/Inspector<Thing>.cpp` (declared in `InspectorInternal.hpp`). The
   page has a title, its facts, and its **lists** (members, rooms,
   entries...), each a scrolling table at most 12 rows high drawn with
   `ImGuiListClipper` for every length (`InspectorOrganization.cpp` is the
   model to copy).
2. **A link** `<Thing>Link` in `app/Links.*`, built on the private `Link()`
   there (link colour, tooltip, `##<kind><id>` label, repeat-safe ids).
   Never call `ImGui::Selectable` or `ColorButton` with an empty or shared
   label yourself.
3. **Links back**: every page that relates to the new thing names it as a
   link (a robot to its gang, a building to the project on it, an
   organization to its union), and every robot, building, room and
   organization on the new page is a link.
4. **Testable lists**: the code that picks and sorts a page's rows is a plain
   function in `app/<Thing>Lists.hpp/.cpp`, in the `mechapunk_layout`
   library, with tests in `layout_tests` that build a `WorldFrame` from a
   generated city (`tests/OrganizationListsTests.cpp` shows how) and never
   skip. The drawing code only draws what those functions return.
5. **Frame data by id, not by look-alike**: put the thing's id in
   `RobotFrame` (or a shared list in `WorldFrame`), never match on a name
   or a colour.
6. Add the new files to modules in `scripts/context_budget.py`.
