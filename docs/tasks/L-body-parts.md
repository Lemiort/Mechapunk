# Task L: body parts

Status: planned on 2026-11-27 and revised the same day; L1 done on 2026-11-28
(save version 42, game 0.42.0), L3a and L3b done on 2026-11-28 (no save change:
the body is already saved state, and step L3 was split on 2026-11-28 into L3a
and L3b - see why under L3a), L2a done on 2026-11-28 (no save change, and step L2 was split into L2a and
L2b - see why under L2a), L2b1 - the first of the parts L2b was split into on
2026-11-28 - done the same day (no save change), the rest still planned.
Revised
again on 2026-11-28 for the save rule: the game was at save version 41 and
`kOldestSupportedSaveVersion` is 41 too, so **no step of this task adds a
migration** - a save step bumps `kCurrentSaveVersion` (the next free version is
**42**) and raises `kOldestSupportedSaveVersion` with it, replacing the fixture
and its tests. The game is 0.41.0. Skins are colours only, in task M
(`docs/tasks/M-skins.md`). Steps that change saved state take the next free
save version (rule 3); settings follow the guide's "adding a setting" recipe.

Every step has: **Module**, **Depends on**, **Save change**, **Open** (the only
files to read in full), **Do**, **Tests**, **Check**. Do one step, commit, push,
tick it, and stop. Steps that change results say so. All others must leave
the seed 7 headless log byte-identical (guide section 2) apart from new save
keys.

## What and why

A robot's body is two numbers today: `Robot::wear` (lasting damage that
scraps the robot at `kMaxWear`) and `Needs::condition` (wear and tear that
repair shops and clinics fix). This task gives every robot five **parts**,
and each part decides something the robot can do:

| Part | Decides | Wears from | Damaged or lost |
| --- | --- | --- | --- |
| Head | Talking, punctuality | Age, robberies, fights | Talks go worse, punctuality slips. **Destroyed: the robot is scrapped.** |
| Torso | Power (the battery) | Age, every charge | Charge drains faster and tops out lower. **Destroyed: the robot is scrapped.** Can be exchanged by surgery before that. |
| Left hand, right hand | Holding things, working | Every shift, the dominant hand more | Each held item needs a free working hand. Work needing two hands goes badly with one. |
| Legs | Walking | Every minute walked | Walks are slower. Broken: metro only. Lost: crawls, and only to a clinic. |

**Holding things** becomes simulation state: shopping from a shop, a parcel
from the black market, a baton for an officer on shift, a tool for a builder
on shift. The view draws what the simulation says, instead of guessing from
the room the robot left (`Carried` in `app/CityView.cpp`).

## Decisions (the user, 2026-11-27)

1. The head and the torso are basic parts: without either a robot cannot
   exist. It is scrapped when its head or torso is destroyed, and total wear
   no longer scraps it by itself.
2. Hands and legs can be lost and replaced: new at a clinic, or cheap
   second-hand and sometimes dodgy at the black market.
3. A torso can be exchanged by surgery at a clinic before it is destroyed.
   The head cannot be exchanged, because it is the robot.
4. Skins are colours only (task M). Every part carries basic chameleon
   coverage, and a fitted part comes with its own.

## Design fixed for the implementer (2026-11-27)

The design keeps every step small and lets most of them leave the headless
log unchanged:

- A part has a **condition** from 0 to 1000 and a **lost** flag, and nothing
  else. Parts start at 1000. They lose condition only through the new rules
  of this task (walking, work, fights, robberies), and only repairs give it
  back.
- `Robot::wear` keeps its meaning (lasting damage from age and neglect, which
  clinics reduce) and **caps the head and the torso**: their condition is never
  above `kMaxWear - wear`. A pure `void CapBody(Robot&)` in `Body.cpp` applies
  the cap; call it wherever `wear` grows. So a robot whose `wear` reaches
  `kMaxWear` has a destroyed head and torso and is scrapped on the same day
  as today.
- `Needs::condition` stays what it is today: the robot's general upkeep need
  that repair shops restore. The parts are separate from it.
- The head and the torso are never lost: at condition 0 the robot is
  scrapped instead (decision 1).
- When a rule hurts "one part", it picks the part by `(robot.id + tick) % 5`,
  not by a random draw, so `state_.random` does not move (rule 1) and the
  rest of the city's story stays the same.

## Steps

### - [x] L1. Body parts are saved state (new module `bodies`)

- Module: bodies (new: `sim/include/mechapunk/sim/Body.hpp`, `sim/src/Body.cpp`,
  `tests/BodyTests.cpp`), plus the save files. Depends on: none. Save change:
  yes.
