# Task E: simulation level of detail

Status: optional. Steps E1 (profiling) and E3 (caching trip times) moved to
task J (`docs/tasks/J-performance.md`), which measured on 2026-11-27 that
10000 robots take about 4 s per simulated day, 97% of it in `Decide`.
E2 and E4 stay here.

## Steps (world module)

- [x] E1. (Done in task J: see its measurements.) Profile first: time `World::Step` parts in the headless runner
  (`--robots 20000 --days 5`). Fix only what shows up.
- [ ] E2. Robots at home or at work update needs every 30 minutes instead
  of 10 (keep the totals equal: three times the effect). Results change, so
  rerun the 40-day check.
- [ ] E3. (Moved to task J, steps J2 to J4.) Cache `TravelMinutes` per building pair (an unsaved cache).
- [ ] E4. Saves are about 40 MB: store `history` and `emissions` more
  compactly (save change, rule 3).

No details step: level of detail adds nothing new to inspect. If E2 changes
how often needs update, the robot page must still show needs that move
smoothly (interpolate between updates in the view if they jump).
