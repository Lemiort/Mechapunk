# Mechapunk

A cyberpunk city simulation: humanoid robots live, work and spend their
evenings in a 3D city that you observe. Built on the
[NEE](https://github.com/Lemiort/NEE) engine.

## Repository layout

- `external/NEE` – the engine, as a git submodule. Engine features are
  developed and merged in the NEE repository, then picked up here by
  updating the submodule. No game code goes into NEE.
- `app/` – the game executable.
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

## Updating the engine

```bash
git -C external/NEE fetch
git -C external/NEE checkout <commit or origin/master>
git add external/NEE
```