- Open: `sim/include/mechapunk/sim/World.hpp` (the `Robot` struct only:
  `rg -n "struct Robot" -A 70`), `sim/src/Save.cpp` (the `glz::meta` blocks at the
  top), `sim/include/mechapunk/sim/Save.hpp` (the version numbers and the
  comment above them), `sim/src/Generation.cpp`, `sim/src/LifeCycle.cpp` (`AddNewcomer` only).
- Do:
  1. In `Body.hpp`:
     `enum class BodyPart : std::uint8_t { Head, Torso, LeftHand, RightHand, Legs };`,
     `struct PartState { std::int32_t condition{1000}; bool lost{false}; };` and
     `struct Body { std::array<PartState, 5> parts; BodyPart dominantHand{BodyPart::RightHand}; };`,
     plus `PartState& Part(Body&, BodyPart)` (and a const one) and `void CapBody(Robot&)`.
  2. Add `Body body;` to `Robot`, and the enum names to `Save.cpp`.
  3. Generation and newcomers: the parts stay at 1000, then `CapBody` runs.
     The dominant hand is the left when `id % 10 == 0`, so no random draw
     moves.
  4. No migration (rule 3, revised 2026-11-28): raise `kOldestSupportedSaveVersion` in
     `Save.hpp` together with `kCurrentSaveVersion`, leaving the `kMigrations` table in
     `Save.cpp` empty, so a version 41 save is refused with the game's plain message
     instead of upgraded. Fix the comment above those numbers in `Save.hpp`, which
     still promises a migration for every change: a body only has to be right for a
     game that built it, which is what the bullet above does.
  5. Replace `tests/fixtures/saves/v41-city.json` with a v42 fixture made by the guide's
     command, its README row, and retarget `CurrentVersionFixtureLoadsAndRoundTrips`
     to it. No `Version41FixtureMigratesAndRuns`: version 41 saves do not load.
- Tests (`BodyTests.cpp`): `CapBody` caps the head and torso only; a new
  city's robots have capped bodies; a save round trip keeps the body.
- Check: build, ctest, seed 7 log byte-identical, save differs only by the
  new keys.

### - [x] L2a. The parts are checked on load — split from L2 on 2026-11-28

- Module: validation. Depends on: L1. Save change: no.
- Open: `sim/src/Validation.cpp` (the robot loop), `sim/include/mechapunk/sim/Body.hpp`.
- Do: in the robot loop of `ValidateWorldState`: every part's condition is in
  `[0, 1000]`; a lost part has condition 0; the head and the torso are not lost;
  the dominant hand is `LeftHand` or `RightHand`.
- Tests: `tests/ValidationTests.cpp` (a new file, registered as the fourth file
  of the `validation` module): the save of a new town, with one number or one
  flag changed in one robot's body, is refused by `World::FromState` with a
  message naming the part and the rule.
- Check: build, ctest. Nothing in the simulation reads a part yet, so the
  headless log is untouched.

### L2b. The wear cap on the head and the torso is checked on load — split on 2026-11-28 into L2b1 to L2b6

- Depends on: L1, L3a, L2a. Save change: no new version (no key changes), but
  `tests/fixtures/saves/v42-city.json` is regenerated (same version 42: the
  body keys are already there, only their values move).
- **Why L2b was split again.** The rule itself is four lines of the validation
  module. Everything else is states that break it: the v42 fixture, written
  before step L3a capped the parts wherever wear grows, and four test files
  that write a state by hand. Six modules in one step, and the guide's rule is
  one module per step, so each module gets a part: **L2b1 (money), L2b2
  (seed-town), L2b3 (life-cycle), L2b4 (bodies), L2b5 (saves), L2b6
  (validation)**.
- **The rule comes last, not first** (the note under L2a suggested validation
  first, and it cannot be: a part must leave every test passing). Adding the
  rule while the states are still uncapped fails 6 of 244 tests, checked by
  hand on 2026-11-28 (the rule patched into `Validation.cpp`, built, `ctest`,
  then reverted): `Save.CurrentVersionFixtureLoadsAndRoundTrips`,
  `SeedTown.RichNewcomersBringMoreSavings` and `Body.BodySurvivesASaveRoundTrip`
  fail their load assertion, and `LifeCycle.WornOutRobotsAreScrappedAndReplaced`,
  `LifeCycle.DestroyedHeadOrTorsoScrapsAtMidnight` and
  `LifeCycle.NewcomersSkipSiteHomes` **segfault**, because `LifeCycleTests.cpp`'s
  `Load` helper hands back an empty world once `FromState` refuses the state.
  `Economy.ScrappedRobotsLeaveTheirThingsBehind` is the seventh and is already
  gone: part L2b1 repaired it. Repairing a hand-written state the way the game
  does - `CapBody` after the wear - cannot change any test's outcome, because
  nothing reads a part except step L3b's scrapping, and the cap pins the parts
  to the wear that already kills the robot on the same night. So the first five
  parts are independent of each other and of the order; the validation part
  must be last.
