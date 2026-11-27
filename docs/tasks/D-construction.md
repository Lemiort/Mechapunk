# Task D: construction

Status: open. D1 split on 2026-11-24 into D1a and D1b; D1a is done and
raised the save version to 22, and D1b is done (the view draws parks from
the saved plots, no save change). D2 split on 2026-11-25 into D2a to D2c;
D2a is done and raised the save version to 23, and D2b is done (no save
change: `projects` and `lots` shipped in v23 and v22). D2b2 was added between
D2b and D2c on 2026-11-25 and is done, also with no save change: it retunes
the two triggers D2b wrote, which as shipped could never fire as intended. D2c
was split on 2026-11-25 into D2c and D2c2: its `AddNewcomer` half lives in
`LifeCycle.cpp`, the `life-cycle` module. D2c is done, with no save change
(`projects` shipped in v23) and no D2d: `ReviewUnits` only works on ground-floor
rooms, and a site's ground floor is empty until D3. D2c2 is done on 2026-11-26,
also with no save change: a newcomer's home is a finished one. D2c3 was added
between D2c2 and D3 on 2026-11-26 and is done, also with no save change: a home
a robot buys is a finished one, which needed the `property` module split (its
venue tests moved to a new `units` module) to make room for the test. D3
split on 2026-11-26 into D3a to D3d; D3a is done and raised the save
version to 24. Save change. The biggest remaining task: take it slowly,
one step per module. D3b is done on 2026-11-26 with no save change: a
building going up gets its site office on the ground floor, the kind
D3a shipped. D3c is done on 2026-11-26, also with no save change: that
office has staff rules and is an opening, so a site going up gets
builders. D3d is done on 2026-11-26, also with no save change: when a site's
nights are up its builders are laid off and its office closes into an empty
unit, which `ReviewUnits` may then reopen. D4 is done on 2026-11-26, also
with no save change: the view draws a building going up as a scaffold and
takes it in when its project raises it. D5 split on 2026-11-26 into D5a to
D5e - its bullets live in four modules, not the two it names - and D5a is
done, also with no save change: the published frame carries the projects. D5b
is done on 2026-11-26, also with no save change: the layout library has the
lists a project's pages show. D5b2 was added on 2026-11-27, after D5b, and
is done: a project saves the credits its site has paid for its supplies and
the shortage that started it, which raised the save version to 25 and the
game to 0.25.0. D5c is done on 2026-11-27, also with no save change: the
inspector has a project's own page, opened by a new link.

## What and why

The city grows: when homes or jobs run short, organizations build new
buildings on free land, with builders who work there until it is done.

## Read first

`docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5; `docs/MODULES.md`
(modules `generation`, `property`, `view-core`). Construction gets its
own module: `Construction.hpp`, `sim/src/Construction.cpp`,
`tests/ConstructionTests.cpp`.

## Steps

### D1. Land as simulation state — split on 2026-11-24

The step names two modules and its last bullet lives in a third: the field,
the filling and the save change are `generation` and `saves`, "the view draws
parks from `lots`" is `view-core`. Cut into steps of one module and one commit
each, the original bullets living on inside D1a and D1b.

### - [x] D1a. Free plots as saved state (generation + saves)

Open: `City.hpp`, `City.cpp` (the end of `GenerateCity`), `CityTests.cpp`,
`Save.hpp`, `Save.cpp` (the migration list), `Migrations.hpp`, the end of
`Migrations.cpp`, `SaveTests.cpp`, plus `Validation.cpp` (rule 5, as C1 did
for the chronicle).

Done as (one commit): `City.hpp` gains `kCornersPerNode` (4), `CityLot
{NodeId node; std::uint32_t corner;}` and `City::lots`, sorted by node and
then corner. A lot is a corner slot no building of that node stands on: the
view numbers an intersection's four corners and puts a node's buildings on
its first slots in id order (`CityView.cpp`'s `usedSlots`, and buildings are
saved sorted by id), so the free plots of a node are the slots after the
buildings at it - which is what `NoIntersectionHasMoreThanFourBuildings`
keeps to four.

- `GenerateCity` fills them at the end, counting its own `city.buildings`
  by entrance rather than reusing `buildingsAt`, which also holds the depot
  reservations; the list is built by node then corner, so it is sorted.
- `V21ToV22` (declared in `Migrations.hpp`, registered in `kMigrations`)
  computes the same list from the save's own `city.nodes` and every
  `city.buildings[i].entrance`, and writes `"lots"` into `world.city`: a
  version 21 save names its grid and its buildings, so the land is derived
  from facts it did store, not invented. Save version 22, game 0.22.0.
- Fixture `tests/fixtures/saves/v22-city.json` from the guide's command (seed
  33, 100 robots, 28370 minutes) at game 0.22.0, plus its row and a sentence
  in that folder's README.
- Validation (`ValidateWorldState`, before the station checks): every lot
  names a node of the city, a corner below `kCornersPerNode`, a slot at or
  after that node's building count, and the list is strictly increasing by
  node then corner. What is *not* checked: that every free corner is listed.
  A save may know fewer plots than it has, and D2 only ever consumes them.
- Tests: `City.LotsAreTheFreeCornersOfTheGrid` (seeds 1, 7, 33 at 100, 1000
  and 10000 residents) compares the whole list against the free corners
  recomputed from the buildings; `CurrentVersionFixtureLoadsAndRoundTrips`
  points at v22 and checks the fixture has plots and that none sits on a
  taken corner; `Version21FixtureMigratesAndRuns` parses the v21 fixture's
  JSON and asserts `world.city` has no `"lots"` key - so what loads is the
  migration's work - then that every migrated lot is a free corner of its
  node, and runs the city a day.

First make room, as C4c did: `saves` stood at 23905 of 25000, and this step's
migration and tests need about 1250 tokens, so `tests/SaveFixtureTests.cpp`
was split by era. The frozen versions 1 to 8 moved to a new
`tests/SaveFixtureTestsEarly.cpp` in the `saves-frozen` module (the way
`MigrationsEarly.cpp` holds the frozen migrations), added to
`tests/CMakeLists.txt` and to `scripts/context_budget.py`. Code moved only:
the same tests run, and ctest went 172 to 174 because of this step's two new
tests. `saves` is now 22675, `saves-frozen` 8535.

The bump changed no behaviour: the seed 1, 10000 robots, 40 days `--quiet`
log matched byte for byte except its own `Simulated ... in N s` line, and
the seed 7, 2000 robots, 6 days save differs only in the version and game
version lines and the new `"lots"` block - compared as parsed JSON with
`lots` removed the two saves are equal, and the block holds exactly the
free corners of that city.

Warning for the steps after this: `generation` is now 24353 of 25000, 647
short of its budget, and `saves` 2325 short. A step that adds to `CityTests.cpp`
or to the saves module must make room first. Note for D2: generation fills
corners up to four per node, so a big city has little free land - the seed 7,
2000 robots city has 31 plots - and construction will find it scarce.

### - [x] D1b. The view draws parks from the plots (view-core)

Open: `app/CityView.cpp` (the park loop at the end of the constructor, and
the `occupied` array that only it reads - `usedSlots` stays, the building
placement needs it).

Done as (view-core module, one commit): the loop at the end of the `CityView`
constructor walks `city_.lots` instead of every node's four corners; the
quadrant is `kQuadrants.at(lot.corner)` and the position comes from
`NodePosition(lot.node)` plus `kQuadrantOffset`, so the position formula and
the `insideCity` test are unchanged and a corner outside the grid's bounding
box still gets no park. The `occupied` array went with it: the building loop
wrote it only for the park loop to read, and `usedSlots` stays because the
placement needs the slot index (a fifth building at a node still moves to the
far ring).

Why the city looks the same:

- `City.LotsAreTheFreeCornersOfTheGrid` (D1a) compares `lots` elementwise
  against a node's slots after its buildings for seeds 1, 7 and 33 at 100,
  1000 and 10000 residents, which is the order the old loop walked (`nodes`
  in index order, slots 0 to 3, skipping the taken ones), so the same parks
  are drawn in the same order and take the same draws from the
  `kDecorationStream` random.
- A node with four or more buildings has no lot and had no free corner: the
  old loop marked slots 0 to 3 taken, the saved list starts at that node's
  building count and is therefore empty.
- Checked on the seed 7 / 2000 robots save: 36 nodes, 113 buildings, 31 plots,
  and the free corners recomputed by the old rule are the same 31 pairs in
  the same order.
- `Validation` already rejects a `corner` at or above `kCornersPerNode`, so
  `kQuadrants.at` cannot read past its end.

No new source files, so `scripts/context_budget.py` is unchanged: view-core
18320 of 25000. ctest 174/174 including ContextBudget. `mechapunk-sim.exe`
was not relinked (no sim file changed; its timestamp predates the edit), so
the headless log and save cannot move; the seed 7 / 2000 robots / 6 days
`--all --save` run was made anyway to have a save to compare the plots
against. The parks are for the user to see in the game.

### D2. Projects (construction module) — split on 2026-11-25

The step names one module, but its bullets live in four: the field, the
migration, the fixture and the tests are `construction` and `saves` (with
`Validation.cpp`, rule 5, as C1 and D1a did); the nightly `ReviewConstruction`
and its call site in the midnight order are `construction` with a few lines
of `World.cpp` (as C2 opened `World.cpp` for `Emit`); "cannot be rented
or staffed until done" is `careers` (`ReviewJobs` and `ReviewHomes` both
live in Careers.cpp) and possibly `property` (`ReviewUnits` counts quiet
nights and reopens units); the supplies cost is a call to the existing
`PaySupplies` in Money.cpp, no money change. Cut into D2a to D2c, one
module and one commit each, the original bullets living on inside them.

### - [x] D2a. Projects as saved state (construction + saves)

Open: a new `Construction.hpp`, `World.hpp` (WorldState), `Save.hpp`,
`Save.cpp` (the list), `Migrations.hpp`, the end of `Migrations.cpp`,
`Validation.cpp`, `SaveTests.cpp`, plus the fixture and its README row.

Done as (one commit): `Construction.hpp` declares `Project` exactly as
the step's text has it - `building`, `owner`, `daysLeft`, `floorsDone` -
and `WorldState` gains `std::vector<Project> projects`, sorted by
building id (rule 1: the same world reviews the same projects
everywhere). The step's "or fold into D1" is moot: D1 shipped v22, so
this is the task's second save change, version 23, game 0.23.0.

- `V22ToV23` (declared in `Migrations.hpp`, registered in `kMigrations`)
  writes `"projects": []` into `world`: a version 22 game never raised a
  building, so the migration opens the list and starts nothing; the
  story of construction begins when the current game runs the save.
- Fixture `tests/fixtures/saves/v23-city.json` from the guide's command
  (seed 33, 100 robots, 28370 minutes) at game 0.23.0, plus its row and
  a sentence in that folder's README.
- Validation (`ValidateWorldState`, after the chronicle): unlike the
  chronicle's ids, a project's building and owner must exist - the
  building stands on its lot from the night the project starts and its
  owner pays the supplies the site costs every night - `floorsDone`
  cannot pass the building's floors, and the list is sorted by building
  id. What is *not* checked: that the building's rooms are empty or that
  its lot was free; a save may hold a project an older game left half
  built, and only the review (D2b) starts new ones.
- Tests: `CurrentVersionFixtureLoadsAndRoundTrips` points at v23 and
  checks the fixture's list is empty - nothing starts a project until
  D2b, and the round trip is what proves the save carries the key;
  `Version22FixtureMigratesAndRuns` parses the v22 fixture's JSON and
  asserts `world` has no `"projects"` key - so what loads is the
  migration's work - then that the migrated list is empty, and runs
  the city a day; `RejectsProjectsOutOfOrderOrBroken` writes projects
  by hand through a new `PushProject` helper: two real buildings in
  decreasing order, a building the city does not have, an owner that
  is none, and more floors done than the building has. All four bite.
- No `Construction.cpp` or `ConstructionTests.cpp` yet: as with C1's
  `Chronicle.hpp`, the data ships before the code that fills it. The
  `construction` module in `scripts/context_budget.py` holds
  `Construction.hpp` and `World.hpp` (7433); `docs/MODULES.md` gained
  the module row, the `Construction.hpp` row and a "which module for
  which task" row.

Warning for the steps after this: `saves` stands at 24440 of 25000, 560
short, and `world` at 24350, 650 short. A step that adds to `SaveTests.cpp`
or `WorldTests.cpp` must make room first.

The bump changed no behaviour: the seed 1, 10000 robots, 40 days `--quiet`
log matched byte for byte except its own `Simulated ... in N s` line, and
the seed 7, 2000 robots, 6 days save differs only in the version and
game version lines and the new `"projects": []` block - compared as parsed
JSON with `projects` removed the two saves are equal, and the `--all`
log is identical to its last two lines. ctest: 176/176 including
ContextBudget.

### - [x] D2b. `ReviewConstruction` (construction module, `World.cpp` call site)

Nightly `ReviewConstruction` after `ReviewUnits` in the midnight order
(the call site is a few lines in `World.cpp`, as C2's `Emit` call was):
home vacancy under 5% for 7 nights - `DailyStats::vacantApartments` over
the city's apartments, the streak read from the saved `history`, no new
field - starts a habitat on the free lot nearest the centre, owned by
the housing trust (the step's "Habitat Trust"); jobs per robot under
1.05 - `employed` over `population` in the saved history - starts a
workplace owned by an industry corporation. The project consumes its
lot, raises the building that exists from the start with `floors`
growing as `floorsDone` counts the finished ones, counts `daysLeft`
down at midnight, and pays the supplies the site costs every night
through the existing `PaySupplies` (Money.cpp). When `daysLeft` reaches
0 the project ends and the building is ordinary. Tests in a new
`ConstructionTests.cpp`; add the new files to the budget script.

Warning: until D2c lands, a site's rooms can attract staff and tenants,
so do not trust a 40-day headless run of this step for hiring or homes
counts; the "money adds up over 40 days at 10000 robots" watch belongs
after D2c.

Done as (construction module, one commit): a new `sim/src/Construction.cpp`
implements `World::ReviewConstruction`, declared in `World.hpp` next to
`ReviewUnits` and called in `World.cpp` after `SettleEconomy()` - which is
where the midnight order puts it after `ReviewUnits` (that call lives inside
`SettleEconomy`, Settlement.cpp) and after the books roll over, so a site's
supplies count into the new day's books. The guide's section 4 midnight line
gained it.

- Advance first, then start: every project pays `PaySupplies(owner, 200)`
  (Money.cpp takes the owner's sector share of the site's cost: half for the
  housing trust, and nothing for an industry corporation, whose supplies are
  what the other sectors buy from it), raises one floor - `building.floors`
  and `floorsDone` grow together and that floor's homes or workspaces appear
  with it - and `daysLeft` counts down; a project whose nights are used up is
  erased and its building is ordinary.
- A new site starts at `floorsDone` 1 (the ground floor stands from the first
  night) with `daysLeft` the floors to raise: 6 for a habitat, 4 for a
  workplace. The building is created with `floors` 1 and no rooms at all: the
  ground floor stays empty until step D3 puts the site office there, which
  also keeps `ReviewUnits` - it only works on ground-floor units - away from a
  site, so no D2d split was needed. Homes are studios low, flats in the middle,
  two-bedroom flats near the top, at generation's rents; a workplace gets two
  workspaces of 8 places a floor on an assembly line's shift.
- The lot is consumed: the project takes a node's *lowest* free corner, which
  is where the view puts the building (D1a: a node's buildings hold its first
  slots in id order), and erasing that one lot keeps `Validation`'s rule
  "a lot is a corner no building of that node stands on" true. `rooms_` is
  rebuilt and `cityVersion_` bumped whenever the city changed.
- What the step's text left open, and what was measured:
  - "jobs per robot under 1.05 - `employed` over `population`" is always true:
    `employed` counts robots with a workspace (EconomyStatistics.cpp), so it
    cannot pass `population`. The city therefore raises a workplace at every
    midnight while it has a free plot - the seed 5 / 1000 robots city over 12
    days took 4 plots (31 to 27) and all four new buildings are workplaces -
    so the brake is land, not money.
  - "home vacancy under 5%" is rare in a young city: seed 5 / 1000 robots has
    598 of about 1560 apartments empty (38%), and the seed 1 / 10000 robots
    run 5130 of about 15000 (34%), so no habitat starts in 40 days there. The
    habitat tests therefore run a hand-made city whose un-lived apartments are
    removed (vacancy 0%), the guide's "set the exact state and run" pattern.
    Before D2c, ask the user whether the jobs share should be a real one
    (unemployment over some share, say) and whether vacancy should be read
    over the market homes alone, as `SetPrices` does per class.
- No new saved field, no random draw, no new event kind: `projects` and `lots`
  shipped in v22 and v23, so no save version bump, and the saved random stream
  is untouched. `Validation` needed no new check (D2a's project checks and
  D1a's plot checks already cover what this step writes).
- Tests in the new `tests/ConstructionTests.cpp` (5, none can skip): a site
  grows one floor a night and ends when its nights are up; the owner pays the
  supplies and the industry corporation pays none, with `ValidateEconomy`
  after; a site takes the plot nearest the middle and the plots stay true
  (`ValidateWorldState`); a 30-day city stays valid, savable and raises no
  habitat; the same seed twice gives the same save, a different seed not.
- First make room, as C4c and D1a did: `social` stood at 24983 of 25000 and
  this step's `World.hpp` declaration put it over, so `LifeCycle.cpp` and
  `tests/LifeCycleTests.cpp` moved out of `social` into a new `life-cycle`
  module (aging, wear, scrapping, newcomers) in `scripts/context_budget.py`
  and both MODULES.md tables. No code moved: `social` 20754, `life-cycle`
  11498.
- `scripts/context_budget.py` gained the two new files in `construction`
  (15429 of 25000), `sim/CMakeLists.txt` and `tests/CMakeLists.txt` the two
  files, `docs/MODULES.md` the `Construction.cpp` row and the task rows.
- ctest: 181/181 including ContextBudget (176 before, +5 new). Simulation code
  changed, so the headless check ran: seed 1, 10000 robots, 40 days `--quiet`
  finished in 243 s, money adding up every day (`money` equal to `created`
  with `destroyed 0`), `created` 7491 on the last day, `wages at 110%`, 18 of
  10000 out of work, 5893 vacant apartments, 27 empty units, `supplies` 921217
  and rising. As the warning above says, hiring and homes counts are not to
  be trusted until D2c lands, and the money watch belongs after D2c.

Warning for the steps after this: `world` is now 24449 of 25000 and
`generation` 24436; a step that adds to `World.cpp`, `WorldTests.cpp` or
`CityTests.cpp` must make room first.

### - [x] D2b2. The two triggers retuned (construction module)

Added on 2026-11-25, before D2c, because the rules D2b shipped cannot do what
they say: `employed` counts robots that have a workspace, so "jobs per robot
under 1.05" is never false and the city raised a workplace at every midnight
while it had a free plot; and vacancy over all apartments sits at 34 to 38 per
cent, so "under 5% for 7 nights" never fires. This is the question D2b's own
text left open ("ask the user whether the jobs share should be a real one, and
whether vacancy should be read over the market homes alone, as `SetPrices` does
per class"), answered.

Open: `Construction.cpp`, `ConstructionTests.cpp`, plus the lines of `Units.cpp`
that hold `SetPrices` and `ReviewUnits` - the market homes, and the unemployment
share the property module already uses - and `DailyStats` in `Economy.hpp`.

Done as (construction module, one commit): the two lambdas in
`World::ReviewConstruction` measure different things; nothing else moved.

- Jobs: over 5 per cent of the population out of work for 7 nights raises a
  workplace. Read as `DailyStats::unemployed` over `DailyStats::population`,
  both already saved, and 5 per cent is the share at which `ReviewUnits`
  (Units.cpp) reopens an empty unit as a workspace (`kHighUnemploymentPercent`):
  the city reuses an empty unit first and builds only when that has not kept up
  for a week. The old `employed`-over-`population` comparison is gone.
- Homes: vacancy over the market's homes under a quarter for 7 nights raises a
  habitat. The market's homes are the ones `SetPrices` counts class by class -
  `ApartmentClass` from `Studio` up, a government dorm bed being off the market,
  the homes robots can afford and move into - and the robots who lived in them
  each night come from `DailyStats::residentsByClass`, the entries after the
  dorm beds. The homes a past day was counted over are not saved, so every
  night of the streak is measured against the homes the city has now, as D2b
  already did. A home two robots share reads as one vacancy fewer than it is,
  which is why the share leaves room.
- Unchanged from D2b: the midnight call site after `SettleEconomy`, the lot
  consumption, the supplies through `PaySupplies`, the floors and `daysLeft`,
  and no new random draw, so the saved stream is untouched.
- No save change: `population`, `employed`, `unemployed` and `residentsByClass`
  are fields `DailyStats` has had long before v23, so no new key, no migration,
  no fixture, and `Validation` needs nothing new.
- What the new rules see, measured night by night on the seed 5 / 1000 robots
  city over 30 nights: the market's homes start 100 per cent empty and fill as
  robots move up out of the dorm beds - 77 per cent at night 9, 44 at 15, 24 at
  21, 7 at 29 - so under a quarter from night 21, and the first habitat goes up
  at night 27: 3 buildings raised in 30 nights, all habitats. Unemployment stays
  at 0 to 3 per cent there, so no workplace: the jobs rule bites only where
  jobs really went.
- The jobs rule was shown firing on a hand-made city (`JobsTakenState` in the
  tests): every workspace nobody is in or on its way to is removed and its
  workers laid off as `CloseUnit` (Units.cpp) does it - no workplace, junior,
  no performance, `commuteAt = kNoCommute` - which leaves 76.9 per cent of the
  robots out of work and raises a workplace at the seventh midnight. Clearing
  `commuteAt` is what makes it hold: a robot whose job was taken but whose
  departure is still planned walks to room 0 and `PlanTrip` throws
  "Trip between unknown rooms"; and leaving the rooms standing as empty units
  does not work either, since `ReviewUnits` reopens them at that same 5 per
  cent and the robots are hired back.
- Tests: 9 in `tests/ConstructionTests.cpp`, 4 new, none can skip. `HomesShortRaisesAHabitat`
  (the 30-night city: the site's kind, an owner of sector Housing, 6 nights left,
  the ground floor empty, and all 7 nights it read under the share);
  `HomesNotShortLeavesNoHabitat` (the same city 15 nights in: 44 per cent of the
  market empty, nothing raised); `JobsShortRaisesAWorkplace` (the jobs-taken
  city: the site's kind, a corporation of industry as owner, workspaces of that
  owner a floor up, and no habitat); `JobsNotShortLeavesNoWorkplace` (30 nights
  of a young city raise no workplace). The three D2b tests that had used a
  hand-made city whose empty homes were removed now use the real city, and
  `HomesTakenState` is gone: the market fills up on its own.
- Budget: `construction` went 15429 to 18653 of 25000, so no module was split;
  `world` (24452) and `generation` (24436) are untouched, as no `World.cpp`,
  `World.hpp` or `CityTests.cpp` line changed.
- ctest: 185/185 including ContextBudget (181 before, +4 new). Simulation code
  changed, so the headless check ran: seed 1, 10000 robots, 40 days `--quiet`
  finished with the money adding up every day (`money` equal to `created`,
  `destroyed 0`), `created` 839 on the last day, `wages at 110%`, 7 of 10000
  out of work, 6003 vacant apartments, 22 empty units, `supplies` 919606. The
  market fills up there too: comparing the save of that run against the save
  of the same seed after 1 day, the city raised 9 buildings, all habitats
  ("Hab Block"), took 9 of its 50 plots (50 to 41) and had 6 sites still going
  up at the end - and no workplace, since the jobs rule never sees over 5 per
  cent out of work. As D2b's warning says, hiring and homes counts are still
  not to be trusted until D2c lands, and the money watch belongs after D2c.

### - [x] D2c. A site's rooms cannot be rented or staffed until done (careers)

`OpeningsFor` and `ReviewJobs` skip rooms of a building under construction
(Careers.cpp); so do `ReviewHomes` and `AddNewcomer`'s choice of apartment.
If `ReviewUnits` (Units.cpp, property) counts a site's quiet nights or
reopens its rooms, split a D2d for that file.

Split on 2026-11-25: the step names the careers module, but `AddNewcomer`
lives in `LifeCycle.cpp`, which is the `life-cycle` module, so its half of the
step became D2c2. No D2d is needed for `Units.cpp`: `ReviewUnits` skips every
room with `room.floor != 0` (Units.cpp) and a site's ground floor holds nothing
until step D3 (D2b's note), so it never counts a site's quiet nights nor reopens
its rooms.

Done as (careers module, one commit): `Construction.hpp` gained
`IsUnderConstruction(projects, building)`, a lookup of the saved list by
building id - `WorldState` keeps it sorted (D2a), so it is a binary search like
`ReviewConstruction`'s own `buildingFor`. It is inline in that public header
because two areas ask it (careers now, life-cycle in D2c2) and "public headers
are the only way between areas" (docs/MODULES.md); `World.hpp` gained no member.

- `OpeningsFor` skips a site's rooms in all three of its loops (workspaces,
  venue kinds, police stations and crew rooms) through one file-local `onSite`
  lambda. `ReviewJobs` needed no change of its own: it hires from
  `OpeningsFor`, so a site's rooms are simply not openings - and that already
  covers the job a newcomer takes, which `AddNewcomer` picks from `OpeningsFor`
  too (LifeCycle.cpp); only its choice of *home* is left, in D2c2.
- `ReviewHomes` skips them in its `pick` helper, which is one place for all
  three of its choices: the cheapest home an evicted robot is put in, the
  priciest affordable one a saver moves up to, and the shared flat two friends
  move into together.
- No save change: `projects` shipped in v23 (D2a) and `lots` in v22 (D1a), no
  field, key, migration or fixture was added, and `Validation` needs nothing
  new - D2a's project checks and D1a's plot checks already cover what this step
  reads. No random draw is added either: the skip filters a candidate list
  before the draw, so the saved stream is untouched; the outcome moves, which
  is the point of the step.
- Tests: 2 new in `tests/CareersTests.cpp`, neither can skip.
  `SitesTakeNoWorkers` puts a workplace building of the seed 7 / 200 robots
  city on a site by hand (`Raise`: a project whose owner is the employer of
  that building's rooms, 30 nights left), lays off everyone working in it as
  `CloseUnit` does, and runs 7 midnights: nobody works in the site, the project
  is still going up, and the laid off robots did find other work (`rehired > 0`,
  so the openings path really ran). With the skip taken out, 3 robots work
  there (`CareersTests.cpp:452: Value of: staffed, Actual: 3, Expected: 0`).
  `EvicteesSkipSiteHomes` puts a habitat building on a site and sets one of its
  free homes to rent 1 - under a dorm's 8 (City.cpp), so it is the cheapest
  home in the city - then evicts the only debtor at the next midnight (the
  others' `rentOwed` is cleared, and `CollectRent` charges them one night, under
  the five that evict): it moves, remembers the eviction, and not onto the site.
  With the skip out it lands there (`CareersTests.cpp:502`).
- Budget: `careers` 19974 to 22534 of 25000 (the two tests, and `Construction.hpp`
  in the module); `construction` 18663 to 18951 (the helper); `world` 24452 to
  24485 and `generation` 24436 to 24469 (the `OpeningsFor` comment in `World.hpp`),
  both still under 25000. `scripts/context_budget.py` gained `Construction.hpp`
  in `careers`; `docs/MODULES.md` the two sizes and the `Construction.hpp` row.
- ctest: 187/187 including ContextBudget (185 before, +2 new).
- Simulation code changed, so the headless check ran: seed 1, 10000 robots, 40
  days `--quiet` finished in 315 s, and the money adds up on all 40 days
  (`money` equal to `created in all`, `destroyed 0` every day). Day 40 reads
  `created 696 today`, `wages at 108%`, 24 of 10000 out of work, 6003 vacant
  apartments, 17 empty units, `supplies 913905`, gini 503, 1068 broke robots,
  13 behind on rent - the same shape as D2b2's run (created 839, wages at 110%,
  7 out of work, 6003 vacant, 22 empty units, supplies 919606), so the fix
  moved the city's story without straining its money. The save of that run
  (the same seed and days with `--save`) holds 41 of the city's 50 plots and 6
  projects still going up, so 9 sites were raised in 40 days and none is a
  workplace: the same land the run took before this step (D2b2's note), so the
  homes trigger still fires as often now that a site's homes hold no tenants.
  The two `--quiet` logs match line for line except their own timing and save
  path lines, which also proves the header's wrapped line changed nothing.

### - [x] D2c2. A newcomer's home is a finished one (life-cycle module)

Open: `sim/src/LifeCycle.cpp` (`AddNewcomer`'s scan of
`rooms_.RoomsOfKind(RoomKind::Apartment)`), `tests/LifeCycleTests.cpp`.

- Skip the apartments of a building a project is raising, asking
  `IsUnderConstruction(state_.projects, apartment.building->id)`
  (`Construction.hpp`, D2c), in both the free homes and the free dorms lists.
- Test: a city with a site going up never moves a newcomer into it, and the
  newcomer did get a home (the test cannot pass because the list was empty).
- No save change, no new random draw. `life-cycle` stands at 11531 of 25000.

Done as (life-cycle module, one commit): one condition in `AddNewcomer`'s scan
of `rooms_.RoomsOfKind(RoomKind::Apartment)` - `!IsUnderConstruction(state_.projects,
apartment.building->id)` beside the "somebody lives here" test - keeps a site's
homes out of both lists at once, since `freeDorms` is built inside that same loop
as a subset of `freeHomes`. `LifeCycle.cpp` gained the `Construction.hpp` include
(`Careers.cpp` spells it out the same way, though `World.hpp` already pulls it in)
and `scripts/context_budget.py` gained that header in the `life-cycle` module.

What the step's text left open, and what was chosen:

- The test has to force the newcomer's hand. A newcomer takes a free dorm when
  there is one, and a site's homes are never dorms (`HomeClassFor`, Construction.cpp:
  studios low, flats in the middle, two-bedroom flats near the top), so in a
  generated city the skip changes nothing a newcomer does. The test therefore
  writes the classes by hand: the site's empty homes become the city's only free
  dorms, and every other free dorm becomes a studio. Without the skip the newcomer
  then moves onto the site for sure; with it, it takes a finished home.
- The site is a hand-made project on a generated habitat building (`Raise`, as
  D2c's tests have it): the shortage that starts a project is read off seven
  saved days, so a two-day city raises none by itself. Its owner is the housing
  trust, which pays the site's supplies at the midnight the test runs through.
- The newcomer is made by wearing one robot out at day 2, 23:00 (`wear = kMaxWear
  - 1`) and running 90 minutes: `AgeRobots` scraps it and calls `AddNewcomer`.
- It cannot pass on an empty list: the test asserts the city had a free finished
  home before the run, that a robot with a new id arrived, that its apartment is
  valid, and that it is not a room of the site; `ValidateWorldState` after. With
  the skip taken out it fails at `tests/LifeCycleTests.cpp:275`: `Value of:
  InBuilding(after, site, robot.apartment) Actual: true Expected: false` - "a
  newcomer moved onto a site".
- The file had no helpers of its own, so `HousingTrust`, `Raise`,
  `HabitatWithFreeHome` and `InBuilding` are local copies of `CareersTests.cpp`'s,
  which the split forbids reaching into.

No save change: `projects` shipped in v23 (D2a) and `lots` in v22 (D1a), no field,
key, migration or fixture was added, and `Validation` needs nothing new. No random
draw is added: the skip filters a candidate list before the pick. The saved stream
moves only where the trim empties that list - a city whose only free homes stand
on a site - and there the newcomer simply does not move in, which is the point.

Budget: `life-cycle` 11531 to 13592 of 25000; no new source file, so `docs/MODULES.md`
gained the module's new size and file, and its `Construction.hpp` row now says a
newcomer's choice of home asks it too. ctest: 188/188 including ContextBudget (187
before, +1 new).

Simulation code changed, so the headless check ran: seed 1, 10000 robots, 40 days
`--quiet` finished in 241 s, and the money adds up on all 40 days (`money` equal to
`created in all`, `destroyed 0` every day) - the watch D2b deferred to after D2c
holds. Day 40 reads `created 696 today`, `wages at 108%`, 24 of 10000 out of work,
6003 vacant apartments, 17 empty units, `supplies 913905`, gini 503, 1068 broke
robots, 13 behind on rent: the numbers D2c's note quotes, because that city always
has a free dorm and a site's homes are never dorms, so the list this step trims is
one a newcomer there never draws from.

### - [x] D2c3. A home a robot buys is a finished one (property module)

Open: `sim/src/Property.cpp` (`TradeProperty`'s choice of a home to buy, the
"dearest it can put a fifth down on" loop), `tests/HousingTests.cpp`.

The step's text did not ask for this: it names `ReviewHomes` and `AddNewcomer`
as the other places a home is chosen, and `TradeProperty` is a third. A habitat
site's homes belong to `project.owner`, which is the housing trust
(`Construction.cpp`), and this loop buys a robot into a home owned by the trust,
so a robot can buy its way into a half-built tower.

- Skip the apartments of a building a project is raising, asking
  `IsUnderConstruction(state_.projects, apartment.building->id)`
  (`Construction.hpp`, D2c). The two other apartment loops of the file need
  nothing: one sells homes nobody lives in, the other collects homes that
  *are* lived in, and a site's homes are neither.
- Test: a city with a site going up never buys a robot into it, and the city
  did have sales that night (the test cannot pass because nothing was bought).
- No save change, no new random draw. `property` stands at 23719 of 25000:
  make room first if the test does not fit.

Done as (property module, one commit): the buy loop's candidate test gained
`IsUnderConstruction(state_.projects, apartment.building->id)` as its last
condition, and `Property.cpp` spells out the `Construction.hpp` include the way
`Careers.cpp` and `LifeCycle.cpp` do. The step's claim about the other two
loops holds as written, and was checked rather than assumed: the selling loop
(Property.cpp:149) continues unless `FindRobot(room.owner)` finds a robot, so a
trust-owned site home is never sold, and the `letByTrust` loop (Property.cpp:238)
collects only homes in `lived`, which a site's homes never are - `startSite`
raises a new building whose rooms appear floor by floor, and D2c and D2c2 keep
workers, tenants and newcomers out of them.

- What the step's text left open, and what was chosen:
  - The test cannot trust a site's homes to be dear by accident: generation
    and `raiseFloor` give them mid-range rents, so the test raises the site's
    free homes one place there are above every finished home of that size,
    doing the arithmetic through the saved `economy.rentPercent` because
    `RentFor` (Units.cpp) reads a home through its class's market percent.
    A buyer that can afford one therefore buys there, and one that cannot
    still buys the dearest finished home, so sales happen either way.
  - The site is a hand-made project on a generated habitat (`Raise`, as in
    `CareersTests.cpp` and `LifeCycleTests.cpp`): the shortage that starts a
    project is read off seven saved days, so a two-day city raises none.
  - The buy is a three per cent draw a night, so one rich robot over twenty
    nights would be luck: every tenth robot gets 20000 credits through
    `sim::SetCredits`, gangs are cleared so robbery cannot drain them, and the
    test asserts the city had sales rather than that one named robot bought.
- Test `Housing.BuyersSkipSiteHomes`: seed 5 / 1000 robots one day in, the
  site's building found by `HabitatAndVacantHome`, 20 nights run, then
  `sales > 0` (counted from `EventKind::BoughtProperty`), no robot living in
  the site building, and `ValidateWorldState` after. With the skip taken out
  it fails at `tests/HousingTests.cpp:402`: `Value of: InBuilding(world.State(),
  site, robot.apartment) Actual: true Expected: false` - "a robot bought into a
  building going up" - for several robots.
- First make room, as the step's last bullet says and C4c, D1a and D2b did:
  the test put `property` at 25739 of 25000, so `tests/HousingTests.cpp` was
  split by topic. The three venue tests (`VenuePricesFollowDemand`,
  `QuietVenuesClose`, `EmptyUnitsOpenAsVenuesInDemand`) and their `EmptyVenue`
  helper moved verbatim to a new `tests/VenueTests.cpp`, which joins `sim/src/Units.cpp`
  in a new `units` module - the way D1a split the fixture tests by era into
  `saves-frozen`. `tests/CMakeLists.txt` gained the file, `scripts/context_budget.py`
  the module, and `docs/MODULES.md` its row and the `Units.cpp` task row. The
  same tests run: ctest went 188 to 189 only because of the new test.
  `property` 23719 to 20920, `units` 15564.
- No save change: `projects` shipped in v23 (D2a) and `lots` in v22 (D1a), so
  no field, key, migration or fixture, and `Validation` needs nothing new. No
  random draw is added: the skip filters the candidate list before the pick, so
  the saved stream is untouched; the outcome moves, which is the point.
- Checked: build, ctest 189/189 including ContextBudget. Simulation code changed,
  so the headless check ran: seed 1, 10000 robots, 40 days `--quiet` in 245 s,
  money equal to `created in all` with `destroyed 0` on all 40 days, and day 40
  reading `created 696 today`, wages at 108%, 24 of 10000 out of work, 6003
  vacant apartments, 17 empty units, `supplies 913905`, gini 503, 1068 broke
  robots, 13 behind on rent - the numbers D2c2's note quotes. Compared against
  the same run with the skip taken out: the two logs differ in one line only,
  day 40's `economy:` line (corporations 2774180 and banks 435470 before,
  2773680 and 435970 after: 500 credits of one mortgage), and the two saves
  differ in 52 fields - five robots (22901, 23759, 24844, 25617, 31830) buy
  rooms 37072 to 37076 of "Hab Block 36566", a habitat whose project was still
  raising floors, instead of rooms 3943 to 3946 of the finished "Hab Block
  42". Both runs' money adds up; the skip moved five sales in the reference
  city and nothing else. That the end state of both runs shows no robot on a
  site is only because those projects had finished by day 40.

### D3. Builders — split on 2026-11-26

The step names one module, but its one sentence lives in four: the room
kind is `generation` (`City.hpp`) and a save change (`saves`, with the
enum names in `Save.cpp`, as C1, D1a and D2a did); the room in the new
building is `construction` (`Construction.cpp`'s `startSite`); the jobs
are `careers` (`JobsAt` and the staff rules in Venues.cpp, `OpeningsFor`
in Careers.cpp); and "then it becomes a vacant unit" is `construction`
again (the review that ends the project). Cut into D3a to D3d, one module
(plus `saves` for the format change) and one commit each, the original
sentence living on inside them.

### - [x] D3a. The site office as a saved room kind (generation + saves)

Open: `City.hpp` (the `RoomKind` enum), `Save.hpp`, `Save.cpp` (the
`glz::meta` of `RoomKind`, the migration list), `Migrations.hpp`, the end
of `Migrations.cpp`, `SaveTests.cpp`, plus the fixture and its README row.

- `RoomKind` gains `SiteOffice`, named `"siteOffice"` in `Save.cpp`:
  save version 24, game 0.24.0.
- `V23ToV24` adds no key and changes no value: no version 23 game ever
  wrote a site office, so no room of an old save can have the kind, and
  the bump is there because a version 24 save may name a kind a version
  23 game cannot read.
- Fixture `tests/fixtures/saves/v24-city.json` from the guide's command,
  plus its row and a sentence in that folder's README.
- `CurrentVersionFixtureLoadsAndRoundTrips` points at v24;
  `Version23FixtureMigratesAndRuns` checks the v23 fixture text has no
  `"siteOffice"`, so what loads is the migration's work; and a hand-edited
  save with one room of kind `"siteOffice"` loads, validates and
  serializes back — the new name read and written.
- First make room, as D1a did: `saves` stands at 24440 of 25000, so
  move the frozen migrations 9 to 17 verbatim from `Migrations.cpp`
  into `MigrationsEarly.cpp` (the same era split) before adding to it.
- No validation change: a room is checked by floor, capacity and id,
  not by a kind table, and no robot works at a site office until D3c.

Done as (generation + saves, one commit): `City.hpp`'s `RoomKind` gains
`SiteOffice` after `CrewRoom`, `Save.cpp` names it `"siteOffice"`, and
`Save.hpp` raises the save version to 24 and the game to 0.24.0.

- `V23ToV24` (declared in `Migrations.hpp`, registered in `kMigrations`)
  is the version number alone: no key is new, and no version 23 game ever
  wrote a site office, so no room of an old save can name the kind and
  there is nothing to change; the bump is there because a version 24 save
  may name a kind a version 23 game cannot read.
- First make room, as the step says and D1a did: `saves` stood at 24440
  of 25000, so the frozen migrations `V9ToV10` to `V17ToV18` moved verbatim
  from `Migrations.cpp` into `MigrationsEarly.cpp` - the same era split,
  code moved only, no migration edited. `saves` 24440 to 20823,
  `saves-frozen` 8658 to 13835; `docs/MODULES.md` gained the two sizes and
  its rows now say "frozen migrations 1 to 18" and "migrations from
  version 18 on", and the guide's state line says version 24, game 0.24.0.
- Fixture `tests/fixtures/saves/v24-city.json` from the guide's command
  (seed 33, 100 robots, 28370 minutes) at game 0.24.0, plus its row and
  a sentence in that folder's README. The fixture ran with the nightly
  review (D2b) and raised nothing: its projects list is empty and its
  51 plots untouched, so `CurrentVersionFixtureLoadsAndRoundTrips` keeps
  its checks - the projects comment now says the review ran and saw no
  seven-night shortage - and adds that no room of it is a site office.
- `Version23FixtureMigratesAndRuns` loads v23: it asserts the fixture
  text has no `"siteOffice"`, so what loads is the migration's work,
  and runs the city a day. `SiteOfficeRoomKindRoundTrips` edits a seed
  3 / 10 robots save so one room no robot references - chosen by id
  against the robots' apartment, workspace, location and destination,
  since a visiting robot's room must keep its venue kind - reads
  `"siteOffice"`: it deserializes, loads through `FromState`, and
  serializing the loaded world names the kind again. What the step left
  open: "serializes back" is checked as the written name, not whole-
  string equality, because `EditSave` writes compact JSON while
  `SerializeWorld` pretty-prints.
- No new source file, so `scripts/context_budget.py` is unchanged;
  `generation` 24469 to 24515 (the enum comment). ctest: 191/191
  including ContextBudget (189 before, +2 new).
- The bump changed no behaviour: the seed 1, 10000 robots, 40 days
  `--quiet` log matched byte for byte except its own `Simulated ... in N
  s` line, and the seed 7, 2000 robots, 6 days `--all` log is identical
  and its save differs only in the version and game version lines -
  compared as parsed JSON with those two removed, the saves are equal.
  Money adds up on all 40 days (`money` equal to `created in all`,
  `destroyed 0` every day).

### - [x] D3b. The site office room in a new building (construction)

Open: `Construction.cpp` (`startSite`), `ConstructionTests.cpp`.

- `startSite` puts one room on the ground floor: kind `SiteOffice`,
  named "Site Office", a capacity of builders, owner the project's owner,
  rent 0. D2b left the ground floor empty for exactly this step;
  `ReviewUnits` (Units.cpp) branches its ground-floor loop by kind
  (`IsVenue`, `Workspace`, `VacantStore`), so it leaves a `SiteOffice`
  room alone — checked, no D3b2 for `Units.cpp` is needed.
- Tests: a started site has the office on floor 0 of its building,
  owned by the project's owner, and the city stays valid.

Done as (construction module, one commit): `startSite` pushes the office
onto the building before it goes into `City::buildings`: kind `SiteOffice`,
name "Site Office", floor 0, capacity `kSiteBuilders`, owner the project's
owner; its rent is the aggregate's 0, which the new test reads back. The
room id is allocated just after the building's, so the building's rooms
stay sorted by id and `ValidateWorldState`'s id rules hold.

- What the step left open, and what was chosen: "a capacity of builders"
  names no number, so `Construction.cpp` sets `kSiteBuilders` to 6 - a
  crew of six, the jobs step D3c gives out through the kind's staff rules.
  Until D3c lands nobody works there: `FindStaffRules` (Venues.cpp) has
  no entry for `SiteOffice`, so `JobsAt` is 0 and `OpeningsFor` offers
  nothing, and the capacity only has to be the non-zero number
  `ValidateWorldState` asks of every room.
- The step's claim about `ReviewUnits` was checked, not assumed: its
  ground-floor loop (Units.cpp:220) branches `IsVenue`, then `Workspace`,
  then `VacantStore`, and a `SiteOffice` room matches none of the three,
  so it neither counts a site's quiet nights nor reopens its rooms. No
  D3b2 for `Units.cpp`.
- Five D2b and D2b2 tests asserted a site's ground floor is empty, which
  this step makes false, and now read the office instead: `HomesShortRaisesAHabitat`
  and `JobsShortRaisesAWorkplace` expect one room of kind `SiteOffice`
  in a site's first night; `GrowsOneFloorPerNightAndEndsWhenDone` counts
  7 rooms after a night (the office and six studios) and counts homes
  per floor over apartments only; `ASiteTakesTheFreePlotNearestTheMiddle`
  expects the office in every new building; `KeepsTheCityValidAndSavable`
  asks every site going up for exactly one office on floor 0 and its
  homes and workspaces above it.
- New test `AStartedSiteHasASiteOffice`: floor 0, the name, the owner
  equal to the project's owner, capacity above 0, rent 0, exactly one
  office in the building, `ValidateWorldState` after; then one night
  more, where the floor above went up, the office is still a
  `SiteOffice` room, and the city validates again.
- No save change: `SiteOffice` shipped in v24 (D3a), `projects` in v23
  (D2a) and `lots` in v22 (D1a), so no field, key, migration or fixture,
  and `Validation` needs nothing new - a room is checked by floor,
  capacity and id, not by a kind table. No random draw is added.
- The step moves the entity-id stream: every site allocates one room id
  more, so the newcomers and gangs created later get different ids, and
  `ShiftSlot` (Venues.cpp) reads a robot's id. The reference city's story
  therefore moves, and one population test could not tell a rap sheet
  that fits from one trimmed to fit: `Social.KeepsItsRapSheetAsLongAsItFits`
  counted a robot with exactly 12 crime memories as "fits", but at the
  cap the oldest crime - its arrest - is legitimately gone. Its bound is
  now `crimes < 12`, which is what its own comment and name already
  meant; the guide's "leave margin" pitfall. Nothing in `Social.cpp`
  changed.
- Budget: `construction` 18653 to 20694 of 25000, `social` 20754 to
  20857; `docs/MODULES.md` gained the new size and says the file also
  raises the office. ctest: 192/192 including ContextBudget (191 before,
  +1 new test).
- Simulation code changed, so the headless check ran: seed 1, 10000
  robots, 40 days `--quiet` in 255 s, money equal to `created in all`
  with `destroyed 0` on all 40 days, day 40 reading `created 2194 today`,
  wages at 108%, 6005 vacant apartments, 22 empty units, supplies 919058,
  gini 504, 1080 broke robots, 10 behind on rent - the shape D2c3's note
  quotes (created 696, wages at 108%, 6003 vacant, 17 empty units,
  supplies 913905, gini 503, 1068 broke, 13 behind), the sites' offices
  added to it. Its save holds 9 `siteOffice` rooms, 6 projects still
  going up and 41 of the city's 50 plots - the same 9 sites D2c's note
  counted, each now with its office. And the seed 7, 2000 robots, 6 days
  `--all --save` run is byte for byte the same log and save as before
  this step: that city raises no site, so the step changes nothing where
  there is no site.

### - [x] D3c. Builders: jobs at the site office while the project is up (careers)

Open: `Venues.cpp` (a `StaffRules` entry for the kind), `Careers.cpp`
(`OpeningsFor`), `CareersTests.cpp`.

- Staff rules for `SiteOffice` — a site's shift, wage and one shift —
  so `JobsAt`, `JobShift` and `JobKey` read it the way they read a
  venue's staff, and the site office of a habitat gets a real shift
  where its building has none.
- `OpeningsFor` adds the site offices' rooms, and the D2c skip
  (`onSite`) exempts them: a `SiteOffice` room exists only while its
  project is going up (D3d turns it into a vacant unit when the project
  ends), so the skip must not swallow the builders' jobs.
- Tests: a city with a site going up gets builders there, and the
  site's other rooms still take no workers.

Done as (careers module, one commit): `Venues.cpp` gained `kSiteStaff`
beside `kPoliceStaff` and `kCrewStaff` - one shift from 07:00, 480
minutes long, wage 100, `guestsPerWorker` 1 - and `FindStaffRules`
returns it for `RoomKind::SiteOffice`. Those hours and that wage are
`kSiteShift` (Construction.cpp), the shift a site's workplace building
already keeps, so a habitat's office gets real hours where its building
carries `Shift{}`, and `guestsPerWorker` 1 makes `StaffPerShift` the
office's capacity: the crew of six D3b gave it. `JobsAt` is therefore
6, `JobShift` the site's day shift and `JobKey` one key a room
(`ShiftSlot` of a one-shift kind is always 0), which also makes
`Validation`'s "a robot's job room holds at most `JobsAt` robots"
accept a builder at an office: with no staff rules `JobsAt` was 0, so
`Validation.cpp` read a robot working there as a robot with a job at
no room at all.

- `OpeningsFor` gained a fourth loop over
  `rooms_.RoomsOfKind(RoomKind::SiteOffice)` that asks
  `taken(...) < StaffPerShift(...)` and never calls `onSite`; the comment
  above the skip says why, and the `World.hpp` declaration's comment and
  the `Careers.hpp/.cpp` and `Venues.hpp/.cpp` rows of `docs/MODULES.md`
  say the same.
- What the step left open, and what was chosen: "a site's shift, wage
  and one shift" names no numbers, so the office keeps the site's own
  hours (07:00 to 15:00) and wage 100 rather than the police and crew
  06:00/14:00/22:00 at 120: builders work the site's day shift, and the
  wage is the site's, the same one its owner pays supplies for.
- Test `Careers.SitesGetBuildersAtTheOffice`: the seed 7 / 200 robots
  city, a workplace building put on a site by hand (`Raise`, 60 nights
  left) with a site office written onto it by a new `AddOffice` helper
  as `startSite` makes it - floor 0, "Site Office", capacity 6, the
  project's owner, its id allocated so the building's rooms stay sorted
  by id - because the shortage that starts a real site is read off seven
  saved nights, so a careers test writes the room instead of waiting.
  Everyone whose job is a workspace is laid off (`LayOff`), which leaves
  the office one of few openings with many robots asking for one: over
  14 midnights the office fills its crew of six, and no robot works in
  the site's workspaces, so the D2c skip still holds. The bound asserts
  3 builders, half what the city reached, for the random stream to move
  in a later step (the guide's "leave margin").
- What the step's text gets ahead of itself: it says a `SiteOffice` room
  exists only while its project is going up, which only becomes true when
  D3d turns the office into a vacant unit. Until then an office outlasts
  its project - the reference city's save has 3 such rooms, 2 of them
  with a builder in - and this step keeps hiring for them. Nothing invalid
  happens (`ValidateWorldState` asks only that a job room holds at most
  `JobsAt` robots), and D3d is the step that clears them.
- No save change: `SiteOffice` shipped in v24 (D3a), `projects` in v23
  (D2a) and `lots` in v22 (D1a), so no field, key, migration or fixture,
  and `Validation` needed nothing new. No new random draw: the openings
  list is built from `rooms_` vectors in a fixed order and `staff` is
  only ever looked up, never iterated (rule 1); wages move by `PayWage`
  through `PayOrBorrow`, the office's owner being the project's owner
  (rule 2).
- Budget: `careers` 22690 to 24050 of 25000, `world` 24485 to 24491
  (the `OpeningsFor` comment); no new source file, so
  `scripts/context_budget.py` is unchanged. Warning for the steps after
  this: `careers` has 950 tokens left and `world` 509, so a step that
  adds to `CareersTests.cpp` or `World.hpp` must make room first.
- ctest: 193/193 including ContextBudget (192 before, +1 new test).
- Simulation code changed, so the headless check ran: seed 1, 10000 robots,
  40 days `--quiet` in 322 s, and the money adds up on all 40 days (`money`
  equal to `created in all`, `destroyed 0` every day, checked line by line).
  Day 40 reads `created 1775 today`, wages at 110%, 28 of 10000 out of work,
  6004 vacant apartments, 26 empty units, supplies 924882, gini 505, 1067
  broke robots, 13 behind on rent - the shape D3b's note quotes (created
  2194, wages at 108%, 6005 vacant, 22 empty units, supplies 919058, gini
  504, 1080 broke, 10 behind), wages up and `created` down because the
  sites' builders now earn and the homes those sites raise are let. The
  save of that run (same seed and days with `--save`) holds the same 9
  site offices, 6 projects still going up and 41 of the city's 50 plots
  D3b's note counted, and 2 robots working at an office - both at one of
  the 3 offices whose project had already finished, which is what step
  D3d turns into a vacant unit. A city this busy hires nobody there:
  day 40 reads 28 of 10000 out of work and 26 finding a job a day, and
  an office is one opening among thousands, so the builders' test has
  to make jobs short by hand. The seed 7, 2000 robots, 6 days `--all
  --save` run is unchanged - that city raises no site (its save has 0
  projects, 0 `siteOffice` rooms and all 31 plots), so the step changes
  nothing where there is no site.

### - [x] D3d. A finished site's office becomes a vacant unit (construction)

Open: `Construction.cpp` (the review's end-of-project step),
`ConstructionTests.cpp`, plus the lines of `Units.cpp` that hold `CloseUnit`
— the layoff pattern to copy, no change to that file.

- When a project's nights are used up, its office's workers are laid
  off as `CloseUnit` does (workspace cleared, `commuteAt = kNoCommute`),
  and the room's kind becomes `VacantStore`, which `ReviewUnits` may
  then reopen as a venue or a workspace like any other empty unit.
- A room changes kind only when nobody is in it or on the way to it:
  the layoff must clear a builder whose destination is the office.
- Tests: a finished site has a vacant unit where its office was, no
  robot works there, `ValidateWorldState` and `ValidateEconomy` after.

Done as (construction module, one commit): in `World::ReviewConstruction`,
between the advance loop and the erase of finished projects, a new loop walks
`state_.projects` in its saved order and, for every project whose `daysLeft`
has reached 0 - the site that just raised its last floor, and a site a save
left finished - asks its building for the one `SiteOffice` room and closes
it with `CloseUnit` (Units.cpp). The step's "the layoff pattern to copy" was
taken as calling that function instead of restating it: it lays off the
builders exactly as the step asks (workspace cleared, junior, no performance,
`commuteAt = kNoCommute`, a `LostJob` memory, home when it stood at work),
renames the room "Empty Unit" with `pricePercent` 100 and no visits or quiet
nights, bumps `cityVersion_` and records the closing (`UnitClosed`,
`today.venuesClosed`) - and one implementation keeps a closing office and a
closing venue honest with each other. `cityChanged` is set so `rooms_` is
rebuilt and the views reload the changed kind; `ReviewUnits` then finds the
room in its `VacantStore` branch and may reopen it as a workspace, a police
station or the most wanted venue.

- What the step's second bullet guards, and why it holds: a room may change
  kind only when nobody stands in it or is on the way to it. The review runs
  at midnight, when a site's 07:00-to-15:00 shift (D3c) is over: no robot
  stands in a site's office or walks to it then - a commute arrives near
  07:00 - so a builder's only link to the office at that moment is the
  departure it still plans that morning, and `CloseUnit`'s
  `commuteAt = kNoCommute` cancels exactly that trip. A robot literally
  mid-walk to a room would break `Arrive` (Behaviour.cpp throws "Robot
  arrived at a room it has no business in"), which is why the midnight
  timing matters; the comment in `Construction.cpp` says so.
- No save change: `SiteOffice` shipped in v24 (D3a), `projects` in v23 (D2a)
  and `lots` in v22 (D1a): no field, key, migration or fixture, and
  `Validation` needs nothing new - a room is checked by floor, capacity and
  id, not by a kind table. No new random draw of the city's own: the saved
  stream moves only where builders are laid off, because `CloseUnit` calls
  `PlanAtHome`, which draws the resting robot's next outing - as any
  closing venue already does.
- First make room, as D2c3 did: the new test put `tests/ConstructionTests.cpp`
  at 10034 tokens, over the 10k file limit, so the two tests that read the
  office itself - D3b's `AStartedSiteHasASiteOffice` and the new one - moved
  to a new `tests/SiteOfficeTests.cpp` (suite `SiteOffice`), which joins the
  `construction` module: the module total is 23853 of 25000, so unlike
  D2c3's split it needs no new module. `tests/CMakeLists.txt` and
  `scripts/context_budget.py` gained the file; `docs/MODULES.md` the module
  row (20k to 24k) and the `Construction.cpp` row.
- Test `SiteOffice.FinishedSiteOfficeBecomesAVacantUnit`: the seed 7 / 200
  robots city, a workplace building put on a site by hand (`Raise`, 20 nights
  left) with its office written on by a new `AddOffice` helper as `startSite`
  makes it, and every workspace worker laid off (`LayOff`) so the office is
  one of the few openings - D3c's builders test setup, with the nights short
  enough to end. Over 14 midnights the office fills its crew: `builders >= 3`
  (D3c measured six; the bound leaves margin for the stream to move in a
  later step), so the layoff below bites on real builders. The run continues
  to the twentieth midnight, where the project is gone; then the office room
  reads `VacantStore` named "Empty Unit", the building has no `SiteOffice`
  room left, no robot works at the office, and `ValidateWorldState` and
  `ValidateEconomy` pass. The helpers (`WorkplaceAndOwner`, `Raise`,
  `AddOffice`, `LayOff`, `SiteOffice`, `RunUntilSiteOf`, `BuildingWithId`)
  are local copies, as the split forbids reaching into another test file.
- ctest: 194/194 including ContextBudget (193 before, +1 new test; D3b's
  test moved, not added).
- Simulation code changed, so the headless check ran: seed 1, 10000 robots,
  40 days `--quiet` in 286 s, money equal to `created in all` with
  `destroyed 0` on all 40 days (checked line by line). Day 40 reads
  `created 700 today`, wages at 110%, 29 of 10000 out of work, 6004 vacant
  apartments, 29 empty units, supplies 926911, gini 505, 1071 broke robots,
  11 behind on rent - the shape D3c's note quotes (created 1775, wages at
  110%, 28 out of work, 6004 vacant, 26 empty units, supplies 924882, gini
  505, 1067 broke, 13 behind), with three more empty units: the same 9 sites
  raised and 6 projects still going up, and the 3 offices whose projects had
  already finished (D3c's note) are now ordinary empty units - the day-40
  save holds 6 `siteOffice` rooms, one per going-up site, 30 `vacantStore`
  rooms all named "Empty Unit" and 41 of the city's 50 plots (the log's
  29 is counted by `RecordStatistics` before that midnight's review). And
  the seed 7, 2000 robots, 6 days `--all --save` run is byte for byte the
  same log, except its own timing line, and the same save as before this
  step: that city raises no site (its save has 0 projects and all 31 plots),
  so the step changes nothing where there is no site.

### - [x] D4. Drawing new buildings (view-core)

The static batches are built once (`CityStatic.cpp`). Add
`CityView::AddBuilding` and `RemoveBuilding` that edit `staticBatches_`,
`shapes_`, `buildingBlocks_`, facades and signs; call them from
`UpdateCity` when `cityVersion` changes and buildings differ. A building
under construction is a scaffold of thin boxes, one floor per
`floorsDone`.

Done as (view-core module, one commit): `CityView.cpp` gained
`AddBuilding` and `RemoveBuilding` (declared in `CityView.hpp` beside
`BuildStaticBatches`), and `CityStatic.cpp` a `BatchStaticsFor` that appends
the items just pushed into `statics_` as batches owned by one building id,
so a new building's statics hide with its block while it is open, exactly
like the generated ones.

- `AddBuilding` places the building where the constructor's loop would:
  its slot is the number of *other* buildings at its entrance node, which
  for a construction building is the corner its project consumed (D1a: a
  node's buildings hold its first slots in id order, and a project takes
  the node's lowest free corner), with the same far-ring rule past four.
  A finished one gets the block (`buildingBlocks_`), the facade, the big
  street-side sign and a unit sign per ground-floor unit, as generated
  buildings do.
- A building under construction — one whose rooms include a `SiteOffice`
  (the kind exists only while its project is up, D3d) — is drawn as a
  scaffold instead: one thin `kConcrete` slab per finished floor at
  `kSlabMeters`, no block, no facade, no signs. `BuildingShape` gained a
  `scaffold` flag so `UpdateCity` can tell what it drew: it diffs every
  building against `shapes_` and redraws (remove + add) those whose floors
  count or scaffold flag moved — a site therefore regrows its scaffold
  every night it raises a floor, and becomes a finished building the
  midnight its project ends. Buildings the simulation no longer has are
  dropped by id.
- Signs: `NeonSign.unit` and `Neon.unit` changed from an index into
  `unitSigns_` to the unit's room id, because `RemoveBuilding` erasing
  unit signs would otherwise shift every later index and light the wrong
  rooms; `UnitIsVenue`/`UnitColor` read the room through `rooms_` now, and
  the constructor's sign-panel lambda became the shared `AddSignPanel`.
- What the step left open, and what was chosen: a new building's neon
  signs glow by their material but get no point light — `AddLights` runs
  once at startup and the step does not ask to move lights; the user
  sees the difference in game.
- No save change, no simulation change: only `app/` files moved, so
  `mechapunk-sim.exe` was not relinked and the headless log and save
  cannot move (as in D1b). No new source file, so `scripts/context_budget.py`
  is unchanged; `docs/MODULES.md` gained the view-core size (18320 to
  20971 of 25000) and the new functions' rows.
- ctest: 194/194 including ContextBudget (no new test: the view needs a
  live renderer and GL context, so it is checked by building, by
  ContextBudget and by the user in the game, per the guide).

### D5. Construction details — split on 2026-11-26

The step names two modules, `inspector` and `statistics`, but its bullets live
in four: the frame data is `ui` (`app/SimulationHost.hpp/.cpp`), the testable
lists are a new `project-lists` module (as `organization-lists`, `gang-lists`
and `chronicle-lists` are their own), the page and its links are `inspector`,
and the Property tab's section is `statistics`. Cut into D5a to D5e, one module
and one commit each, the original bullets living on inside them.

Warning for the steps after this: `inspector` stands at 23237 of 25000, 1.7k
short, so the step that adds `app/InspectorProject.cpp` must make room first.

### - [x] D5a. The projects in `WorldFrame` (ui module)

Open: `app/SimulationHost.hpp`, `app/SimulationHost.cpp`.

- `WorldFrame` gains the projects (a shared list, copied only when they
  change): the guide's section 9, item 5, a shared list in `WorldFrame` by id
  and never by a look-alike.
- `ReviewConstruction` is the only code that touches `state_.projects`
  (Construction.cpp) and it runs at midnight (World.cpp), so frames of a night
  share one copy and the copy is made at the night's first minute, the way
  `history_` and `owners_` already are.
- No `RobotFrame` field: a builder's project is found through its job room's
  building id (D5b's list function), not by an id carried per robot.
- No save change, no simulation change: only `app/` files move.

Done as (ui module, one commit): `WorldFrame` gained
`std::shared_ptr<const std::vector<sim::Project>> projects` beside the
chronicle, `SimulationHost` the members `projects_` and `projectsDay_`, and
`Publish` a block that recopies the list when `sim::StartOfDay(frame.tick)`
moved on - the same test the economy block uses for `history_` and `owners_`.
`SimulationHost.hpp` includes `mechapunk/sim/Construction.hpp` for the type.

- Why the day boundary is the right trigger, checked rather than assumed:
  `state_.projects` is written only in `Construction.cpp` (the `push_back`
  that starts a site, the advance loop's floors and `daysLeft`, the `erase_if`
  that ends one), called only from `World.cpp`'s second midnight block, and
  `Step` raises `state_.tick` before that block, so the frame published at a
  midnight minute is the first that has to show the new list.
- No new test: `SimulationHost` runs the world on a worker thread and is in
  no test target - the `ui` module has none, as for the chronicle's and the
  gangs' frame fields. The data is tested where it is read: D5b's `ProjectLists`
  tests build a `WorldFrame` by hand, projects included.
- Budget: no new source file, so `scripts/context_budget.py` is unchanged;
  `ui` 16176 to 16518 of 25000. `docs/MODULES.md` gained the projects in the
  `SimulationHost` row, and the guide's task table row for D its modules.
- ctest: 194/194 including ContextBudget. Only `app/` files changed, so
  `mechapunk-sim.exe` was not relinked and the headless log and save cannot
  move (as in D1b and D4).

### - [x] D5b. `ProjectLists`: the rows a project page shows (new project-lists module)

Open: a new `app/ProjectLists.hpp/.cpp` (the `mechapunk_layout` library), a
new `tests/ProjectListsTests.cpp`, `app/OrganizationLists.hpp/.cpp` and
`tests/OrganizationListsTests.cpp` as the model, `app/SimulationHost.hpp`
(only `WorldFrame`).

- `ProjectLists` (plain, tested): builders of a project (robots whose job
  is its site office - the robot's `workspace` room, that room's building,
  the project on that building), projects of an organization, the lot or
  building a project is on.
- Tests for every list function; add the new files to a new `project-lists`
  module in the budget script, and the module and its rows to
  `docs/MODULES.md`.

Done as (project-lists module, one commit): `app/ProjectLists.hpp` declares
four plain functions over the published frame (step D5a's `WorldFrame::projects`)
and the city's `RoomIndex`, and `app/ProjectLists.cpp` implements them:

- `BuildersOf(frame, rooms, building)` - the robots whose job room is a
  `SiteOffice` room of that building, by robot id. The step's chain (the
  robot's `workspace` room, that room's building, the project on that
  building) is read from the room instead: the office is the only room of a
  site anyone works in (D2c keeps the other rooms from hiring, D3d closes
  the office when the project ends), so the room's kind names both the
  building and the fact it is going up, and no per-robot project lookup is
  needed.
- `ProjectsOf(frame, owner)` - the sites of one organization, in the saved
  order (by building id), which is the order the page shows them in.
- `ProjectOn(frame, building)` - the binary search `IsUnderConstruction`
  (Construction.hpp) does, over the frame's list: the building page's
  "Being built" line and a builder's project link ask this direction.
- `LotOf(frame, project)` - the building, its entrance node and the corner
  of that node it occupies. What the step's "the lot or building a project
  is on" left open: the lot is gone from `City::lots` from the night the
  project starts (D2b erases the one it takes), so the corner is derived
  the way the view places buildings - a node's buildings take its first
  slots in id order (D1a) - which is exactly the lot the project consumed.
- What the step left open, and what was chosen: the functions take the
  project's *building id*, not the project: the page holds a `Project` and
  asks by `project.building`, and `ProjectOn` answers the other way for the
  pages that start from a building or a robot. A frame before the first
  project list (`projects` null), no city, an invalid id or a project on a
  building the city does not have give empty lists and a null building,
  never a crash.
- Tests: 5 in the new `tests/ProjectListsTests.cpp` (suite `ProjectLists`),
  none can skip. `BuildersAreListedUnderTheirSite`: three robots hand-set
  to the office of a site raised by hand (`Raise` and `AddOffice`, as
  `SiteOfficeTests.cpp` has them), listed by id; a finished building and
  no id at all have none. `ProjectsOfOwnerInSavedOrder`: the trust's habitat
  and an employer's workplace, each listed under its owner alone, an
  unknown owner and none empty. `ProjectOnFindsTheSite`, and
  `LotOfProjectNamesTheCorner` against the test's own count of the node's
  buildings with a smaller id, a project on a building the city lacks
  reading as nowhere. `NoProjectsYetGivesEmptyLists`. The frame is built
  by hand from a generated seed 3 / 300 robots city, as
  `OrganizationListsTests.cpp` shows, with the projects copied into it as
  `Publish` does.
- No save change, no simulation change: only `app/` and `tests/` files
  moved, so `mechapunk-sim.exe` was not relinked and the headless log and
  save cannot move (as in D1b, D4 and D5a). No random draw and no money
  movement: the lists only read the frame.
- Budget: the new `project-lists` module in `scripts/context_budget.py`
  (5040 of 25000); `CMakeLists.txt` gained `app/ProjectLists.cpp` in
  `mechapunk_layout` and `tests/CMakeLists.txt` the test file; `docs/MODULES.md`
  the module row, the `ProjectLists` row and the task row. No existing
  module grew, so the `inspector` warning above still stands for D5c.
- ctest: 199/199 including ContextBudget (194 before, +5 new).

### - [x] D5b2. A site's cost and reason as saved state (construction + saves)

Added on 2026-11-27, before D5c, because the page D5c draws asks for two
numbers `Project` does not save - what a site has cost so far and why it
was started - and D5c's own text says saving either is a save change. The
user decided the fields come first, so this step is that save change and
D5c is left to draw what it can read.

Open: `Construction.hpp`, `Construction.cpp` (`startSite` and the advance
loop), `Save.hpp`, `Save.cpp` (the enum names, the migration list),
`Migrations.hpp`, the end of `Migrations.cpp`, `Validation.cpp`,
`SaveTests.cpp`, plus the fixture and its README row.

Done as (construction + saves, one commit): `Project` gains `costSoFar`,
the credits the site has paid for its supplies since the night it started,
and `reason`, the shortage that started it; save version 25, game 0.25.0.

- `reason` is `ProjectReason {Unknown, HomesShort, JobsShort}` in
  `Construction.hpp`, named in `Save.cpp` the way `RoomKind` is:
  `"unknown"`, `"homesShort"`, `"jobsShort"`. An enum rather than a free
  string, so a save naming no known reason fails at the read and the page
  can say which of the two shortages it was.
- `startSite` takes the reason and records it: `HomesShort` where the
  market's homes ran short, `JobsShort` where jobs did.
- The advance loop adds what the site was charged that night to
  `costSoFar`: it reads the owner's credits before and after the existing
  `PaySupplies` call and adds the difference, which is the money that
  really moved - the owner's sector share of the 200 a site asks, half for
  the housing trust and nothing for an industry corporation. No new
  accounting path and nothing counted twice (rule 2); no new random draw
  (rule 1), so the saved stream is untouched.
- `V24ToV25` (declared in `Migrations.hpp`, registered in `kMigrations`,
  at the end of `Migrations.cpp`) gives every project of a version 24
  save `"costSoFar": 0` and `"reason": "unknown"`. That game paid the
  supplies into the night's books without naming a site, and read the
  shortage off seven saved nights without recording which of the two it
  saw, so the migration cannot know either and must not invent it: an
  old project reads as a site that has paid nothing yet for reasons the
  save never told.
- Validation (rule 5): a project's `costSoFar` cannot be negative. The
  reason needs no check - an unknown name fails at the read.
- Fixture `tests/fixtures/saves/v25-city.json` from the guide's command
  (seed 33, 100 robots, 28370 minutes) at game 0.25.0, plus its row and a
  sentence in that folder's README. Like the v24 one it raised no site,
  so its projects list is empty and the new keys appear in no fixture:
  the hand-written saves are what prove them.
- Tests: `Save.ProjectCostAndReasonRoundTrips` writes a project with cost
  500 and reason `"jobsShort"` into a current save, reads both back and
  serializes them again; `Save.Version24FixtureMigratesAndRuns` checks the
  v24 fixture names neither key (`"reason"` alone would prove nothing, an
  emission has one, so the test looks for `"costSoFar"` and for
  `"unknown"`), loads it and runs it a day, then writes a project into
  that fixture as a version 24 game would - four keys - and reads the
  loaded project as cost 0 and reason `Unknown`;
  `CurrentVersionFixtureLoadsAndRoundTrips` points at v25.
  `SiteSupplies.SiteCountsItsCostAndNamesItsReason` runs the seed 5 /
  1000 robots city to its first habitat: the site raised that midnight
  has paid nothing, and two nights later it has paid 100 a night, the
  trust's half of the ask, with the reason `HomesShort`; the two trigger
  tests read their own reason.
- First make room, as D1a, D2b, D2c3 and D3d did: `construction` stood at
  23853 of 25000 and this step's additions put it at 25045, so the two
  tests of what a site pays - D2b's `TheOwnerPaysTheSuppliesTheSiteCosts`
  and the new one - moved to a new `tests/SiteSuppliesTests.cpp` (suite
  `SiteSupplies`), which joins a new `site-supplies` module: `construction`
  24031, `site-supplies` 15709, `saves` 20823 to 22647. Warning for the
  steps after this: `construction` has 969 tokens left.
- ctest: 202/202 including ContextBudget (200 before this step, +2 new;
  the two moved tests were not added - the count the task file last
  recorded, 199, was one behind the tree).
- Simulation code changed, so the headless check ran: the seed 1, 10000
  robots, 40 days `--quiet` log adds up money on all 40 days and reads
  day 40 as D3d's note quotes it; the seed 7, 2000 robots, 6 days
  `--all --save` run is byte for byte the same log, except its own timing
  and save-path lines, and its save's `world` is equal to the old one
  as parsed JSON - only `version` and `gameVersion` moved, and that city
  raises no site, so no project gains a key.

### - [x] D5c. The project page (inspector module)

Open: a new `app/InspectorProject.cpp`, `app/InspectorInternal.hpp`,
`app/Inspector.cpp` (the dispatch), `app/Links.hpp/.cpp`, `app/CityView.hpp`
(only `Selection`), `app/main.cpp` (only the block that applies requests),
`app/ProjectLists.hpp`.

- `Selection` gains `project`; `ProjectLink` in `Links`, labelled
  `"<text>##project<id>"`; `DrawInspector` dispatches to `DrawProject`.
- First make room, as D1a, D2b and D2c3 did: `inspector` stands at 23237 of
  25000.
- **Project page**: what is built (habitat or workplace) and where
  (`BuildingLink`); owner (`OrganizationLink`); progress (floors done of
  floors, days left, as a bar); what it has cost so far and costs a night;
  why it was started (low vacancy or too few jobs, with the number);
  builders as a clipped table (name, level, shift, now).
- What the step's text assumes: the floors still to come are readable as
  `floorsDone` plus `daysLeft`, since a site raises one floor a night; what
  the site has cost so far and why it was started are saved now - `Project`'s
  `costSoFar` and `reason`, step D5b2, save version 25 - so nothing about
  this page needs asking the user again. A project an older save started
  reads as cost 0 and reason `unknown`, which is what the page says.

Done as (project-pages module, one commit): `Selection` (CityView.hpp) gained
`project`: the id of the building whose project is raising it, the way
`WorldState` and `WorldFrame::projects` key the sites (step D5a's rule: by
id, never by a look-alike); the camera and the cutaway ignore it, as they
ignore the gang. `Links.hpp/.cpp` gained `ProjectLink`: the site's building
name ending in `"##project<id>"`, so it never shares an ImGui id with the
same building's `"##building<id>"` link; a click shows the project's page
and opens the building at its ground floor, so the player reads the facts
and sees the scaffold, and a building whose project ended since the click
reads as disabled "no site there" the way an unknown gang does in `GangLink`
(the frame's list answers through `ProjectOn`, ProjectLists.hpp). `DrawInspector`
dispatches to `DrawProject` - declared in `InspectorInternal.hpp` the way
`DrawGangPage` is - after the gang branch and before the building page, and
a project alone opens the window. `ApplySelection` (main.cpp): the building
branch puts `project` back the way it puts `gang` back (a project link names
its building and its project together), and a page selected in place clears
it with the gang.

- The page, in a new `app/InspectorProject.cpp`: the building's name as its
  title with "a habitat"/"a workplace" beside it, read from the building's
  own `kind` (City.hpp) rather than guessed from its rooms; "Where:" a
  `BuildingLink`; "Owner:" an `OrganizationLink`; a Progress section -
  `ImGui::ProgressBar` (the robot page's widget for needs and wear) of
  `floorsDone` over `floorsDone + daysLeft`, overlaid "3/7", with the line
  "N floors done of M, K nights left": the step's assumption that the floors
  still to come are `daysLeft` holds, since a site raises one floor a night
  (Construction.cpp); a Cost section - `costSoFar` and the 200 cr a night
  the site asks, with a comment that the two read alike only for the
  housing trust, whose share `PaySupplies` moves; a "Why it started" line
  naming the rule with its share - under 25 per cent of the market's homes
  empty, or over 5 per cent of the robots out of work, seven nights running
  (the step's "with the number": the number of the night that fired is not
  saved, so the page names the rule) - and "not recorded: an older game
  started this site" for `Unknown`; and the Builders as a clipped table
  (name a `RobotLink`, level through `JobTitle`, shift through `JobShift`
  read over the office as the organization page reads a worker's, now) from
  `BuildersOf` (ProjectLists.hpp, D5b).
- The step's "First make room" bullet, and what was done instead: the text
  assumed the page would join `inspector`, 23237 of 25000. The C4c precedent
  fits better - that step moved its history pages into a `chronicle-pages`
  module rather than crowd the inspector - so the new file joined a new
  `project-pages` module with the headers it leans on (`InspectorInternal.hpp`,
  `Links.hpp`, `ProjectLists.hpp`, `Inspector.hpp`) and no source file moved:
  `inspector` grew only by the dispatch, the declaration and the link, 23237
  to 23856 of 25000, `project-pages` 6465. Warning for D5d: `inspector` has
  1144 tokens left and `Inspector.cpp` stands at 9653 of the 10k file limit,
  so D5d must make room first.
- No save change, no simulation change: only `app/` files moved, so
  `mechapunk-sim.exe` was not relinked and the headless log and save cannot
  move (as in D1b, D4, D5a and D5b). No random draw and no money movement:
  the page only reads the published frame.
- No new test: the page needs a live renderer and a GL context, so it is
  checked by building, by ContextBudget and by the user in the game (as D4
  and D5a did); the data it shows is what D5b's `ProjectLists` tests already
  pin down. ctest: 202/202 including ContextBudget, unchanged.
- Budget: `view-core` 20971 to 21041, `view-robots` 22475 to 22545 and
  `view-interiors` 12136 to 12206 (the `Selection` field in `CityView.hpp`,
  which all three modules read), `ui` 16518 to 16566 (the `ApplySelection`
  lines). `CMakeLists.txt` gained `app/InspectorProject.cpp`; `scripts/context_budget.py`
  the `project-pages` module; `docs/MODULES.md` the module row, the
  `InspectorProject.cpp` row and the task row; the guide's task table row
  for D its new module and its progress.

### - [ ] D5d. Links back to the project (inspector module)

Open: `app/Inspector.cpp` (the building page and the robot page),
`app/InspectorOrganization.cpp`, `app/ProjectLists.hpp`.

- **Links back**: a building under construction says "Being built:
  <ProjectLink>" and lists its builders; the organization page gets a
  "Building now" list; a builder's robot page shows its project as a link.

### - [ ] D5e. The Construction section (statistics module)

Open: `app/Statistics.cpp` (the Property tab), `app/StatisticsInternal.hpp`
if a helper is shared, `app/ProjectLists.hpp`.

- **Statistics**: a "Construction" section in the Property tab: projects
  started, finished, builders employed, by day, and a table of current
  projects with links.
- What is not saved: `DailyStats` and `DayCounters` count no construction
  (no projects started or finished per day), so "by day" needs new saved
  counters, and that is a save change of its own (guide rule 3): version
  26, a `V25ToV26` at the end of `Migrations.cpp`, a new fixture and its
  tests, the way step D5b2 did version 25. The counters go in `DailyStats`
  - every `world.history[i]` gains the keys - not in `Project`, which
  step D5b2 already grew. Decided, not asked: until that step lands this
  section draws only what the current projects allow.

## Done when

- [ ] New homes and workplaces are built when short, by builders, paid from
  supplies; money adds up over 40 days at 10000 robots.
- [ ] The view shows scaffolds and finished buildings without a restart.
- [ ] Every project has an inspector page with its builders, linked from its
  building, owner and builders.
- [ ] The guide's task table marks D done.
