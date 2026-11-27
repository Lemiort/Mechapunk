# Task K: a city that grows from one dormitory

Status: planned on 2026-11-27; K1 to K20 done: all of task K. Save version 41, game 0.41.0.
Steps that change saved state take the next free save version at the time
(rule 3); most of them only add settings, through the guide's "adding a
setting" recipe.

Every step has the same parts: **Module**, **Depends on**, **Save change**,
**Open** (the only files to read in full), **Do**, **Tests** and **Check**. Do
one step, commit, push, tick it, and stop. When a step says "make room
first", follow the guide's "making room" recipe as its own commit.

## What and why

Today a city is generated at its full size: every building, all ~480 venues,
the whole metro, every organization and two to six gangs. The population
only replaces scrapped robots. Construction (task D) raises a habitat or a
workplace when homes or jobs run short, but nothing else grows.

This task adds a **seed town**: one dormitory, one workshop, two venues, 30
robots and the smallest economy that adds up. It grows on its own:
- newcomers arrive while there is room;
- streets and buildings spread;
- the metro opens;
- banks, corporations and gangs are founded;
- the central bank keeps inflation on target.

It is all deterministic, and every rule is a setting. A full city stays the
default (`city.seedTown = 0`), so old saves and existing tests do not change.

## Decisions (the user, 2026-11-27)

1. **Start:** one dormitory of 4 floors, one workshop of 2 workspaces, a
   charging bay and a diner on the dormitory's ground floor, and 30 robots.
2. **Streets grow with the town:** a 3 by 3 block of intersections at first,
   and a ring of streets is added at the edge when plots run short.
3. **The Metro Authority is founded with the first line.**
4. **Money comes with the robots.** Robots arrive with savings, some poor and
   some rich; that money is created on arrival, as today
   (`EmissionReason::NewcomerSavings`). New banks and corporations sell
   shares (`Organization::owners`, a list of `Share`) to robots and
   organizations through `Pay`. The central bank creates money only by
   lending, and moves its rate to keep inflation near **1% a year** (365 game
   days), a setting the user can change.

## Facts the steps rely on (checked 2026-11-27)

- `City::nodes`, `edges`, `stations` and `lots` are saved. A node's id is its
  index, and its `x`/`z` are meters on the block grid.
- `Metro::Metro(const City&)` builds lines from `city.stations`: one line per
  row and per column that has stations. So opening a station means adding
  one to `city.stations` and rebuilding `metro_`.
- `Centrality(NodeId, gridSize)` in `sim/src/City.cpp` computes a node's place
  from `node.value % gridSize`. That only works while nodes are a full square
  grid in index order, so step K6a changes it.
- `World::FoundEconomy` (`sim/src/Founding.cpp`) founds every organization.
  `World::FoundGangs` (`sim/src/Gangs.cpp`) founds `clamp(robots / 1500, 2, 6)`
  gangs. `World::AddNewcomer` (`sim/src/LifeCycle.cpp`) only replaces scrapped
  robots.
- `ReviewConstruction`'s homes rule counts market homes (studio and up) only,
  so a town of dorm beds never reads as short of homes. Step K6 adds a beds
  rule.

## Steps

### - [x] K1. Make room in the generation module (generation)

- Module: generation (23274 of 25000). Depends on: none. Save change: no.
- Open: `sim/src/City.cpp`, `scripts/context_budget.py`, `sim/CMakeLists.txt`.
- Do: move `MakeHabitat`, `MakeWorkplace` and the helpers only they use into a
  new `sim/src/CityBuildings.cpp` in a new module `city-buildings`, which holds
  `City.hpp`, a new private `sim/src/CityInternal.hpp` for what both files
  share, and `CityBuildings.cpp`. This is a pure move.
- Check: build, ctest, seed 7 log and save byte-identical.

### - [x] K2. The seed-town settings and layout (city-buildings)

