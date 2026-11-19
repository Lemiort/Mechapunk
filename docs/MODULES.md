# Modules: what to open for a task

Mechapunk is split so that any one task fits a small model's context (about
57k tokens). A **module** is what a task needs open at once: its sources, the
headers it leans on, and its tests. Each is at most 25k tokens, leaving room
for the instructions, the guide's section and the work. Open one module, plus
the section of `docs/IMPLEMENTATION_GUIDE.md` for the task, and nothing else
unless a compile error points elsewhere.

`scripts/context_budget.py` holds the list below and checks it (ctest runs it
as `ContextBudget`): every source file belongs to a module, no file is over
10k tokens, no module over 25k. Sizes are bytes / 3.5.

| Module | Tokens | Files |
|---|---:|---|
| world | 21k | `World.hpp`, `WorldInternal.hpp`, `World.cpp`, `Behaviour.cpp`, `Residents.cpp`, `WorldTests.cpp` |
| generation | 21k | `World.hpp`, `WorldInternal.hpp`, `Generation.cpp`, `City.hpp`, `City.cpp`, `Names.hpp`, `CityTests.cpp` |
| validation | 10k | `World.hpp`, `Validation.cpp`, `Describe.cpp` |
| money | 22k | `World.hpp`, `Economy.hpp`, `EconomyInternal.hpp`, `Economy.cpp`, `Money.cpp`, `Settlement.cpp`, `EconomyTests.cpp` |
| property | 22k | `World.hpp`, `Economy.hpp`, `EconomyInternal.hpp`, `Founding.cpp`, `Units.cpp`, `Property.cpp`, `HousingTests.cpp` |
| statistics | 13k | `Economy.hpp`, `EconomyInternal.hpp`, `EconomyStatistics.cpp`, `Statistics.hpp`, `Statistics.cpp` |
| careers | 18k | `World.hpp`, `Careers.hpp`, `Careers.cpp`, `Venues.hpp`, `Venues.cpp`, `CareersTests.cpp` |
| social | 20k | `World.hpp`, `Social.hpp`, `Social.cpp`, `LifeCycle.cpp`, `SocialTests.cpp`, `LifeCycleTests.cpp` |
| metro | 11k | `Metro.hpp`, `Metro.cpp`, `Transit.cpp`, `MetroTests.cpp`, `TransitTests.cpp` |
| saves | 18k | `Save.hpp`, `Save.cpp`, `Migrations.hpp`, `MigrationsInternal.hpp`, `Migrations.cpp`, `SaveTests.cpp` |
| saves-frozen | 5k | `Migrations.hpp`, `MigrationsInternal.hpp`, `MigrationsEarly.cpp` |
| basics | 4k | `GameTime.hpp`, `Ids.hpp`, `Random.hpp`, `Weather.hpp`, `GameTime.cpp`, `Random.cpp`, `Weather.cpp`, `RandomTests.cpp`, `WeatherTests.cpp` |
| runner | 11k | `World.hpp`, `main.cpp` |
| view-core | 17k | `CityView.hpp`, `CityViewInternal.hpp`, `CityView.cpp`, `CityStatic.cpp`, `CityLights.cpp` |
| view-robots | 21k | `CityView.hpp`, `CityViewInternal.hpp`, `CityRobots.cpp`, `CityMetro.cpp`, `CityDrivers.cpp`, `Metro.hpp` |
| view-interiors | 11k | `CityView.hpp`, `CityViewInternal.hpp`, `CityInteriors.cpp`, `Interior.hpp` |
| floor-plans | 19k | `Interior.hpp`, `InteriorKit.hpp`, `Interior.cpp`, `InteriorHomes.cpp`, `InteriorStores.cpp`, `InteriorWorkplaces.cpp`, `LayoutTests.cpp` |
| rendering | 13k | `GameRenderer.hpp`, `GameRenderer.cpp`, `Props.hpp`, `Props.cpp`, `Rain.hpp`, `Rain.cpp`, `OrbitCamera.hpp` |
| ui | 24k | `main.cpp`, `Inspector.hpp`, `Inspector.cpp`, `SimulationHost.hpp`, `SimulationHost.cpp` |

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
| Inspector, panels, frames from the simulation | ui | `Inspector.cpp`, `SimulationHost.cpp` |

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
