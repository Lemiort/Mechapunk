# Task L: body parts

Status: planned on 2026-11-27, revised the same day to keep skins apart
(skins are colours only: task M, `docs/tasks/M-skins.md`). Nothing done. Save
version 29, game 0.29.0. Steps that change saved state take the next free
save version at the time (rule 3).

## What and why

A robot's body is two numbers today: `Robot::wear` (lasting damage, 0 to
`kMaxWear`, which scraps the robot at the top) and `Needs::condition` (wear and
tear that repair shops and clinics fix). Nothing a robot can or cannot do
depends on its body, apart from going to be repaired.

This task gives every robot a body of five **parts**: head, torso, left
hand, right hand and legs. Each has its own condition and wear, and each decides
something the robot can do. The parts are about function, not looks:

| Part | What it decides | Wears from | When damaged or lost |
| --- | --- | --- | --- |
| Legs | Whether and how fast the robot walks | Every minute walked | Walks take longer. Below `body.brokenLegs` the robot can only ride the metro or stay in. Lost legs: it cannot leave its room until legs are fitted. |
| Left and right hand | What the robot can hold and which jobs it can do | Every shift (more in workshops than offices), the dominant hand more | Each held item takes one working hand. Work performance falls. A job that needs two hands cannot be done with one. |
| Head | Thinking and talking | Age, being robbed, fighting | Talks go worse (affinity), punctuality slips, memories fade faster. A destroyed head scraps the robot. |
| Torso | Power: the battery and what holds the rest together | Age and every charge cycle | The charge drains faster and holds less, so the robot must charge more often. A destroyed torso scraps the robot, but a worn one can be exchanged by surgery. |

**Holding things** becomes simulation state, not only a picture. Today the
view decides from the room a robot left whether it carries a shopping bag
or a parcel (`Carried` in `app/Props.hpp`). With this task a robot holds
items in its hands:
- shopping from a shop;
- a parcel from the black market;
- an officer's baton on duty;
- a builder's tool on shift.

A robot with no free working hand cannot take one more. So a robot with
one hand that holds a bag cannot also carry a parcel. The view draws what
the simulation says it holds.

Every number is a setting (a new `body` group in `Settings.hpp`), so a test
can break a leg or take a hand in one minute.

## Decisions (the user, 2026-11-27)

1. **The head and the torso are basic parts: a robot cannot exist without
   either.** It is scrapped when its head or its torso is destroyed. Total
   wear no longer scraps it by itself. Age wears both, so old robots still
   die of age.
