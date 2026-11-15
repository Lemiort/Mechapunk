# Mechapunk

A cyberpunk city simulation: humanoid robots live, work and spend their
evenings in a 3D city that you observe. Built on the
[NEE](https://github.com/Lemiort/NEE) engine.

## Repository layout

- `external/NEE` – the engine, as a git submodule. Engine features are
  developed and merged in the NEE repository, then picked up here by
  updating the submodule. No game code goes into NEE.
- `app/` – the game executable.
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

## Simulation

The simulation advances in steps of one game minute. Each robot has a home
and a workplace with fixed shift hours. It leaves home in time to arrive a
little early (its own habit plus some daily variation), works its shift,
and walks home. Walking times come from shortest routes over the street
graph.

Runs are deterministic: the same save or seed always produces the same
states and events, on every platform. All simulation state is integral, and
randomness comes from a saved PCG32 generator rather than `<random>`.

Try it from the build directory:

```bash
./mechapunk-sim --seed 42 --robots 200 --days 2 --follow 3
./mechapunk-sim --seed 42 --robots 200 --days 2 --save city.json
./mechapunk-sim --load city.json --hours 12 --all
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
