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
| `v6-city.json` | 6       | `mechapunk-sim --seed 10 --robots 60 --minutes 28500 --save v6-city.json` (game 0.6.0) |
| `v7-city.json` | 7       | `mechapunk-sim --seed 10 --robots 60 --minutes 26432 --save v7-city.json` (game 0.7.0) |
| `v8-city.json` | 8       | `mechapunk-sim --seed 33 --robots 60 --minutes 28370 --save v8-city.json` (game 0.8.0) |
| `v9-city.json` | 9       | `mechapunk-sim --seed 33 --robots 60 --minutes 18093 --save v9-city.json` (game 0.9.0) |
| `v10-city.json` | 10     | `mechapunk-sim --seed 33 --robots 60 --minutes 28370 --save v10-city.json` (game 0.10.0) |
| `v11-city.json` | 11     | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v11-city.json` (game 0.11.0) |
| `v12-city.json` | 12     | `mechapunk-sim --seed 33 --robots 100 --minutes 28370 --save v12-city.json` (game 0.12.0) |

`v1-city.json` stops at Day 2 07:40, with robots at home, commuting and
working. `v2-city.json` stops at Day 2 19:00, with robots resting, walking,
working and out at venues. `v3-city.json` stops on Saturday (Day 6) at 19:00,
with every activity again and some robots on their days off. `v4-city.json`
stops on the second Saturday (Day 13) at 19:00, when robots have friends,
close friends and rivals, and memories of making them. `v5-city.json` stops
on the third Saturday (Day 20) at 19:00, with robots at every job level, one
out of work, and memories of losing and finding jobs and moving up.
`v6-city.json` stops at the same time in a city that ages: some robots
have been scrapped and are remembered by friends, and newcomers have moved
in. `v7-city.json` stops during a weekday morning commute (Day 19,
08:32), with robots walking to, riding and coming up from the metro.
`v8-city.json` stops on a rainy Saturday afternoon (Day 20, 16:50) with a
robot riding the metro to a diner, one out of work and a scrapped robot
still remembered. It uses another seed than the older fixtures: the city
generator changed, and seed 10 no longer makes a town anyone rides the
metro in.
`v9-city.json` stops on the second Saturday (Day 13, 13:33) with a robot
on its way to lunch on the timetable's trains, changing lines; the town is
small, and few rides need a change.
`v10-city.json` stops on the third Saturday (Day 20, 16:50) in a closed
economy: tenants, savers, robots and a corporation in debt, and nineteen
days of statistics.
`v11-city.json` stops at the same moment in a town of 100 robots, with venue
staff on shifts, daily budgets and a market price for supplies; 60 robots
no longer make a town with tenants and scrapped robots by then.
`v12-city.json` stops at the same moment in a city with apartment classes:
most robots still in the dorms they started in, others in studios, flats
and two-bedroom flats, some of them shared.
`Version1FixtureMigratesAndRuns` loads the v1
fixture through the migration, checks every robot kept its home and
workplace, and runs two days of evenings; `Version2FixtureMigratesAndRuns`
checks that the v2 fixture gains rent and days off and keeps paying rent;
`Version3FixtureMigratesAndRuns` checks that every robot of the v3 fixture
gets two interests and no bonds yet, and that friendships form;
`Version4FixtureMigratesAndRuns` checks that v4 robots start as juniors with
a clean record and keep working. `Version5FixtureMigratesAndRuns` checks that v5
robots get an age and matching wear and go on aging. `Version6FixtureMigratesAndRuns` checks
that a v6 city gets its stations and that robots start riding.
`Version7FixtureMigratesAndRuns` checks that a v7 world starts dry for six
hours and then its weather changes. `Version8FixtureMigratesAndRuns` checks
that v8 riders change at their last station, that is, they don't, and that
robots go on riding. `Version9FixtureMigratesAndRuns` checks that a v9
city is founded an economy whose money adds up, and records statistics.
`Version10FixtureMigratesAndRuns` checks that v10 robots start with an
unspent budget, supplies cost 60% of venue takings, and the city runs on.
`Version11FixtureMigratesAndRuns` checks that v11 apartments become studios
and the city gets a housing office.

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