- Found by `rg -n "\.wear\s*=" tests` plus the suite above. `CareersTests.cpp`
  line 271 and `WorldTests.cpp` line 38 set `wear = 0` and need nothing.
  `LifeCycleTests.cpp` line 128 (`ClinicsTakeWearAway`) passes today, but only
  by luck: the test sets `wear = 500` and lowers the head to 100, and the torso
  is at or below 500 only because that patient's wear had already reached 500
  during the run that chose it. Part L2b3 caps it anyway.

### - [x] L2b1. A hand-worn robot gets the cap too (money) — part 1 of L2b

- Module: money. Depends on: L2a. Save change: no.
- Open: `tests/EconomyTests.cpp` (`ScrappedRobotsLeaveTheirThingsBehind`, line
  ~274).
- Do: after `robot.wear = sim::kMaxWear - 1;` call `sim::CapBody(robot);`, the
  way the game does wherever wear grows (step L3a; `CapBody` comes with
  `World.hpp`, which the test already includes). Its head and its torso come to
  1; the next midnight, one minute later than this test runs, lands them on 0
  and step L3b scraps the robot, exactly as the test expects. The load at line
  ~289 goes through `World::FromState`, which validates, so part L2b6 refuses
  this state without the cap.
- Tests: no new test. `Economy.ScrappedRobotsLeaveTheirThingsBehind` must pass
  as before.
- Check: build, ctest (the money module stays under 25k:
  `python scripts/context_budget.py`). Nothing outside the test changed, so the
  headless log is untouched.

### - [ ] L2b2. A hand-worn robot gets the cap too (seed-town) — part 2 of L2b

- Module: seed-town. Depends on: L2a. Save change: no.
- Open: `tests/SeedTownTests.cpp` (`RichNewcomersBringMoreSavings`, line ~162).
- Do: the loop that sets ten robots to `kMaxWear - 1` calls `sim::CapBody` on
  each of them after the wear.
- Tests: no new test; `SeedTown.RichNewcomersBringMoreSavings` must still pass
  (the ten die at the next midnight, which is what brings the newcomers).
- Check: build, ctest.

### - [ ] L2b3. The hand-written life-cycle states get the cap — part 3 of L2b

- Module: life-cycle. Depends on: L2a. Save change: no.
- Open: `tests/LifeCycleTests.cpp`.
- Do: four places, five wear sites.
  1. Line ~128 (`ClinicsTakeWearAway`): `sim::CapBody(robot);` after
     `robot.wear = 500;`. The torso this test leaves alone is at or below the
     cap of 500 today only because that patient's wear had already reached 500
     in the run that chose it; the cap makes the state hold whichever robot the
     run picks. The head is already at 100, well under the cap, so the
     assertions under it still hold (the head rises by exactly the wear the
     clinic takes away, the legs do not move).
  2. Line ~159 (`WornOutRobotsAreScrappedAndReplaced`): `sim::CapBody(doomed);`
     after the wear.
  3. Lines ~240-242 (`DestroyedHeadOrTorsoScrapsAtMidnight`): `sim::CapBody` on
     all three after their wear and before the two `condition = 0` lines, so the
     surviving basic part of each of the two broken robots sits at the cap (500
     and 1) instead of whole, and the `EXPECT_GT` under them still holds.
  4. Line ~378 (`NewcomersSkipSiteHomes`): `sim::CapBody(doomed);` after the wear.
- Tests: no new test; all four `LifeCycle` tests must pass unchanged.
- Check: build, ctest.

### - [ ] L2b4. The round-trip body is written under the cap (bodies) — part 4 of L2b

- Module: bodies. Depends on: L2a. Save change: no.
- Open: `tests/BodyTests.cpp` (`BodySurvivesASaveRoundTrip`, lines ~146-151).
- Do: the front robot of a seed 5 city of 20 robots is 136 days old when the city
  is built, so its wear is `WearAtAge(136)` = 302 and the cap 698: the hand-written
  head of 700 sits above it. Pick conditions clearly under the cap instead (a
  head at 300, for instance), keeping the point of the test: numbers and flags a
  new city never starts with, so the round trip proves they travel. Read the
  robot's `wear` in the test if a fixed number feels brittle.
