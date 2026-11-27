# Mechapunk implementation guide

How to continue the game without earlier conversations. Written for models
with a small context (about 57k tokens): everything is split so one step of
one task needs only this guide, one task file and one module.

State as of 2026-11-27: save version 34, game version 0.34.0, all tests
passing (`sim_tests`, `layout_tests` and `ContextBudget`; ctest counts them
individually).

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
7. **Dispatching steps to several models at once:** run `git pull` before
   starting a step and again before pushing. Two steps with a save change
   must never run at the same time: both would take the same next save
   version. Steps of tasks J to M name their **Depends on**; never start a step
   before those are ticked. The planned order is J, K, L, M. L4 needs J0, and
   M1 reads `kWorkplaceTypes`, which K1 may have moved.
6. **Recipe: making room** when a step would push a module over 25k (the
   step says so, or `ContextBudget` fails): move whole functions, by topic,
   into a new file. Add the file to `sim/CMakeLists.txt` (or the root
   `CMakeLists.txt` for `app/`, `tests/CMakeLists.txt` for tests), to a module in
   `scripts/context_budget.py` (a new module, if the old one is still full),
   and regenerate the table in `docs/MODULES.md` with
   `python scripts/context_budget.py --markdown`. It is a pure move: the seed 7
   headless log and save must stay byte for byte identical (section 2). Step
   I1a of `docs/tasks/I-architecture.md` is the example. Commit the move on
   its own, before the step's change.

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
- The game: `mechapunk.exe --save FILE` continues a save; `--new --save FILE`
  starts a city; `--seed`, `--robots`, `--follow ROBOT_ID`. Keys: 1-5 speed,
  Space pause, T statistics, F follow, Q/E floors, F12 screenshot.
  **Start it with the working directory `build/build/Release`**: it opens
  `fonts/Roboto-Medium.ttf` and the engine's `shaders/`, `models/` and
  `textures/` by a path relative to the working directory (CMake copies them
  next to the exe), and writes `shader-cache/`, `mechapunk-ui.ini` and the save
  there; from the repo root it finds no font and no shaders, and `--save FILE`
  names a file in the working directory. `tools/drive-game.ps1` starts it that
  way, with `-WorkingDirectory build/build/Release`. The headless runner needs
  no assets, so the `mechapunk-sim.exe` commands above do run from the repo
  root.
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

