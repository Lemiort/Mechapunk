# Mechapunk roadmap

Where the game and its engine stand, and what is left, as of 2026-11-17.
The goal is a cyberpunk robot city at Dwarf Fortress level of detail: every
building, room, store and robot can be seen and inspected.

## Repositories and workflow

- **Mechapunk** (this repo): the game. `sim/` is the headless simulation
  library, `app/` the game window, `tests/` the GoogleTest suites
  (`sim_tests`, `layout_tests`) and save fixtures. Finished work is pushed
  straight to `master`, with no pull request.
- **NEE** (`external/NEE`, also `C:\Users\lemio.BEHEMOTH\Projects\NEE`): the
  engine, consumed as the editable Conan package `nee/0.1.0`. NEE `master`
  stays engine-only and generic, and every change goes through a pull
  request on GitHub (`Lemiort/NEE`) with CI and linters (Black, Ruff, mypy,
  Prettier, markdownlint). Once a NEE PR is merged, update the submodule
  here.
- UI uses plain Dear ImGui calls (`Begin`/`End`, `PushID`/`PopID`), never
  RAII wrappers around them. NEE's `ImGuiOverlay` is fine.
- Saves are glaze JSON with frozen migrations. Any change to saved state
  bumps the save version, adds a migration and a fixture (see
  `tests/fixtures/saves/README.md`). The current save version is 7, game
  version 0.7.0.

### Building and testing

```bash
conan install . --output-folder=build --build=missing --build=editable -pr:a mingw
cmake --build build/build/Release
ctest --test-dir build/build/Release --output-on-failure
```

The build tree is `build/build/Release`. `build/build` is not a build tree.
The game build rebuilds `external/NEE/build/Release` first. Quit a running
`mechapunk.exe` before building, or the link fails.

- `build/build/Release/mechapunk.exe`: the game. It continues
  `mechapunk-save.json` in the working directory. To leave that save alone,
  use `--new --save <scratch file>`.
- `build/build/Release/mechapunk-sim.exe --seed N --robots N --days N --quiet`:
  headless run with daily statistics.
- In-game checks: `tools/drive-game.ps1` sends clicks, keys and F12
  screenshots to the running game. Its comments explain how. Screenshots
  land in `build/build/Release/screenshots/`.

## Milestones

| Milestone | State |
| --- | --- |
| M1 Headless sim | Done: ids, clock, seeded PCG32, city graph, home to work to home, CLI runner, save v1 with fixtures and determinism tests |
| M2 See it | Done: buildings and robots drawn, smooth movement, picking, inspector, time controls |
| M3 Needs and the economy | Done: charge, condition, social, fun; wages, rent, debt, venues, evenings out, days off |
| M4 Social life | Done: interests, talks, bonds (friend, close friend, rival), memories. The "chronicle" is only the inspector's recent events, which are not saved |
| M5 Atmosphere | **Partly done**: day and night, neon signs, street lamps, room lamps, walking animation, emissive surfaces, HDR, tone mapping and bloom. **Missing: rain** |
| M6 Scale and depth | **Partly done**: simulation on a worker thread, breakdowns and repairs (wear, clinics, black markets, scrapping, newcomers). **Missing: simulation level of detail, corporations and factions, construction** |

Also done since the plan was written:

- Careers and moving house: promotions, firing, rehiring, evictions.
- Transit: an elevated metro with platforms, stairs and trains.
- A dense city of 2000 robots by default.
- Parks on free corners.
- Carried items and table props.
- Robots walking through doors, corridors and stairs.
- Cutaway interiors for every floor.

## Engine work from the plan