- Tests: no new test; `Body.BodySurvivesASaveRoundTrip` must pass.
- Check: build, ctest.

### - [ ] L2b5. The v42 fixture is regenerated with the parts capped — part 5 of L2b

- Module: saves. Depends on: L3a. Save change: no new version; the fixture of
  the current version is replaced, the format is unchanged.
- Open: `tests/SaveTests.cpp` (the body block at ~375),
  `tests/fixtures/saves/README.md`,
  `sim/include/mechapunk/sim/Save.hpp` (the two version numbers: they stay 42
  and 0.42.0).
- Do:
  1. `build/build/Release/mechapunk-sim.exe --seed 33 --robots 100 --minutes
     28370 --save tests/fixtures/saves/v42-city.json` from the repository root:
     the same command, the same version, over the same file. Its heads move
     down (its first robot had a head of 978 at wear 120, and step L3a now pins
     the basic parts to `kMaxWear - wear` wherever wear grows), and the file
     loads through the rule part L2b6 adds.
  2. The body block of `SaveTests.cpp` (~375): from now on the head and the
     torso **equal** `sim::kMaxWear - robot.wear` in a save this game built,
     not "capped by the wear it was born with"; the round trip and the "at
     least one robot is below whole" check stay.
  3. `tests/fixtures/saves/README.md`: the draft's sentence about wear at birth
     is not in the README (it is the comment inside `SaveTests.cpp`); if the
     README is touched, it is to say the body values are pinned to the robot's
     wear, not fixed at birth.
- Tests: `Save.CurrentVersionFixtureLoadsAndRoundTrips` on the new file.
- Check: build, ctest. Not a pure move: the fixture's body values move on
  purpose; say so in the commit.

### - [ ] L2b6. The cap is refused on load (validation) — part 6 of L2b

- Module: validation. Depends on: L2b1 to L2b5. Save change: no.
- Open: `sim/src/Validation.cpp` (the body block L2a added, line ~261),
  `sim/include/mechapunk/sim/Body.hpp`, `tests/ValidationTests.cpp`.
- Do: in that block, for the head and the torso only: the condition is never
  above `kMaxWear - robot.wear` (`wear` is already checked to lie in `[0,
  kMaxWear]`, so the cap is never negative). Hands and legs are not capped:
  step L12 replaces them.
- Tests: `Validation.RejectsBasicPartsAboveTheWearCap`: one more than the cap
  on the head, and one more on the torso, is refused with a message naming the
  part, the cap and the wear; and the cap exactly is accepted.
- Check: build, ctest, and the headless check `--seed 1 --robots 10000 --days
  40 --quiet`: the runner loads states through `FromState`, so a city whose
  parts are capped wrongly would show here. Nothing reads a part, so the log
  stays byte-identical.

**Why L2 was split.** The rules themselves are four lines of one module, and
they stay there. What reaches out is the consequence of the `kMaxWear - wear`
rule: it holds in every state the game writes since step L3a, but not in two
kinds of state that already exist - the v42 fixture, made before L3a, and four
test files that write a state by hand (three set `wear` near `kMaxWear` and
load it through `FromState`, which validates, and one of them segfaults once
the load is refused and a helper's world comes back empty; `BodyTests.cpp`
writes part conditions above the cap). Switching that rule on therefore needs
a fixture for the `saves` module and repairs in the `money`, `seed-town`,
`life-cycle` and `bodies` modules: not one step. The other four rules hold
everywhere today, so L2a is the validation module on its own and everything
still passes.

The note below was written while doing L1 and delayed L2 until after L3a. It
is about the game's own states only and misses the fixture and the hand-written
states above.

- Note (found while doing L1): the rule "not above `kMaxWear - wear`" only
  holds from step L3 on, which is where `CapBody` starts running wherever
  wear grows; until then a running city's head sits above that cap, so doing
  L2 before L3 makes a real city fail validation and breaks
  `CurrentVersionFixtureLoadsAndRoundTrips`. Do L2 after L3a, which is where
  the cap and the mend are put at every place that writes `wear`.

### - [x] L3a. The head and the torso follow every change of wear — split on 2026-11-28

- Module: bodies (`Body.hpp`, `Body.cpp`, `BodyTests.cpp`), then every place
  that writes `wear`. Depends on: L1. Save change: no. The log must stay
  **byte-identical**; the saved *values* of the head and the torso change,
  because they are now kept up to date instead of only at birth.
