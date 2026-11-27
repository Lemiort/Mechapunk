# Task M: skins (chameleon coverage)

Status: planned on 2026-11-27 and revised the same day; nothing done. Save
version 29, game 0.29.0. Skins are **colours only** and change nothing a
robot can do; that is body parts, task L (`docs/tasks/L-body-parts.md`).

Every step has: **Module**, **Depends on**, **Save change**, **Open** (the only
files to read in full), **Do**, **Tests**, **Check**. Do one step, commit, push,
tick it, and stop. No step may change the simulation's results: the seed 7
headless log must stay byte-identical (guide section 2), and only the save
may gain keys.

## What and why

Every robot looks alike today apart from one accent colour, chosen by
`Accent` in `app/CityViewInternal.hpp`: its gang's colour, or one of eight
colours by id.

A skin is **chameleon coverage**: an e-ink layer on every body part of every
robot (head, torso, both hands, legs). Nothing is bought or installed. The
coverage shows colours, changes them at once and for free, and is matte and
limited to a fixed palette, so it **cannot be camouflage**. What a robot
shows:

1. on shift in a job with a **dress code**, its employer's colours;
2. otherwise its **own colours**, saved and chosen when it arrives;
3. a gang member's own primary colour is its gang's colour.

## Decisions (the user, 2026-11-27)

1. Dress codes: police officers, metro drivers and builders always; a
   corporation's workers where their job has one (offices yes, workshops
   not); venue staff by venue kind.
2. Coverage, not paint, and no stealth. Stealth through body modification is
   a later feature.
3. Basic coverage on all parts for every robot from the start.

## Design fixed for the implementer (2026-11-27)

- **The palette:** 16 colours, the only colours coverage can show. Use them
  in this order, so palette index 0 is the first:
  `0x1F3A93` police blue, `0xE8E8E8` white, `0x2E7D32` metro green, `0xFFC107`
  amber, `0xFF8F00` hi-vis orange, `0x212121` black, `0xB71C1C` red,
  `0x6A1B9A` violet, `0x00838F` teal, `0x9E9D24` olive, `0x5D4037` brown,
  `0x546E7A` steel, `0xF06292` pink, `0x26C6DA` cyan, `0xC0CA33` lime,
  `0x8D6E63` tan.
- **Fixed uniforms** (settings, as palette indices): police 0 and 1, metro
  drivers 2 and 3, builders 4 and 5.
- **Other organizations' colours** are derived, never saved:
  `OrganizationColours(id) = {6 + id % 10, (6 + id / 10) % 16}`. The primary
  then never takes a fixed uniform's colour.
- **Which jobs have a dress code:** the police station, the crew room and the
  site office always do. A workspace does when its building's
  `Building::dressCode` is true. That is a new saved flag, set from the
  workplace type at generation; buildings raised later are false. A venue
  job does when `skin.venueStaffDressCode` (0 or 1, default 1) is on.
- **A gang member's own primary** shows as the palette colour nearest its
  gang's `color` (sum of squared RGB differences, the lowest index on a tie).
  This is derived, not saved.
- **Coverage follows the parts** for free: a lost part (task L) is not drawn,
  and a fitted part shows the robot's colours the moment it is fitted.
  Nothing about coverage is saved.

## Steps

### - [ ] M1. Own colours and the dress-code flag are saved state (new module `skins`)

- Module: skins (new: `sim/include/mechapunk/sim/Skin.hpp`, `sim/src/Skin.cpp`,
  `tests/SkinTests.cpp`), plus the save files. Depends on: none. Save
  change: yes.
- Open: `sim/include/mechapunk/sim/World.hpp` (`Robot` only:
  `rg -n "struct Robot" -A 70`), `sim/include/mechapunk/sim/City.hpp` (`Building`
  only), `sim/src/Save.cpp` (top), the end of `sim/src/Migrations.cpp`,
  `sim/src/Migrations.hpp`, `sim/src/Generation.cpp`, `sim/src/LifeCycle.cpp`
  (`AddNewcomer`), and the file holding `kWorkplaceTypes` (`rg -n
  "kWorkplaceTypes" sim/src`).
