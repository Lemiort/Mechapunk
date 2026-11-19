# Task B: gangs

Status: open. Save change (one, in step B1). Do task A first: the
inspector steps here use its links.

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

## Steps

### - [ ] B1. Data and save version 18 (saves module + Gangs.hpp)

Open: `Gangs.hpp` (new), `World.hpp` (only `Robot`, `WorldState`,
`EventKind`), `Economy.hpp` (only `DayCounters`, `DailyStats`),
`Social.hpp` (only `MemoryKind`), `Save.hpp`, `Save.cpp`,
`Migrations.hpp`, end of `Migrations.cpp`, `tests/SaveTests.cpp`.

- Add every type and field above at once, so there is one save change.
  New enum values get names in `Save.cpp` and in `MemoryText` /
  `Describe` (plain sentences; `rg -n "MemoryText|case EventKind" sim/src`).
- `V17ToV18`: `gangs: []`; every robot `gang: 0`, `arrests: 0`; the new
  counters 0 in `today` and in every `history` element (and its
  `counted`), `gangMembers: 0` in every history element.
- Validation (`Validation.cpp`): gangs sorted by unique id, relations
  sorted, pointing at other existing gangs, no relation with itself; a
  robot's gang exists; turf is a habitat or none.
- Fixture `v18-city.json` and save tests (rule 3). Commit.

### - [ ] B2. Founding gangs (gangs module)

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

### - [ ] B3. Joining and leaving (gangs module)

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

### - [ ] B4. Robbery, arrests, fights (gangs module)

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

### - [ ] B5. Statistics (statistics module)

Open: `EconomyStatistics.cpp` (only `RecordStatistics`),
`app/Statistics.cpp`, `tools/sim_runner/main.cpp` (only the daily print).

- `RecordStatistics` fills `gangMembers`. The Statistics window gets a
  "Crime" tab: plots of robberies, robbed credits, arrests, fights, gang
  members; a table of gangs (name as a link to the gang page from B6,
  colour swatch, members, turf as a `BuildingLink`, relations).
- The runner prints a `crime:` line. Commit.

### - [ ] B6. Inspector and view (inspector, view-robots modules)

Open: `app/Links.*`, `app/Inspector.cpp` (robot page only), a new
`app/InspectorGang.cpp`, `app/SimulationHost.hpp/.cpp` (`RobotFrame`,
`Publish`), `app/CityViewInternal.hpp` (only `Accent`).

- `Selection` gains `gang`; `GangLink`; a gang page: colour, turf,
  relations (each a `GangLink`), members (`RobotLink`s, clipped).
- Robot page: its gang (link) and arrests.
- `RobotFrame` gains `gangColor` (0 for none); `Accent` uses it so
  members wear their gang's colour. Commit.

## Done when

- [ ] Gangs exist in new and old cities, members join and leave, rob, get
  arrested and fight; money adds up over 40 days at 10000 robots.
- [ ] Save version 18 with migration, fixture and tests.
- [ ] Crime tab, gang page, gang colours in the city.
- [ ] The guide's task table marks B done.