- Module: city-buildings. Depends on: K1. Save change: yes, settings only.
- Open: `sim/src/City.cpp` (`GenerateCity`), `sim/src/CityBuildings.cpp`,
  `sim/include/mechapunk/sim/Settings.hpp` (the `CitySettings` struct only),
  `sim/src/Settings.cpp`.
- Do:
  1. Add the settings, by the recipe: `city.seedTown` (0 or 1, default 0),
     `city.seedGridSize` (3, range 3 to 9), `city.seedDormFloors` (4, range 2
     to 12) and `city.seedWorkspaces` (2, range 1 to 8).
  2. In `GenerateCity`, when `seedTown` is 1, lay out a `seedGridSize` square
     grid instead of `GridSizeFor(residents)`, with `blockMeters` spacing,
     the same as a full grid. Place one dormitory of `seedDormFloors` floors,
     with a charging bay and a diner as its ground-floor units, at the middle
     node. Place one workshop with `seedWorkspaces` workspaces at the next
     node. There are no stations, and every other corner is a free plot.
- Tests: new `tests/SeedTownTests.cpp` in a new module `seed-town`. A seed
  town has exactly one habitat and one workplace, no stations, the venues
  asked for, free plots on every other corner, and the same seed gives the
  same town.
- Check: build, ctest; the seed 7 run is byte-identical apart from the new
  settings keys in the save.

### - [x] K3. A seed town runs and stays valid (validation)

- Module: validation, with fixes in other modules as separate commits.
  Depends on: K2. Save change: no.
- Open: `tests/SeedTownTests.cpp`, `sim/src/Validation.cpp`.
- Do:
  1. Add the test `SeedTown.RunsTwentyDaysValid`: `World::Generate({.seed = 1,
     .robots = 30, .settings = seed})` with `city.seedTown = 1`, run 20 days,
     `ValidateWorldState` after each day.
  2. Fix each failure where it starts: a city with no stations, no police
     station, no crew room or one corporation. Expect `FoundGangs` (wants
     edge habitats), `IndexStations` (no stations), metro drivers and officers
     (no rooms) and `FoundEconomy` (sectors with no rooms). Each fix that is
     outside the validation module is its own commit, naming its module.
- Tests: the test above.
- Check: build, ctest, and the seed 7 run byte-identical (a full city must
  not change).

### - [x] K4. Robots arrive with poor-to-rich savings (generation)

- Module: generation. Depends on: K1. Save change: yes, settings only.
- Open: `sim/src/Generation.cpp` (starting credits), `sim/src/LifeCycle.cpp`
  (`AddNewcomer`, savings only).
- Do:
  1. Add the settings `population.wealthSpread` (0 or 1, default 0, so a full
     city is unchanged), `population.richPercent` (10) and
     `population.richMultiplier` (20).
  2. With `wealthSpread` 1, a starting robot's credits are the current draw,
     and `richPercent` of robots get `richMultiplier` times it. Use a fixed
     stream, `kWealthStream = 8`, never `state_.random` (rule 1).
  3. `AddNewcomer` uses the same rule for its savings, drawn from
     `state_.random` as today.
- Tests (`SeedTownTests.cpp`): with the spread on, about `richPercent` of 1000
  robots hold `richMultiplier` times more; with it off, credits equal
  today's.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K5. The smallest economy that adds up (property)

- Module: property (`Founding.cpp` is in it). Depends on: K3, K4. Save
  change: no.
- Open: `sim/src/Founding.cpp`, `sim/src/EconomyInternal.hpp`,
  `sim/include/mechapunk/sim/Economy.hpp` (`Organization`, `Share`).
- Do:
  1. For a seed town, `FoundEconomy` founds City Hall, the central bank, one
     bank, the housing trust, and one corporation per sector that owns a room
     (the workshop, the bay and the diner).
  2. A corporation's capital comes from the richest robots buying shares:
     move credits from each buyer to the corporation with `Pay`, and add a
     `Share` to `owners`.
  3. The bank's capital comes the same way. What it still lacks for
     `economy.bankFloor`, the central bank lends, which is the only money created.