2. **Parts can be lost and replaced.** A hand or the legs at 0 are lost. A
   clinic fits a new part, and the black market fits a cheap second-hand one
   that is sometimes dodgy (today's bad part).
4. **A torso can be exchanged by robot surgery** before it is destroyed: a
   long, dear clinic visit that gives the robot a new torso with no wear,
   and so more life. The head cannot be exchanged, because it is the robot
   itself. If the user wants head surgery too, it is a later feature.
3. **Skins are not part of this task.** Every part carries basic chameleon
   coverage that shows colours only (task M), changing nothing a robot can
   do. A fitted or exchanged part comes with its own basic coverage.

## Steps

### - [ ] L1. Parts are saved state (new module `bodies`, saves)

- New `sim/include/mechapunk/sim/Body.hpp`, `sim/src/Body.cpp`, `tests/BodyTests.cpp`,
  in a new module in `scripts/context_budget.py`.
- `enum class BodyPart : std::uint8_t { Head, Torso, LeftHand, RightHand, Legs };`
  `struct PartState { std::int32_t condition; std::int32_t wear; bool lost; };`
  `struct Body { std::array<PartState, 5> parts; BodyPart dominantHand; };`
  and `Robot::body`. Pure helpers in `Body.hpp`: `WalkFactor(body)`,
  `CanWalk(body)`, `FreeHands(body, held)`, `HandsForWork(body)` and
  `ChargeFactor(body)` (from the torso).
- `Robot::wear` is replaced by the parts' wear. Today's scrap rule reads the
  head and the torso (L2).
- Save change (rule 3): bump the version. The migration splits each robot's
  `needs.condition` and `wear` across the parts (derivable, as task I
  derived styles) and picks a dominant hand by a hash of the id. Both are
  spelled out in the migration, which never calls game code. Also add enum
  names to `Save.cpp`, a new fixture and tests.
- Validation (rule 5): parts in range, a lost part has condition 0, a valid
  dominant hand, and no living robot with a destroyed head or torso (neither
  can be marked lost).
- New robots (`World::Generate`, `AddNewcomer`) get their parts from today's
  condition and wear draws, and their dominant hand from a new fixed random
  stream (never `state_.random`, so today's draws do not move).

### - [ ] L2. Parts wear, and the head decides life and death (world, life-cycle)

- Legs wear per walked minute and hands per shift, by workplace type and
  with the dominant hand by `body.dominantHandShare`. The head wears with age
  (`body.headAgeShare`). The nightly `AgeRobots` spreads its wear over the
  parts.
- The torso wears with age (`body.torsoAgeShare`) and with every charge at a
  charging bay or at home (`body.torsoChargeWear`).
- Scrapping (decision 1): `AgeRobots` scraps a robot when its head or its
  torso reaches `kMaxWear`, and only then.
- `Needs::condition` becomes the worst working part's condition, computed
  rather than stored. If that is too wide a change, keep a stored mirror
  updated in one place; say which.
- The gang fight in `Gangs.cpp` (`wear += 20`, `condition - 100`, still
  literals) and the bad part in `Behaviour.cpp` hit one part, and their
  numbers become settings.
- This changes results: run the guide's 40-day check at 10000 robots and
  compare the economy and the scrap rate with before. Write both in the note.

### - [ ] L3. Walking depends on the legs (world, metro)

- A walk's minutes are multiplied by `WalkFactor(body)`. Task J caches trip
  minutes per room pair, so apply the factor to the walking part of a trip
  *after* the cache: `PlanTrip` stays per room, while `StartWalk` and `Decide`
  scale the walking legs.
- Below `body.brokenLegs` a robot only takes trips the metro covers, with
  short walks at each end. With lost legs it stays in its room, and work
  and outings fail as being late does today. Say in the note how such a
  robot gets to a clinic: the proposal is that a clinic sends for it, a
  walk of the clinic's choosing at a fee.
- Tests: a robot with damaged legs takes longer by the factor, and one with
  broken legs takes the metro or stays in. A robot with healthy legs walks
  exactly as before L3.

### - [ ] L4. Holding things takes hands (world, units)

- `Robot::held`, a short list of `HeldItem` (shopping, parcel, baton, tool),
  is saved state (save change). A shop visit adds shopping, the black market
  a parcel, an officer on shift a baton, a builder on shift a tool. Arriving
  home drops shopping and parcels; ending a shift drops a baton or tool.
- An item needs a free working hand: `Decide` skips a shop or the black
  market when the robot has no free hand.
- The view draws `held` instead of guessing from the room the robot left
  (`Carried` in `app/CityView.cpp`), so `RobotFrame` gains it (guide section
  9.5).
- Tests: a one-handed robot holding shopping does not go to the black market
  until it has been home; an officer on shift holds a baton and drops it
  after.

### - [ ] L5. Working depends on the hands and the head (careers, social)

- Each workplace type and each staff room (`StaffRules`) says how many hands
  its work needs (a setting per type). With fewer working hands the shift's
  performance falls by `body.missingHandPenalty`, and the existing firing
  rule does the rest. A worn dominant hand counts more than the other.
- The head: talk affinity and punctuality fall with its condition.
- The torso: the charge need falls faster and its effective top is lower by
  `ChargeFactor(body)`, so a robot with a worn torso charges more often.
  Apply it in `UpdateNeeds` (world module) and on arrival at a charging bay.
- Tests per rule, plus: a healthy robot works and talks exactly as before L5.

### - [ ] L6. Repairs, replacement parts and torso surgery (units, money)

- Repair shops mend hands and legs, and clinics mend the head and the
  torso. A lost hand or lost legs are replaced (decision 2): new and dear at
  a clinic, or second-hand and cheap at the black market, sometimes dodgy and
  starting worn. `Decide` picks a repair by the worst part, and prices depend
  on the part.
- Torso surgery (decision 4): when its torso's wear passes
  `body.surgeryWear` and it can pay `body.surgeryPrice` from its savings, a
  robot books an exchange at a clinic. The clinic visit lasts
  `body.surgeryMinutes`, and the robot leaves with a torso of no wear. This is
  how a robot outlives its first torso. Record it in the chronicle.
- Every payment goes through `Sell` (rule 2), and `ValidateEconomy` must pass.

### - [ ] L7. Injuries in crime (gangs, chronicle)

- A robbery damages a part of the victim (a draw from `state_.random` in id
  order, rule 1). A robber needs a free hand to take something. A fight
  damages both robots and can knock an item out of a hand.
- A destroyed head or torso scraps the robot: it is killed in the fight. The
  chronicle names the part ("lost a hand in a fight with ..."), and a new
  `MemoryKind` "was injured" is a save change.

### - [ ] L8. The body in the view, the inspector and the statistics (view-robots, robot-pages, statistics)

- `RobotFigure` (`app/Props.cpp`) shows the parts' state as function, not
  colour (colours are task M): a limp from damaged legs (the walk phase), a
  stump for a lost hand, the held items in the hand that holds them, and
  dents and sparks on a badly worn part.
- The robot page gets a Body section: a bar per part, lost parts, the
  dominant hand, what it holds and in which hand, what its body lets it do
  ("walks 30% slower", "cannot hold more"), and its last repair and injury
  as links. The rows come from a tested `app/BodyLists.*` (guide section 9.4).
- The Statistics window gets injuries, parts lost and parts fitted per day.
  New `DailyStats` fields are a save change (rule 3 table).

## Done when

- [ ] Every robot has five parts and a dominant hand, saved, migrated and
      validated (L1).
- [ ] Parts wear, a destroyed head or torso scraps the robot, and torso
      surgery can extend its life (L2, L6).
- [ ] Legs decide walking, hands decide holding and working, and the head
      decides talking and punctuality (L3-L5).
- [ ] Parts are repaired and replaced, and crime injures them (L6, L7).
- [ ] It shows in the game, the inspector and the statistics (L8).
- [ ] The guide's task table marks L done.
