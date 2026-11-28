# Save fixtures

Saves older than `kOldestSupportedSaveVersion` (`Save.hpp`) are not
supported: their migrations, fixtures and tests were removed, and the game
refuses them with "Save version N is not supported". Only the fixture of the
current version is kept.

| File            | Version | How it was made                                                         |
| --------------- | ------- | ----------------------------------------------------------------------- |
| `v42-city.json` | 42      | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v42-city.json` (game 0.42.0) |

`v42-city.json` stops on the third Saturday (Day 20, 16:50) in a town of 100
robots, with every system of the game running: City Hall collecting taxes,
venues with prices and rents moved, apartment classes, police and metro
staff, gangs, a chronicle that names accounts, the city's free plots and
every building's architectural style. It raised no construction site, so its
projects are empty. Every tunable number of the simulation is a saved setting
(behaviour, careers, the city's layout, money, population, social life,
venues; since version 30 the seed town's, since 31 the wealth spread, since
32 the dormitory rule, since 33 immigration, since 34 the street growth
settings, since 35 the metro growth settings, since 36 the bank growth
setting, since 37 the corporation growth setting, since 38 the price index in
the daily statistics, since 39 the inflation target settings, since 40 the
gang growth settings, since 41 the growth events of the chronicle); this one
was made with the defaults. From version 42 every robot also carries its
five body parts: head, torso, left hand, right hand and legs, each with a
condition from 0 to 1000 and a flag saying the part is lost, plus the hand
the robot works with. The head and the torso carry what the robot's wear
allows today - `kMaxWear - wear`, never below 0 - rather than a value fixed
at birth, because step L3a caps and mends them wherever wear moves: this
town's oldest robots sit well below whole and its most worn robot sits at
0 and is scrapped at the next midnight. `CurrentVersionFixtureLoadsAndRoundTrips`
loads it, checks that saving it again reproduces it byte for byte, and
runs a day.

## Changing the save format

Any change to `WorldState` or the types inside it (field names, enum names,
nesting, meaning of a value) changes the format:

1. Increase `kCurrentSaveVersion` and `kGameVersion` in
   `sim/include/mechapunk/sim/Save.hpp`.
2. Raise `kOldestSupportedSaveVersion` to the new version: a save of an older
   version is refused, so no migration is written and `kMigrations` in
   `sim/src/Save.cpp` stays empty. A migration is kept only for a version this
   game still reads, and it edits the untyped JSON of the `world` object, so
   it never depends on today's C++ types; reading uses
   `error_on_missing_keys`, so every new key would have to be added to the
   old save's JSON.
3. Write a new fixture for the new version with `mechapunk-sim` and add it to
   the table above; point `CurrentVersionFixtureLoadsAndRoundTrips` at it.
   Delete the fixture of a version that is no longer supported.
