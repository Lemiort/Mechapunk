# Mechapunk

A cyberpunk city simulation: humanoid robots live, work and spend their
evenings in a 3D city that you observe. Built on the
[NEE](https://github.com/Lemiort/NEE) engine.

## Repository layout

- `external/NEE` – the engine, as a git submodule. Engine features are
  developed and merged in the NEE repository, then picked up here by
  updating the submodule. No game code goes into NEE.
- `app/` – the game executable: runs the simulation on NEE's
  `SimulationThread` and draws the city with NEE's deferred renderer.
- `sim/` – the city simulation as a library (`mechapunk::sim`). Pure C++ with
  no engine or OpenGL dependency, so it also runs headless.
- `tools/sim_runner/` – `mechapunk-sim`, a command-line runner for the
  simulation.
- `tests/` – simulation tests and save fixtures.
- `assets/` – game assets; copied next to the executable after the engine's
  assets, so files here override engine files with the same name.

## Building

```bash
git clone --recurse-submodules https://github.com/Lemiort/Mechapunk.git
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=23
cmake --preset conan-release
cmake --build --preset conan-release
```

Run `mechapunk` from the build directory so it finds the copied assets.
Requires an OpenGL 4.6 core driver.

Run the tests with `ctest --test-dir build/build/Release --output-on-failure`.

## Playing

`mechapunk` continues `mechapunk-save.json` from the working directory when
it exists, and otherwise starts a new city. It saves there when you quit.
Start over with `--new`, or pick a city with `--seed N --robots N`; use
`--save FILE` for another save file. A save that cannot be read is moved
aside to `FILE.broken` instead of being overwritten.

| Input             | Action                                         |
| ----------------- | ---------------------------------------------- |
| Click             | Open a building; select a room or robot inside |
| Q / E, PgDn / PgUp | Floor down / up in an open building          |
| Drag (either button) | Orbit the camera                            |
| Wheel             | Zoom                                           |
| WASD / arrows     | Move the camera                                |
| Space             | Pause or resume                                |
| N                 | One minute forward while paused (hold to repeat) |
| 1–5               | Speed: 1, 10, 60, 300 or 1200 game minutes per second |
| F5                | Save now                                       |
| F12               | Screenshot to `screenshots/`                   |
| Esc               | Close the selection, then save and quit        |

### Inspecting

Click a building to open it as a cutaway: the floors below are solid, the
floor you are looking at shows every room as a colored tile, and the floors
above are hidden. Robots in a room sit on its tile. Habitats open on their
first apartment floor; Q goes down to the stores. A panel on the right lists
the floor's rooms and who is in them.

- Click a room for its kind, opening hours, price, and the robots inside
  with when each will leave.
- Click a robot, inside a room or walking in the street, for what it is
  doing and until when, its four needs, credits, home, job and shift, and
  its recent events.

Recent events are kept while the game runs and are not saved, so the list
starts empty after loading.

Walking robots are drawn as yellow spheres moving along their route; robots
in an apartment, workspace or venue are inside their building. Habitats are
cyan towers with colored store signs at street level, workplaces are
magenta, and the dark towers are decoration. Building height follows the
number of floors. Sunlight follows the time of day, and neon lights the
streets at night.

The simulation steps once per game minute on its own thread and publishes a
snapshot after each step. The renderer places walking robots from their
departure and arrival times, so movement stays smooth at any frame rate
and speed.

## Simulation

The simulation advances in steps of one game minute and models the city
down to single rooms:

- Habitats have stores on the ground floor (bars, noodle diners, charging
  bays, repair shops, arcades and parts shops, each with a name, opening
  hours, capacity and prices) and apartments above.
- Workplaces are split into workspaces over their floors and pay a wage per
  shift.
- Every robot has its own apartment and workspace, and is always in a known
  room or walking between two.

Robots have four needs from 0 to 1000: charge, condition, social and fun.
They change every ten minutes with what the robot is doing, and venues
restore some of them; a bar restores more social the more guests it has.

A robot leaves home in time to arrive a little early for its shift (its own
habit plus daily variation) and works it. After work it weighs each open
venue it can afford and fit in: how much the visit would relieve its needs,
the walk, the price and a little whim. It compares that with going home,
stays a while, and decides again. It always gets home early enough for six
hours of rest before its next shift. Walking times come from shortest routes
over the street graph plus stairs.

Runs are deterministic: the same save or seed always produces the same
states and events, on every platform. All simulation state is integral, and
randomness comes from a saved PCG32 generator rather than `<random>`.

Try it from the build directory:

```bash
./mechapunk-sim --seed 42 --robots 200 --days 2 --follow 3
./mechapunk-sim --seed 42 --robots 200 --days 2 --save city.json
./mechapunk-sim --load city.json --hours 12 --all
```

A followed robot's evening reads like this:

```text
Day 1 18:00  LU-775 Lumen left Service Bay 2, Repair Shop 1 for Spark Lot (charging bay, Hab Block 4) (4 min walk)
Day 1 18:04  LU-775 Lumen arrived at Spark Lot (charging bay, Hab Block 4), paid 6 cr, 6 others there, staying 47 min
Day 1 19:50  LU-775 Lumen arrived at The Overclock (bar, Hab Block 6), paid 15 cr, 12 others there, staying 132 min
```

### Saves

Saves are JSON (`format`, `version`, `gameVersion`, `world`). They are
written to a temporary file and then renamed over the target, so a crash
mid-save leaves the old save intact. Loading reads the save into an untyped
JSON tree, upgrades it one version at a time with migrations, then reads it
strictly: missing or unknown fields are errors. Finally it validates
references between entities before the world runs.

Only state is saved. Caches such as walking distances are rebuilt after
loading. `tests/fixtures/saves/README.md` describes how to change the format
without breaking existing saves.

## Updating the engine

```bash
git -C external/NEE fetch
git -C external/NEE checkout <commit or origin/master>
git add external/NEE
```
