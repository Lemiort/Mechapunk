# Task I: architecture

Status: all steps done on 2026-11-27 (I2 to I7 still to be looked at by the user in the game). Save
version is 27 and the game is 0.27.0. Budgets that are tight: `world` 24491,
`careers` 24405 and `construction` 24284 of 25000; after step I1b,
`generation` stands at 23626, the new `city-styles` at 13780 and `saves` at
20503. `view-core` has room (21041), `inspector` has room (19070),
`statistics` has room (21973).

## What and why

Every building in the city is the same box: `kBuildingWidth` by `kBuildingDepth`
metres, `floors` high, windows every 3.5 m on all four sides. The skyline is a
row of identical slabs, and the only thing that varies is the height. Robots
and money do not care what a building looks like, so this task adds a saved
**style** per building and makes the view draw it: massing (setbacks, twin
shafts, a plinth, a pitched or sawtooth roof, a crowned tower), facade rhythm,
and roof clutter.

The style is **purely architectural**: it changes no room, no job, no price,
no money and no walk. It must not change where a robot stands or what it does.

The style is chosen by a **pure function of data the city already has** -
`StyleOf(kind, id, floors)` - and never by a new draw from the city's random
stream. That is deliberate: the whole headless log then stays byte for byte
identical apart from the new saved key, so the determinism check of rule 1
proves the step changed nothing about the simulation. If the spread of styles
turns out ugly, only then consider drawing from the stream, and say so in the
commit body, because the city layout will then legitimately change.

## Steps

One module and one commit each. Steps that change saved state follow rule 3.

### - [x] I1a. Make room in generation and saves (pure moves)

- Split `tests/CityTests.cpp` by topic: buildings and their rooms stay in
  `CityTests.cpp` (module `generation`), the streets, the route planner and
  the free plots go to a new `tests/CityStreetTests.cpp`, module `city-streets`
  (`World.hpp`, `City.hpp`, the new file). Moved the frozen migrations V19ToV20
  through V25ToV26 out of `Migrations.cpp` into a new `sim/src/MigrationsMid.cpp`
  - the first draft said `MigrationsEarly.cpp`, but that file would pass the
  10k file budget - and the fixture tests of versions 19 to 24 out of
  `SaveFixtureTests.cpp` into `SaveFixtureTestsEarly.cpp` (with the `EditSave`
  helper, which only those tests use). Code moved only; the files went into
  `sim/CMakeLists.txt`, `tests/CMakeLists.txt` and `scripts/context_budget.py`,
  and the table in `docs/MODULES.md` was regenerated.
- After: `generation` 22954, `saves` 18838, `saves-frozen` 19503 of 25000.
  Proved nothing changed: `--seed 7 --robots 2000 --days 6 --all --save X`
  before and after, log and save byte for byte identical.

### - [x] I1b. A building's style is saved state (generation + saves)

- Open: `sim/include/mechapunk/sim/City.hpp` (the `Building` struct, next to
  `BuildingKind`), `sim/src/City.cpp` (`GenerateCity`, where a building is
  built), `sim/src/Save.cpp` (the `glz::meta` of `BuildingKind`, copy it),
  `sim/src/Save.hpp` (the version), the end of `sim/src/Migrations.cpp`,
  `sim/src/Migrations.hpp`, `tests/CityTests.cpp`, `tests/SaveTests.cpp`,
  `tests/fixtures/saves/README.md`.
- `enum class BuildingStyle : std::uint8_t { Plain, Setback, Stepped,
  TwinTowers, Plinth, Gabled, Sawtooth, Tower };` and `Building::style`.
  `StyleOf(BuildingKind, EntityId id, std::uint32_t floors)` in `City.hpp`,
  implemented in `City.cpp`: a hash of the id mixed with the kind and the
  floor count, mapped onto the styles that kind may have - a habitat never
  gets `Sawtooth`, a workplace never gets `Gabled` or `Tower`. No random
  draw, so the city's layout, its rooms and its story do not move.
- Save version 27, game 0.27.0. `V26ToV27` writes `"style"` for every
  `world.buildings[i]`, computed by the same `StyleOf` the generator uses:
  unlike the chronicle's account (task C, step C4e1), this fact is
  **derivable** from data the old save already holds, so the migration is
  honest rather than invented. New fixture `tests/fixtures/saves/v27-city.json`
  by the guide's command, its README row, `CurrentVersionFixtureLoadsAndRoundTrips`
  retargeted to v27, new `Version26FixtureMigratesAndRuns`.
- Rule 5: `Validation.cpp` checks nothing new (an unknown style name fails
  when the save is read), but say so in the step note.