- Tests: on day 0 and after 20 days `ValidateEconomy` passes, every room has
  an owner, every corporation's shares are held by robots, and the town pays
  its first week of wages.
- Check: build, ctest, seed 7 byte-identical (a full city's founding must
  not change).

### - [x] K6. Make room in the construction module, then raise dormitories (construction)

- Module: construction (23643 of 25000: make room first). Depends on: K3.
  Save change: yes, settings only.
- Open: `sim/src/Construction.cpp`.
- Do:
  1. Make room: move the file-local helpers above `ReviewConstruction` into
     a new `sim/src/ConstructionRules.cpp` with its own module. Commit that
     on its own.
  2. Add a **beds rule**: when free dorm beds stay under
     `construction.freeBedsPercent` (new setting; default 0 = off, so a full city, whose dorms start about full, is unchanged; a town sets 10) of all dorm beds for
     `streakNights` nights, raise a habitat whose floors are all dorm rooms (a
     dormitory, named "Dormitory N"). It is owned by the housing trust.
- Tests (`tests/SiteStyleTests.cpp` or a new file): in a seed town with
  `construction.streakNights = 2` and full dorms, a dormitory site starts
  within 3 nights.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K7. Newcomers arrive while there is room (life-cycle)

- Module: life-cycle. Depends on: K4. Save change: yes, settings only.
- Open: `sim/src/LifeCycle.cpp` (`AgeRobots`, `AddNewcomer`).
- Do:
  1. Add the settings `population.immigration` (0 or 1, default 0) and
     `population.immigrantsPerNight` (3, range 0 to 100).
  2. In `AgeRobots`, after scrapped robots are replaced and when
     `immigration` is 1, call `AddNewcomer` up to `immigrantsPerNight` times,
     while the city has a free dorm bed and a job with a free place.
- Tests (`SeedTownTests.cpp`): with immigration on, a seed town with free
  beds gains robots every night until beds or jobs run out, and never has
  more robots than beds.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K8. A seed town grows for 120 days (seed-town)

- Module: seed-town (tests only). Depends on: K5, K6, K7. Save change: no.
- Open: `tests/SeedTownTests.cpp`.
- Do: add `SeedTown.GrowsInAHundredTwentyDays`, with fast settings
  (`construction.streakNights = 2`, `population.immigrantsPerNight = 5`).
  After 120 days the town has more than 5 buildings, more than 60 robots,
  every robot has a bed, and the world and the economy are valid. If it
  does not grow, change settings, not code, and write which in the note.
- Check: build, ctest. The test must take under 20 seconds.

### - [x] K9. Streets grow at the edge (new module `streets`)

- Module: streets (new: `sim/src/Streets.cpp`, `tests/StreetsTests.cpp`).
  Depends on: K2. Save change: yes, settings only.
- Open: `sim/include/mechapunk/sim/City.hpp` (`City`, `CityNode`, `CityEdge`,
  `CityLot`), `sim/src/City.cpp` (`Centrality` only), `sim/src/World.cpp`
  (`Step` only).
- Do:
  1. (Skipped: `Centrality` only sizes the first grid in `GenerateCity`, never a grown one, so it stays as it is.) `Centrality` would use node coordinates, not `node.value % gridSize`. The
     middle is the mean of the first `seedGridSize`² nodes (or of all nodes
     in a full city), and the edge distance is `city.maxGridSize` blocks.
     It must give the same numbers as today for a full city: check with the
     seed 7 run.
  2. Add the settings `city.freePlotsMin` (default 0 = off so a full city is unchanged; a town sets 4) and `city.maxGridSize` (30).
  3. `World::ReviewStreets()` runs at midnight just before
     `ReviewConstruction`. When `city.lots.size() < freePlotsMin` and the grid
     is smaller than `maxGridSize`, it adds one ring of nodes around the grid:
     - new nodes are appended, so id = index;
     - edges join each new node to its neighbours, at `blockMeters` length;
     - the new corners become lots, and `city.lots` stays sorted as
       `Validation.cpp` requires.

     Rebuild `routes_` and bump `cityVersion_`.
  4. `ValidateWorldState` must accept the grown city; check edges and lots.
