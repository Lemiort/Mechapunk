# Task K: a city that grows from one dormitory

Status: planned on 2026-11-27, nothing done. Save version 29, game 0.29.0.
Each step that changes saved state takes the next free save version at the
time it is done (rule 3).

## What and why

Today a city is generated at its full size: every building, all ~480 venues,
the whole metro grid, every organization and two to six gangs (scaled to the
population) exist from minute one. The population only replaces scrapped
robots, so it never grows. Construction (task D) can raise a habitat or a
workplace when homes or jobs run short, but nothing else in the city grows.

This task adds a second way to start: a **seed town**. It is one dormitory,
one small workplace, a few ground-floor venues and a handful of robots,
owned by the smallest economy that adds up. From there the city grows on
its own:

- Newcomers arrive while there are beds and jobs.
- Construction raises buildings (already implemented).
- The metro opens stations and lines as the town spreads.
- New banks and corporations are founded as the economy grows.
- Gangs form once there are enough robots.

Everything stays deterministic: the same seed and the same settings grow the
same city. Every new rule is a setting (`Settings.hpp`), so a test can make a
town grow in days instead of months.

The full city stays the default. A seed town is a setting
(`city.seedTown = 1`), so old saves and every existing test are unchanged.

## What exists already (read before starting)

- Construction: `sim/src/Construction.cpp`. `ReviewConstruction` raises a
  habitat when market homes stay over-full for `construction.streakNights`
  nights, and a workplace when unemployment stays high. It takes the free
  plot nearest the middle.
- The metro is rebuilt from the saved `City::stations` (`Metro::Metro(const City&)`):
  lines run along the grid's rows and columns through the stations on them.
  Opening a station means adding one to `city.stations` and rebuilding the
  `metro_` cache. No new saved type is needed.
- Organizations are founded once, in `World::FoundEconomy` (`sim/src/Founding.cpp`).
- Gangs are founded once, in `World::FoundGangs` (`sim/src/Gangs.cpp`):
  `clamp(robots / 1500, 2, 6)` gangs, turfs at the edges. `ReviewGangs` only
  founds gangs when there are none.
- Newcomers: `World::AddNewcomer` (`sim/src/LifeCycle.cpp`) only replaces
  scrapped robots.

## Decisions (the user, 2026-11-27)

1. **Start:** one dormitory of 4 floors, one workshop of 2 workspaces, a
   charging bay and a diner on the dormitory's ground floor, and 30 robots.
2. **Streets grow with the town.** A seed town starts with a few
   intersections around the middle, and the review adds streets and plots at
   the edge as the town fills up (step K5b). `City::nodes`, `edges` and `lots`
   are saved already, so the type does not change, only the rules that may
   add to them.
3. **The Metro Authority is founded with the first line** (K6b), with its
   depot and crew room.
4. **Money comes with the robots.** Every robot that arrives brings savings,
   some poor and some rich (a spread set by settings). That money is created
   on arrival as today (`EmissionReason::NewcomerSavings`), and it must be
   enough to start the economy. New banks and corporations raise their
   capital from robots and organizations buying shares, through `Pay`; the
   city already has shareholders and dividends. The central bank creates
   money only by lending, and moves its policy rate to keep **inflation**
   inside a target band, about 1% (K7b). Today `SetPolicyRate` reacts to money
   creation instead.
   - Still to confirm: whether the 1% target is per year of game time or per
     some other period. The proposal is per year (365 game days), measured
     over the last 30 days.

## Steps

### - [ ] K1. The seed-town setting and its layout (generation)

- Open: `sim/include/mechapunk/sim/City.hpp` (`CityParams`), `sim/src/City.cpp`
  (`GenerateCity`), `sim/include/mechapunk/sim/Settings.hpp`, `sim/src/Settings.cpp`.
- New settings `city.seedTown` (0 or 1, default 0), `city.seedDormFloors` (4),
  `city.seedJobs` (2 workspaces) and `city.seedGridSize` (3: a 3 by 3 block of
  intersections). With `seedTown = 1`, `GenerateCity` lays out only that small
  grid around the middle and places the dormitory, with a charging bay and a
  diner on its ground floor, and the workshop. It adds no stations, and every
  other corner is a free plot. Node ids keep being the node's index, and
  node coordinates stay on the same block grid a full city uses, so step
  K5b can extend the grid outward.
- New settings are saved state: bump the save version, add a migration that
  writes the defaults, a fixture and tests (rule 3).