- Tests: `City.StyleIsSettledAndSpreads` - over seeds 1, 7 and 33 at 1000
  residents, every building's style equals `StyleOf` of its own data, both
  kinds use more than one style, and no style is more than half of its kind's
  buildings; the style survives a save round-trip; the v26 fixture gains the
  style the same function gives it and runs a day.
- Headless: the log must be **identical** to the run before the step (no new
  draw); the seed 7 / 2000 / 6 days save differs only in the version lines and
  the new `"style"` key on every building.
- Done as written, with four notes. The floors a style needs are my choice,
  and the first draft's rule (a roof style needing a floor above the ground)
  left 23 of the 39 workplaces of seed 1's 1000-robot city plain, which fails
  the spread test: a one floor workshop with roof lights and a one floor house
  with a pitched roof are ordinary, so `Sawtooth` and `Gabled` are allowed
  from one floor up, `Setback` and `Plinth` from three and `TwinTowers` from
  two, and the styles spread. `V26ToV27` calls `StyleOf` - the one migration
  that reads game code - because the style is derivable from the id, kind and
  floor count the save already holds; the names it writes are spelled out in
  `Migrations.cpp` and must match `glz::meta<BuildingStyle>` in `Save.cpp`.
  `Validation.cpp` checks nothing new: an unknown style name fails when the
  save is read. The fixture README had no row for v26, left out by step D5e2;
  it has one now.
- The step did not fit one module as drafted: `City.cpp` passed the 10k file
  budget and `generation` the 25k module budget. Split by topic the way step
  I1a split `CityTests.cpp`: `StyleOf`, the massing tables and the hash went
  to a new `sim/src/CityStyle.cpp`, the style test to a new
  `tests/CityStyleTests.cpp`, and both files into a new module `city-styles`
  (`City.hpp`, `City.cpp`, `CityStyle.cpp`, `CityStyleTests.cpp`) in
  `scripts/context_budget.py`, with `docs/MODULES.md` regenerated. After:
  `generation` 23626, `city-styles` 13780, `saves` 20503 of 25000.

### - [x] I2. The block is drawn per style (view-core)

- Open: `app/CityStatic.cpp` (the building block and `AddFacade`),
  `app/CityView.cpp` (`BuildStaticBatches`, `AddBuilding`, `RemoveBuilding`,
  the picking box), `app/CityViewInternal.hpp` (colours, `Accent`, sizes),
  `app/CityView.hpp`.
- One massing per style, all inside the same footprint so picking, the open
  floor and the interiors keep working: `Plain` as today; `Setback` pulls the
  top third of the floors in by 1.5 m; `Stepped` pulls in twice; `TwinTowers`
  is two shafts with a gap between them; `Plinth` is a wider base two floors
  high under a narrower shaft; `Gabled` adds a pitched roof; `Sawtooth` gives
  a workplace a row of angled roof lights; `Tower` is a slim crown on the top
  floor.
- The scaffold of a building going up (task D, step D4) stays a stack of
  slabs: the style appears when the building is finished.
- No test is possible without a renderer: build, `ContextBudget`, and the
  user looking at the city.
- Done, with notes. The shapes are pure geometry in new `app/Massing.hpp/.cpp`
  (in `mechapunk_layout`), so `tests/MassingTests.cpp` checks them: every box
  and roof piece lies in the footprint and the first box is a full-width base
  from the ground. `app/CityMassing.cpp` turns them into statics. The step as
  drafted left three things out that it needed: (1) `AddFacade` now puts
  windows on each box's own walls, or windows would hang in the air beside a
  setback; the lit rule is unchanged for `Plain`; (2) the street sign and its
  neon follow `SignHeightMeters`, so they never float beside a setback, a
  plinth or between twin towers; (3) `buildingBlocks_` holds a list of boxes
  per building. `TwinTowers` has a full-width base of up to two floors, so
  the ground-floor units and their signs stay as they are; `Tower`'s crown
  is one floor above the building, outside the pick box. Picking still uses
  the full footprint and height. New module `view-massing` (11165 of 25000);
  `view-core` stands at 20934. I could not look at it in the game: roof tilt
  signs (the gabled slopes and the sawtooth teeth) follow glm's right-handed
  rotation and should be checked by the user.

### - [x] I3. Facade and roof detail per style (view-core)

- Open: `app/CityStatic.cpp` (`AddFacade`, the lit-window rule),
  `app/CityViewInternal.hpp`, `app/CityView.cpp`.
- Window rhythm per style (a dormitory block packs them, a tower runs them in
  bands, `TwinTowers` keeps them off the gap), a cornice or ledge band at a
  setback and at the top floor, a parapet, bay windows on `Setback` and
  `Gabled`, an arcade of columns at the ground floor of `Plinth`, and roof
  clutter - water tank, vent stacks, aerial, dish - placed by a hash of the
  building id, never by a draw, so two runs look alike.
