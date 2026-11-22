# Task B: gangs

Status: B1-B6 done; review fixes B7-B8 done; gang details B9 split into
B9a-B9e on 2026-11-20, all done; the Crime tab bugs the user found on
2026-11-21 are steps B10a-B10b, and the gang's own inspector view is B11a-B11d,
all done. Reviewing the whole task as one commit found three rules B7 had
claimed and not implemented: steps B12a-B12c, all done; B13 (the officer's
second arrest rule) done. B14 split on 2026-11-22 into B14a (done) and
B14b (open). Save change done (v18). Do task A first: the inspector uses its
links.

## What the user asked for

"Gangs: rival or friendly to each other, always rival to police. Gang
members can rob other robots who are not in their gang." Unions come later
(task G).

## Read first

- `docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5 (rule 3 for the save change).
- `docs/MODULES.md`. Gangs get their **own module** `gangs`:
  `sim/include/mechapunk/sim/Gangs.hpp`, `sim/src/Gangs.cpp`,
  `tests/GangRosterTests.cpp` and `tests/GangCrimeTests.cpp` (the old
  `tests/GangTests.cpp` was split in B12b), plus `World.hpp`. The gang
  page's list functions are a module of their own, `gang-lists`
  (`app/GangLists.*`, `tests/GangListsTests.cpp`). Do not grow `Social.cpp`
  or `Behaviour.cpp` beyond one-line hooks.
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
  midnight when `gangs` is empty, from the save's own `seed` and `kGangStream`
  (the stream lives in `Gangs.hpp`, so a save grows the same gangs whatever
  its clock said).
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
  `state_.gangs` is empty, call `FoundGangs` from `state_.seed` and `kGangStream`
  (not `state_.random`: the dedicated stream is what a generated city uses, and
  it keeps the gangs independent of how far the clock had run). Test: load
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

### - [x] B9b. The gang page's lists (inspector)

Open: `app/InspectorGang.cpp`, `app/Links.*`.

- **Members** table, one row per member: name (`RobotLink`), age, job
  (`RoomLink`, or "out of work"), arrests, credits, home (`RoomLink`),
  what it is doing now. One line above the table: how many members are
  out of work, in debt (`EconomyFrame::LoanOf`), or arrested before.
- **Recent crimes** rows: time (`sim::FormatTime`), what, who (the other
  robot as a `RobotLink`, unless none), where (`RoomLink`).
- **Rivals in its turf now**: draw `RivalsInTurf`; delete the page's
  own city-wide scan that lists rivals anywhere.

### - [x] B9c. Links back (inspector)

Open: `app/Inspector.cpp` (building and room pages only), `app/Links.*`,
`scripts/context_budget.py`.

Budget note, corrected at work: splitting a file cannot lower a module's
total, so instead the organization-list files (`OrganizationLists.hpp/.cpp`
and their tests) moved to a new `organization-lists` module (like the gangs
module holds `GangLists`), leaving `inspector` at ~21k.

- Building page: when `TurfOf(frame, building)` answers, "Turf of
  <GangLink>".
- Room page: the occupants table (task A lists everyone present already)
  gains a "Gang" column: members get their `GangLink`, others "-". A separate
  `MembersInRoom` list would only repeat names already in that table.
- Robot page: "Arrests: N" once, on its own line, also for robots who
  left their gang (B8b asked; it had been a section header with a bare
  number, only shown when in a gang).
- No new tests: pure drawing over frame data already covered; `ContextBudget`
  must pass with the new module row.

### - [x] B9d. The Crime tab counts (statistics)

Open: `app/Statistics.cpp` (`DrawCrime`).

- "Robberies by its members today": count `GangCrimes` of kind `Robbery`
  whose `tick` is in today (the count by text prefix "Robbed " is dead
  since B9a changed `text` to the bare act).
- Members column: `MembersOf(frame, gang.id).size()`.

### - [x] B9e. B7 leftovers, found during B9 (gangs module)

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

Done as (2026-11-21):

- One fixture in `tests/GangTests.cpp`: `MakeStage` puts a crowd of robots
  Visiting in an empty bar at 21:00, clears every other robot's gang and
  (when asked) adds an officer standing in that room on a patrol leg. Every
  robot in it, gangless idlers included, stays put and the day's counters
  can only be about the crowd, so each test asks the event log
  (`Watch`/`Between`) about the robots it scripted rather than trusting
  the day's totals - a gangless idler that wanders into the room can legally
  be robbed by the crowd. `SameGangNoRobbery` therefore gangs the whole
  crowd with one gang and asserts no `Robbed`/`Fought` between crowd robots;
  it runs now instead of skipping, and removing the same-gang return in
  `ConsiderCrime` makes it fail (23 robberies).
- `Socialize` builds `officerRooms` (rooms holding an officer on shift) once
  per call and `ConsiderCrime` looks a room up in it. Proven behaviour
  identical: the same headless scenario (seed 7, 2000 robots, 6 days, `--all`
  with a save) gave a byte-identical log and save before and after.
- B7 was half wrong: `Robot` has no `condition`, but `Robot.needs.condition`
  (0..1000, capped at kMaxWear minus wear) is there, so a fight cuts it by
  100 as B4 said. No save version bump: no state field, meaning or layout
  changed, only what a fight does.
- The fight cost is measured against a control run of the same crowd with
  nobody ganged, since idle needs drift swamps a single 100; within one day
  nothing else moves wear or condition, so the difference is exact: 40 wear
  and 200 condition per fight. The robbery test compares the two robots'
  credits around one logged robbery and calls `ValidateEconomy`.
- Finding: `--seed 1 --robots 10000 --days 40` reported robberies and the odd
  arrest but 0 gang fights over all 40 days, so the fight rule was effectively
  never seen in a real city. Added to the roadmap's gaps. B12c's habitat tie-
  break moves which habitat a gang claims at this seed, and the same run then
  reports 3 fights in 40 days - still all but never seen.

### - [x] B10a. Bug the user found in the game: gang squares share ImGui ids

Asked for on 2026-11-21 (inspector module: `app/Links.cpp`, `app/Links.hpp`,
`app/InspectorGang.cpp`). A gang's colour square was drawn as
`ColorButton("##swatch")` inside `PushID(gang id)`, so every square of one
gang had one id. One frame shows the same gang more than once - its own row
and another gang's relation in the Crime tab's gang table, and the tab drew
its own square next to the one `GangLink` drew for the same row - so two
squares fought over one id and ImGui treated them as one widget: hovering
or clicking one moved the other.

Done as (commit 271d40f): `Links.cpp` gained a public `GangSwatch(what,
colour, size)` whose id comes from the same per-frame repeat count the private
`Link()` already uses for labels, so each repeat of a gang in a frame gets
its own square; `GangLink` uses it and its name label now ends in `##gang<id>`,
which `Links.hpp` documents and the gang link was the one link that broke;
`InspectorGang.cpp` draws its title square through `GangSwatch` too, so no
window calls `ColorButton` with a shared label any more (guide §9.2).