- Tests (new `tests/SeedTownTests.cpp`, new module): a seed town has exactly
  one habitat, one workplace and no stations, the plots are free, and the
  same seed gives the same town.

### - [ ] K2. A city that runs with what a seed town has (validation, world)

- First list what a full city always has that a seed town lacks: stations,
  depots and crew rooms, a police station, venues of every kind, more than
  one organization per sector. Then find every place that assumes them. Start
  with `ValidateWorldState`, `Metro` with no stations, `FoundGangs`, the
  drivers and officers, and `IndexStations`.
- Make each one cope: no metro means everyone walks, no police station means
  no officers, and no gang until K8.
- Test: a 30-robot seed town runs 20 days, stays valid, and its money adds up.

### - [ ] K3. The smallest economy that adds up (money)

- Open: `sim/src/Founding.cpp` (`FoundEconomy`), `sim/src/EconomyInternal.hpp`,
  `sim/src/Generation.cpp` (starting credits).
- The first 30 robots arrive with savings drawn from a skewed spread, most
  poor and a few rich (settings `population.startingWealthMedian`,
  `population.richShare` and `population.richMultiplier`). Newcomers in K4 use
  the same draw. The money is created on arrival as today.
- A seed town is founded with City Hall, the central bank, one bank, the
  housing trust and one corporation per sector that has a room: the workshop
  and the two venues. The bank's and the corporations' capital is bought as
  shares by the richest robots, through `Pay`. The central bank lends the bank
  what it still needs, which is the only money it creates.
- Tests: `ValidateEconomy` passes on day 0 and after 20 days; every room has
  an owner; the shares add up to each organization's capital; a seed town
  started with 30 robots can pay its first week of wages.

### - [ ] K4. Newcomers arrive while there is room (life-cycle)

- Open: `sim/src/LifeCycle.cpp` (`AgeRobots`, `AddNewcomer`).
- At midnight, after the scrapped robots are replaced, up to
  `population.immigrantsPerNight` newcomers arrive while the city has a free
  dorm bed and a free job (or a job will be free: a site under
  construction). They come through `AddNewcomer`, so their savings are an
  `Emission` as now. A setting `population.immigration` (0 or 1, default 0
  for a full city, 1 for a seed town) switches it.
- Save change only for the new settings. DailyStats already counts arrivals
  (`arrived`).
- Test: a seed town with free beds gains robots every night until the beds or
  jobs run out, and never goes over either.

### - [ ] K5. Construction grows a seed town (construction)

- Open: `sim/src/Construction.cpp`. `construction` stands at 24k of 25k: make
  room first, as I5 did.
- Check, with a test that runs a seed town for 120 days on fast settings
  (`construction.streakNights` 2), that construction raises dormitories while
  beds run short, workplaces while jobs do, and that the ground-floor units of
  the new habitats open as venues through `ReviewUnits`. Tune with settings,
  not code. If a seed town needs a building kind construction cannot raise
  yet (a dormitory of dorm rooms, not studios), add it here.
- Test: after 120 fast days the town has more than 5 buildings, every robot
  has a bed, and the city is valid.

### - [ ] K5b. The street grid grows at the edge (city-streets, view-core)

- A midnight `ReviewStreets` that runs before `ReviewConstruction`. When the
  free plots fall under `city.freePlotsMin`, it adds the next ring of
  intersections and streets around the grid (or the side where most
  buildings stand), and the corners of the new intersections become free
  plots. A ring adds about a block in every direction, to at most
  `city.maxGridSize`. Deterministic: no draws, or draws from a fixed
  stream.
- Saved state grows (`nodes`, `edges`, `lots`), but no type changes. Check
  `ValidateWorldState`: node ids equal indices, and every edge and lot names
  a node.
