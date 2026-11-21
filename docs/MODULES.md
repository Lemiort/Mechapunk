# Modules: what to open for a task

Mechapunk is split so that any one task fits a small model's context (about
57k tokens). A **module** is what a task needs open at once: its sources, the
headers it leans on, and its tests. Each is at most 25k tokens, leaving room
for the instructions, the guide's section and the work. Open one module, plus
`docs/IMPLEMENTATION_GUIDE.md` and the task file in `docs/tasks/` for the
step you are doing, and nothing else unless a compile error points
elsewhere.

`scripts/context_budget.py` holds the list below and checks it (ctest runs it
as `ContextBudget`): every source file belongs to a module, no file is over
10k tokens, no module over 25k. Sizes are bytes / 3.5.

| Module | Tokens | Files |
|---|---:|---|
| world | 21k | `World.hpp`, `WorldInternal.hpp`, `World.cpp`, `Behaviour.cpp`, `Residents.cpp`, `WorldTests.cpp` |
| generation | 22k | `World.hpp`, `WorldInternal.hpp`, `Generation.cpp`, `City.hpp`, `City.cpp`, `Names.hpp`, `CityTests.cpp` |
| validation | 11k | `World.hpp`, `Validation.cpp`, `Describe.cpp` |
| money | 22k | `World.hpp`, `Economy.hpp`, `EconomyInternal.hpp`, `Economy.cpp`, `Money.cpp`, `Settlement.cpp`, `EconomyTests.cpp` |
| property | 22k | `World.hpp`, `Economy.hpp`, `EconomyInternal.hpp`, `Founding.cpp`, `Units.cpp`, `Property.cpp`, `HousingTests.cpp` |
| statistics | 15k | `Economy.hpp`, `EconomyInternal.hpp`, `EconomyStatistics.cpp`, `Statistics.hpp`, `Statistics.cpp` |
| careers | 18k | `World.hpp`, `Careers.hpp`, `Careers.cpp`, `Venues.hpp`, `Venues.cpp`, `CareersTests.cpp` |
| social | 21k | `World.hpp`, `Social.hpp`, `Social.cpp`, `LifeCycle.cpp`, `SocialTests.cpp`, `LifeCycleTests.cpp` |
| metro | 11k | `Metro.hpp`, `Metro.cpp`, `Transit.cpp`, `MetroTests.cpp`, `TransitTests.cpp` |
| saves | 19k | `Save.hpp`, `Save.cpp`, `Migrations.hpp`, `MigrationsInternal.hpp`, `Migrations.cpp`, `SaveTests.cpp` |
| saves-frozen | 5k | `Migrations.hpp`, `MigrationsInternal.hpp`, `MigrationsEarly.cpp` |
| basics | 4k | `GameTime.hpp`, `Ids.hpp`, `Random.hpp`, `Weather.hpp`, `GameTime.cpp`, `Random.cpp`, `Weather.cpp`, `RandomTests.cpp`, `WeatherTests.cpp` |
| runner | 11k | `World.hpp`, `main.cpp` |
| view-core | 18k | `CityView.hpp`, `CityViewInternal.hpp`, `CityView.cpp`, `CityStatic.cpp`, `CityLights.cpp` |
| view-robots | 22k | `CityView.hpp`, `CityViewInternal.hpp`, `CityRobots.cpp`, `CityMetro.cpp`, `CityDrivers.cpp`, `Metro.hpp` |
| view-interiors | 11k | `CityView.hpp`, `CityViewInternal.hpp`, `CityInteriors.cpp`, `Interior.hpp` |
| floor-plans | 19k | `Interior.hpp`, `InteriorKit.hpp`, `Interior.cpp`, `InteriorHomes.cpp`, `InteriorStores.cpp`, `InteriorWorkplaces.cpp`, `LayoutTests.cpp` |
| rendering | 13k | `GameRenderer.hpp`, `GameRenderer.cpp`, `Props.hpp`, `Props.cpp`, `Rain.hpp`, `Rain.cpp`, `OrbitCamera.hpp` |
| ui | 15k | `main.cpp`, `SimulationHost.hpp`, `SimulationHost.cpp` |
| organization-lists | 3k | `OrganizationLists.hpp`, `OrganizationLists.cpp`, `OrganizationListsTests.cpp` |
| inspector | 21k | `Inspector.hpp`, `Inspector.cpp`, `Links.hpp`, `Links.cpp`, `InspectorInternal.hpp`, `InspectorOrganization.cpp`, `InspectorGang.cpp` |
| gangs | 23k | `Gangs.hpp`, `Gangs.cpp`, `GangRosterTests.cpp`, `GangCrimeTests.cpp`, `GangLists.hpp`, `GangLists.cpp`, `GangListsTests.cpp` |
| chronicle | 8k | `Chronicle.hpp`, `Chronicle.cpp`, `ChronicleTests.cpp`, `World.hpp` |