- Open: `sim/include/mechapunk/sim/Body.hpp`, `sim/src/Body.cpp`,
  `sim/src/LifeCycle.cpp` (`AgeRobots`), `tests/BodyTests.cpp`,
  `tests/LifeCycleTests.cpp`, plus the wear sites:
  `rg -n "\.wear\s*[+=]|wear =" sim/src` (Behaviour.cpp's black market,
  World.cpp's clinic, Gangs.cpp's fight; Generation.cpp and
  `AddNewcomer` already cap, from L1).
- Do:
  1. `void MendBody(Robot&, std::int32_t wearTaken)`: the head and the torso
     get back as much condition as the wear taken away, never above the cap.
  2. `CapBody` after every place wear grows (nightly wear, `badPartWear`,
     a fight's 20) and `MendBody` where a clinic takes `clinicWearRepair`
     away.
  3. Nothing yet reads a part's condition, so the city behaves as before.
- Tests: `Body.MendBodyFollowsTheWearItTook` and
  `Body.MendBodyNeverGoesAboveTheCap`;
  `LifeCycle.TheBasicPartsFollowTheWearBothWays` (after 10, 20, 30 and 40
  nights every robot's head and torso are exactly `max(0, kMaxWear - wear)`);
  `LifeCycle.ClinicsTakeWearAway` extended: the head rises by exactly the wear
  the clinic took, the legs do not move.
- Check: build, ctest, seed 7 byte-identical log; the save differs only by
  the head and torso conditions.

**Why the split, and what was wrong in the draft.** L3 as drafted ("cap the
head and the torso: their condition is never above `kMaxWear - wear`", plus
"scrapped when the head's or the torso's condition is 0") is **not** byte-
identical: a one-way cap makes the head remember the *worst wear ever reached*,
while the old rule looks at today's wear, and clinics repair wear
(`behaviour.clinicWearRepair` 2 per 10 minutes at a clinic). A robot whose
dodgy part (`behaviour.badPartWear` 60, one visit in ten at a black market)
pushed wear to `kMaxWear` one morning keeps a head at 0 forever, and the
next midnight the parts rule kills it although the wear rule let it live.
With 1000 robots that is about 2 extra deaths a day, and it broke
`Settings.ConstructionKnobsRaiseASiteInTwoNights` (seed 5, 1000 robots, 3
days: the deaths free homes and jobs, so the shortage never runs long enough
to start a site). So `MendBody` had to come first: with the parts pinned to
the wear in both directions, the two rules pick the same robot on the same
night and L3b's swap is a no-op for the log.

**Make room first (its own commit, the guide's recipe).** `world` was at
24880 of 25000 tokens and step L3a's calls took it to 25063, so
`World::Patrol` and `World::NextPatrolAt` moved to `sim/src/BehaviourPatrols.cpp`
(byte for byte), registered in `sim/CMakeLists.txt` and as the new module
`officer-patrols` in `scripts/context_budget.py` and `docs/MODULES.md`.
`world` fell to 23881. Verified as a pure move: the seed 7 log and save are
byte-identical.

Found and left alone (pre-existing, task L6 will rewrite these lines): the
fight in `Gangs.cpp` adds `first.wear += 20` **without** clamping to `kMaxWear`,
so wear can pass `kMaxWear` and such a city would be refused by
`ValidateWorldState` if it were saved before the next midnight. `CapBody`
and `MendBody` use `max(0, kMaxWear - wear)` and are safe with it.

### - [x] L3b. The head and the torso decide scrapping (life-cycle)

- Module: life-cycle. Depends on: L1, L3a. Save change: no. The log must stay
  **byte-identical**: after L3a the parts are pinned to the wear in both
  directions, so `head == 0 or torso == 0` at a midnight is exactly the
  robots `wear >= kMaxWear` picked that night.
- Open: `sim/src/LifeCycle.cpp` (`AgeRobots`).
- Do: a robot is scrapped when its head's or its torso's condition is 0, in
  place of `wear >= kMaxWear` (a local `Destroyed(const Robot&)` in the
  anonymous namespace; hands and legs at 0 never scrap, decision 2). Nothing
  else changes: the cap and the mend of L3a are already everywhere wear moves.
- Tests: `LifeCycle.DestroyedHeadOrTorsoScrapsAtMidnight` (a torso set to 0,
  and a head set to 0, on robots at half wear: both gone at the next
  midnight, at the same minute a robot whose wear runs out is scrapped);
  `LifeCycle.BrokenHandsAndLegsDoNotScrap` (hands and the legs at 0, alive
  two days later).
- Check: build, ctest, seed 7 byte-identical.

### - [ ] L4. Walking wears the legs (world)

- Module: world. Depends on: L1, J0 (room in `world`). Save change:
  settings only. The log stays byte-identical, because nothing reads leg
  condition until L8.
- Open: `sim/src/Behaviour.cpp` (`Arrive`).
- Do: add the setting `body.legWearPerHundredMinutes` (1). On arrival, the
  legs lose `walked minutes × that / 100` condition, never below 0. Walked
  minutes are `nextActionAt - departedAt` of the walk that just ended.
- Tests: a robot that walks 1000 minutes has 10 less leg condition.
- Check: build, ctest, seed 7 byte-identical except the new keys.

### - [ ] L5. Work wears the hands (careers)

- Module: careers (23979 of 25000: make room first, as its own commit).
  Depends on: L1. Save change: settings only. Byte-identical log, because
  nothing reads hands until L10.
- Open: `sim/src/Careers.cpp` (`EndShift`).
- Do: add the settings `body.handWearPerShift` (2) and `body.dominantHandPercent`
  (70). At the end of a shift the dominant hand loses that share of
  `handWearPerShift` (rounded up) and the other hand the rest.
- Tests: after 10 shifts the dominant hand has lost more than the other.
- Check: build, ctest, seed 7 byte-identical except the new keys.

### - [ ] L6. Fights and bad parts hurt one part (gangs, then world)

- Module: gangs, then world (the black market's bad part, in `Arrive`) as a
  second commit. Depends on: L1, L3. Save change: settings only.
  Byte-identical log, because the hurt part is picked without a draw.
- Open: `sim/src/Gangs.cpp` (the fight: `rg -n "Fight at" sim/src/Gangs.cpp`),
  then `sim/src/Behaviour.cpp` (`rg -n "badPart" sim/src/Behaviour.cpp`).
- Do: the fight's literals `20` and `100` become the settings `crime.fightWear`
  (20) and `crime.fightConditionLoss` (100), with the same effect as now. Each
  fighter also loses `crime.fightPartDamage` (150) condition on the part picked
  by `(id + tick) % 5`. A bad part also costs `body.badPartDamage` (100) on the
  part picked the same way. If that takes the head or torso to 0, the robot
  is scrapped at the next midnight (L3's rule).
- Tests: a fight hurts one part of each fighter; a bad part hurts one part.
- Check: build, ctest, seed 7 byte-identical except the new keys.

### - [ ] L7. One trip time for a body (metro)

- Module: metro (`Transit.cpp`). Depends on: L1. Save change: settings only.
- Open: `sim/src/Transit.cpp`, `sim/include/mechapunk/sim/Body.hpp`.
- Do:
  1. Add the settings `body.slowLegsCondition` (500), `body.brokenLegsCondition`
     (100) and `body.crawlFactor` (500, percent).
  2. A pure `std::uint32_t WalkFactorPercent(const Body&)`: 100 at or above
     `slowLegsCondition`, rising linearly to 300 at `brokenLegsCondition`, and
     `crawlFactor` when the legs are lost.
  3. `World::TravelMinutesFor(const Robot&, EntityId from, EntityId to)`:
     `PlanTrip` as today, then the walking minutes are scaled by the factor.
     A metro trip's walking minutes are `minutesToStation` plus the minutes
     after alighting; the minutes underground are not scaled. With broken
     legs a walking-only trip of more than 10 minutes returns
     `std::numeric_limits<std::uint32_t>::max()` ("cannot").
- Tests: a robot with legs at 100% condition gets today's minutes; at the
  broken condition, walking trips take 3 times as long; a lost-legs robot
  crawls.
- Check: build, ctest, seed 7 byte-identical: nobody calls the new function
  yet.

### - [ ] L8. Robots walk as their legs let them (outings, then world)

- Module: outings (`Decide`), then world (`StartWalk`, `LatestHomeTick`) as a
  second commit. Depends on: L7, J0. **Changes results.** Save change: no.
- Open: `sim/src/Outings.cpp`, then `sim/src/Behaviour.cpp`.
- Do:
  1. `Decide` and `StartWalk` use `TravelMinutesFor`, and skip a venue it
     says "cannot" reach.
  2. A robot with lost legs is only offered clinics (it crawls there).
  3. If J6 is done, its trip rows must keep the walking and metro minutes
     apart so the factor can be applied; extend the row to two numbers.
- Tests: a robot with broken legs only goes where the metro takes it; one
  with lost legs only goes to a clinic; a healthy robot behaves as before.
- Check: build, ctest, 40-day numbers in the note.

### - [ ] L9. Held items are saved state (bodies)

- Module: bodies. Depends on: L1. Save change: yes.
- Open: `sim/include/mechapunk/sim/Body.hpp`, `sim/src/Body.cpp`,
  `sim/src/Save.cpp` (`glz::meta`), `sim/include/mechapunk/sim/Save.hpp` (the
  two version numbers),
  `sim/src/Validation.cpp` (the robot loop).
- Do:
  1. `enum class HeldItem : std::uint8_t { Shopping, Parcel, Baton, Tool };`,
     `Robot::held` as a `std::vector<HeldItem>`, and the pure
     `std::uint32_t FreeHands(const Robot&)`: working hands (not lost and
     condition above `body.brokenHandCondition`, a new setting, 100) minus the
     items held.
  2. No migration (rule 3, revised): raise `kOldestSupportedSaveVersion` with
     `kCurrentSaveVersion` and replace the fixture; `Robot`'s default gives a new
     robot an empty list.
  3. Validation: never more items than working hands.
- Tests: `FreeHands` for two hands, one hand, one hand holding shopping.
- Check: build, ctest, new fixture.

### - [ ] L10. Robots pick things up and put them down (world, then careers)

- Module: world, then careers as a second commit. Depends on: L9. **Changes
  results** only where a hand is missing. Save change: no.
- Open: `sim/src/Behaviour.cpp` (`Act`'s `Visiting` case, `Arrive` at home),
  then `sim/src/Careers.cpp` (clock-in and `EndShift`).
- Do:
  1. Leaving a shop adds `Shopping`, and leaving the black market adds
     `Parcel`, when `FreeHands > 0`. Arriving home drops both.
  2. Clocking in at a police station adds `Baton`, and at a site office
     `Tool`. `EndShift` drops them.
  3. In `Decide` (outings module, its own commit), a shop or the black market
     is skipped when `FreeHands` is 0.
- Tests: a one-handed robot holding shopping skips the black market until it
  has been home; an officer holds a baton on shift and none after.
- Check: build, ctest; the seed 7 run is byte-identical (nobody lacks a hand
  in a new city).

### - [ ] L11. Hands decide work, the head talking and punctuality, the torso charge

- Module: careers (hands), then social (head talks), then world (punctuality
  and charge), as three commits. Depends on: L1. **Changes results.** Save
  change: settings only.
- Open (one per commit): `sim/src/Careers.cpp` (where a shift's performance is
  scored: `rg -n "performance" sim/src/Careers.cpp`); `sim/src/Social.cpp`
  (where a talk's affinity change is computed); `sim/src/World.cpp`
  (`UpdateNeeds`) and `sim/src/Behaviour.cpp` (`ScheduleNextCommute`).
- Do:
  1. Hands: each `WorkplaceType` and staff room gets `handsNeeded` (1 or 2; a
     field in the table, offices 1, workshops 2). With fewer working hands,
     a shift loses `body.missingHandPenalty` (5) performance points.
  2. Head: a talk's affinity change falls by `(1000 - head condition) /
     body.headTalkDivisor` (200). Punctuality: `(1000 - head condition) /
     body.headLateDivisor` (100) extra minutes late.
  3. Torso: while not charging, `charge` falls by
     `100 + (1000 - torso condition) / body.torsoDrainDivisor` (10) percent of
     today's drain.
- Tests per rule, plus: a robot with all parts at full condition behaves
  exactly as before.
- Check: build, ctest, 40-day numbers in the note.

### - [ ] L12. Repairs, replacement parts and torso surgery (venues, then outings)

- Module: the venue code that applies repairs (find it: `rg -n
  "clinicWearRepair|RepairShop|Clinic" sim/src`), then outings for `Decide`.
  Depends on: L1, L8. **Changes results.** Save change: settings only.
- Open: the file `rg` finds for the repairs (likely `sim/src/Behaviour.cpp`'s
  `Arrive` or `sim/src/World.cpp`), `sim/src/Outings.cpp` (`Decide`, for the
  second commit), `sim/include/mechapunk/sim/Body.hpp`.
- Do:
  1. A repair shop mends the worse of hands and legs, and a clinic the head
     and the torso.
  2. A lost hand or lost legs are replaced at a clinic for
     `body.newPartPrice` (300), or at the black market for
     `body.usedPartPrice` (80). A black-market part is dodgy with today's
     `behaviour.badPartPercent` and starts with `body.usedPartWear` (300) wear.
  3. Torso surgery: when `wear` passes `body.surgeryWear` (700) and the
     robot's savings cover `body.surgeryPrice` (2000), `Decide` sends it to a
     clinic. The visit takes `body.surgeryMinutes` (240). It lowers `wear` by
     `body.surgeryWearRemoved` (500) and sets the torso's condition to the new
     cap: a new torso, so more life. The head keeps its condition.
  4. Every payment goes through `Sell` (rule 2).
- Tests: each of the four above; the money adds up.
- Check: build, ctest, 40-day numbers in the note.

### - [ ] L13. Robberies hurt, injuries are remembered (gangs, then social)

- Module: gangs, then social (`MemoryKind`) as a second commit. Depends on:
  L6. **Changes results.** Save change: yes (a new `MemoryKind` name).
- Open: `sim/src/Gangs.cpp` (`ConsiderCrime`: the robbery), then
  `sim/include/mechapunk/sim/Social.hpp` (`MemoryKind`), `sim/src/Social.cpp` (where
  a memory is added: `rg -n "Remember(" sim/src/Social.cpp`), `sim/src/Save.cpp`
  (`glz::meta<MemoryKind>`).
- Do: a robbery hurts one part of the victim by `crime.robberyDamage` (60),
  picked by `(id + tick) % 5`. A robber needs a
  free hand (`FreeHands > 0`) to rob. A new `MemoryKind::Injured` is
  remembered by the injured robot.
- Tests: a robbed robot has a hurt part and remembers it; a robot with no
  free hand does not rob.
- Check: build, ctest, new fixture.

### - [ ] L14. The figure shows the body (ui, then rendering, then view-core)

- Module: ui (`RobotFrame`, `Publish`), then rendering (`Props.cpp`), then
  view-core (`CityView.cpp`), as three commits. Depends on: L9, L10. Save
  change: no.
- Open (one per commit): `app/SimulationHost.hpp` and `.cpp`; `app/Props.hpp` and
  `app/Props.cpp` (`RobotFigure`); `app/CityView.cpp` (where `Carried` is chosen).
- Do:
  1. `RobotFrame` gains the five parts' conditions, their `lost` flags and
     `held`.
  2. `RobotFigure` takes them: a limp when the legs are below
     `slowLegsCondition` (halve the swing of one leg), no hand drawn for a lost
     hand, the held items in the hands that hold them, and a dark dent box on
     a part below 300.
  3. The view passes `held` and stops guessing from the room the robot left.
- Tests: none without a renderer. Check: build, ctest, `ContextBudget`.

### - [ ] L15. The body on the robot page (robot-pages, new module `body-lists`)

- Module: body-lists (new: `app/BodyLists.hpp`, `app/BodyLists.cpp`,
  `tests/BodyListsTests.cpp`, in `mechapunk_layout`), then robot-pages. Depends
  on: L14. Save change: no.
- Open: `app/OrganizationLists.hpp`, `app/OrganizationLists.cpp` and
  `tests/OrganizationListsTests.cpp` (the pattern to copy), `app/InspectorRobot.cpp`
  (for the second commit), `app/SimulationHost.hpp` (`RobotFrame` only).
- Do: `BodyLists` gives the rows (part, condition, wear, lost, held item) and
  the sentences ("walks 2.5 times slower", "one hand free", "a new
  torso would add life"). The robot page draws them in a Body section with
  `ProgressBar`s. Test the rows from a frame built from a generated city
  (guide section 9.4), and never skip a test.
- Check: build, ctest.

### - [ ] L16. Injuries and parts in the statistics (statistics)

- Module: statistics. Depends on: L12, L13. Save change: yes (`DayCounters`
  fields: `world.today` and every `world.history[i].counted`).
- Open: `sim/include/mechapunk/sim/Economy.hpp` (`DayCounters`), the places
  L12 and L13 changed (to add `++state_.today.x`), `app/Statistics.cpp` (the
  Society tab), `sim/include/mechapunk/sim/Save.hpp` (the two version numbers).
- Do: count `injuries`, `partsLost`, `partsFitted` and `torsoSurgeries` per
  day, and plot them in the Society tab with `PlotCount`.
- Tests: a day with an injury counts it.
- Check: build, ctest, new fixture.

## Done when

- [ ] L1-L16 are ticked, robots' parts wear, break, slow, limit and are
      repaired, the head and torso decide life, and the guide's table marks
      L done.