### - [x] B10b. Crime tab: links that work, and a list of every gang's members

Asked for on 2026-11-21 (statistics module: `app/Statistics.cpp`, `DrawCrime`).
`DrawCrime` threw away the value every link function returns, so a click in
that tab did nothing while the other tabs pipe it out.

Done as (this step): `DrawCrime` takes `std::optional<InspectorRequest>*`
like `DrawCentralBank` and a local `follow` pipes the gang, turf, relation
and member links into it (the last click of the frame wins, as on the gang
page). The tab gained a **Members** table under the gang table: each gang
heads a header row of its own and under it one row per member - the member's
name clickable (one click shows it in the inspector, a double-click moves the
camera), what it is doing, the room it stands in, and how many crimes it
remembers of this night. Rows come only from the tested `MembersOf` and
`GangCrimes` (guide §9.4): the gangs are read once a frame into
`members`, `robberiesToday` and `crimesToday`, which the gang table's two
count columns now also read instead of walking the memories again. The table
is resizable and 260 px high, as the other long statistics tables are.

Nothing to test here: the app's link drawing is in no test target, so both
steps are for the user to see in the game. `ctest` 147/147 pass,
`ContextBudget` fine (statistics 16.3k, inspector 21.9k of 25k).

### - [x] B11. The gang's own view instead of lists in the Crime tab

Asked for on 2026-11-21: clicking a gang somewhere should open the gang's
inspector page and move the camera to its turf; the Crime tab's relations and
member lists belong on that page, not in the tab. One step per module.

### - [x] B11a. A gang link names the gang and its turf; a gang page needs no open building

`app/Links.cpp`, `app/Links.hpp`, `app/Inspector.cpp` (inspector module).

- `GangLink` returns `{.gang = id, .building = turf, .floor = 0}` when the
  gang owns a turf, and `{.gang = id}` when it does not: the page opens and
  `Game::ApplySelection` opens the building and points the camera at
  `CityView::FloorCenter(building, floor)`, which already exists for exactly
  that. A gang with no turf has nothing to look at, so the camera stays.
- `DrawInspector` returned early unless an organization, building, robot or
  room was selected, so a gang selected on its own - which is what a gang
  link from the Statistics window gives - never opened the window at all.
  That is why the Crime tab's gang names looked clickable but did nothing.
  A gang now counts as something selected.