## What lives where

### Simulation library (`sim/`, namespace `mechapunk::sim`)

Every `World` member is implemented in one of several source files, by
topic. Helpers shared between those files live in a private
`*Internal.hpp` next to them; helpers used by one file stay in its
anonymous namespace. `docs/MODULES.md` says which files to open for a task.

| File | What lives there |
| --- | --- |
| `World.hpp` | `Robot`, `WorldState`, `EventKind`/`SimEvent`, gangs (`Gang`, `Stance`), the chronicle (`ChronicleEntry`, declared in `Chronicle.hpp`), the `World` class (every member, grouped by the file implementing it) |
| `World.cpp` | Construction, `FromState`, `Step` (the midnight order), `RunFor`, `UpdateNeeds`, `IsGuestTrip`, `WalkOf` |
| `Behaviour.cpp` | A robot's day: `Act`, `Arrive`, `Decide`, `StartWalk`, commutes, `PlanAtHome`, officers' `Patrol` |
| `Generation.cpp` | `World::Generate`, `AssignDorm`, `AssignJob`, robot names |
| `Residents.cpp` | `RentOf`, `ResidentsOf`, `CountResidents`, `CollectRent` |
| `Describe.cpp` | `Describe`, `DescribeRoom`: event and room text |
| `Validation.cpp` | `ValidateWorldState` |
| `City.hpp/.cpp` | Streets, stations, buildings, rooms, `RoomKind`, `ApartmentClass`, `GenerateCity`, `RoomIndex`, `RoutePlanner` |
| `Venues.hpp/.cpp` | `VenueRules` per venue kind (hours, price, effects, capacity), `StaffRules` (also police and crew rooms), `FindStaffRules`, `StaffPerShift`, `JobsAt`, `ShiftSlot` |
| `Careers.hpp/.cpp` | Wages, promotions, firing, `ReviewJobs`, `ReviewHomes` (eviction, moving up, friends moving in together), `JobShift`, `JobKey`, `OpeningsFor` |
| `Gangs.hpp` | Gangs (`Stance`, `GangRelation`, `Gang`) |
| `Chronicle.hpp` | The saved story (`ChronicleEntry`, `kChronicleCap`) |
| `Chronicle.cpp` | `World::Chronicle`: which events `Emit` keeps in the chronicle |
| `Economy.hpp` | Organizations, loans, emissions, `DayCounters`, `DailyStats` |
| `Economy.cpp` | `SetCredits` (tests), `ValidateEconomy` |
| `Founding.cpp` | `FoundEconomy` (organizations, owners, starting money), `IndexEconomy` |
| `Money.cpp` | Accounts and all money movement: `Pay`, `ChangeMoney`, `Lend`, `Withdraw`, `PayBill`, `PayOrBorrow`, `Sell`, `PayWage`, budgets |
| `Units.cpp` | Prices and rents, `CloseUnit`, `OpenUnit`, `ReviewUnits`, `SetPrices` |
| `Property.cpp` | `Transfer`, `Bequeath`, `TradeProperty` (sales, mortgages) |
| `Settlement.cpp` | `SettleEconomy`, `RunCityHall`, `SetPolicyRate` |
| `EconomyStatistics.cpp` | `RecordStatistics`, `GiniPerMille`, sector and emission names |
| `Social.hpp/.cpp` | Interests, bonds, memories (`MemoryKind`, `MemoryText`), talks, `Remember` (also counts daily statistics) |
| `LifeCycle.cpp` | Aging, wear, `Scrap` (with `Bequeath`), `AddNewcomer` |
| `Transit.cpp`, `Metro.hpp/.cpp` | Trip planning, the metro network, timetable, trains, `PlanDriverRun` |
| `Weather.*`, `Random.*`, `GameTime.*`, `Ids.hpp` | As named |
| `Save.hpp/.cpp` | glaze JSON, enum names, the migration list |
| `Migrations.cpp` | Migrations from version 9 on: append new ones here |
| `MigrationsEarly.cpp` | Frozen migrations 1 to 9 |

