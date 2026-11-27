# Task J: performance

Status: planned on 2026-11-27, nothing done. Save version 29, game 0.29.0.
It takes over steps E1 (profiling, done below) and E3 (caching trip times)
of `docs/tasks/E-level-of-detail.md`; E2 and E4 stay there.

Every step below has the same parts: **Module** (the one module it changes),
**Depends on**, **Save change**, **Open** (the only files to read in full),
**Do**, **Tests**, and **Check** (what to run before committing). Read the
guide first. Do one step, commit, push, tick it, and stop.

## Measurements behind this task (2026-11-27)

The numbers below are for a 12-thread machine, a release build, seed 1 and
10000 robots.

| Part of `World::Step` | Share of the time |
| --- | --- |
| `Act` (robots deciding what to do) | 97% |
| `UpdateNeeds` + `Socialize` (every 10 minutes) | about 2.5% |
| The midnight passes | under 1% |

Almost all of `Act` is `World::Decide` (`sim/src/Behaviour.cpp`). It scores
every venue (about 480) and calls `TravelMinutes` twice per venue. Two
simulated days made 112 000 decisions, 66 million `PlanTrip` calls and 57
million `Metro::Route` calls.

The game runs the world on a worker thread (`SimulationHost`, NEE
`SimulationThread`), but it builds a full snapshot (`SimulationHost::Publish`)
after every simulated minute. That costs about 18% of a busy morning:
throughput falls from 277 to 226 minutes a second.

Timing commands used in every step's note:
- headless: `build/build/Release/mechapunk-sim.exe --seed 1 --robots 10000 --days 2 --quiet`
  (the last line prints the seconds; 8.4 s on 2026-11-27);
- determinism: `mechapunk-sim --seed 7 --robots 2000 --days 6 --all --save X.json`,
  run before and after the change and compared byte for byte, ignoring the
  last two lines of the log (guide section 2).

## Rule for this whole task

Every step except J1 must leave the seed 7 log and save **byte for byte
identical**. If they differ, the step is wrong: undo it and find out why.
Do not accept "a small difference".

## Steps

### - [ ] J0. Make room in the world module (world)

- Module: world (23979 of 25000, too full for J6). Depends on: none. Save
  change: no.
- Open: `sim/src/Behaviour.cpp`, `scripts/context_budget.py`, `sim/CMakeLists.txt`.
- Do: move `World::Decide` and the file-local helpers only it uses (`Relief`
  and the scoring helpers above it; check each with `rg -n "Name" sim/src`)
  into a new `sim/src/Outings.cpp`. Put it in a new module `outings` in
  `scripts/context_budget.py` (`World.hpp`, `WorldInternal.hpp`, `Outings.cpp`).
  Helpers that `Behaviour.cpp` also uses go into `sim/src/WorldInternal.hpp`
  as `inline`. This is the guide's "making room" recipe.
- Tests: none new.
- Check: build, ctest, seed 7 log and save byte-identical.

### - [ ] J1. Publish a snapshot only when a frame is due (ui)

- Module: ui. Depends on: none. Save change: no.
- Open: `app/SimulationHost.hpp`, `app/SimulationHost.cpp`,
  `external/NEE/include/SnapshotBuffer.hpp` (read only: NEE changes only by
  pull request).
- Do:
  1. Add `std::chrono::steady_clock::time_point lastPublish_` to `SimulationHost`.
  2. Add a free function in `SimulationHost.hpp`:
     `bool ShouldPublish(std::chrono::nanoseconds sinceLast, bool paused)`,
     which returns `paused || sinceLast >= 8ms`.
  3. In `SimulationHost::Step`, after `world_.Step()`, call `Publish()` only
     when `ShouldPublish(now - lastPublish_, thread_ == nullptr ||
     thread_->IsPaused())` is true, and then set `lastPublish_ = now`. The
     constructor's first `Publish()` stays.
  4. A single step taken while paused must still publish, so the "paused"
     argument covers it.
- Tests: none possible without a window (`SimulationHost` is not in a test
  library). Add a `static_assert` or a comment with the expected values of
  `ShouldPublish` instead.
