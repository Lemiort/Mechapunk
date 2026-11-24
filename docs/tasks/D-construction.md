# Task D: construction

Status: open. D1 split on 2026-11-24 into D1a and D1b; D1a is done and
raised the save version to 22, and D1b is done (the view draws parks from
the saved plots, no save change). Save change. The biggest remaining task: take
it slowly, one step per module.

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

### - [ ] D2. Projects (construction module)

`struct Project { EntityId building; EntityId owner; std::uint32_t daysLeft{};
std::uint32_t floorsDone{}; };` in `WorldState` (second save change of this
task, or fold into D1). Nightly `ReviewConstruction` after `ReviewUnits`:
home vacancy under 5% for 7 nights → the Habitat Trust starts a habitat on
the lot nearest the centre; jobs per robot under 1.05 → an industry
corporation starts a workplace. The building exists from the start with
`floors` growing; its rooms cannot be rented or staffed until done. Costs
are supplies paid every night (`PaySupplies`).

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
