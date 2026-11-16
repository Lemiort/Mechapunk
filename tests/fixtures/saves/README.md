# Save fixtures

Each file is a save written by a released save-format version. Tests load
every fixture, so old saves keep working as the format evolves.

| File           | Version | How it was made                                                       |
| -------------- | ------- | --------------------------------------------------------------------- |
| `v1-city.json` | 1       | `mechapunk-sim --seed 6 --robots 60 --minutes 1900 --save v1-city.json` (game 0.1.0) |
| `v2-city.json` | 2       | `mechapunk-sim --seed 5 --robots 60 --minutes 2580 --save v2-city.json` (game 0.2.0) |
| `v3-city.json` | 3       | `mechapunk-sim --seed 9 --robots 60 --minutes 8340 --save v3-city.json` (game 0.3.0) |
| `v4-city.json` | 4       | `mechapunk-sim --seed 9 --robots 60 --minutes 18420 --save v4-city.json` (game 0.4.0) |
| `v5-city.json` | 5       | `mechapunk-sim --seed 10 --robots 60 --minutes 28500 --save v5-city.json` (game 0.5.0) |

`v1-city.json` stops at Day 2 07:40, with robots at home, commuting and
working. `v2-city.json` stops at Day 2 19:00, with robots resting, walking,
working and out at venues. `v3-city.json` stops on Saturday (Day 6) at 19:00,
with every activity again and some robots on their days off. `v4-city.json`
stops on the second Saturday (Day 13) at 19:00, when robots have friends,
close friends and rivals, and memories of making them. `v5-city.json` stops
on the third Saturday (Day 20) at 19:00, with robots at every job level, one
out of work, and memories of losing and finding jobs and moving up.
`Version1FixtureMigratesAndRuns` loads the v1
fixture through the migration, checks every robot kept its home and
workplace, and runs two days of evenings; `Version2FixtureMigratesAndRuns`
checks that the v2 fixture gains rent and days off and keeps paying rent;
`Version3FixtureMigratesAndRuns` checks that every robot of the v3 fixture
gets two interests and no bonds yet, and that friendships form;
`Version4FixtureMigratesAndRuns` checks that v4 robots start as juniors with
a clean record and keep working.

## Changing the save format

Any change to `WorldState` or the types inside it (field names, enum names,
nesting, meaning of a value) changes the format:

1. Increase `kCurrentSaveVersion` in `sim/include/mechapunk/sim/Save.hpp`.
2. Add a `Migration` from the previous version in `sim/src/Save.cpp`. It edits
   the untyped JSON of the `world` object, so it never depends on today's C++
   types.
3. Write a new fixture for the new version with `mechapunk-sim` and add it to
   the table above.
4. Point `CurrentVersionFixtureLoadsAndRoundTrips` at the new fixture, and
   add a test that each older fixture still loads (through the migrations)
   and keeps running.

Never edit or regenerate a committed fixture of an older version: it stands
for saves that players already have.