- Check: build, ctest. In the note, write the steps per second from the
  throwaway benchmark:
  1. Add `${PROJECT_SOURCE_DIR}/app/SimulationHost.cpp` and a test file to
     `layout_tests` in `tests/CMakeLists.txt`.
  2. In the test, build `World::Generate({.seed = 1, .robots = 10000})`, run it
     to minute 480, and wrap it in a `SimulationHost`.
  3. Call `SetSpeed(100000)`, sleep 4 s, and read `LatestFrame()->tick` before
     and after.
  4. Remove both files again.

  Before J1 it gave 226 steps a second.

### - [ ] J2. Skip a metro route that cannot win (metro)

- Module: metro (`Transit.cpp` is in it; check `scripts/context_budget.py`).
  Depends on: none. Save change: no.
- Open: `sim/src/Transit.cpp` (`PlanTrip`).
- Do: in `PlanTrip`, after `toStation` is known, compute
  `fromStation` (the walk from the alighting station, already computed further
  down) before calling `metro_.Route`. If `toStation + fromStation >=
  trip.minutes` (the walking trip), return the walking trip without calling
  `Route`. The metro also takes at least the minutes underground, so it
  cannot win. This is exact.
- Tests: none new; the existing suite and the byte-identical check prove it.
- Check: build, ctest, seed 7 byte-identical, headless time in the note.

### - [ ] J3. Remember metro routes within a minute (metro)

- Module: metro. Depends on: J2. Save change: no.
- Open: `sim/src/Transit.cpp`, `sim/include/mechapunk/sim/World.hpp` (only the
  private members near `routes_`; do not read the whole file),
  `sim/src/WorldInternal.hpp`.
- Do:
  1. Add a small struct `RouteMemo` in `WorldInternal.hpp`: `Tick tick` and a
     `std::unordered_map<std::uint64_t, Arrival>`. The key packs
     `(board, alight, onPlatform)` into 64 bits. `Arrival` holds the minute
     off the last train and the first leg's `toStation`. Looking up by key
     never decides an order, so rule 1 holds.
  2. `PlanTrip` takes an optional `RouteMemo*`. When given, and the memo's
     tick is not `state_.tick`, clear it and set the tick. Then look up the
     key, or call `metro_.Route` and store the result.
  3. `World` owns one `RouteMemo routeMemo_` (unsaved, like `routes_`), and
     `TravelMinutes` passes it. J6 gives each worker thread its own memo
     instead, which is why it is a parameter and not hidden inside `PlanTrip`.
- Tests: none new. Check: build, ctest, seed 7 byte-identical, time in the
  note.

### - [ ] J4. Read-only street distances and room places (city-streets)

- Module: city-streets. Depends on: none. Save change: no.
- Open: `sim/include/mechapunk/sim/City.hpp` (`RoutePlanner`, `RoomIndex`), the
  `RoutePlanner` part of `sim/src/City.cpp` (`rg -n "RoutePlanner::" sim/src/City.cpp`,
  read only those functions), and `sim/src/Transit.cpp`.
- Do:
  1. Give `RoutePlanner` a method `void PrecomputeAll()` that fills the tree of
     every origin node. After it, `DistanceMeters` and `Path` only read.
  2. Call it wherever `routes_` is (re)built in `World`
     (`rg -n "routes_ =" sim/src`).
  3. Make `World::IndexStations` run where `metro_` is built instead of
     lazily inside `PlanTrip`, so `PlanTrip` writes nothing (`rg -n
     "IndexStations" sim/src`).
- Tests: in `tests/CityStreetTests.cpp`, after `PrecomputeAll` the distances
  equal the lazily computed ones for 50 node pairs.
- Check: build, ctest, seed 7 byte-identical. Write the memory the trees take
  at 10000 robots in the note (nodes × nodes × 8 bytes).

### - [ ] J5. A thread pool and `--threads` (new module `threads`)

- Module: threads (new). Depends on: none. Save change: no.
- Open: `sim/CMakeLists.txt`, `tests/CMakeLists.txt`, `scripts/context_budget.py`,
  `tools/sim_runner/main.cpp` (the options part only, `rg -n "struct Options" -A 20`).