### Game (`app/`, namespace `mechapunk`)

| File | What lives there |
| --- | --- |
| `SimulationHost` | Runs the World on a worker thread; publishes an immutable `WorldFrame` after each step: `RobotFrame`s, shared `RobotSocial`, `RobotDirectory`, `EconomyFrame` (organizations, loans, emissions, history, room owners; refreshed every 10 minutes), and `city` + `cityVersion` when rooms changed. |
| `CityView.hpp` | The `CityView` class: everything drawn |
| `CityView.cpp` | Construction, `UpdateCity`, `Submit` (the frame: robots, signs, trains, interior), picking, routes |
| `CityStatic.cpp` | Static batches: buildings, facades, parks, street lamps |
| `CityMetro.cpp` | Elevated lines, stations, stairs, platforms, trains, doors, `LegsOf`, `StationRoute` |
| `CityRobots.cpp` | Spots in rooms, ways out of rooms, `RobotPosition`, `FollowWalk` |
| `CityDrivers.cpp` | Metro drivers: `PlanDriver`, `DriverPlacement`, `CabOf` |
| `CityInteriors.cpp` | The open floor: `PlanOf`, `SubmitInterior`, props, unit signs |
| `CityLights.cpp` | `AddLights`, `UpdateLights` |
| `CityViewInternal.hpp` | Constants and helpers shared by the `City*.cpp` files (colours, `Accent`, sizes) |
| `Interior.cpp` | `PlanFloor` (which plan a floor gets), `WayOut` paths |
| `InteriorKit.hpp` | Sizes, walls, windows, `Frame`, spots, lamps: the pieces of every plan |
| `InteriorHomes.cpp` | Dorm rooms, studios, flats, penthouses |
| `InteriorStores.cpp` | Ground-floor stores: venues, empty units, police stations, crew rooms |
| `InteriorWorkplaces.cpp` | Workspaces fitted out for their trade |
| `Props` | Box models of every prop and the robot figure. |
| `Inspector` | Inspector window for the selected building, room or robot. |
| `Links.hpp/.cpp` | Clickable names in the inspector: `RoomLabel`, `ShowRoom`, `RoomLink`, `ShowRobot`, `RobotLink`. |
| `Statistics` | Statistics window (T): Overview, Economy, Society, Property, Organizations, Central bank tabs, plots from `DailyStats`. |
| `main.cpp` | Window, input, camera, panels, applying frames. |


## Which module for which task

