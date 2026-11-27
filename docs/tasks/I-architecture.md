# Task I: architecture

Status: planned on 2026-11-27, nothing done yet. Save version is 26 and the
game is 0.26.0; I1 raises it to 27 and the game to 0.27.0. Budgets that are
already tight and will have to be made smaller first: `generation` 24521 of
25000, `saves` 24006, `world` 24491, `careers` 24405, `construction` 24284.
`view-core` has room (21041), `inspector` has room (19070), `statistics` has
room (21973).

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

### - [ ] I1. A building's style is saved state (generation + saves)

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
- **Make room first**: `generation` has 479 tokens left and `saves` 994. Split
  `tests/CityTests.cpp` by topic into a new module (streets and stations
  against buildings and rooms), the way `SaveFixtureTestsEarly.cpp` split the
  saves tests, and move the frozen migrations from version 19 on into
  `MigrationsEarly.cpp` and their fixture tests into
  `SaveFixtureTestsEarly.cpp`. Code moved only; prove it with the headless
  check before and after, byte for byte.
- Headless: the log must be **identical** to the run before the step (no new
  draw); the seed 7 / 2000 / 6 days save differs only in the version lines and
  the new `"style"` key on every building.

### - [ ] I2. The block is drawn per style (view-core)

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

### - [ ] I3. Facade and roof detail per style (view-core)

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

### - [ ] I4. Lights follow the buildings (view-core)

- Open: `app/CityLights.cpp` (`AddLights`, `UpdateLights`), `app/CityView.cpp`
  (`AddBuilding`, where `AddLights` is called once at startup).
- Task D, step D4 left a hole: a building the simulation raises later gets
  neon signs that cast no light, because `AddLights` runs once. Make the
  lights of one new building be added with it and removed with it, and give
  the roof clutter of I3 its small light where it has one.
- This step is for the user to see in the game; no test.

### - [ ] I5. A site raises a styled building (construction)

- Open: `sim/src/Construction.cpp` (`startSite`), `sim/include/mechapunk/sim/City.hpp`
  (only to read `StyleOf`).
- `startSite` sets the new building's style with the same `StyleOf`, so no
  new saved field and no save change: the style of a building raised at
  midnight reads the same way as one generated at the start.
- Test in `tests/ConstructionTests.cpp` (or a new file if the 10k file limit
  bites): a site's building has a style, and it is the one `StyleOf` gives it.
  `construction` stands at 24284 of 25000: make room first if needed.

### - [ ] I6. The style in the inspector (inspector)

- Open: `app/Inspector.cpp` (the building page), `app/Links.hpp`.
- The building page names the style in its facts ("a stepped block of 9
  floors", "a sawtooth yard of 4 floors") and the site's page (task D, step
  D5c) says what the finished building will look like.
- No test; the words come from a `StyleName(BuildingStyle)` helper - put it
  in `City.hpp` beside `ApartmentClassName`, which is the same kind of thing.

### - [ ] I7. Buildings by style in the statistics window (statistics)

- Open: `app/Statistics.cpp` (the Property tab), `app/StatisticsInternal.hpp`.
- A small table or bar row in the Property tab: how many buildings of each
  style, habitats and workplaces apart, counted from the frame's city - no
  saved counter, so no save change.
- No test; the counting is one loop over `city.buildings`.

## Done when

- [ ] Every building has a style, saved, migrated and tested (I1).
- [ ] The city looks like a city and not a row of slabs: massing, facades,
      roofs and lights differ by style (I2, I3, I4).
- [ ] A building raised at midnight gets a style and turns into it when it
      is finished (I5).
- [ ] The style is readable in the inspector and counted in the statistics
      window (I6, I7).
- [ ] The guide's task table marks I done.

## Open question for the user

This task adds **form**, not **function**: no new `BuildingKind`. A new
functional kind (a civic hall, a market hall, a temple or monument a robot
can walk into) is a different, bigger change: it needs a new `RoomKind`,
floor plans, staff or resident rules, validation and probably a save change
of its own. Ask the user whether they want it, and if so plan it as task J.