- Tests (`StreetsTests.cpp`): a seed town with no free plots grows a ring at
  midnight, every lot is reachable from every building, `Centrality` of the
  middle node is 1, and a save round trip keeps the new streets.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K10. The view draws new streets (view-core)

- Module: view-core. Depends on: K9. Save change: no.
- Open: `app/CityView.cpp` (the constructor's street loop, `UpdateCity`,
  `AddBuilding`), `app/CityStatic.cpp` (`AddStreetLamps`, `AddPark`),
  `app/CityView.hpp`.
- Do: when `UpdateCity` sees edges or lots the view did not draw, it draws
  their street slabs and lamps and the parks of new lots, and removes the
  parks of lots that became buildings (not done: parks are batched with building 0 and a building drawn on a plot already covers its park, as since task D). Batch them under building 0 the way
  `BatchStaticsFor` does. Street lamps are spot lights: add them in
  `SyncLights` the way task I, step I4 added neon.
- Tests: none without a renderer. Check: build, ctest, `ContextBudget`.

### - [x] K11. The metro opens stations as the town spreads (metro)

- Module: metro (`Transit.cpp`, `Metro.cpp`). Depends on: K9. Save change: yes,
  settings only.
- Open: `sim/src/Transit.cpp`, `sim/src/Metro.cpp` (the constructor),
  `sim/include/mechapunk/sim/Metro.hpp`.
- Do:
  1. Add the settings `metro.stationReachMeters` (400), `metro.minBuildings`
     (default 0 = off, so a full city is unchanged; a town sets 6) and `metro.stationCost` (2000).
  2. `World::ReviewTransit()` runs at midnight after `ReviewStreets`. When at
     least `minBuildings` buildings stand farther than `stationReachMeters`
     from any station, open stations, two at once if no line exists yet. A
     station goes on a free intersection on the same row or column as
     another station, picked as the one that serves the most of those
     buildings, with ties broken by node id.
  3. City Hall pays `stationCost` per station to the supply market with `Pay`
     (rule 2); it opens nothing it cannot pay for.
  4. Append to `city.stations`, rebuild `metro_`, re-run `IndexStations`,
     and bump `cityVersion_`.
- Tests (new `tests/TransitGrowthTests.cpp`): a grown seed town opens its
  first two stations; a trip between them may use the metro; the money adds
  up; a save round trip keeps the stations.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K12. The Metro Authority comes with the first line (new module `metro-authority`)

- Module: metro-authority (new: `sim/src/MetroAuthority.cpp`). Depends on: K11,
  K5. Save change: no.
- Open: `sim/src/Founding.cpp` (how the Metro Authority and depots are founded
  today: `rg -n "Metro Authority\|CrewRoom" sim/src`), `sim/src/Units.cpp` (how
  a room changes kind: `CloseUnit`).
- Do: when the first line opens and no organization is the Metro Authority,
  found it with capital lent by the central bank. Turn the empty ground-floor
  unit (`RoomKind::VacantStore`) nearest the line's first station into its
  `CrewRoom`. Only an empty unit nobody is in or walking to may change kind;
  if there is none, try again the next night. Drivers are then hired as
  they are today.
- Tests: in a grown seed town the first line gets the Metro Authority and a
  crew room; drivers are hired within 2 days; trains run.
- Check: build, ctest, seed 7 byte-identical.

### - [x] K13. The view follows new stations and lines (view-robots)

