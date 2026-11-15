# Save fixtures

Each file is a save written by a released save-format version. Tests load
every fixture, so old saves keep working as the format evolves.

| File           | Version | How it was made                                                       |
| -------------- | ------- | --------------------------------------------------------------------- |
| `v1-city.json` | 1       | `mechapunk-sim --seed 6 --robots 60 --minutes 1900 --save v1-city.json` |

`v1-city.json` stops at Day 2 07:40, so robots are at home, commuting and
working at once.

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