- Still half done after this step: `Game::ApplySelection` in `app/main.cpp`
  opens the building through `OpenBuilding`, which replaces the whole
  selection and never copies `gang`, so until step B11b a turf click moves
  the camera but shows the building's page rather than the gang's.

### - [x] B11b. The gang survives the camera move

`app/main.cpp` (ui module). `Game::ApplySelection` opens the building with
`OpenBuilding`, which replaces `selection_` entirely and never copied `gang`,
so the turf jump B11a asks for showed the building's page instead of the
gang's. The gang is now put back after the building and floor, and cleared
in the in-place organization branch: a selection naming no gang means the
player is no longer looking at a gang.

### - [x] B11c. The relations the Crime tab used to squeeze in, as a headed list

`app/InspectorGang.cpp` (inspector module). The page already had its relations,
members, crimes and rivals-in-turf, but the relations block was the one table
without a header row and without scrolling: it was crammed into the tab's
"one line per gang" cell instead, which is why the tab needed the cramped
stance text. It is now a scrolling list like the three below it - header
row pinned by `ImGui::TableSetupScrollFreeze(0, 1)`, "Gang" and "Stance"
columns, `ListSize` height, `kListFlags` - and its section title counts the
rivals and friends, as the other sections count their rows. The members,
crimes and intruders tables already pinned their headers; checked, not
changed.

### - [x] B11d. The Crime tab keeps only what fits a cell

`app/Statistics.cpp` (`DrawCrime`), statistics module. The tab lost what B10b
added and what it never should have held: the members table went, the
relations went out of the "Turf / Relations" cell (the column is "Turf" now
and holds only the turf link), and the per-member `crimesToday` map and the
`DoingNow` helper with them. What is left is what can be a number per gang:
robberies this night, member count, colour, and the two links - the gang's
name, which opens its page and looks at its turf, and the turf building.
Under the table one disabled line says where the lists went.

Rows still come only from the tested functions: `MembersOf` for the count
and `GangCrimes` for the robberies, read once a frame into `members` and
`robberiesToday`.

## Review of the whole task (2026-11-21)

Read back as one commit, the code disagreed with this file in three places
B7 had ticked. Each is one step, gangs module, with a test that fails
without it.

### - [x] B12a. One pair roll decides robbery and arrest

`sim/src/Gangs.cpp` (`World::ConsiderCrime`), gangs module. The roll was
drawn and then tested only on the arrest path: `if (!officerPresent)` robbed
unconditionally, `else if (roll <= 3)` arrested. So a robbery was certain
for every eligible pair, and an officer in the room switched crime off
instead of changing it - the opposite of B4's "3% per pair per 10 minutes"
and of B7's "roll first; only a robbery that would happen becomes an arrest
instead". The roll is now drawn once, named `kRobberyPercent`, and compared
before the officer lookup, which decides between the two branches.

