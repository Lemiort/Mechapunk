# Task M: skins (chameleon coverage)

Status: planned on 2026-11-27, revised the same day with the user's answers.
Nothing done. Save version 29, game 0.29.0. Split from task L: skins are
**colours only** and change nothing a robot can do; that is body parts, task
L (`docs/tasks/L-body-parts.md`).

## What and why

Every robot looks alike today apart from one accent colour, chosen by
`Accent` in `app/CityViewInternal.hpp`: the gang's colour for a member, else
one of eight colours by id.

A skin is **chameleon coverage**: a thin e-ink layer a robot has
**installed** on parts of its body. Coverage on a part shows whatever
colours the robot chooses, and it changes them at once and for free. A part
without coverage shows bare metal. So a robot's look has two halves:

- **What is installed:** which areas carry coverage. This is saved, and
  installing it costs money at a repair shop (`Sell`, rule 2).
- **What it shows:** the colours on that coverage right now. This is derived
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
2. **Not paint:** robots install chameleon coverage on their body parts, and
   then change its colours freely. It is e-ink, so no stealth. Stealth
   through body modification is another feature, later.

## Coverage areas

Coverage goes on five areas: head, torso, left hand, right hand and legs.
The torso is not a body part in task L, but it carries the most coverage.
When task L is done, the four others are its parts. A part that is lost
loses its coverage, and a replacement part comes bare until coverage is
installed on it. If M is done before L, the areas stand on their own and L
links them to its parts.

## Steps

### - [ ] M1. Installed coverage and own colours are saved state (new module `skins`, saves)

- New `sim/include/mechapunk/sim/Skin.hpp`, `sim/src/Skin.cpp`, `tests/SkinTests.cpp`,
  in a new module in `scripts/context_budget.py`.
- `enum class CoverageArea : std::uint8_t { Head, Torso, LeftHand, RightHand, Legs };`
  `struct Skin { std::uint8_t covered; std::uint8_t primary; std::uint8_t secondary; };`
  `covered` is a bit per area. `primary` and `secondary` index the fixed e-ink
  palette in `Skin.cpp`: a palette index, not free RGB, which is what keeps it
  from being camouflage. Add `Robot::skin`.
- New robots arrive with coverage on the torso and head, drawn by
  `skin.startCoveredPercent`, and own colours from the palette. Both come from
  a new fixed random stream, never `state_.random` (rule 1).
- Save change (rule 3). The migration covers every robot's torso and head
  and picks the palette entry nearest the colour it shows today: its gang's
  colour, or the one `Accent` picks by id. The mapping is spelled out in the
  migration. Also a new fixture and tests.
- Validation: palette indices in range and only known area bits.

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
- A dress code wants coverage on the torso. A robot hired into such a job
  without it has torso coverage installed in its first days, and the
  employer pays, through `Sell`, to the repair shop that installs it. That
  is the proposal; confirm with the user when the step starts.
- Tests: an officer shows police colours from clock-in to the end of its
  shift and its own colours outside it; an office worker shows its
  corporation's colours on shift; a workshop worker does not; a gang member
  off duty shows the gang's colour; the money adds up.

### - [ ] M3. Installing coverage (units, money)

- A robot with money to spare beyond its savings target now and then has
  coverage installed on one more area at a repair shop, for
  `skin.installPrice` per area, paid through `Sell`. Its draws come from
  `state_.random` in id order. Changing what coverage shows is free and never
  needs a visit.
- With task L: a lost part loses its coverage, and a fitted part comes bare.
- Test: over 30 days some robots install coverage, nobody pays to change
  colours, and the money adds up.

### - [ ] M4. The view draws what the coverage shows (view-robots, rendering)

- `RobotFrame` gains the covered areas and the colours `ShownColours` gives
  (frame data by id, guide section 9.5). The view never works out a dress
  code itself. This replaces `gangColor` and `Accent`.
- `RobotFigure` (`app/Props.cpp`) paints each covered area in the shown
  primary (torso, head) and secondary (hands, legs). An uncovered area shows
  bare metal, and the joints stay metal. A colour change shows at once, with
  no fade, the way e-ink switches.
- No test without a renderer: build, `ContextBudget`, and the user looking.

### - [ ] M5. Skins in the inspector (robot-pages, inspector)

- The robot page shows which areas are covered, its own colours as swatches,
  and what it shows now and why: "in Kessler Logistics colours, on shift",
  with the organization as a link. A gang member's swatch links to the gang.
- The organization page shows its colours and which of its jobs have a dress
  code.

## Not in this task

- Stealth or camouflage of any kind, and body modifications that would allow
  it.
- Coverage that protects (dropped: skins are colours only).

## Done when

- [ ] Every robot has its installed coverage and own colours, saved and
      migrated (M1).
- [ ] Services and corporations with a dress code put their workers in
      their colours on shift, and gang members show their gang's colour (M2).
- [ ] Robots install coverage now and then and change colours freely (M3).
- [ ] The game and the inspector show it (M4, M5).
- [ ] The guide's task table marks M done.