- Module: view-robots (22733 of 25000; make room first if `ContextBudget`
  fails). Depends on: K11. Save change: no.
- Open: `app/CityMetro.cpp` (`AddStations`, `AddElevatedLines`, `SubmitTrains`),
  `app/CityView.hpp`.
- Do: when `UpdateCity` sees more stations than the view drew, rebuild the
  view's `metro_` and draw the new platforms and track, as K10 does for
  streets.
- Check: build, ctest, `ContextBudget`.

### - [x] K14. New banks (property)

- Module: property. Depends on: K5. Save change: yes, settings only.
- Open: `sim/src/Founding.cpp`, `sim/src/Property.cpp` (how a robot's `bank` is
  chosen: `rg -n "\.bank =" sim/src`).
- Do:
  1. Add the setting `economy.robotsPerBank` (default 0 = off so a full city is unchanged; a town sets 400).
  2. A midnight `ReviewBanks()`, after `SettleEconomy`: when robots per bank
     exceed `robotsPerBank`, found a bank. Its capital comes from shares
     bought by robots with savings over `economy.comfortableSavings`, and the
     central bank lends what it still lacks for `economy.bankFloor`.
  3. New robots and robots moving home pick the bank with the fewest
     accounts. Keep `Economy::organizations` sorted as `ValidateEconomy`
     expects.
- Tests: a seed town grown past `robotsPerBank` has two banks, the money adds
  up, and robots hold accounts at both.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K15. New corporations (property)

- Module: property. Depends on: K14. Save change: yes, settings only.
- Open: `sim/src/Founding.cpp`, `sim/src/ConstructionRules.cpp` or
  `Construction.cpp` (where a workplace site's owner is chosen:
  `industryEmployer`).
- Do: add the setting `economy.workplacesPerCorporation` (default 0 = off so a full city is unchanged; a town sets 3). When a site
  raises a workplace and every corporation of its sector already owns that
  many workplaces, found a corporation for it, with its capital bought as
  shares as in K5.
- Tests: a seed town that raises its fourth workplace founds a second
  industry corporation; the money adds up.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K16. A price index in the daily statistics (statistics)

- Module: statistics (`EconomyStatistics.cpp`). Depends on: none. Save change:
  yes (a `DailyStats` field: add it to every `world.history[i]`, rule 3 table).
- Open: `sim/src/EconomyStatistics.cpp`, `sim/include/mechapunk/sim/Economy.hpp`
  (`DailyStats`), `app/Statistics.cpp` (the Economy tab).
- Do: `DailyStats::priceIndex`, the average of venue prices (`pricePercent`)
  and market rents (`rentPercent`), in per mille of the starting prices.
  Plot it in the Economy tab with `PlotCount`.
- Tests: on day 1 the index is 1000 in a new city; it moves when prices move.
- Check: build, ctest, new fixture by rule 3.

### - [x] K17. The central bank targets inflation (money)

- Module: money (23122 of 25000: keep it small, or make room first). Depends
  on: K16. Save change: yes, settings only.
- Open: `sim/src/Settlement.cpp` (`SetPolicyRate`).
- Do:
  1. Add the settings `economy.inflationTarget` (0 or 1, default 0, so a full
     city is unchanged), `economy.inflationTargetPermille` (10, a year) and
     `economy.inflationBandPermille` (5).
  2. With it on, `SetPolicyRate` compares the price index now with 30 days
     ago, scaled to a year. Above target plus band it raises the rate one
     step; below target minus band it lowers it. It still stays between the
     `economy.minPolicyRate` and `economy.maxPolicyRate` settings.
- Tests: with the target on and prices rising fast, the rate rises; with
  prices flat, it falls toward the minimum.
