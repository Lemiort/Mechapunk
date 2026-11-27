# Save fixtures

Saves older than `kOldestSupportedSaveVersion` (`Save.hpp`) are not
supported: their migrations, fixtures and tests were removed, and the game
refuses them with "Save version N is not supported". Only the fixtures of the
current version and of the versions its migrations upgrade (27 and 28) are kept.

| File            | Version | How it was made                                                         |
| --------------- | ------- | ----------------------------------------------------------------------- |
| `v27-city.json` | 27      | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v27-city.json` (game 0.27.0) |
| `v28-city.json` | 28      | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v28-city.json` (game 0.28.0) |
| `v29-city.json` | 29      | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v29-city.json` (game 0.29.0) |

`v27-city.json` (before settings were saved) stops on the third Saturday (Day 20, 16:50) in a town of 100
robots, with every system of the game running: City Hall collecting taxes,
venues with prices and rents moved, apartment classes, police and metro
staff, gangs, a chronicle that names accounts, the city's free plots and
every building's architectural style. It raised no construction site, so its
projects are empty; `CurrentVersionFixtureLoadsAndRoundTrips` loads the current one,
checks that saving it again reproduces it byte for byte, and runs a day.

`v28-city.json` stops at the same moment in the same kind of city: from this
version the simulation's settings (`WorldState::settings`, Settings.hpp) are
saved with the city, and this one was made with the defaults.
`Version27FixtureMigratesAndRuns` loads the v27 fixture through
`V27ToV28`, which gives it those defaults.

`v29-city.json` stops at the same moment in the same kind of city: from this
version every tunable number of the simulation is a saved setting (behaviour,
careers, the city's layout, money, population, social life, venues), and this
one was made with the defaults. `Version28FixtureMigratesAndRuns` loads the
v28 fixture through `V28ToV29`, and `Version27FixtureMigratesAndRuns` the
v27 one through both steps.

## Changing the save format

Any change to `WorldState` or the types inside it (field names, enum names,
nesting, meaning of a value) changes the format:

1. Increase `kCurrentSaveVersion` in `sim/include/mechapunk/sim/Save.hpp`.
2. Add a `Migration` from the previous version to the `kMigrations` table in
   `sim/src/Save.cpp` (it is empty now). It edits the untyped JSON of the
   `world` object, so it never depends on today's C++ types. Or, when old
   saves do not matter, raise `kOldestSupportedSaveVersion` to the new
   version instead and write no migration.
3. Write a new fixture for the new version with `mechapunk-sim` and add it to
   the table above; point `CurrentVersionFixtureLoadsAndRoundTrips` at it.
   Delete the fixture of a version that is no longer supported.