- Keep the lit-window rule seeded by face, floor and window as it is now,
  mixed with the building id, so a block's lit windows do not move when the
  city is rebuilt from the same seed.
- Done. `WindowRhythmOf` (Gabled packs windows at 2.8 m, Sawtooth spaces them
  at 4.5 m, the towers leave every third floor blank) and `DetailOf` (ledge
  under every box, parapet on flat roofs, bays on Setback and Gabled, columns
  on Plinth, roof clutter by hash of the id with a red beacon on the aerial)
  are pure and tested in `tests/MassingTests.cpp`. `TwinTowers` blanks the
  faces that look into the gap (`MassBox::blankFaces`). The lit rule is
  untouched: it is still seeded from the building's centre, face (plus four
  per box, so `Plain` is unchanged), floor and window. `view-massing` 14389.

### - [x] I4. Lights follow the buildings (view-core)

- Open: `app/CityLights.cpp` (`AddLights`, `UpdateLights`), `app/CityView.cpp`
  (`AddBuilding`, where `AddLights` is called once at startup).
- Task D, step D4 left a hole: a building the simulation raises later gets
  neon signs that cast no light, because `AddLights` runs once. Make the
  lights of one new building be added with it and removed with it, and give
  the roof clutter of I3 its small light where it has one.
- This step is for the user to see in the game; no test.
- Done. `CityView::SyncLights` (called every frame from `main.cpp` before
  `UpdateLights`; `AddLights` calls it too) gives each `NeonSign` not yet lit
  a point light, taking over the light of a removed building's sign first
  (`Neon::retired`: dark and hidden until reused), so a building a project
  raises later has working neon and nothing leaks. The aerial's beacon on a
  roof is such a sign, in red. Not seen in the game.

### - [x] I5. A site raises a styled building (construction)

- Open: `sim/src/Construction.cpp` (`startSite`), `sim/include/mechapunk/sim/City.hpp`
  (only to read `StyleOf`).
- `startSite` sets the new building's style with the same `StyleOf`, so no
  new saved field and no save change: the style of a building raised at
  midnight reads the same way as one generated at the start.
- Test in `tests/ConstructionTests.cpp` (or a new file if the 10k file limit
  bites): a site's building has a style, and it is the one `StyleOf` gives it.
  `construction` stands at 24284 of 25000: make room first if needed.
- Done. `startSite` writes `.style = StyleOf(kind, id, floors + 1)`: the
  final height is the ground floor plus the `floors` nights, so the site
  already knows its style while the view draws scaffold, and the finished
  building keeps it. No save change; no draw, so the log is unchanged. The
  test is a new `tests/SiteStyleTests.cpp` (module `site-style`) instead of
  `ConstructionTests.cpp`, whose module has no room (24262 of 25000).

### - [x] I6. The style in the inspector (inspector)

- Open: `app/Inspector.cpp` (the building page), `app/Links.hpp`.
- The building page names the style in its facts ("a stepped block of 9
  floors", "a sawtooth yard of 4 floors") and the site's page (task D, step
  D5c) says what the finished building will look like.
- No test; the words come from a `StyleName(BuildingStyle)` helper - put it
  in `City.hpp` beside `ApartmentClassName`, which is the same kind of thing.
- Done. `StyleName` is declared in `City.hpp` and defined in `CityStyle.cpp`
  (module `city-styles`, with a small test that every style has its own
  name). The building page says "A stepped block of 9 floors" under its first
  line; the site's page says "It will be a sawtooth yard of 5 floors" under
  its progress. Not seen in the game.

### - [x] I7. Buildings by style in the statistics window (statistics)

- Open: `app/Statistics.cpp` (the Property tab), `app/StatisticsInternal.hpp`.
- A small table or bar row in the Property tab: how many buildings of each
  style, habitats and workplaces apart, counted from the frame's city - no
  saved counter, so no save change.
- No test; the counting is one loop over `city.buildings`.
- Done. A "Buildings by style" table (style, habitats, workplaces) sits in
  `DrawProperty` between the plots and the Construction section. Not seen in
  the game.

## Done when

- [x] Every building has a style, saved, migrated and tested (I1).
- [x] The city looks like a city and not a row of slabs: massing, facades,
      roofs and lights differ by style (I2, I3, I4).
- [x] A building raised at midnight gets a style and turns into it when it
      is finished (I5).
- [x] The style is readable in the inspector and counted in the statistics
      window (I6, I7).
- [x] The guide's task table marks I done.

## Open question for the user

This task adds **form**, not **function**: no new `BuildingKind`. A new
functional kind (a civic hall, a market hall, a temple or monument a robot
can walk into) is a different, bigger change: it needs a new `RoomKind`,
floor plans, staff or resident rules, validation and probably a save change
of its own. Ask the user whether they want it, and if so plan it as task J.
