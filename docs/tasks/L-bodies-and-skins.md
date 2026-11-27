# Task L: robot bodies and skins

Status: planned on 2026-11-27, nothing done. Save version 29, game 0.29.0.
Steps that change saved state take the next free save version at the time
(rule 3).

## What and why

A robot's body is two numbers today: `Robot::wear` (lasting damage, 0 to
`kMaxWear`, which scraps the robot at the top) and `Needs::condition` (wear and
tear that repair shops and clinics fix). Every robot looks alike apart from
one accent colour (its gang's colour, or one of eight by id), drawn by
`RobotFigure` in `app/Props.cpp`, which is a torso, two legs and two arms of
fixed metal.

This task gives every robot a body of **parts** - head, left hand, right hand
and legs - that wear, break and are repaired one by one. It also gives every
robot a **skin**, the robots' clothes: a shell of plating with a pattern,
colours and a finish, bought at shops, that wears and fades and says who
the robot is. Both feed the simulation, not only the view:

| Part | Wears from | When it is damaged |
| --- | --- | --- |
| Legs | Every minute walked | Walks are slower, so trips take longer; a robot with broken legs takes the metro or stays in |
| Hands | Every shift at work (more in workshops than offices); the dominant hand more | Work performance drops; some jobs need both hands, so the robot may be fired (a careers rule) |
| Head | Age; being robbed or in a fight | Talks go worse (affinity), punctuality slips, memories fade faster |

A skin is chosen by budget and taste. It wears with time and in fights.
Gang members wear their gang's colours, and wealthy robots wear chrome. The
money moves through `Sell` (rule 2).

Every number is a setting (a new `body` and `skin` group in `Settings.hpp`),
so tests can break a leg in a day.

## Open questions for the user (ask before L1)

1. Does `Robot::wear` stay as the total that scraps a robot (the sum or the
   worst of its parts), or does a robot scrap when its head is gone? The
   proposal: `wear` stays the lasting total, and the parts carry today's
   `condition` split four ways.
2. Can a part be lost completely (a missing hand) and be replaced with a
   new or second-hand one? The proposal: yes, at clinics (new) and the black
   market (cheap, sometimes dodgy: today's bad part goes into one part).
3. Are skins only how a robot looks and what it says, or do they protect
   (armour plating against fights)? The proposal: looks and status only at
   first.

## Steps

### - [ ] L1. Parts and a skin are saved state (new module `bodies`, saves)

- New `sim/include/mechapunk/sim/Body.hpp`, `sim/src/Body.cpp`, `tests/BodyTests.cpp`.
- `enum class BodyPart : std::uint8_t { Head, LeftHand, RightHand, Legs };`
  `struct PartState { std::int32_t condition; std::int32_t wear; };`
  `struct Body { std::array<PartState, 4> parts; BodyPart dominantHand; };`
  `struct Skin { std::uint8_t pattern; std::uint32_t primary, secondary;
  std::uint8_t finish; std::int32_t wear; };`, and `Robot::body` and
  `Robot::skin`.
- Save change (rule 3): bump the version. The migration splits each robot's
  `needs.condition` and `wear` across the parts (a derivable fact, as task I
  derived styles). It gives each robot a plain skin in its accent colour and
  a dominant hand picked by a hash of its id. Both are spelled out in the
  migration and never call game code. Also add enum names to `Save.cpp`, a
  new fixture and tests.
- Validation (rule 5): every part's numbers in range, a valid dominant hand,
  a known pattern and finish.
- New robots (`World::Generate`, `AddNewcomer`) get their parts from the same
  draws as today's condition and wear, and their skin and hand from a new
  fixed random stream (never `state_.random`, so today's draws do not move).

### - [ ] L2. Parts wear and are what condition means (world, life-cycle)

- `Needs::condition` becomes the worst part's condition, computed rather than
  stored. If that is too wide a change, keep it as a stored mirror updated in
  one place. Say which in the note.
- Legs wear per walked minute, hands per shift (by workplace type, the
  dominant hand by `body.dominantHandShare`), head with age. The nightly
  `AgeRobots` spreads its wear over the parts.
- The gang fight in `Gangs.cpp` (`wear += 20`, `condition - 100`) and the bad
  part in `Behaviour.cpp` hit one part. Their numbers become settings too; the
  fight's numbers are still literals today.
- This changes results: run the guide's 40-day check at 10000 robots and
  compare the economy and the scrap rate with before. Write both in the note.

### - [ ] L3. Damaged parts change what a robot can do (world, careers, social)

- Legs: a walk's minutes are multiplied by a factor from the legs' condition.
  Task J caches trip minutes per room pair, so apply the factor to the
  walking part of a trip *after* the cache (`PlanTrip` stays per room,
  `StartWalk` and `Decide` scale). Below `body.brokenLegs` a robot only rides
  the metro or stays home.
- Hands: work performance per shift falls with the hands' condition. Jobs
  that need two hands (a setting per workplace type) count the worse hand.
  The existing firing rule then does the rest.
- Head: talk affinity and punctuality fall with the head's condition.
- Tests per part: a robot with its part set low behaves as stated; a healthy
  robot behaves exactly as before L3.

### - [ ] L4. Repairs by part (units, money)

- Repair shops mend hands and legs, clinics mend the head and fit new parts,
  and the black market fits cheap second-hand parts (sometimes dodgy).
  `Decide` picks a repair by the worst part; prices depend on the part.
- Every payment goes through `Sell` (rule 2); `ValidateEconomy` must pass.

### - [ ] L5. Injuries in crime (gangs, chronicle)

- A robbery damages a part of the victim (a draw from `state_.random` in id
  order, rule 1), and a fight damages both robots. Chronicle entries name the
  part ("lost a hand in a fight with ..."). A new `MemoryKind` "was injured"
  is a save change.

### - [ ] L6. Skins: bought, worn, chosen by taste and standing (social, money)

- Shops sell skins. A robot with budget to spare and a worn or out-of-fashion
  skin buys one: gang members pick their gang's colours, rich robots pick
  chrome, others a pattern from their interests. Skins wear daily and in
  fights.
- The money moves through `Sell` at the shop. A skin's price comes from the
  finish (settings).
- Test: over 30 days some robots change skins, gang members wear their gang's
  colour, and the money adds up.

### - [ ] L7. The view draws parts and skins (view-robots, rendering)

- `RobotFrame` gains the parts' conditions and the skin (frame data by id,
  guide section 9.5). `RobotFigure` (`app/Props.cpp`) colours each part from
  the skin, shows dents and sparks on a damaged part, a limp on damaged legs
  (change the walk phase), and a stump for a missing hand. It replaces the
  single accent colour, and the gang colour then comes from the skin.
- No test without a renderer: build, `ContextBudget`, and the user looking.

### - [ ] L8. Bodies and skins in the inspector and the statistics (robot-pages, statistics)

- The robot page gets a Body section: a bar per part, the dominant hand, the
  skin with its pattern, colours and finish, and the robot's last repair and
  injury as links.
- The Statistics window gets injuries, parts replaced and skins bought per
  day. New `DailyStats` fields are a save change (rule 3 table).
- Lists that pick rows go into a tested `app/BodyLists.*` (guide section 9.4).

## Done when

- [ ] Every robot has four parts, a dominant hand and a skin, saved,
      migrated and validated (L1).
- [ ] Parts wear, break, slow and hinder robots, and are repaired one by one
      (L2-L4).
- [ ] Crime injures parts, and skins are bought and worn by taste and
      standing (L5, L6).
- [ ] It all shows in the game, the inspector and the statistics (L7, L8).
- [ ] The guide's task table marks L done.
