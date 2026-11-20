# Task D: construction

Status: open. Save change. The biggest remaining task: take it slowly, one
step per module.

## What and why

The city grows: when homes or jobs run short, organizations build new
buildings on free land, with builders who work there until it is done.

## Read first

`docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5; `docs/MODULES.md`
(modules `generation`, `property`, `view-core`). Construction gets its
own module: `Construction.hpp`, `sim/src/Construction.cpp`,
`tests/ConstructionTests.cpp`.

## Steps

### - [ ] D1. Land as simulation state (generation + saves)

Free corners are parks today; only the view knows them
(`rg -n "AddPark" app`). Make them state: `City::lots` (node and corner
index, sorted). `GenerateCity` fills them; the migration fills them from
the grid exactly as the view computes free corners (four corners per node,
minus taken ones; `NoIntersectionHasMoreThanFourBuildings` in
`CityTests.cpp` shows how corners are counted). The view draws parks from
`lots`. Save change, fixture, tests. Check the city looks the same.

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