Numbers before and after, seed 1 / 10000 robots / 40 days: 1003 to 4373
robberies and 52514 credits a day by 734 members, now 61 to 72 robberies
and 4725 to 5518 credits, which is what B4 asked for ("a few per hundred
robots a day").

Test `Gangs.RobberyNeedsThePairRoll`: 100 robots in one venue from 21:00,
half of them in one gang, so nearly every pair the night forms is eligible
for a robbery. The count must be small next to the pairs that could rob,
and must not be zero. Measured both ways: 12 with the rule, 240 with the
roll ignored, against a bound of 150, so the test bites. `RobberyMovesCredits
AndMoneyAddsUp` needed a bigger crowd (60 robots at 300 instead of 12 at
60): one night of twelve robots may simply never roll the 3%.

### - [x] B12b. An officer on shift is recognised without being in a gang

`sim/src/Gangs.cpp`, `sim/src/Social.cpp`, `World.hpp` (the gang section),
gangs module. B4's "an officer and a member talking lose 20 affinity" was
dead code: `HandleOfficerAffinity` recognised an officer only under
`first.gang.value && … PoliceStation …`, and `ReviewGangs` forces every
police job out of a gang - which `Gangs.NoOfficerIsAGangMember` pins - so
the condition could never hold and the -20 never happened. The same notion
was written out four times with three different conditions (the watched
rooms in `Socialize`, the affinity rule, the never-join rule and the
force-out rule), so it is now two `World` members all four call:
`WorksInPoliceStation` (a job in a police station) and `OfficerOnShift`
(that, plus working or on a patrol leg).

The tests moved into a split of the test file, which went over the per-file
token limit: `tests/GangRosterTests.cpp` keeps founding, relations, saving
and the roster, `tests/GangCrimeTests.cpp` the stage fixture and the crime
of the night. Two robots with no bonds and no interests, alone in a venue
while the rest of the city is frozen, talk exactly once at these seeds, so
the arithmetic is exact: the strangers' band is -5 to +3, and an officer on
shift must land the bond in -25 to -17 (`Gangs.OfficerCoolsTheBondToAMember`,
seed 38, bond -21 both ways), while the same robot visiting the room off
shift may not cool it at all (`Gangs.OfficerOffShiftCoolsNothing`, seed 4).
Bite-checked both ways: putting `gang.value` back in front of the officer
test breaks the first; dropping the working-or-patrol half of `OfficerOnShift`
breaks the second. `MakeStage` now only hands a room over as a police station
when the officer it adds fits that station's three job slots next to whoever
already worked there, because the stage has to load through `World::FromState`
and a room that held a bigger crew would be over capacity.

### - [x] B12c. A world loaded without gangs founds them at the next midnight

`sim/src/Gangs.cpp` (`World::ReviewGangs`, `World::FoundGangs`), `Gangs.hpp`,
`Generation.cpp`, `Migrations.cpp` (one comment), gangs module. B7 asked for
this and the code instead invented a gang per gang id the robots happened
to name: no relations, no turf, colour 0, sorted by id - and unreachable,
because validation already refuses a robot naming a missing gang. So
`V17ToV18` left a world that would never get gangs, while this file, the
migration comment and the commit message all claimed it would. `ReviewGangs`
now calls `FoundGangs` from `state_.seed` and `kGangStream`, which moved to
`Gangs.hpp` so both founders see one value: a save grows the same gangs
however far its clock had run, with turfs and the full set of relations, and
the membership review in the same call can pick members up that same night.
Reproduced before the fix: `mechapunk-sim --load tests/fixtures/saves/v17-
city.json --days 14 --quiet` printed `0 in gangs` on all fourteen days - with
no gangs the join rules have nothing to join, so the save stayed gangless
however long it ran. After the fix the same command grows members day by day
(1, 1, 2, 2, 2, 3, 3, 4, 5, 5 from the fifth day) and its first robbery
appears on day 13.

Test `Gangs.OldSaveGrowsGangsAtMidnight` (in `tests/GangRosterTests.cpp`)
loads that fixture, runs one midnight and wants gangs that every gang names
every other gang once, all of them with a turf, at least one rival stance, a
valid state, and the same sketch from a second load; `Save.Version17Fixture
MigratesAndRuns` now also wants gangs after its three-day run. Bite-checked:
founding turned off fails both tests; founding without the relations loop
fails the new one (`Gang 'Chrome Jackals' has 0 relations, expected 1`,
`rivalStances 0 vs 0`).

Also in this step, from the review: the habitats in `FoundGangs` were sorted
by distance alone, so equal distances left which habitat a gang claims to
`std::sort`'s choice - not the same on every machine (rule 1). The smaller
building id now wins, and that is the only thing a newly generated city
changes about. Measured by byte-comparing runs before and after the tie-break,
with `--all` and a save: seed 7 at 2000 robots for 6 days changes 4 save lines,
two turfs (2499 to 883, 2457 to 1871); seed 3 at 1000 robots for 6 days
changes 22 lines, two turfs (1506 to 180, 180 to 708) and the 9 robots that
follow them (gang 2843 to 2844). Both event logs came out identical apart from
their own "simulated in N s" and "Saved <path>" summary lines: nothing about
where robots were, what they earned or what they did differed. Saves made
before this keep the turfs they had.

### - [x] B13. The officer's second arrest rule (gangs module)

The first "Done when" item could not be met as written, so it was measured
first, at `7c03785` (head, task C done), seed 1 / 10000 robots / 40 days:
1473 robberies (107183 credits), **4 arrests**, 24 fights, 813 members by
day 40. So the note "arrests still come out at 0 and fights at 0" was wrong
on both counts (B12c's turf tie-break moved which venues officers reach),
but an arrest in one robbery out of 370 is still invisible in the game, which
is what that item is about. One rule was missing: B4 asked for two ways an
officer takes a robber - the room is watched when the pair rolls, *and* the
officer arriving where a member robbed in the last hour - and `ConsiderCrime`
had only the first.

Open: `sim/src/Gangs.cpp`, `sim/include/mechapunk/sim/World.hpp` (the gang
section), `sim/src/Social.cpp` (only the `Socialize` room loop),
`tests/GangCrimeTests.cpp`, `scripts/context_budget.py`, `docs/MODULES.md`.

Done as:

- `World::PatrolArrests(present)` in `Gangs.cpp`, called from `Socialize` once
  per watched room with the robots standing in it: a member standing where an
  officer on shift stands, that remembers robbing in the last hour, is arrested.
  It takes no random number and walks the room in id order (rule 1), so a
  room gives the same arrests on every machine.
- The robbery's **room is not matched**, only the hour: an officer visits one
  venue for 15 minutes once a shift hour out of the city's ~484 venues, so
  matching the room (the first thing tried here) left 7 arrests in 40 days
  against 1557 robberies. The officer cannot see where a crime was, only who
  stands in the room with it now.
- `World::Arrest(robot)` now holds the whole arrest - the fine to City Hall
  through `PayBill`, `arrests += 1`, the event and the memory - and both
  officer paths call it, replacing the block that had been inline in
  `ConsiderCrime` (one notion written twice was B12b's lesson). The arrest's
  own memory, fresh in that hour, is what keeps one crime from being arrested
  twice.
- No save change: no field, meaning or layout was added; the rule writes
  existing memories (`RobbedSomeone`, `Arrested`), the existing `Arrested`
  event and the existing `DayCounters.arrests`.
- Test `Gangs.OfficerCatchesRobbersOfTheHourBefore`: the stage runs in two
  halves, the night half with no officer in the room (30 to 40 robberies
  happen) and the officer arriving after 04:00, where `ConsiderCrime` does not
  act at all, so an arrest can only come from the hour it walked into. The
  robbers' memories are moved to their apartments on the way, so a rule that
  matched the room would arrest nobody. Bite-checked twice: with the hook
  out, and with the room matched again, the test reports 0 arrests.
- `ContextBudget` failed after the new code (gangs 25453 of 25000), so the
  gang list files (`app/GangLists.hpp/.cpp`, `tests/GangListsTests.cpp`)
  became their own `gang-lists` module, as B9c did for the organization lists:
  gangs 20584, gang-lists 4990.

Numbers, same command before and after: robberies 1473 to 1538, arrests 4 to
**9**, fights 24 to 19, members 813 to 758 (an arrested robot leaves its gang
at 3 arrests, so arrests also thin the gangs). Economy unchanged in kind on
day 40: money 5188000 to 5169909, `created` 2625 to 2069 a day, wages still
110%, robot debt 324927 in 1182 robots to 318367 in 1200, banks 252733 to
235305, broke 1008 to 1053, gini 497 to 500, treasury 285302 to 280112 - a
robber fined 100 has less to lend, so the central bank creates less. Money
adds up: `ValidateEconomy` passes in the test after the arrests, and a 12 day
save at 10000 robots loads back clean.

Still short of seen: 9 arrests in 40 days is one in 170 robberies, because
the police are the limit - about 9 officers at 10000 robots, one venue each
shift hour - and rival members barely meet, which is why 40 days gives 19 to
24 fights. Both need `World::Patrol` (`sim/src/Behaviour.cpp`), a different
module: recorded below as step B14, not done here.

### B14. Arrests and gang fights seen in a running city — split on 2026-11-22

The item wants two fixes and they live in different modules, so B14 is
two steps of one module each (guide, section 1 item 5): the officer going
where the crime is (world module, `World::Patrol`), and rival members
meeting at all (gangs module, a gang choice made through a one-line hook
in `Decide`; task B keeps Behaviour.cpp to hooks).

### - [x] B14a. Officers patrol venues the city just robbed (world module)

Open: `sim/src/Behaviour.cpp` (`World::Patrol`, the patrol constants),
`tests/WorldTests.cpp`; for reading only `Gangs.cpp` and `Chronicle.cpp`,
to see what a robbery records and where.

Done as: task B made robberies notable and task C keeps every `Robbed`
event in `state_.chronicle` with its tick and room, so `Patrol` can walk
the chronicle back from the newest entry over `kCrimeLookbackMinutes`
(60) and find the venues the city robbed in the last hour. When one of
the hour's reachable candidates is among them, the officer picks at
random among **those** instead of among all of the city's ~484 venues:
still one draw from `state_.random`, both lists sorted, the scan follows
the chronicle's own order (rule 1). The robbers of a fresh crime still
stand in its room, which is exactly where `PatrolArrests` (B13) arrests a
member that robbed in that hour, so arrests now follow the crime's
geography instead of the officer's luck. No saved state changed: the
chronicle already existed (v19), nothing new is written.

Test `World.OfficersPatrolWhereTheCityWasRobbed` (population level, guide
§5): seed 1, 4000 robots, 20 days, summing `counted.arrests` over
`history`; the run gives 1 arrest without the rule and 38 with it
(measured both ways), the bound of 10 bites from both sides.

Numbers, seed 1 / 10000 robots / 40 days, before to after: robberies
1538 to 1464, **arrests 9 to 337** (one in four robberies now), fights
19 to 24, members 758 to 764. Economy the same in kind: money 5169909
to 5165746, `created` 2069 to 1105 a day (a fined robber has less to
lend, as in B13), wages 110% to 108%, robot debt 318367 in 1200 to
335195 in 1186, banks 235305 to 225786, broke 1053 to 1071, gini 500
to 499. Money adds up: `ValidateEconomy` passes in every test.

### - [ ] B14b. Rival gang members meet (gangs module)

`sim/src/Gangs.cpp` (a `World` member taking the robot and its outing
decision, e.g. `GangOuting`), one-line hook in `Decide` in
`sim/src/Behaviour.cpp`, gangs module. Fights are still 24 in 40 days
because members of rival gangs share a room barely at all: gangs pick
venues as if they were not in a gang, out of ~484, so the 1% fight roll
in `ConsiderCrime` has almost nothing to act on. A gang member must
sometimes choose its outing among venues tied to its gang — near its
turf, or near a rival's turf, which is what rivals do to each other —
so rival members meet "more often than one pair in a hundred" (the
Done-when item) and `fights` rises from ~20 in 40 days at 10000 robots,
with the usual economy watch (guide §5) and the population tests
(guide §5: no skipping, leave margin).

### Arrests name the arresting officer — requested 2026-11-22 (B15a-B15d)

The user asked: an arrest must say **who** did it. The chronicle line
should read "officer X arrested robber Y", the officer's card should
gain "arrested Y" and the robber's card "arrested by X". Today nothing
can say that: `World::Arrest` takes only the robber, because neither
officer path ever works out *which* officer - `officerRooms` is a set of
room ids (B7's speed fix) and `PatrolArrests` gets the room's robots
without its officer - and `Social.hpp`'s comment for `Arrested`
promises an `otherRobot` ("victim (if an officer)") that no code ever
fills. One new memory kind (the officer's side) is a saved-state change:
save version 20, so the work starts in the saves module.

### - [x] B15a. Save version 20: the officer's side of an arrest (saves module)

Open: `Social.hpp` (only `MemoryKind`), `Save.hpp`, `Save.cpp`,
`Migrations.hpp`, end of `Migrations.cpp`, `tests/SaveTests.cpp`,
`tests/fixtures/saves/README.md`, the guide's state line.

- New `MemoryKind::ArrestedSomeone` (the officer remembers taking someone;
  robot: the arrested robber); `Arrested`'s stale comment becomes true in
  the next step: its `otherRobot` is the arresting officer.
- `V19ToV20` adds nothing: every key a version 19 save has is still there,
  and the new kind cannot occur in an older file; the bump records the
  format's new meaning and stops a v20 save loading into an older game.
  (B15b changes what an `arrested` chronicle entry's `robot` names; that
  fix-up lives in step B15e, written when B15b made it necessary.)
- Fixture `v20-city.json` (`--seed 33 --robots 100 --minutes 28370`), a
  README row and sentence; `CurrentVersionFixtureLoadsAndRoundTrips` at
  the v20 fixture (the first one whose chronicle is not empty, since the
  recording step exists now) and `Version19FixtureMigratesAndRuns`.

### - [x] B15b. The arrest names its officer (gangs module)

Open: `sim/src/Gangs.cpp` (`Arrest`, `ConsiderCrime`, `PatrolArrests`),
`World.hpp` (the gang section), `sim/src/Social.cpp` (only the `Socialize`
loop that builds `officerRooms`), `tests/GangCrimeTests.cpp`.

`Socialize` builds a map room → the smallest-id officer on shift in it
(id order, rule 1); `ConsiderCrime` and `PatrolArrests` look the officer
up there and pass it to `Arrest(robber, officer)`, which sets the event's
`robot` to the officer and `otherRobot` to the robber (so the chronicle
shows who and whom), remembers `Arrested` on the robber with the officer,
and `ArrestedSomeone` on the officer with the robber. No new random
draws. Tests: both memories, both directions, on the stage fixture.

Done as: as written; `officerInRoom` replaces the set, `PatrolArrests`
takes the room's officer index. The tests pin the naming: every
`Arrested` event of the second half of `OfficerCatchesRobbersOfTheHour
Before` has `robot` = the stage officer and a valid `otherRobot`, and
the officer ends with `ArrestedSomeone` memories;
`OfficerTurnsTheRobberyIntoAnArrest` checks the robber-side memory
names the officer and the officer has the other kind. `CountOf`'s
either-seat OR still matches, and the arrest's own-memory guard
(once an hour) still holds on the robber side. The flip means
`Arrested` chronicle entries in version 19 saves read backwards and
the v20 fixture of B15a was written by the old code - recorded as
step B15e (saves module), not done here. `tests/GangCrimeTests.cpp`
went over the 10k per-file limit with the new assertions, so the
officer-bond/rival/hour tests moved to `tests/GangOfficerTests.cpp`
with its own copy of the stage in an anonymous namespace; new
`gang-officer` module in `scripts/context_budget.py` and `docs/MODULE
S.md` (gangs 16818, gang-officer 8233). ctest: 158/158 pass.

### - [x] B15e. Old arrests in old saves read the new way (saves module)

Open: `Migrations.cpp` (only `V19ToV20`), `SaveTests.cpp`, the fixture
directory, `README.md`. `V19ToV20` was written as a no-op in B15a, but
B15b changed what `robot` means in an `arrested` chronicle entry: version
19 files hold entries with the **robber** in `robot` and no `otherRobot`.
The migration must swap them (`robot` → unknown, `robber` →
`otherRobot`) so old entries print "An officer arrested X" and new ones
"X arrested Y", and the v20 fixture - written by the pre-B15b binary -
is regenerated (allowed: only older fixtures are frozen). Test: an
`arrested` chronicle entry edited into the v19 fixture's JSON loads
swapped.

Done as: `V19ToV20` walks `world.chronicle` and, for entries of kind
`arrested`, moves `robot` into `otherRobot` and clears `robot`: the
migration may not invent an officer the old game never recorded, and
`Describe` (B15d) reads the cleared form as "An officer arrested X".
The v20 fixture was regenerated and is **byte-identical** (seed 33 at
100 robots never arrests, and the new kind and swapped fields only
appear in runs that do), so it stays as B15a committed it.
`Version19FixtureMigratesAndRuns` grew the bite: an `arrested` entry
(robber 424242, no officer) edited into the fixture's JSON with `EditSave`
loads with `robot` empty and `otherRobot` 424242; without the swap the
assertion fails. Older fixtures untouched, as always. ctest: 159/159.

### - [x] B15c. The personal card lines (social module)

Open: `sim/src/Social.cpp` (only `MemoryText`), `tests/SocialTests.cpp`.
`Arrested`: "arrested by <officer>", falling back to the old "arrested
for robbing" when the other robot is unknown (memories from v19 saves);
`ArrestedSomeone`: "arrested <robber>". The robot page already links
`memory.robot`, so the names become links by themselves.

Done as: as written, both lines append the venue as "at <room>" through
`MemoryText`'s `at()` helper. Test `Social.ArrestMemoryLinesNameTheOfficer`
pins the four spellings; without the new `ArrestedSomeone` case the text
falls back to "remembered something" and the test fails. The inspector's
robot page already links `memory.robot` (`DrawMemories`), so officer and
robber names on both cards click through without any app change.

### - [x] B15d. The chronicle and log line (validation module)

Open: `sim/src/Describe.cpp` (only the `Arrested` case), and
`tests/WorldTests.cpp` if a description is pinned there:
"{officer} arrested {robber} for robbing at {venue}" - the `who` column
is the officer, `otherRobot` the robber.

Done as: the new convention reads "{officer} arrested {robber} for robbing
at {venue}"; an entry whose `robot` names nobody - what B15e's migration
leaves for version 19 stories - keeps the old "An officer arrested
{robber} for robbing at {venue}". `World.DescribesEventsWithRoomsAndBuildings`
pins both spellings with synthetic events (no fixture of an old version
is touched). The chronicle tab and the `--all` log print through
`Describe`, so both show the who and the whom for free.

### - [x] B15f. The gang page's crime row names the officer (gang-lists module)

Open: `app/GangLists.hpp`, `app/GangLists.cpp` (the `Arrested` case),
`tests/GangListsTests.cpp`. B15b put the officer in the `Arrested` memory's
other robot, so the arrest row that had to say "no robot" can name the one
that took the member.

Done as: the row's `perpetrator` becomes `memory.robot` (the officer; empty
for a memory of a version 19 save, which B15e's migration leaves without one
- the row does not invent an officer) and `victim` becomes the member, which
is who the arrest was done to. The header's comments for both fields say so.
Nothing else reads the two fields: the Crime tab's Robberies column filters
on `kind == Robbery`, and the gang page prints the counterpart through
`Counterpart`, which is step B15g. `GangLists.GangCrimesFieldsAndOrder` now
carries three rows: an arrest with an officer, a was-robbed row, and an
arrest of a version 19 memory with no officer (perpetrator empty, member
still the victim). ctest: 159/159 pass.

### - [x] B15g. The gang page shows that officer (inspector module)

Open: `app/InspectorGang.cpp` (`Counterpart`, and the comment above it),
no sim change. "Who" in the gang page's crime table is
`Counterpart(crime)`; for `Arrest` it returned none, so the column read
"nobody" beside "arrested" even though B15f made the row name the officer.
Return `crime.perpetrator` for an arrest - an old save's empty officer still
prints "nobody". The inspector has no render test of this table
(guide, section 9.4: the list functions are what is tested, the drawing is
what the user checks), so the bite is that the officer's name becomes a
link; whether it reads well is for the user to see in the game.

Done as: as written - one case added, comment corrected. The row's officer
goes through the same `RobotLink` the other kinds use, so it opens that
officer's page; with `perpetrator` empty (a version 19 arrest) the column
keeps its "nobody" rather than linking the member to itself. No test of
the drawing exists, so this step is checked by the compile and by the user
looking at a gang page; `GangLists.GangCrimesFieldsAndOrder` (B15f) is
what pins the data behind it. ctest: 159/159 pass.

### Arrest memory has to outlive the card — requested 2026-11-22 (B16)

The user: "robber has no memory of arrest - there should be". The robot page
prints `Arrests: N` (`Inspector.cpp`, from `Robot::arrests`, a counter that
never fades) above its memory list, and the list kept only the **twelve
newest memories of everything**: a bond that grew close, a promotion, a move
or the next visit erased the arrest. Measured at seed 1: 4000 robots for 40
days arrests 66 robots, 11 of which no longer list their arrest; 10000 robots
for 40 days arrests 226, 30 have forgotten. An officer, which makes dozens of
arrests a month, loses nearly all of its `ArrestedSomeone` lines the same way,
so the "arrested <robber>" card B15 built is invisible where it matters most.
Not a save format change: no field, no enum value, no new key - only which
memories a robot keeps changes, and a version 19 or 20 file stays readable.

### - [x] B16. Two memory shares: ordinary life and the rap sheet (social module)

Open: `sim/src/Social.cpp` (`kMaxMemories`, `World::Remember`, the robot part
of `ValidateWorldState`), `tests/SocialTests.cpp`. A robot keeps twelve
memories of ordinary life **and** twelve of crime - robbery done or suffered,
arrest made or taken - and the two shares cannot push each other out: within
a share the oldest entry makes way for the newest, so the store stays in tick
order (rule 1) and `Robot::arrests` never contradicts the list under it.
Validation counts the same two shares, not only the whole store (rule 5).
No sim behaviour change is expected: every rule that reads memories bounds them
by tick (`RememberedInLastHour`) or uses the counter, so measure the headless
city anyway (guide 5) and the population test `Social.KeepsItsRapSheetAsLongAs
ItFits` must hold with margin and bite from both sides.

Done as: `kMaxOrdinaryMemories` and `kMaxCrimeMemories` (12 each) and
`kMaxMemories` as their sum for validation; one list of the rap sheet kinds,
`IsCrimeMemory`, that `Remember` and validation share, so the two sides cannot
disagree about what a crime is. `Remember` counts the share the new memory
belongs to and erases that share's oldest entry - the first one, because the
store is kept in tick order; no container order decides (rule 1). The robot
part of `ValidateWorldState` now counts crimes while walking the memories and
rejects a store over either share. Test (population, nothing scripted): seed
1, 4000 robots, 40 days, every robot with `arrests > 0` whose crime count fits
the share must hold an `Arrested` memory - measured 11 of 66 failing under the
old rule, 0 under the new one, and `arrestedRobots > 0` keeps it from going
vacuous. Headless seed 1 / 10000 robots / 40 days is unchanged in every figure
the city prints (money 5165746, created 1105, wages 108%, robot debt 335195 in
1186, banks 225786, broke 1071, gini 499) and 30 of the 226 arrested robots
now carry their arrest memory again. `docs/MODULES.md`: social 21k to 25k.
ctest: 160/160 pass (the new test runs 37 s).

## Done when

- [x] Gangs exist in new and old cities, members join and leave, rob, get
  arrested and fight; money adds up over 40 days at 10000 robots. Old cities
  found gangs (B12c) and the money adds up. Measured at head before B13:
  1473 robberies, 4 arrests, 24 fights - the old note here claiming arrests
  and fights "come out at 0" was stale, see B13.
- [ ] Arrests and gang fights are seen in a running city (step B14, split
  into B14a world module, done, and B14b gangs module): 9 arrests and ~20
  fights in 40 days at 10000 robots is invisible next to 1500 robberies.
  The limit is the police, not the rule: about 9 officers, each visiting
  one venue for 15 minutes per shift hour out of ~484 venues. B14a made
  `World::Patrol` in `sim/src/Behaviour.cpp` send officers where the crime
  is; members of rival gangs still have to meet more often than one pair
  in a hundred (B14b, see the roadmap gap).
- [x] Save version 18 with migration, fixture and tests.
- [ ] Crime tab, gang page, gang colours in the city. Steps B10a-B11d fixed
  the tab's link ids, made its links work and moved the member, relation and
  crime lists onto the gang's own page, which a gang's name opens while the
  camera looks at its turf. Whether it reads well is for the user to see in
  the game.
- [ ] The guide's task table marks B done.
