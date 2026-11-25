# Task D: construction

Status: open. D1 split on 2026-11-24 into D1a and D1b; D1a is done and
raised the save version to 22, and D1b is done (the view draws parks from
the saved plots, no save change). D2 split on 2026-11-25 into D2a to D2c;
D2a is done and raised the save version to 23, and D2b is done (no save
change: `projects` and `lots` shipped in v23 and v22). Save change. The
biggest remaining task: take it slowly, one step per module.

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

### - [ ] D2c. A site's rooms cannot be rented or staffed until done (careers)

`OpeningsFor` and `ReviewJobs` skip rooms of a building under construction
(Careers.cpp); so do `ReviewHomes` and `AddNewcomer`'s choice of apartment.
If `ReviewUnits` (Units.cpp, property) counts a site's quiet nights or
reopens its rooms, split a D2d for that file.

### - [ ] D3. Builders (careers module)

A site office room in the new building: jobs like a workspace (`JobsAt`,
`OpeningsFor`) until the project is done, then it becomes a vacant unit.

### - [ ] D4. Drawing new buildings (view-core)

The static batches are built once (`CityStatic.cpp`). Add
`CityView::AddBuilding` and `RemoveBuilding` that edit `staticBatches_`,
`shapes_`, `buildingBlocks_`, facades and signs; call them from
`UpdateCity` when `cityVersion` changes and buildings differ. A building
under construction is a scaffold of thin boxes, one floor per
`floorsDone`.

### - [ ] D5. Construction details (inspector, statistics)

Follow the guide's section 9 (detail pages). Open: a new
`app/InspectorProject.cpp`, `app/ProjectLists.hpp/.cpp` and
`tests/ProjectListsTests.cpp`, `app/Links.*`, `app/Inspector.cpp`
(building page), `app/InspectorOrganization.cpp`, `app/Statistics.cpp`,
`app/SimulationHost.hpp/.cpp` (projects into `WorldFrame`).

- `WorldFrame` gains the projects (a shared list, copied only when they
  change); `Selection` gains `project`; `ProjectLink`.
- `ProjectLists` (plain, tested): builders of a project (robots whose job
  is its site office), projects of an organization, the lot or building a
  project is on.
- **Project page**: what is built (habitat or workplace) and where
  (`BuildingLink`); owner (`OrganizationLink`); progress (floors done of
  floors, days left, as a bar); what it has cost so far and costs a night;
  why it was started (low vacancy or too few jobs, with the number);
  builders as a clipped table (name, level, shift, now).
- **Links back**: a building under construction says "Being built:
  <ProjectLink>" and lists its builders; the organization page gets a
  "Building now" list; a builder's robot page shows its project as a link.
- **Statistics**: a "Construction" section in the Property tab: projects
  started, finished, builders employed, by day, and a table of current
  projects with links.
- Tests for every list function; add the new files to the budget script.

## Done when

- [ ] New homes and workplaces are built when short, by builders, paid from
  supplies; money adds up over 40 days at 10000 robots.
- [ ] The view shows scaffolds and finished buildings without a restart.
- [ ] Every project has an inspector page with its builders, linked from its
  building, owner and builders.
- [ ] The guide's task table marks D done.
