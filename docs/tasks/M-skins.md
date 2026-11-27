# Task M: skins

Status: planned on 2026-11-27, nothing done. Save version 29, game 0.29.0.
Split from task L on the user's word: skins are **colours only**. They
change nothing a robot can do; that is body parts, task L.

## What and why

Every robot looks alike today apart from one accent colour, chosen by
`Accent` in `app/CityViewInternal.hpp`: the gang's colour for a member, else
one of eight colours by id. A skin replaces it with a robot's own colours,
and with the colours of what it is doing:

- **Its own skin:** a primary and a secondary colour, given when it arrives and
  kept for life. It is repainted when the robot joins or leaves a gang, or
  when it pays for a new look.
- **Uniforms on duty:** a police officer on shift wears police colours, a
  metro driver on shift the Metro Authority's, and a builder on shift a
  site's high-visibility colours. Off duty each wears its own skin again. A
  uniform is *derived* from the robot's job and shift, never saved, so it
  cannot drift from what the robot is doing.
- **Gang colours:** a member's own skin uses its gang's colour (today's
  `gangColor`), so a gang still reads at a glance.

Skins change no need, price, walk or choice, except the one purchase in M3,
which is money through `Sell` (rule 2).

## Open questions for the user (ask before M1)

1. Do corporations put their own workers in company colours on shift, or only
   the city's services (police, metro, builders)? The proposal: services
   only at first.
2. Should robots pay to repaint (M3), or is a skin given once and changed
   only by joining or leaving a gang? The proposal: a repaint at a repair
   shop now and then, when a robot has money to spare.

## Steps

### - [ ] M1. A robot's own skin is saved state (new module `skins`, saves)

- New `sim/include/mechapunk/sim/Skin.hpp`, `sim/src/Skin.cpp`, `tests/SkinTests.cpp`.
- `struct Skin { std::uint32_t primary; std::uint32_t secondary; };` (0xRRGGBB)
  and `Robot::skin`. New robots get one from a palette in `Skin.cpp`, drawn
  from a new fixed random stream (never `state_.random`, rule 1).
- Save change (rule 3). The migration gives each robot the colour it shows
  today (its gang's colour, or the one `Accent` picks by id, spelled out in
  the migration) as primary, and a darker shade as secondary. New fixture and
  tests.
- Validation: nothing to check beyond the colour fitting in 24 bits.

### - [ ] M2. Uniforms on duty and gang colours (careers, gangs)

- A pure `SkinShown(const Robot&, ...)` in `Skin.cpp`: the uniform of the
  robot's job while it is on shift (police, metro driver, builder), else its
  own skin. The uniform colours are settings (`skin.policePrimary` and so on).
- Joining a gang repaints the own skin's primary in the gang's colour, and
  leaving gives the robot's old primary back. This happens in `ReviewGangs`,
  where joining and leaving happen.
- Tests: an officer shows police colours from clock-in to the end of its
  shift and its own colours outside it; a robot that joins a gang shows the
  gang's colour.

### - [ ] M3. A new look now and then (units, money)

- Only if the user wants it (question 2). A robot with money to spare beyond
  its savings target now and then repaints at a repair shop: a new primary
  and secondary from the palette, for `skin.repaintPrice`, paid through
  `Sell`. The draw comes from `state_.random` in id order.
- Test: over 30 days some robots repaint and the money adds up.

### - [ ] M4. The view draws skins (view-robots, rendering)

- `RobotFrame` gains the colours `SkinShown` gives, published by the
  simulation (guide section 9.5). The view never works out a uniform
  itself, and it replaces `gangColor` and `Accent`.
- `RobotFigure` (`app/Props.cpp`) paints the torso and head in the primary and
  the limbs in the secondary, and keeps the metal joints.
- No test without a renderer: build, `ContextBudget`, and the user looking.

### - [ ] M5. Skins in the inspector (robot-pages)

- The robot page shows its own skin as two swatches, and when it is in
  uniform, which uniform and why ("on duty at the Police Station" as a
  link). A gang member's swatch links to the gang.

## Done when

- [ ] Every robot has its own colours, saved and migrated (M1).
- [ ] Officers, drivers and builders wear uniforms on duty, and gang members
      their gang's colour (M2).
- [ ] The game and the inspector show it (M4, M5); repaints if wanted (M3).
- [ ] The guide's task table marks M done.
