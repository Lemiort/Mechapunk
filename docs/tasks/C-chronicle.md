# Task C: chronicle (saved city history)

Status: C1 done, save version 19 shipped; C2, C3, C4 open. Save change was C1.
Do after A (links) and B (gang events).

## What and why

A saved list of notable events, so the player can read the city's story and
each robot's history after the event log has scrolled away.

## Read first

`docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5; `docs/MODULES.md`. The
chronicle gets its own module `chronicle`: `Chronicle.hpp`,
`sim/src/Chronicle.cpp`, `tests/ChronicleTests.cpp`, plus `World.hpp`.

## Steps

### - [x] C1. Data and save version (saves module + Chronicle.hpp)

```cpp
struct ChronicleEntry { Tick tick{}; EventKind kind{}; EntityId robot; EntityId otherRobot;
                        EntityId room; std::int64_t credits{}; MemoryKind memory{}; };
// WorldState: std::vector<ChronicleEntry> chronicle;  // Oldest first, at most 5000.
```
Migration: `chronicle: []`. Validation: ticks never decrease, at most 5000.
Fixture and save tests (rule 3). Commit.

Done as above; what the step had to add that the text left open:

- `ChronicleEntry` needs `EventKind`, which `World.hpp` declares *below*
  `WorldState`, so `Chronicle.hpp` declares the enum's size first
  (`enum class EventKind : std::uint8_t;`) and lets `World.hpp` fill in the
  enumerators. A scoped enum with a fixed underlying type can be redeclared
  like that; including `World.hpp` from it would have closed the loop.
- "At most 5000" is `kChronicleCap` in `Chronicle.hpp`: the recording step and
  `Validation` have to agree on one number.
- `EventKind` reaches a save for the first time here, so `Save.cpp` gets its
  `glz::meta` (24 camelCase names), as rule 3.3 says.
- The validation checks are in `ValidateWorldState`, which runs from
  `World::FromState` (rule 5); `DeserializeWorld` only types the JSON, so the
  new test loads through `FromState`. Entries are not checked against the
  robots and rooms they name: an entry about a scrapped robot outlives that
  robot on purpose, and C2 writes such entries.

Fixture `tests/fixtures/saves/v19-city.json`, from the guide's command (seed 33,
100 robots, 28370 minutes) at game 0.19.0, plus its row and a sentence in that
folder's README. `CurrentVersionFixtureLoadsAndRoundTrips` points at it;
`Save.Version18FixtureMigratesAndRuns` loads v18 and first checks the fixture
text has no `"chronicle"` key, so what loads is the migration's work and not a
fixture that grew the field. `Save.RejectsChroniclesOutOfOrderOrOverfull` writes
entries by hand through `PushChronicleEntry`: ticks 500 then 400, and
`kChronicleCap` + 1 entries. All three bite: cap check off fails the second
half (`tests/SaveTests.cpp:775: Value of: rejected Actual: true Expected:
false`), order check off fails the first (`:762`), and an empty
`V18ToV19` fails the v18 test with `Save world does not match version 19:
1:292248: missing_key`.

The bump changed no behaviour: the seed 1, 10000 robots, 40 days `--quiet` log
matched byte for byte except its own `Simulated ... in N s` line, and the seed
7, 2000 robots, 6 days save differs in 7 lines only - version, game version, a
comma, and `"chronicle": []`. ctest: 153/153 including ContextBudget.

Note for later: the frozen v18 fixture has an empty gang list (task B's step
generated it before founding worked), so no fixture shows a v18 save that
already has gangs; `Gangs.OldSaveGrowsGangsAtMidnight` covers that case from
the v17 fixture instead, and old fixtures must not be regenerated.

### - [ ] C2. Recording (chronicle module)

Open: `Chronicle.cpp`, `ChronicleTests.cpp`, `World.cpp` (only `Emit`).

- `World::Emit` calls `Chronicle(event)`, which keeps only notable events:
  scrapped, arrived, a close friendship or a rivalry formed, fired,
  promoted to lead, evicted, a home bought, a unit opened or closed, gang
  events, money created over 10000 in one emission. Drop the oldest beyond
  5000.
- Text comes from `World::Describe`: build a `SimEvent` from an entry.
- Tests: a 30-day city has entries of several kinds, in order, at most
  5000, and they survive a save. Commit.

### - [ ] C3. Showing it (statistics and inspector modules)

- `EconomyFrame` (or a new shared pointer in `WorldFrame`) carries the
  chronicle, copied only when it changed.
- Statistics: a "Chronicle" tab, newest first, clipped; every robot, room,
  building and organization in an entry is a link (task A helpers).
- Robot page: a "History" section with that robot's entries. Commit.

### - [ ] C4. Chronicle details (inspector, statistics)

Follow the guide's section 9 (detail pages). Open: a new
`app/ChronicleLists.hpp/.cpp` and `tests/ChronicleListsTests.cpp`,
`app/Statistics.cpp` (the Chronicle tab), `app/Inspector.cpp` (robot,
room and building pages), `app/InspectorOrganization.cpp`.

- `ChronicleLists` (plain, tested): entries about a robot, a room, a
  building (any of its rooms) and an organization (rooms it owns, robots it
  employs, money it was given), newest first; and a filter by kind and by
  day range.
- **Chronicle tab**: filter buttons by kind (people, work, homes, venues,
  crime, money) and a day range; a search box for a robot's name; each
  entry's time, its text, and every robot, room, building and organization
  in it as links, clipped.
- **History sections**: the robot page ("History"), the room and building
  pages ("What happened here") and the organization page ("History"), each
  a clipped list of its entries with links.
- An entry about a robot that was scrapped shows its name as plain text
  (`AccountLink` already does this).
- Tests for every list function; add the new files to the budget script.

## Done when

- [ ] The chronicle is saved, migrated, tested, and shown with links.
- [ ] Robots, rooms, buildings and organizations show their own history.
- [ ] The guide's task table marks C done.
