# Task J: performance

Status: planned on 2026-11-27, nothing done. Save version 29, game 0.29.0.
This task takes over steps E1 (profiling, done below) and E3 (caching trip
times) of `docs/tasks/E-level-of-detail.md`; E2 and E4 stay there.

## What and why

Measured on 2026-11-27 (12-thread machine, release build, seed 1, 10000
robots), with throwaway timers that were removed again:

| Part of `World::Step` | Share of the time |
| --- | --- |
| `Act` (robots deciding what to do) | 97% |
| `UpdateNeeds` + `Socialize` (every 10 minutes) | about 2.5% |
| The midnight passes (rent, jobs, economy, construction) | under 1% |

Almost all of `Act` is `Decide` (`sim/src/Behaviour.cpp`), which scores every
venue (about 480) for a robot that goes out and calls `TravelMinutes` twice per
venue: there, and from there home. Two simulated days made 112 000 decisions,
66 million `PlanTrip` calls (about 590 per decision) and 57 million
`Metro::Route` calls. Planning trips is most of the simulation's cost.

The simulation already runs apart from rendering: `SimulationHost` steps the
world on a worker thread (NEE `SimulationThread`) and hands the renderer
immutable `WorldFrame` snapshots through a `SnapshotBuffer`. What still costs
is that the worker builds a full snapshot (`SimulationHost::Publish`) after
*every* simulated minute: 10000 `RobotFrame`s, and every 10 minutes a deep copy
of all bonds and memories. On a busy morning that cut the game's throughput
from 277 to 226 simulated minutes a second (about 18%); at night, when the
simulation is cheap, the share is larger. At speed 100x and up almost none of
those snapshots are ever drawn.

Rule 1 (determinism) decides what may run in parallel. Robots act in id
order, draw from one `state_.random` and reserve venue places that later
robots see, so `Act` itself must stay sequential. The trip times a decision
needs are pure: they depend only on the city, the metro timetable and the
clock. They can be computed in parallel beforehand, with byte-identical
results.

## Rules for every step

- Every step except J1 must leave the headless log and the save **byte for
  byte identical**: `mechapunk-sim --seed 7 --robots 2000 --days 6 --all --save X.json`
  before and after, compared ignoring the last two lines (guide section 2).
  A step that changes the log is wrong, not "a small difference".
- Time before and after with `mechapunk-sim --seed 1 --robots 10000 --days 2 --quiet`
  and write both numbers in the step's note.
- No save change in this task: thread counts and caches are not saved state.

## Steps

### - [ ] J1. Publish a snapshot only when a frame is due (ui)

- Open: `app/SimulationHost.hpp`, `app/SimulationHost.cpp` (`Step`, `Publish`),
  `external/NEE/include/SnapshotBuffer.hpp` (read only: NEE changes only by
  pull request).
- `SimulationHost::Step` calls `Publish` only when about one render frame has
  passed since the last one (8 ms of steady clock), when the thread is paused
  or single-stepping, and on the first step. Put that rule in a small pure
  function, for example `ShouldPublish(sinceLast, paused, singleStep)`, so it
  can be tested without a thread.
- Check that the renderer copes with snapshots many minutes apart. Robots are
  drawn from `departedAt`/`arrivesAt`, so they should; the `SnapshotBuffer`
  alpha clamps at 1. Say in the note what was checked.
- Measure: a throwaway test that builds a `SimulationHost` on a 10000-robot
  city at 08:00, sets speed 100000 for 4 seconds and prints steps per second.
  It needs `app/SimulationHost.cpp` added to `layout_tests` temporarily. Remove
  it again afterwards. Today's number is 226; 277 is the bare world.

### - [ ] J2. Skip a metro route that cannot win (world)

- Open: `sim/src/Transit.cpp` (`PlanTrip`), `sim/include/mechapunk/sim/Metro.hpp`.
- A metro trip is never shorter than the walk to the boarding station plus
  the walk from the alighting station. When the walking trip is already no
  longer than that, `PlanTrip` returns it without calling `Metro::Route`. This
  is exact, so the log must not change.
- Count in the note how many `Metro::Route` calls are left (throwaway counter).

