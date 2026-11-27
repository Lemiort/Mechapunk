# Task M: skins (chameleon coverage)

Status: planned on 2026-11-27, revised the same day with the user's answers.
Nothing done. Save version 29, game 0.29.0. Split from task L: skins are
**colours only** and change nothing a robot can do; that is body parts, task
L (`docs/tasks/L-body-parts.md`).

## What and why

Every robot looks alike today apart from one accent colour, chosen by
`Accent` in `app/CityViewInternal.hpp`: the gang's colour for a member, else
one of eight colours by id.

A skin is **chameleon coverage**: a thin e-ink layer on every part of a
robot's body. Every robot has **basic coverage on all five parts** (head,
torso, both hands, legs); nothing is bought or installed. Coverage shows
whatever colours the robot chooses, and changes them at once and for free.
So a robot's look is:

- **What it shows:** the colours on its coverage right now. This is derived
  from what the robot is doing and is never saved, so it cannot drift:
  - on duty with a dress code, the employer's colours;
  - otherwise, its own colours (saved, chosen when it arrives);
  - a gang member's own colours carry its gang's colour.

The coverage is e-ink: matte, opaque, from a fixed palette. It **cannot be
used for stealth** (no matching the surroundings, no fading out). Body
modifications that would allow that are a separate, later feature and not
part of this task.

## Decisions (the user, 2026-11-27)

1. **Dress codes:** the city's services and corporations alike. Police
   officers, metro drivers and builders always wear their organization's
   colours on shift. A corporation's workers do so where the job has a dress
   code: some office jobs do, and a workshop may not.
2. **Not paint:** robots carry chameleon coverage on their body parts and
   change its colours freely. It is e-ink, so no stealth. Stealth through
   body modification is another feature, later.
3. **Basic coverage on all parts** for every robot, from the start. Better
   coverage (more palette, glow) could be a later upgrade; it is not part of
   this task.

## Coverage and body parts

Coverage is on the five body parts of task L: head, torso, left hand, right
hand and legs. A part that is lost loses its coverage, and a fitted or
exchanged part (task L, step L6) comes with its own basic coverage, so there
is nothing to buy. If M is done before L, the five areas stand on their own
and L links them to its parts.

## Steps

### - [ ] M1. Own colours are saved state; coverage is on every part (new module `skins`, saves)

- New `sim/include/mechapunk/sim/Skin.hpp`, `sim/src/Skin.cpp`, `tests/SkinTests.cpp`,
  in a new module in `scripts/context_budget.py`.
- `struct Skin { std::uint8_t primary; std::uint8_t secondary; };`: the
  robot's own colours, as indices into the fixed e-ink palette in `Skin.cpp`.
  They are palette indices, not free RGB, which is what keeps coverage from
  being camouflage. Add `Robot::skin`. Coverage itself is not stored: every
  part that is not lost has it (decision 3).
- New robots get their own colours from a new fixed random stream, never
  `state_.random` (rule 1).
- Save change (rule 3). The migration picks the palette entry nearest the
  colour each robot shows today (its gang's colour, or the one `Accent` picks
  by id) as primary, and a darker one as secondary. The mapping is spelled
  out in the migration. Also a new fixture and tests.
- Validation: palette indices in range.

### - [ ] M2. Dress codes (careers, generation)

- Every organization has its colours: a pure `OrganizationColours(id, kind,
  sector)` from a hash of the id onto the palette. Fixed ones for City Hall's
  police, the Metro Authority and builders come from settings. They are not
  saved, because they are derived.
- A dress code belongs to a job. The police station, the crew room and the
  site office always have one. Each workplace type says whether its workers
  have one (offices yes, workshops not; a field of `kWorkplaceTypes` in
  `City.cpp`), and so does each venue kind for its staff (a setting per
  kind).
- A pure `ShownColours(const Robot&, tick, ...)`: on shift in a job with a
  dress code, the employer's colours; otherwise the robot's own colours,
  with a gang member's primary in its gang's palette colour.
- Every robot has coverage, so any robot can follow a dress code: no
  purchase, no hiring rule.
- Tests: an officer shows police colours from clock-in to the end of its
  shift and its own colours outside it; an office worker shows its
  corporation's colours on shift; a workshop worker does not; a gang member
  off duty shows the gang's colour.

### - [ ] M3. Coverage follows the parts (bodies, if task L is done)

- A lost part shows nothing: it is gone. A part fitted by a clinic or the
  black market, or a torso exchanged by surgery (task L, step L6), shows
  the robot's current colours at once, because it comes with basic coverage.
- Nothing to pay and nothing saved beyond L's parts. If L is not done yet,
  skip this step and tick it when L lands.
- Test: a robot that loses a hand and has one fitted shows its colours on
  the new hand.

### - [ ] M4. The view draws what the coverage shows (view-robots, rendering)

- `RobotFrame` gains the colours `ShownColours` gives
  (frame data by id, guide section 9.5). The view never works out a dress
  code itself. This replaces `gangColor` and `Accent`.
- `RobotFigure` (`app/Props.cpp`) paints the torso and head in the shown
  primary and the hands and legs in the secondary. The joints stay metal,
  and a lost part is not drawn (task L draws the stump). A colour change shows at once, with
  no fade, the way e-ink switches.
- No test without a renderer: build, `ContextBudget`, and the user looking.

### - [ ] M5. Skins in the inspector (robot-pages, inspector)

- The robot page shows its own colours as swatches,
  and what it shows now and why: "in Kessler Logistics colours, on shift",
  with the organization as a link. A gang member's swatch links to the gang.
- The organization page shows its colours and which of its jobs have a dress
  code.

## Not in this task

- Stealth or camouflage of any kind, and body modifications that would allow
  it.
- Coverage that protects (dropped: skins are colours only).

## Done when

- [ ] Every robot has basic coverage on all parts and its own colours,
      saved and migrated (M1).
- [ ] Services and corporations with a dress code put their workers in
      their colours on shift, and gang members show their gang's colour (M2).
- [ ] Coverage follows the parts: fitted parts show the robot's colours
      at once (M3).
- [ ] The game and the inspector show it (M4, M5).
- [ ] The guide's task table marks M done.