- Do:
  1. `Skin.hpp`: `inline constexpr std::array<std::uint32_t, 16> kPalette{...}`
     (the design's list), `struct Skin { std::uint8_t primary{11}; std::uint8_t
     secondary{5}; };`, and `std::uint8_t NearestPaletteIndex(std::uint32_t rgb)`.
  2. Add `Skin skin;` to `Robot` and `bool dressCode{false};` to `Building`. Add
     `bool dressCode` to `WorkplaceType`: true for the office-like types, false
     for the workshop-like ones. List the types in the note with your choice.
  3. Generation sets `Building::dressCode` from its type. A robot's own
     colours: primary `6 + (id * 7) % 10`, secondary `(primary + 5) % 16`. That
     is a hash, so no random draw moves; newcomers do the same.
  4. Migration: every `world.robots[i]` gets that `skin`. Every
     `world.city.buildings[i]` gets `dressCode`: true when its `name` starts
     with an office-like type's name, false otherwise. Spell the list out in
     the migration; this one-time read of names is how old saves learn it.
  5. New fixture, the two fixture tests (rule 3).
- Tests: `NearestPaletteIndex` of each palette colour is its own index; new
  robots have colours in range; a save round trip keeps them.
- Check: build, ctest, seed 7 log byte-identical.

### - [ ] M2. What a robot shows (skins)

- Module: skins. Depends on: M1. Save change: settings only (the fixed
  uniforms and `skin.venueStaffDressCode`, by the recipe).
- Open: `sim/src/Skin.cpp`, `sim/include/mechapunk/sim/World.hpp` (public
  lookups only: `FindRobot`, `Rooms()`, `ShiftOf`; `rg -n "\[\[nodiscard\]\]" `
  around them), `sim/include/mechapunk/sim/Gangs.hpp` (`Gang::color`).
- Do: `struct ShownSkin { std::uint8_t primary; std::uint8_t secondary; bool
  uniform; EntityId employer; };` and
  `ShownSkin ShownColours(const World& world, const Robot& robot)`:
  1. If the robot is working (`activity == Working` and in its workspace) and
     its job has a dress code (design above), return the employer's colours
     with `uniform` true. The employer is the workspace room's owner. Police,
     crew and site rooms use the fixed uniform settings.
  2. Otherwise return its own colours, and for a gang member the primary is
     `NearestPaletteIndex(gang.color)`.
- Tests: an officer at the station on shift shows police colours, and at home
  its own; an office worker on shift shows its corporation's colours; a
  workshop worker shows its own; a gang member at home shows its gang's
  nearest colour.
- Check: build, ctest, seed 7 byte-identical except the new keys.

### - [ ] M3. The frame carries what robots show (ui)

- Module: ui. Depends on: M2. Save change: no.
- Open: `app/SimulationHost.hpp` (`RobotFrame`), `app/SimulationHost.cpp` (`Publish`).
- Do: `RobotFrame` gains `std::uint32_t skinPrimary, skinSecondary` (as RGB from
  `kPalette`), `bool uniform` and `sim::EntityId uniformOf`, filled by
  `ShownColours` in `Publish`. Keep `gangColor` until M4 stops using it.
- Check: build, ctest.

### - [ ] M4. The figure is drawn in its skin (rendering, then view-robots)

- Module: rendering (`Props.cpp`), then view-robots and view-core for the
  callers, as separate commits. Depends on: M3. Save change: no.
- Open: `app/Props.hpp`, `app/Props.cpp` (`RobotFigure`), then the callers of
  `RobotFigure` and `Accent` (`rg -n "RobotFigure\(|Accent\(" app`).
- Do: `RobotFigure` takes a primary and a secondary colour in place of
  `accent`. It paints the torso and head in the primary and the arms and legs
  in the secondary, and keeps the joints metal. The callers pass the
  frame's colours. Remove `Accent` and `RobotFrame::gangColor` once nothing
  uses them, and the selected-robot highlight stays as it is. If task L's
  lost parts exist, a lost part is not drawn (L14 draws the stump).
- Tests: none without a renderer. Check: build, ctest, `ContextBudget`.

### - [ ] M5. Skins on the robot and organization pages (robot-pages, then inspector)

- Module: robot-pages, then inspector as a second commit. Depends on: M3.
  Save change: no.
- Open: `app/InspectorRobot.cpp`, then `app/InspectorOrganization.cpp`, and
  `app/Links.hpp` for the link functions.
- Do:
  1. The robot page shows two colour swatches for its own colours and,
     when in uniform, "in <organization> colours, on shift" with
     `OrganizationLink`. A gang member's primary swatch has its `GangLink`.
  2. The organization page shows its two colours and whether its jobs have a
     dress code.
- Check: build, ctest.

## Not in this task

- Stealth, camouflage, or body modifications that would allow them.
- Coverage that protects, or better coverage to buy.

## Done when

- [ ] M1-M5 are ticked: robots show their own, gang or uniform colours as
      the rules say, the game and the inspector show it, the simulation's
      results did not change, and the guide's table marks M done.
