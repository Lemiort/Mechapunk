# Task B: gangs

Status: B1-B6 done; review fixes B7-B8 done (with leftovers, see B9e);
gang details B9 split into B9a-B9e on 2026-11-20 (B9a done, rest open).
Save change done (v18). Do task A first: the inspector uses its links.

## What the user asked for

"Gangs: rival or friendly to each other, always rival to police. Gang
members can rob other robots who are not in their gang." Unions come later
(task G).

## Read first

- `docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5 (rule 3 for the save change).
- `docs/MODULES.md`. Gangs get their **own module** `gangs`:
  `sim/include/mechapunk/sim/Gangs.hpp`, `sim/src/Gangs.cpp`,
  `tests/GangTests.cpp`, plus `World.hpp`. Do not grow `Social.cpp` or
  `Behaviour.cpp` beyond one-line hooks.
- Officers exist (task A of the old plan, done): a robot whose job room is
  a `RoomKind::PoliceStation` is an officer; on shift it is `Working`,
  at the station or at the venue in `Robot::patrol`.

## Design

```cpp
// Gangs.hpp
enum class Stance : std::uint8_t { Friendly, Neutral, Rival };
struct GangRelation { EntityId gang; Stance stance{Stance::Neutral}; };
struct Gang {
    EntityId id;              // From state.ids.
    std::string name;         // "Chrome Jackals", "Rust Saints", ...
    std::uint32_t color{};    // 0xRRGGBB: the members' accent in the view.
    EntityId turf;            // A habitat building it claims, or none.
    std::vector<GangRelation> relations;  // Sorted by gang id; never itself.
};
// WorldState: std::vector<Gang> gangs;            (sorted by id)
// Robot: EntityId gang;  std::uint32_t arrests{};
```
No gang kitty: loot stays in members' credits, so money rules need nothing
new. Robbery is `World::Pay(victim, robber)`, fines are `PayBill` to City
Hall.

Daily statistics (`DayCounters`): `robberies`, `robbedCredits`,
`arrests`, `fights`, `joinedGangs`, `leftGangs`. `DailyStats`:
`gangMembers`. Memories (`MemoryKind`): `JoinedGang`, `LeftGang`,
`WasRobbed`, `RobbedSomeone`, `Arrested`. Events (`EventKind`):
`Robbed`, `Arrested`, `Fought`, `JoinedGang`, `LeftGang`.

## Review of B1-B6 (2026-11-20)

Checked by reading the code, the tests, and a 10-day run at 10000 robots
(seed 1). Gangs are founded, robots join, rob and get arrested, and money
still adds up. But:

- `Careers.SaversMoveUp` fails since B4: the one rich robot in that test is
  now robbed. The commits were made with a failing test.
- B4 asked for tests of robbery, arrests and friendly gangs; none exist.
  `CrimeValidationPasses` checks only that the world validates.
- Membership grows by about 70 a day with no end (754 of 10000 by day 10),
  because "young and in a dorm" covers almost every robot in a new city.
- 0 fights in 10 days at 10000 robots.

Every bug found is a bullet in B7 or B8, with where it is and what to do.

## Steps

### - [x] B1. Data and save version 18 (saves module + Gangs.hpp)

Open: `Gangs.hpp` (new), `World.hpp` (only `Robot`, `WorldState`,
`EventKind`), `Economy.hpp` (only `DayCounters`, `DailyStats`),
`Social.hpp` (only `MemoryKind`), `Save.hpp`, `Save.cpp`,
`Migrations.hpp`, end of `Migrations.cpp`, `tests/SaveTests.cpp`.

- Add every type and field above at once, so there is one save change.
  New enum values get names in `Save.cpp` and in `MemoryText` /
  `Describe` (plain sentences; `rg -n "MemoryText|case EventKind" sim/src`).
- `V17ToV18` (done; this is what it does, by JSON path):
  - `world.gangs = []`;
  - every `world.robots[i]`: `gang = 0`, `arrests = 0`;
  - the six new `DayCounters` fields = 0 in `world.today` and in every
    `world.history[i].counted` (never directly on `history[i]`);
  - the new `DailyStats` field `gangMembers = 0` on every
    `world.history[i]`.
- Validation (`Validation.cpp`): gangs sorted by unique id, relations
  sorted, pointing at other existing gangs, no relation with itself; a
  robot's gang exists; turf is a habitat or none.
- Fixture `v18-city.json` and save tests (rule 3). Commit.

### - [x] B2. Founding gangs (gangs module)

Open: `Gangs.hpp`, `Gangs.cpp` (new), `GangTests.cpp` (new),
`Generation.cpp` (only the end of `World::Generate`), `sim/CMakeLists.txt`,
`tests/CMakeLists.txt`, `scripts/context_budget.py`.

- `World::FoundGangs(Pcg32& random)`: one gang per 1500 robots, at least
  2, at most 6, from `Pcg32::FromSeed(seed, kGangStream)` (a new fixed
  stream number). Names and colours from fixed lists. Turf: habitats nearest
  the city's edge, one each. Relations: each pair random (friendly 25%,
  neutral 45%, rival 30%), forced so at least one pair is rival.
- Old saves have no gangs: `ReviewGangs` (B3) founds them on the first
  night when `gangs` is empty, using `state_.random`.
- Tests: a new city has gangs with symmetric relations and at least one
  rival pair; the save round-trips. Commit.

### - [x] B3. Joining and leaving (gangs module)

Open: `Gangs.cpp`, `GangTests.cpp`, `World.cpp` (only `Step`'s
midnight order), `Social.hpp` (only `Remember`, bonds lookup).

- `World::ReviewGangs()` at midnight, right after `ReviewJobs`.
- Join: robots out of work, or in debt, or broke, or younger than 120 days
  and living in a dorm. Chance 2% a night each; 4 times that if a close
  friend is a member; never if a close friend is an officer, or the robot is
  an officer. It joins the friend's gang, else the gang whose turf is
  nearest its home (`TravelMinutes`). Officers never join.
- Leave: 1% a night when employed and not in debt; always at 3 arrests.
- Robots in id order; draws from `state_.random` (rule 1).
- Tests (population level, 300 robots, 20 days): some join, some leave,
  no officer is a member, the counters match the events. Commit.

### - [x] B4. Robbery, arrests, fights (gangs module)

Open: `Gangs.cpp`, `GangTests.cpp`, `Social.cpp` (only the loop that
pairs robots in the same room every 10 minutes; find it with
`rg -n "Talk\(" sim/src/Social.cpp`), `Money.cpp` (only `Pay`, `PayBill`).

- One hook in that loop: `ConsiderCrime(a, b)` for each pair, implemented
  in `Gangs.cpp`.
- Robbery: at night (20:00 to 04:00), a member with a non-member or a member
  of a gang that is not friendly: 3% per pair per 10 minutes. Takes
  `min(victim.credits, 20 + victim.credits / 4)` with `Pay`. Event and
  memories; the victim's bond to the robber drops by 60.
- Never with an officer on shift in the room: then the officer arrests the
  would-be robber instead. An officer arriving on patrol where a member
  robbed in the last hour also arrests. Arrest: fine `min(credits +
  savings, 100)` to City Hall (`PayBill`), `arrests += 1`, memory,
  event.
- Always rivals: an officer and a member talking lose 20 affinity.
- Gang war: members of rival gangs in one room lose 30 affinity, and fight
  at 1%: both lose condition 100 and gain wear 20; event.
- Tests: robbery moves credits and money adds up; no robbery with an officer
  present; arrests fine and count; friendly gangs never rob each other.
- Run 40 days at 10000 robots: robberies a few per hundred robots a day,
  money created stays small. Commit.

### - [x] B5. Statistics (statistics module)

Open: `EconomyStatistics.cpp` (only `RecordStatistics`),
`app/Statistics.cpp`, `tools/sim_runner/main.cpp` (only the daily print).

- `RecordStatistics` fills `gangMembers`. The Statistics window gets a
  "Crime" tab: plots of robberies, robbed credits, arrests, fights, gang
  members; a table of gangs (name as a link to the gang page from B6,
  colour swatch, members, turf as a `BuildingLink`, relations).
- The runner prints a `crime:` line. Commit.

### - [x] B6. Inspector and view (inspector, view-robots modules)

Open: `app/Links.*`, `app/Inspector.cpp` (robot page only), a new
`app/InspectorGang.cpp`, `app/SimulationHost.hpp/.cpp` (`RobotFrame`,
`Publish`), `app/CityViewInternal.hpp` (only `Accent`).

- `Selection` gains `gang`; `GangLink`; a gang page: colour, turf,
  relations (each a `GangLink`), members (`RobotLink`s, clipped).
- Robot page: its gang (link) and arrests.
- `RobotFrame` gains `gangColor` (0 for none); `Accent` uses it so
  members wear their gang's colour. Commit.

### - [x] B7. Fix the gang simulation (gangs module)

Open: `sim/src/Gangs.cpp`, `tests/GangTests.cpp`, `sim/src/Social.cpp` (only
`Remember`'s counting switch), `tests/CareersTests.cpp` (only
`SaversMoveUp`). Each fix comes with a test that fails without it.

- **Members rob their own gang.** In `ConsiderCrime`, two members of the
  same gang go to the "both members" branch, and `StanceOfGang` of a gang
  with itself returns Neutral, so one robs the other. Same gang: no robbery,
  no fight. Test: put two robots of one gang alone in a room at 22:00, step
  many times, no robbery.
- **Robberies and arrests are counted twice**: once in `ConsiderCrime` and
  again in `Remember` (`WasRobbed`, `Arrested` cases). Remove the cases
  in `Remember`. Test: the day's `robberies` equals the number of
  `Robbed` events.
- **Arrests without a crime.** With an officer in the room, every pair of a
  member and a possible victim at night is an arrest, without the 3% roll.
  Roll first; only a robbery that would happen becomes an arrest instead.
- **The officer rule is wrong.** "An officer and a member lose 20 affinity"
  checks whether *any* officer is in the room and lowers the bond of
  `first`, who may be anyone. It must be: one of the pair is an officer on
  shift, the other a member; both bonds drop (`ChangeBond` both ways).
  Handle both orders of the pair.
- **Fights**: also lower condition by 100 (as written in B4), not only wear.
  Test with a forced pair of rival members, many steps, and check the
  `fights` counter matches `Fought` events.
- **Memories**: `ReviewGangs` never calls `Remember` for `JoinedGang` and
  `LeftGang`. Add them (the gang's id cannot go in `other`, which is a
  robot: leave it empty).
- **The fine fallback** `ChangeMoney(..., EmissionReason::Founding)` is
  wrong: City Hall always exists. Remove the fallback; `PayBill` to City
  Hall.
- **Old saves never get gangs.** At the start of `ReviewGangs`, if
  `state_.gangs` is empty, call `FoundGangs(state_.random)`. Test: load
  the v17 fixture, run past midnight, gangs exist.
- **Relations use the wrong pair index.** `findStanceIdx` in `FoundGangs`
  gives (1,2) the same slot as (0,1) for three gangs. The index of pair
  (i, j), i < j, of n gangs is `i * (2 * n - i - 1) / 2 + (j - i - 1)`.
  Test: with 6 gangs, all 15 pairs use 15 different slots (make the formula
  a small function and test it directly).
- **Joining never levels off.** Drop "younger than 120 days and in a dorm"
  as a reason on its own; it may only double the chance for a robot that is
  out of work, in debt or broke. Leaving: 2% a night when employed and not
  in debt. Run 40 days at 10000 robots: members should settle between 2%
  and 8% of the city. Write the numbers into the commit message.
- **Speed**: `hasOfficerInRoom` and the officer check walk all robots for
  every pair. Build, once per `Socialize` call, the set of rooms that hold
  an officer on shift (a sorted vector or `std::set` of room ids), and look
  it up.
- **`SaversMoveUp`**: keep the test's idea (a saver moves up) and make
  robbery unable to spoil it: run it with no gangs (`state.gangs.clear()`
  and every robot's `gang` = 0 before `Load`).
- Then: all tests pass, and the 40-day run at 10000 robots adds up. Compare
  `wages at N%` with a run of commit `70303c5` (before gangs): the 10-day
  run fell from 99% to 90%. If gangs cause it, say so in the commit message.

### - [x] B8a. The gang id in the robot frame (ui)

Open: `app/SimulationHost.hpp/.cpp` (`RobotFrame`, `Publish`).

- **Gangs are found by colour.** `RobotFrame` has only `gangColor`, and
  the robot page, the gang page and the Crime tab find a gang by matching
  colours. Add `sim::EntityId gang` to `RobotFrame` and set it in
  `Publish`; the pages match on it in B8b and B8c.
- `RobotFrame::arrests` is a `uint8_t` cast from `uint32_t`: make it
  `uint32_t`.

### - [x] B8b. The gang links in the inspector (inspector)

Open: `app/Links.*`, `app/InspectorGang.cpp`, `app/Inspector.cpp` (the
robot page). The robot page and the gang page find a gang by matching
colours: match on `RobotFrame.gang` instead.

- **`GangLink` breaks the link rules**: `ColorButton("")` has an empty
  label (the ImGui id error the user saw in task A) and the name is a plain
  `Selectable` with `SpanAllColumns`. Draw the swatch with
  `ColorButton("##swatch", ..., ImGuiColorEditFlags_NoTooltip)` inside
  `PushID(gang)`, and draw the name through the private `Link()`.
- **The gang page** draws turf and relations with plain `Selectable`s:
  use `BuildingLink` and `GangLink`. `RgbColor` prints red and blue but
  not green: use `std::format("#{:06X}", color)`.
- **The robot page** puts the arrests in the gang link's tooltip and again
  beside it; show "Arrests: N" once, on its own line, also for robots who
  left their gang.

### - [x] B8c. The gangs in the Crime tab (statistics)

Open: `app/Statistics.cpp` (`DrawCrime`). Find the gang by
`RobotFrame.gang` instead of matching colours.

- **The Crime tab** shows the gang as "[name]" text, the turf as "on turf",
  and relations as one long string; its `ColorButton("")` repeats in every
  row. Use `GangLink`, `BuildingLink` for the turf, and one `GangLink`
  per relation with its stance.

### - [x] B8d. The gang colour in the city (view-core, view-interiors)

Open: `app/CityViewInternal.hpp` (`Accent`), `app/CityView.cpp` and
`app/CityInteriors.cpp` (the three `Accent` call sites).

- **Gang colours never show in the city** (B6 asked for it): `Accent`
  ignores `gangColor`. Give `Accent` the robot's frame (or its
  `gangColor`) and use the gang colour when it is not 0, at all three call
  sites.

### B9. Gang details (inspector, statistics) — split on 2026-11-20

Follow the guide's section 9 (detail pages). The B9 commit (`e224b1d`)
ticked this step too early: it only added the `GangLists` functions and
`GangCrimes` ignored its gang argument (it returned every robot's memories
city-wide); the gang page's lists, the links back and the Crime tab
counts were never done. The step also spans three modules. Rewritten as
steps of one module each (the original bullets live on inside B9a-B9d).

### - [x] B9a. Correct the gang list functions (gangs module)

Open: `app/GangLists.hpp`, `app/GangLists.cpp`,
`tests/GangListsTests.cpp`.

- `GangCrimes`: only the memories of the gang's **members**. Walk
  `frame.robots` and `frame.social` in parallel (a frame publishes them
  in the same order) and take `RobbedSomeone`, `Arrested`, `WasRobbed`
  of robots whose `RobotFrame::gang` is the requested gang. Record the
  kind (`CrimeKind`), the member, the other robot and the room; `text`
  says just the act ("robbed", "was robbed", "arrested"); the time goes
  in its own field. Newest first, ties by member id.
- Add `RivalsInTurf(frame, gang)`: robots of rival gangs whose current
  room is in the gang's turf building, sorted by id.
- Tests fail without the fix: a non-member's memory never shows; member,
  other robot and room are kept; the order; `RivalsInTurf` takes only
  rivals of *this* gang, only inside the turf. Commit.

### - [ ] B9b. The gang page's lists (inspector)

Open: `app/InspectorGang.cpp`, `app/Links.*`.

- **Members** table, one row per member: name (`RobotLink`), age, job
  (`RoomLink`, or "out of work"), arrests, credits, home (`RoomLink`),
  what it is doing now. One line above the table: how many members are
  out of work, in debt (`EconomyFrame::LoanOf`), or arrested before.
- **Recent crimes** rows: time (`sim::FormatTime`), what, who (the other
  robot as a `RobotLink`, unless none), where (`RoomLink`).
- **Rivals in its turf now**: draw `RivalsInTurf`; delete the page's
  own city-wide scan that lists rivals anywhere.

### - [ ] B9c. Links back (inspector)

Open: `app/Inspector.cpp` (building and room pages only), `app/Links.*`,
`app/GangLists.*` (add `MembersInRoom(frame, room)` and its test).

- Building page: when `TurfOf(frame, building)` answers, say "Turf of
  <GangLink>".
- Room page: the gang members whose `location` is this room, each name
  with its `GangLink`.
- Robot page: "Arrests: N" once, on its own line, also for robots who
  left their gang (B8b asked; check it is so).

### - [ ] B9d. The Crime tab counts (statistics)

Open: `app/Statistics.cpp` (`DrawCrime`).

- "Robberies by its members today": count `GangCrimes` of kind `Robbery`
  whose `tick` is in today (the count by text prefix "Robbed " is dead
  since B9a changed `text` to the bare act).
- Members column: `MembersOf(frame, gang.id).size()`.

### - [ ] B9e. B7 leftovers, found during B9 (gangs module)

Open: `sim/src/Gangs.cpp`, `sim/src/Social.cpp` (only the `Socialize`
loop), `tests/GangTests.cpp`. Each fix comes with a test.

- `Gangs.SameGangNoRobbery` skips itself and asserts nothing (a test
  must never skip, guide §5). Script two robots of one gang alone in
  a room at 22:00, run the night, assert no `Robbed` and no `Fought`
  events about them.
- B7's speed item was not done: `hasOfficerInRoom` walks all robots for
  every pair. Build the set of rooms holding an officer on shift once
  per `Socialize` call and look rooms up in it.
- B7 claims `Robot` has no condition field; check (`rg -n "condition"
  sim`): if it exists, a fight also cuts condition by 100 (B4 said so).
- Add the crime tests B4 asked for and B7 promised: robbery moves
  credits and money adds up; no robbery while an officer on shift is in
  the room — the 3% pair becomes an arrest instead; friendly gangs
  never rob each other. Population level (guide §5).

## Done when

- [ ] Gangs exist in new and old cities, members join and leave, rob, get
  arrested and fight; money adds up over 40 days at 10000 robots.
- [ ] Save version 18 with migration, fixture and tests.
- [ ] Crime tab, gang page, gang colours in the city.
- [ ] The guide's task table marks B done.