| Task | Module | Start at |
| --- | --- | --- |
| How a robot acts, walks, commutes, visits; officers' patrols | world | `Behaviour.cpp` (`Act`, `Arrive`, `Decide`) |
| Needs, the minute step, the midnight order | world | `World.cpp` (`Step`, `UpdateNeeds`) |
| Rent collection, who lives where | world | `Residents.cpp` |
| New city layout, buildings, depots, stations at start | generation | `City.cpp` (`GenerateCity`), `Generation.cpp` |
| A new invariant, a validation error | validation | `Validation.cpp` |
| Event text in the log | validation | `Describe.cpp` |
| Payments, loans, wages, what robots can afford | money | `Money.cpp` |
| Nightly settlement, City Hall, taxes, the central bank | money | `Settlement.cpp` |
| Who owns what at start | property | `Founding.cpp` |
| Venues and stations opening or closing, prices | property | `Units.cpp` |
| Buying homes, mortgages, inheritance | property | `Property.cpp` |
| A new daily statistic and its plot | statistics | `EconomyStatistics.cpp`, `app/Statistics.cpp` |
| Jobs, shifts, hiring, firing, promotions, moving home | careers | `Careers.cpp`, `Venues.cpp` |
| Friends, rivals, memories, aging, scrapping, newcomers | social | `Social.cpp`, `LifeCycle.cpp` |
| Timetable, trains, trips, driver runs | metro | `Metro.cpp`, `Transit.cpp` |
| Any change to saved state (new version, migration, fixture) | saves | `Save.cpp`, end of `Migrations.cpp` |
| The headless runner's report | runner | `tools/sim_runner/main.cpp` |
| What the city looks like from outside; lights | view-core | `CityStatic.cpp`, `CityLights.cpp` |
| Where a robot is drawn; trains, platforms; drivers | view-robots | `CityRobots.cpp`, `CityMetro.cpp`, `CityDrivers.cpp` |
| Drawing the open floor of a building | view-interiors | `CityInteriors.cpp` |
| Furniture and layout of a room kind | floor-plans | `InteriorStores.cpp`, `InteriorHomes.cpp`, `InteriorWorkplaces.cpp` |
| Prop models, rain, renderer | rendering | `Props.cpp` |
| The inspector window and its clickable names | inspector | `Inspector.cpp`, `Links.cpp` |
| Which rows an organization page shows, sorted | organization-lists | `app/OrganizationLists.cpp` |
| Panels, input, camera; frames from the simulation thread | ui | `main.cpp`, `SimulationHost.cpp` |

A task touching two modules (a new robot field shown in the inspector, say)
is done in steps, one module at a time: the simulation first with its tests,
then the UI.

## Rules of the layout

- `World` is one class implemented across many files. Its header,
  `World.hpp`, groups the members by the file that implements them.
- Each area has one private header for helpers its files share:
  `sim/src/WorldInternal.hpp`, `sim/src/EconomyInternal.hpp`,
  `sim/src/MigrationsInternal.hpp`, `app/CityViewInternal.hpp`,
  `app/InteriorKit.hpp`. Each puts its helpers in its own namespace
  (`world_detail`, `economy_detail`, ...) so two areas can never clash. Only
  that area's files include it.
- A helper used by one file stays in that file's anonymous namespace. Move it
  to the private header (as `inline`) only when a second file needs it.
- Public headers in `sim/include/mechapunk/sim/` are the only way between
  areas.

## Splitting a file

When `ContextBudget` reports a file over budget:

1. Group its functions by topic (look at what they touch, not where they
   sit). Aim for files of 2k to 6k tokens.
2. Move each group to a new file whose first line says what it holds. Move
   code only: no renames, no edits. Helpers shared by groups go to the
   area's private header as `inline`; the others go with their one user.
3. Add the new files to `sim/CMakeLists.txt` or the root `CMakeLists.txt`,
   and to a module in `scripts/context_budget.py`.
4. Prove nothing changed: run the headless check before and after and
   compare the logs and saves byte for byte, ignoring the last two lines
   (timing and save path):

   ```bash
   build/build/Release/mechapunk-sim.exe --seed 7 --robots 2000 --days 6 --all --save before.json > before.txt
   # ... split, build ...
   build/build/Release/mechapunk-sim.exe --seed 7 --robots 2000 --days 6 --all --save after.json > after.txt
   cmp before.json after.json && diff <(head -n -2 before.txt) <(head -n -2 after.txt)
   ```
5. Update the tables in the guide's architecture map and here.
