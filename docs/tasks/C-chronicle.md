# Task C: chronicle (saved city history)

Status: open. Save change. Do after A (links) and B (gang events).

## What and why

A saved list of notable events, so the player can read the city's story and
each robot's history after the event log has scrolled away.

## Read first

`docs/IMPLEMENTATION_GUIDE.md` sections 1 to 5; `docs/MODULES.md`. The
chronicle gets its own module `chronicle`: `Chronicle.hpp`,
`sim/src/Chronicle.cpp`, `tests/ChronicleTests.cpp`, plus `World.hpp`.

## Steps

### - [ ] C1. Data and save version (saves module + Chronicle.hpp)

```cpp
struct ChronicleEntry { Tick tick{}; EventKind kind{}; EntityId robot; EntityId otherRobot;
                        EntityId room; std::int64_t credits{}; MemoryKind memory{}; };
// WorldState: std::vector<ChronicleEntry> chronicle;  // Oldest first, at most 5000.
```
Migration: `chronicle: []`. Validation: ticks never decrease, at most 5000.
Fixture and save tests (rule 3). Commit.

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