### - [ ] J3. Remember metro routes within a minute (metro)

- Open: `sim/src/Transit.cpp`, `sim/src/Metro.cpp` (`Route`), `sim/src/WorldInternal.hpp`.
- A cache in `World`, cleared when the tick changes, keyed by `(board,
  alight, onPlatform)` and holding the arrival minute and the legs. It is
  unsaved, like `routes_`. Many robots of one minute ask the same question.

### - [ ] J4. Flat tables instead of hash lookups in PlanTrip (city-streets)

- Open: `sim/include/mechapunk/sim/City.hpp` (`RoutePlanner`, `RoomIndex`),
  `sim/src/City.cpp`, `sim/src/Transit.cpp`.
- Build all street distances once when the city is loaded (the grid is
  small: 350 buildings at 10000 robots), and a table from room to (entrance
  node, floor). Rebuild both where `rooms_`/`routes_` are rebuilt today (a room
  changing kind, a building raised). After this `RoutePlanner::DistanceMeters`
  and `IndexStations` no longer write anything, which J6 needs.
- Note the memory the distance table takes at 10000 and 20000 robots.

### - [ ] J5. A thread pool and `--threads` (new module `threads`)

- New files `sim/include/mechapunk/sim/ThreadPool.hpp`, `sim/src/ThreadPool.cpp`,
  `tests/ThreadPoolTests.cpp`, added to a new module in `scripts/context_budget.py`.
- A fixed pool of `std::jthread` workers with one call: run `f(i)` for
  `i` in `[0, n)` and wait. Work is split into fixed chunks by index, never by
  who finishes first, so the result cannot depend on timing.
- Default size: `std::thread::hardware_concurrency()` minus 2 (the render
  thread and the OS), at least 1; 1 means "run inline, no threads".
  `mechapunk-sim --threads N` and `mechapunk --threads N` override it. The size
  is a runtime choice, not a setting and not saved: it must never change
  a result.
- Tests: the pool runs every index once, at sizes 1, 2 and 8; an exception in
  a task reaches the caller.

### - [ ] J6. Plan the trips of a minute's decisions in parallel (world)

- Open: `sim/src/World.cpp` (`Step`), `sim/src/Behaviour.cpp` (`Act`, `Decide`),
  `sim/src/Transit.cpp`. The `world` module stands at about 24k of 25k, so put
  the new code in a new `sim/src/TripPlanning.cpp` with its own module.
- Each minute, in three phases:
  1. Sequential: list the robots due this minute whose `Act` will call
     `Decide`, from their activity and state.
  2. Parallel, on the pool: for each of them, the trip minutes from its
     location to every venue and from every venue to its apartment. This
     phase only reads the city, the metro and the clock (J4 made them
     read-only), and writes only its own row.
  3. Sequential, in id order as today: `Act` and `Decide` run unchanged, and
     `Decide` reads the trips from the table instead of calling
     `TravelMinutes`. A robot that was not predicted, or whose location changed
     since phase 1, falls back to computing them on the spot.
- Test `World.SameFutureOnAnyThreadCount`: one city run 3 days at 1, 2 and 8
  threads gives identical serialized saves. Also run the byte-identical
  headless check at `--threads 1` and at the default.

### - [ ] J7. Record the gain (world)

- Run the guide's 40-day check at 10000 robots before J2 (keep the log) and
  after J6, at 1 thread and at the default. Write the times in this file and
  update the speed note at the top of task E and in the guide. If needs,
  social life or the midnight passes now dominate, write down what a next
  task should look at.

## Not in this task

- Running `Act` itself in parallel: id order, the shared random stream and
  venue reservations make that a rule-1 break.
- Threading `UpdateNeeds`, `Socialize` or the midnight passes: about 3%
  together today. Revisit after J7.
- Simulation level of detail (task E, step E2): that changes results on
  purpose.

## Done when

- [ ] Snapshots are published at render rate, not every simulated minute (J1).
- [ ] The trip work per decision is much cheaper on one thread (J2-J4).
- [ ] Trips are planned on all cores with identical results (J5, J6).
- [ ] The gain is measured and written down (J7).