- Rebuild what depends on the grid: `routes_` (and task J's distance table),
  `Centrality` (which floors a building gets: it must use the grid's current
  middle, or the town's first buildings stay its tallest; say which in the
  note), and the view. The view draws streets, lamps and parks once in the
  `CityView` constructor. Make them follow `cityVersion` the way
  `AddBuilding` follows new buildings (task D, step D4).
- Test: a seed town with few plots left grows a ring of streets at midnight,
  every plot is reachable, and a save round trip keeps the new streets.

### - [ ] K6. The metro opens stations as the town spreads (metro, saves)

- K6a (metro): a midnight `ReviewTransit`. When enough buildings stand farther
  than `metro.stationReachMeters` from any station (new settings group
  `metro`), it opens a station at the free intersection that serves the most
  of them. A station only becomes a line once its row or column has two
  stations, so the first line is two stations at once. The cost is paid
  from City Hall to the supply market through `Pay` (rule 2). Rebuild `metro_`
  and bump `cityVersion_`.
- K6b (careers): with the first line, found the Metro Authority, give a depot
  building a crew room, and let drivers be hired as in task D. Check that
  `IndexStations` and the trip planner cope with stations appearing
  mid-game.
- K6c (view-core, view-robots): the view draws stations, elevated track and
  trains once at startup (`AddStations`, `AddElevatedLines`). Make them follow
  `cityVersion` the way `AddBuilding` follows new buildings (task D, step D4)
  and their lights the way task I, step I4 did.
- Save change: none for stations (already saved); settings only.
- Tests: in a seed town that grows past the reach, a station opens; trips
  after it may use the metro; the money adds up; a save round trip keeps the
  stations.

### - [ ] K7. New banks and corporations (money, property)

- A midnight review, after `SettleEconomy`: when the robots per bank pass
  `economy.robotsPerBank`, a new bank is founded. When a construction site
  raises a workplace and every corporation of that sector already owns
  `economy.workplacesPerCorporation`, a new corporation is founded to own it.
- Founding capital is bought as shares (decision 4). The richest robots and
  organizations with money to spare buy in through `Pay`, and the shares pay
  dividends as today's do. A bank short of capital borrows the rest from the
  central bank.
- Organizations are sorted by id today (check `Economy::organizations`); keep
  every order rule and `ValidateEconomy` in step.
- Tests: a growing seed town founds a second bank and a second corporation;
  the money adds up before and after; robots open accounts at the new bank.

### - [ ] K7b. The central bank targets inflation (money)

- Open: `sim/src/Settlement.cpp` (`SetPolicyRate`), `sim/include/mechapunk/sim/Economy.hpp`.
- A price index from what robots pay (venue prices and market rents, weighted
  by what was spent) is recorded each day in `DailyStats` (save change). The
  policy rate rises while inflation over the last 30 days runs above
  `economy.inflationTargetPermille` (10, 1% a year) plus a band, and falls
  while it runs below. The central bank keeps creating money only by lending.
- This changes a full city's economy too: run the 40-day check at 10000
  robots before and after, and compare inflation, debts and the rate.
- Statistics: plot the price index and inflation in the Economy tab.

### - [ ] K8. Gangs form as the town grows (gangs)

- Open: `sim/src/Gangs.cpp` (`FoundGangs`, `ReviewGangs`).
- `ReviewGangs` founds one more gang whenever `robots / crime.robotsPerGang`
  passes the gang count (a new setting, default 1500, the number
  `FoundGangs` uses), up to `crime.maxGangs`. Its turf is an edge habitat no
  gang holds, and its stance toward every existing gang is drawn from the
  gang stream. Members are recruited as today.
- A seed town starts with no gangs. Today `FoundGangs` always founds at least
  two: make that minimum a setting.
- Tests: a seed town has no gang at 30 robots and founds one when it passes
  the threshold (use a low `crime.robotsPerGang`); relations stay symmetric.

### - [ ] K9. The growth is part of the story (chronicle, statistics)

- Chronicle entries for "station opened", "line opened", "bank founded",
  "corporation founded" and "gang formed" (new `EventKind` values, which is a
  save change).
- DailyStats counts buildings, stations and organizations each day, and the
  Statistics window plots them in a new "Growth" section. New `DailyStats`
  fields are a save change (rule 3 table).
- Details step (guide section 9): every new entry links to its station, bank,
  corporation or gang.

### - [ ] K10. Start a seed town from the command line (runner, ui)

- `mechapunk-sim --town` and `mechapunk --new --town` are shorthand for
  `--set city.seedTown=1 --set population.immigration=1` with the robots from
  question 1.
- Run a seed town for 365 simulated days headless at default settings and
  write in this file how big it got.

## Done when

- [ ] A seed town can be started (K1, K10) and runs valid with its money
      adding up (K2, K3).
- [ ] It grows: robots arrive, streets and buildings spread, the metro
      opens, banks, corporations and gangs appear, and the central bank
      keeps inflation on target (K4-K8).
- [ ] The growth shows in the chronicle and the statistics (K9).
- [ ] The guide's task table marks K done.
