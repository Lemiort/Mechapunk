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
cd Mechapunk
conan editable add external/NEE
conan install . --output-folder=build --build=missing --build=editable -s build_type=Release -s compiler.cppstd=23
cmake --preset conan-release
cmake --build --preset conan-release
```

The engine is the Conan package `nee/0.1.0`. `conan editable add` (once per
machine) makes that package the submodule in `external/NEE`, and
`--build=editable` builds it in place, in `external/NEE/build/Release`. The
game's build then rebuilds that tree first, so engine edits reach the game
in one build. The engine brings GLFW, spdlog, GLM, GLAD, gsl-lite and Dear
ImGui; the game itself only adds glaze for saves and GoogleTest. Use the
same Conan profile for both (for example `-pr:a mingw` instead of the `-s`
settings with LLVM-MinGW).

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
| Double-click a robot or its name | Move the camera to the robot  |
| F                 | Follow the selected robot with the camera      |
| Q / E, PgDn / PgUp | Floor down / up in an open building          |
| Drag (either button) | Orbit the camera                            |
| Wheel             | Zoom                                           |
| WASD / arrows     | Move the camera (stops following)              |
| Space             | Pause or resume                                |
| N                 | One minute forward while paused (hold to repeat) |
| 1–6               | Speed x0.5, x1, x10, x60, x300 or x1200; at x1 a game minute takes two seconds |
| F5                | Save now                                       |
| F12               | Screenshot to `screenshots/`                   |
| Esc               | Close the selection, then save and quit        |

### Inspecting

The interface is drawn with [Dear ImGui](https://github.com/ocornut/imgui)
through NEE's `ImGuiOverlay`. The **City** window shows the day and time,
pause and step, speed, how many robots are resting, working, out and
walking, the frame rate, and buttons to save and take a screenshot. Window positions are
kept in `mechapunk-ui.ini` next to the game.

Click a building to open it as a cutaway: the floors below are solid, the
floor you are looking at is drawn room by room at real scale, and the floors
above are hidden. The open building's own signs switch off; pendant lamps
light its rooms, and a faint light from above keeps dark rooms readable.
Lamps are on where they would be: corridors always, apartments while the
resident is home and awake, venues while open or busy, workspaces while
someone works there.

- Apartment floors have a stairwell, an elevator and a corridor with six
  apartments. Each has a bathroom, a bedroom and a living room with a
  kitchen, behind doors, with windows in the outer walls.
- Stores on a habitat's ground floor and workplace rooms are furnished for
  what they are: bar counters and booths, diner tables, charging posts,
  repair lifts and workbenches, arcade cabinets, shop shelves, furnaces and
  conveyors, server racks, office desks.
- Robots are drawn where they are: asleep in bed at night, on the sofa in
  the evening, on a stool at the bar, standing at a machine at work.

Furniture and robots are built from boxes in `app/Props.cpp`, one catalog
entry per kind, so each can later be replaced by a model. Floor plans are
laid out in `app/Interior.cpp`.

Habitats open on their first apartment floor; Q goes down to the stores. The
**Inspector** window then lists the floor's rooms with who is in them, with a
floor slider.

- Click a room, in the city or in the inspector, for its kind, opening
  hours, price, occupancy, and the robots inside with when each will leave.
- Click a robot, in the city or by its name in the inspector, for what it
  likes, what it is doing and until when, its four needs as bars, credits
  and rent owed, home, job level, wage, shift and progress toward the next
  promotion (or how close it is to being fired), days off, who it knows and how it
  feels about them, what it remembers, and its recent events, including
  who it talked to and about what.
- Names of robots in a robot's relationships are links too: clicking one
  selects that robot and shows the room it is in.
- Names of rooms in the inspector are links: clicking one moves the view to
  that building and floor with the room selected.

Clicks, drags and scrolling over a window stay in the window. Recent events
are kept while the game runs and are not saved, so the list starts empty
after loading.

Walking robots stride along their route, drawn larger the further out the
camera is so they stay visible; robots in an apartment, workspace or venue
are inside their building. Habitats are
cyan towers with colored store signs at street level, workplaces are
magenta, and the dark towers are decoration. Building height follows the
number of floors. Sunlight follows the time of day; at night neon signs
glow and street lamps along every street cast pools of light on the road.

The robot panel has **Look at** and **Follow**. Following keeps the camera
on the robot: it opens the building and floor the robot is in and closes it
again when the robot steps outside. Moving the camera with the keys, or
deselecting the robot, stops following.

The simulation steps once per game minute on its own thread and publishes a
snapshot after each step. The renderer places walking robots from their
departure and arrival times, so movement stays smooth at any frame rate
and speed.

## Simulation

The simulation advances in steps of one game minute and models the city
down to single rooms:

- Habitats have stores on the ground floor (bars, noodle diners, charging
  bays, repair shops, repair clinics, arcades, parts shops and black
  markets, each with a name, opening hours, capacity and prices) and
  apartments above.
- Workplaces are split into workspaces over their floors and pay a wage per
  shift.
- Every robot has its own apartment and, unless it is out of work, a
  workspace, and is always in a known room or walking between two.
- Habitats get their stores in a fixed order: repair shops, charging bays,
  diners and clinics first, so even a small city can keep its robots
  running.

Robots have four needs from 0 to 1000: charge, condition, social and fun.
They change every ten minutes with what the robot is doing, and venues
restore some of them; a bar restores more social the more guests it has.

Apartments have a daily rent, a little higher on upper floors, collected at
midnight. What a robot cannot pay is owed and collected first from later
credits. Robots keep a budget: they skip a venue if paying would leave less
than tonight's rent and what they owe.

Days have weekday names; Day 1 is a Monday. Every robot has two consecutive
days off a week, different for each robot so workplaces stay staffed. On a
day off a robot gets up mid-morning and goes out, comes home for breaks, and
goes out again until the evening.

A robot leaves home in time to arrive a little early for its shift (its own
habit plus daily variation) and works it. After work it weighs each open
venue it can afford and fit in: how much the visit would relieve its needs,
the walk, the price, who is there and a little whim. It compares that with
going home, stays a while, and decides again. A robot that is home by the
evening rests for half an hour to an hour and a half, then weighs going out
again, until 22:00. It always gets home early enough for six
hours of rest before its next shift, and every night ends at home by 03:30.
Travel times come from shortest routes over the street graph plus stairs,
or the metro where it is faster.

### Relationships and memories

Every robot has two interests out of eight: drone racing, circuit bending,
old films, poetry, chess, scrap art, synth music and gambling. Every ten
minutes, robots sharing a venue may start a talk, and coworkers sometimes do
too; each robot is in at most one talk at a time and prefers robots it
likes. A talk is about something both like, or failing that something one of
them likes. Robots with an interest in common mostly get along; robots with
none argue more often than not. A good talk lifts social, more so with a
friend; an argument costs a little fun.

Each robot keeps how it feels about up to twelve robots, from -100 to 100,
as acquaintance, friend, close friend or rival. A bond falls back at a lower
value than it rose, so small swings do not make and break friendships, and
once two robots are friends (or rivals) each talk moves them half as far.
Feelings fade toward indifference when two robots have not talked for two
days, and a robot meeting someone new forgets its least known acquaintance.

Robots remember the twelve latest moments that mattered: becoming friends,
growing close, drifting apart, starting a rivalry, making peace, falling
behind on rent and paying it off. When choosing a venue, robots are drawn to
ones where friends are or are heading, and more strongly to close friends;
they stay away from rivals. In a city of 300, most robots have a friend
after a week or two, close friendships take a few weeks, and a fifth or so
end up with a rival.

### Work and money

Robots hold a job level: junior, regular, senior or lead. Each level pays
15% more than the one below. Every shift adds 0 to 2 points of performance.
A robot that clocks in worn down loses 3: its charge is below 150, or its
condition is below 300 and below half of what its wear allows, so a
well-kept old robot is not punished for its age. One that clocks in
miserable (fun or social below 200) loses 1, and arriving late costs 3.
Enough points earn a promotion (15 for a junior, 10 more for each level up).
At -12 the robot is fired: it lives as if every day were its day off, and
each night has an even chance of being hired into a workspace with an
opening, starting again as a junior.

Each night after rent, a robot owing five nights' rent is evicted to the
cheapest free apartment and its debt written off. A robot with 500 credits
saved sometimes (one night in ten) moves to the priciest free apartment it
could pay for twelve nights. Robots at home walk over to their new place
that night. Promotions, firings, new jobs, moves and evictions are all
memories.

### Metro

Every third intersection each way has a metro station; their entrances
stand on the pavement by the corner, with a cyan M sign. A trip between two
buildings takes the metro when walking to the nearest station, waiting a few
minutes, riding (30 km/h, station to station) and walking on from the
station nearest the destination beats walking all the way. A ride costs
4 cr; robots budget for the fare home, and a robot that cannot spare the
fare without missing tonight's rent walks instead. In a city of 300, about a
third of all trips use the metro. Riders walk to the entrance and vanish
underground until they come up at the far station; the robot panel says
which part of the trip they are on, and following one keeps the camera at
the entrance while it rides.

### Aging, clinics and newcomers

Robots age a day every night and gather wear, lasting damage from 0 to 1000:
2 a night, a little more every 300 days of age, and 5 more for a robot left
in poor condition. A robot's condition can never be better than 1000 minus
its wear, so old robots run down sooner. Repair clinics (open 08:00-20:00,
100 cr) repair condition quickly and take wear away while the robot stays,
and robots value them more the more worn they are. Black markets (open
22:00-05:00, 12 cr) are cheap repairs and good company, but one part in
ten is dodgy and adds 60 wear.

A robot whose wear reaches 1000 breaks down for good that night and is
scrapped. Its friends remember losing it, and a newcomer moves into the
city to take its place: a young robot with a free apartment and, if there
is an opening, a job. Scrapped robots are kept by name only while someone
still remembers them. The robot panel shows age and wear. In a city of 300,
a few robots are scrapped each week at first, fewer once the oldest are
gone; robots that can afford clinics live long.

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
conan install . --output-folder=build --build=missing --build=editable -s build_type=Release -s compiler.cppstd=23
```

Re-run `conan install` after moving the submodule when the engine's
`conanfile.py` changed, so its dependencies and build tree are up to date.