- Check: build, ctest, seed 7 byte-identical except the settings keys. Run
  a 40-day seed town with the target on, and write its inflation in the note.
  Note (run on 2026-11-28, seed 1, 30 robots, all town settings, `--set economy.inflationTarget=1`):
  the price index read 1000, 1020, 1047, 1047 and 1011 on days 1, 10, 20, 30 and 40
  (up 1.1% over the 40 days, which is about 10% a year, over the 1% target); the
  policy rate fell to 8 to 9 in the first days and the town stayed valid. A small town
  swings with its few venues; the target only moves the rate, a step a night.

### - [x] K18. Gangs form as the town grows (gangs)

- Module: gangs. Depends on: K3. Save change: yes, settings only.
- Open: `sim/src/Gangs.cpp` (`FoundGangs`, `ReviewGangs`).
- Do:
  1. Add the settings `crime.robotsPerGang` (1500), `crime.minGangs` (2) and
     `crime.maxGangs` (6); `FoundGangs` uses them in place of its literals.
  2. A seed town sets `minGangs` 0 in the test. `ReviewGangs` founds one more
     gang when `robots / robotsPerGang` passes the gang count, up to
     `maxGangs`. Its turf is an edge habitat no gang holds, its stance to
     every gang is drawn from `kGangStream` mixed with the new gang's index,
     and both directions are written, so relations stay symmetric.
- Tests: a seed town with `minGangs = 0` and `robotsPerGang = 40` has no gang
  at 30 robots and one after it passes 40; relations stay symmetric.
- Check: build, ctest, seed 7 byte-identical except the settings keys.

### - [x] K19. The growth in the chronicle (chronicle)

- Module: chronicle. Depends on: K11, K14, K15, K18. Save change: yes (new
  `EventKind` values: names in `Save.cpp`'s `glz::meta`).
- Open: `sim/src/Chronicle.cpp`, `sim/include/mechapunk/sim/World.hpp` (`EventKind`
  only).
- Do: the events `StationOpened`, `BankFounded`, `CorporationFounded` and
  `GangFormed`, emitted where K11, K14, K15 and K18 do it, are notable. The
  chronicle page (`app/InspectorChronicle.cpp`, a later details step) links
  them.
- Tests: a grown seed town's chronicle holds each kind at least once.
- Check: build, ctest, new fixture.

### - [x] K20. Start a seed town from the command line (runner, then ui)

- Module: runner, then ui as a second commit. Depends on: K3, K7. Save
  change: no.
- Open: `tools/sim_runner/main.cpp` (options), then `app/main.cpp` (options).
- Do: `--town` sets `city.seedTown = 1`, `population.immigration = 1`,
  `population.wealthSpread = 1`, `economy.inflationTarget = 1` and
  `crime.minGangs = 0`, `construction.freeBedsPercent = 10`, `city.freePlotsMin = 4` `metro.minBuildings = 6`, `economy.robotsPerBank = 400` and `economy.workplacesPerCorporation = 3`, with 30 robots unless `--robots` says otherwise.
  Explicit `--set` options still win.
- Check: run `mechapunk-sim --town --days 365 --quiet` and write in this file
  how big the town got.
  Note (run on 2026-11-28, seed 1, 5.4 s): from 2 buildings and 30 robots the town
  reached day 366 with 1050 robots in 53 buildings (17 habitats, 36 workplaces), a 5 by 5
  grid of intersections (one ring of streets), 2 metro stations on one line with its
  Metro Authority, 3 banks, 12 industry corporations and 2 venue chains, valid
  throughout. No gang formed (1050 robots is under `crime.robotsPerGang` 1500, and the town
  sets `minGangs` 0). Two things for a later task to look at: the price index
  reached 1582 (venue prices at 216%) so the central bank sat at its maximum rate of 50,
  inflation far over the 1% target; and every robot still lives in a dorm bed, since
  the beds rule raises dormitories while the homes rule sees no market homes at all.

## Done when

- [x] K1-K20 are ticked, a year-long seed town grows streets, buildings, a
      metro, banks, corporations and gangs, and the guide's table marks K done.