1. **Determinism.** The same `WorldState` and the same `SimSettings` give the
   same future on every machine. Settings (`sim/include/mechapunk/sim/Settings.hpp`)
   are the knobs of the simulation (173 of them: crime, construction,
   behaviour, careers, the city's layout, money, population, social life,
   venues; defaults = the game's values; `mechapunk-sim --list-settings`) - passed with the seed to
   `World::Generate` (`WorldParams::settings`) or to `World::FromState`, set
   in a test with `ApplySetting` or `World::SetSettings`, and on the command
   line with `mechapunk-sim --set crime.robberyPercent=100`. They are **saved**
   with the city (`WorldState::settings`, since save version 28), checked on load
   by `ValidateSettings`, and `--load` keeps them unless `--set` changes one. A new
   tunable number goes into `SimSettings` and its table in `Settings.cpp`,
   not into a `constexpr` in a source file. All simulation randomness comes from `state_.random` (a saved
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
3. **Saves of the current version load.** Any change to `WorldState` or a
   type inside it (a field, an enum value) is a save format change. Saves of
   versions before `kOldestSupportedSaveVersion` (`Save.hpp`) are **not
   supported**: their migrations, fixtures and tests were removed, so the
   game refuses them with a plain message. For a format change:
   1. Bump `kCurrentSaveVersion` and `kGameVersion` in `Save.hpp`.
   2. Either raise `kOldestSupportedSaveVersion` to the new version (old
      saves stop loading; no migration), or keep the previous version
      loading: add a `Migration` to the `kMigrations` table in `Save.cpp`
      (empty now) that edits the untyped JSON and never calls game code.
      Reading uses `error_on_missing_keys`, so **every new key must be
      added** to the old save's JSON. Where a new field goes depends on its
      type:

      | New field in | JSON paths to add it to |
      | --- | --- |
      | `Robot` | every `world.robots[i]` |
      | `WorldState` | `world` |
      | `Economy` | `world.economy` |
      | `Project` | every `world.projects[i]` |
      | `DayCounters` | `world.today` and every `world.history[i].counted` |
      | `DailyStats` | every `world.history[i]` |
      | `Books` | every `world.economy.organizations[i].today` and `.yesterday` |

      A migration kept for a supported version is frozen once released.
   3. New enum values get a name in the `glz::meta` specializations at the
      top of `Save.cpp`.
   4. New fixture: `mechapunk-sim --seed 33 --robots 100 --minutes 28370
      --save tests/fixtures/saves/vN-city.json`, plus a row and a sentence
      in `tests/fixtures/saves/README.md`; delete the fixture of a version
      that is no longer supported.
   5. In `tests/SaveTests.cpp`, point `CurrentVersionFixtureLoadsAndRoundTrips`
      at the new fixture (check the new data); a migration kept for the
      previous version gets its own `VersionN-1FixtureMigratesAndRuns`.
   **Recipe: adding a setting** (a tunable number; most new rules need one):
   1. Add the field with its default and a comment to its group's struct in
      `sim/include/mechapunk/sim/Settings.hpp` (`std::int64_t name{default};`).
      A new group needs a struct, a member of `SimSettings` and
      `operator==` like the others.
   2. Add `MECHAPUNK_SETTING(group, name, minimum, maximum),` to `kEntries` in
      `sim/src/Settings.cpp`. A range must never let the simulation divide by
      zero, loop forever or draw from an empty range. A `minX`/`maxX` pair
      also gets a line in `kPairs`.
   3. Read it as `state_.settings.group.name` (a `World` member) or through
      `CityParams::city` (generation), cast to the type the code needs. Free
      functions get the value as a parameter; never use a global.
   4. It is saved state: bump the version, and the migration writes the
      default into `world.settings.group.name` (and creates `world.settings.group`
      for a new group). Then a new fixture and the two fixture tests, as above.
   5. `Settings.EverySettingAtItsLimitsKeepsTheCityRunning` then runs it at both
      ends of its range. If it fails, narrow the range; never skip the test.
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
→ `ForgetDeparted` → `TradeProperty` → `SettleEconomy` → `ReviewConstruction`
(last, after the books roll over, so a site's supplies count into the new day).
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
| B. Gangs (B1-B11d done, all steps) | `docs/tasks/B-gangs.md` | gangs, inspector, statistics | done (v18) |
| C. Chronicle (done; user to check in game) | `docs/tasks/C-chronicle.md` | chronicle-lists, statistics, inspector | yes (v21) |
| D. Construction (done; user to check in game) | `docs/tasks/D-construction.md` | generation, saves, construction, site-supplies, careers, life-cycle, property, view-core, ui, project-lists, construction-counters, statistics, project-pages, inspector, robot-pages | yes (v25, and v26 for step D5e2a's counters) |
| E. Simulation level of detail (E1 and E3 moved to J) | `docs/tasks/E-level-of-detail.md` | world | maybe |
| F. See-through windows | `docs/tasks/F-windows.md` | view-core, view-interiors | no |
| G. Unions | `docs/tasks/G-unions.md` | money, careers | yes |
| H. Engine (NEE, pull requests only) | `docs/tasks/H-engine.md` | external | no |
| I. Architecture: more building types (done; user to check in game) | `docs/tasks/I-architecture.md` | generation, saves, view-core, construction, inspector, statistics | yes (v27) |
| J. Performance: snapshots at render rate, cheaper trips, trip planning on all cores | `docs/tasks/J-performance.md` | ui, world, metro, city-streets, threads (new) | no |
| K. A city that grows from one dormitory: newcomers, buildings, metro, banks, corporations, gangs | `docs/tasks/K-growing-city.md` | generation, validation, money, life-cycle, construction, metro, careers, view-core, property, gangs, chronicle, statistics, runner | yes |
| L. Body parts (head, torso, hands, legs): walking, holding things, working, injuries, replacement parts | `docs/tasks/L-body-parts.md` | bodies (new), saves, world, life-cycle, metro, units, careers, social, money, gangs, chronicle, view-robots, robot-pages, statistics | yes |
| M. Skins: basic chameleon coverage on every body part; own colours, dress codes on duty, gang colours | `docs/tasks/M-skins.md` | skins (new), saves, careers, gangs, units, view-robots, rendering, robot-pages | yes |

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