- Do:
  1. Create `sim/include/mechapunk/sim/ThreadPool.hpp` and `sim/src/ThreadPool.cpp`
     with `class ThreadPool { public: explicit ThreadPool(std::size_t workers);
     void ForEach(std::size_t count, const std::function<void(std::size_t
     index, std::size_t worker)>& work); std::size_t Workers() const; };`.
  2. `ForEach` splits `[0, count)` into one fixed chunk per worker, by index
     only, runs the chunks on `std::jthread` workers that live as long as the
     pool, and returns when all are done. It rethrows the first exception.
  3. `workers == 0` or `1` runs everything inline on the caller.
  4. Add `std::size_t DefaultThreads()`: `max(1, hardware_concurrency() - 2)`.
  5. `mechapunk-sim --threads N` (default `DefaultThreads()`) stores the
     number; J6 uses it. The game gets `--threads N` the same way, in
     `app/main.cpp`'s option parsing.
  6. The thread count is never saved and never a setting.
- Tests (`tests/ThreadPoolTests.cpp`): every index runs exactly once at 1, 2
  and 8 workers; `worker` is below `Workers()`; an exception thrown in one
  index reaches the caller.
- Check: build, ctest, `ContextBudget`.

### - [ ] J6. Plan the trips of a minute's decisions in parallel (outings)

- Module: outings (from J0). Depends on: J0, J3, J4, J5. Save change: no.
- Open: `sim/src/Outings.cpp`, `sim/src/World.cpp` (`Step` only),
  `sim/src/Transit.cpp`, `sim/include/mechapunk/sim/ThreadPool.hpp`.
- Do:
  1. `World` gets a `ThreadPool* pool_` (null means one thread) and a setter
     `SetThreadPool`. The runner and the game create the pool with
     `--threads` and set it. It is not saved.
  2. In `World::Step`, just before the "robots act in id order" loop, collect
     the robots that will reach `Decide` this minute. `Act` calls `Decide` in
     exactly three cases:
     - `activity == Resting` and `tick < commuteAt`;
     - `activity == Working` and `tick >= shiftStartsAt + ShiftOf(robot).lengthMinutes`;
     - `activity == Visiting`;

     each with `nextActionAt <= tick`.
  3. For each such robot, on the pool: a row of `std::uint32_t` trip minutes
     from `robot.location` to every venue, in the order `Decide` walks them
     (`VenueKinds()`, then `RoomsOfKind`), and a second row from every venue to
     `robot.apartment`. Also store `location → apartment` (`walkHome`) and the
     location the row was made for. Each worker uses its own `RouteMemo` (J3).
  4. `Decide` takes the rows when their stored location equals
     `robot.location`, and computes on the spot as today otherwise. Everything
     else (filters, `state_.random`, reservations) stays exactly as it is, in
     id order.
- Tests: `World.SameFutureOnAnyThreadCount` (in a new
  `tests/ParallelTests.cpp`): generate seed 3 with 500 robots and run 3 days
  with no pool, with 2 workers and with 8. The three serialized saves are
  identical.
- Check: build, ctest, seed 7 byte-identical at `--threads 1` and at the
  default; headless time at both in the note.

### - [ ] J7. Record the gain (runner)

- Module: none changed (docs only). Depends on: J6. Save change: no.
- Do: run the guide's 40-day check (`--seed 1 --robots 10000 --days 40 --quiet`)
  at `--threads 1` and at the default. Write both times in this file, and
  update the speed note at the top of task E and in the guide's section 2
  ("40 days at 10000 robots take about ...").

## Not in this task

- Running `Act` itself in parallel: id order, the shared random stream and
  venue reservations make that a rule-1 break.
- Threading `UpdateNeeds`, `Socialize` or the midnight passes (about 3%).
- Fewer need updates (task E, step E2): that changes results on purpose.

## Done when

- [ ] J0-J7 are ticked, and the 40-day run is several times faster with the
      default threads than before J0.