| # | Engine PR | State |
| --- | --- | --- |
| 1 | `nee` library target | Done (NEE #58) |
| 2 | Fixed-step clock, pause, time scale | Done (NEE #59, #63) |
| 3 | Instance handles with generation counters (closes #31, #34) | Not done. The game resubmits everything each frame through `RenderQueue` |
| 4 | Bounds and frustum culling (#39), renderer statistics (#42) | Not done |
| 5 | Input action mapping, orbit and free-fly camera controllers | Not done in the engine; the game handles its own camera |
| 6 | Picking ray against bounding boxes | Done in the game (`CityView::PickAt`), not in the engine |
| 7 | Versioned serialization | Done in the game (`sim/src/Save.cpp`: migrations, temp-file-then-rename write). Rotating autosaves are missing |
| 8 | Dear ImGui overlay | Done (NEE #62) |
| 9 | Keep the node hierarchy on model import, for rigid-part animation | Not done. Robots and furniture are box props in `app/Props.cpp` |
| 10 | Look: emissive G-buffer channel, HDR, bloom, tonemapping, instanced light volumes or clustered lighting, particle fix (#29) for rain | Emissive (NEE #65), HDR and tone mapping (NEE #66) and bloom (NEE #67) done; more lights and the particle fix not done |

Also done in NEE beyond the plan: screenshots (#60), distance-field text
(#61), the Conan package (#64), and skipping dark lights (#63).

The open NEE architecture-review issues are #20, #24, #27 and #29–#43.
#43 is the tracker.

## Recommended next: finish M5 (atmosphere)

Surfaces glow, light is kept above 1, tone-mapped and blooms. Rain is what
is left.
Do it as separate NEE PRs, each generic and with tests, then use each one
in the game:

1. **Emissive channel.** Done (NEE #65): an emissive texture times an
   emissive color per material (`SetEmissiveTexture`, on the last texture
   unit, white by default; `SetEmissiveColor`, black by default), a sixth
   G-buffer attachment, and the light accumulation target starting from the
   emissive light before lights add on top. The NEE demo has an "Emissive
   buffer" render mode. In the game, neon sign panels, lit windows (four in
   ten), station signs, train strips, lamp bulbs, screens and status lights
   glow, dimmed by day through `MaterialPalette::SetGlow`. Sign point
   lights dropped from 2.5 to 0.03, so they only light a pool around the
   sign. The game uses the color only, since each glowing part is its own
   box; an emissive map of the facade textures could later replace the
   window boxes.
2. **HDR and tonemapping.** Done (NEE #66): the light
   target is RGBA16F and a fullscreen pass tone-maps it with exposure and
   None, Reinhard or ACES (`DeferredPipeline::SetToneMapping`). The game
   uses ACES at exposure 1, with a Display panel to change both live.
   Light powers were left as they are.
3. **Bloom.** Done (NEE #67): `BloomPass` downsamples the
   light above a threshold through half-size levels and upsamples it back,
   and the tone-mapping pass adds it times a strength
   (`DeferredPipeline::SetBloom`). In the game, neon, bulbs and screens
   glow at three times their color, so they pass the threshold of 1 while
   lit windows don't, and bloom strength is 0.4. The Display panel has
   bloom strength, threshold and radius sliders.
4. **Rain.** Repair the particle system (#29), then add rain in the game
   with a weather state in the sim (saved, so it needs a migration). Wet
   streets could come later with a reflection or specular tweak.
5. Optionally, more point and spot lights: the game's lamp pool is 32 for
   the open floor. That would need instanced light volumes or clustered
   lighting.

## After that: M6 (scale and depth)

- **Corporations and factions.** Workplaces belong to corporations;
  robots join factions or gangs. Rivalries and loyalties feed the existing
  bonds, memories and venue choice. Black markets tie in.
- **Construction.** The city grows: new buildings on park corners as the
  population grows, and building projects that employ robots.
- **Simulation level of detail.** Cheaper updates for robots nobody is
  looking at, so the city can grow well past 2000.
- **Chronicle.** A saved city history of notable events (deaths,
  friendships, firings), viewable in the UI.

## Known gaps and rough edges

- Indoor walks avoid walls (checked by `layout_tests`) but can clip through
  furniture in some rooms, such as charging bays with no aisle.
- Everyone takes the stairs; the elevator is decoration.
- A robot leaving a room starts from its favourite spot, so it can jump
  when it had been given a different spot.
- At home, robots switch between bed and sofa without walking.
- The fifth building at an intersection sits a block away, and its walk to
  the intersection cuts diagonally.
- Shopping bags show only while walking straight out of the shop.
- Not yet seen in the running game: bags, bar bottles, robots on the
  stairs, robots waiting on platforms.
