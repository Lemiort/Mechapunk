# Task C: chronicle (saved city history)

Status: C1, C2 done, save version 19 shipped; C3 split on 2026-11-21 into
C3a-C3c, C3a, C3b and C3c done; C4 split on 2026-11-24 into C4a-C4e, C4a to C4d
done, C4e open (it is a save change and waits for the user to say whether the
organization page needs the account an emission carried). Save change was C1.
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

### - [x] C2. Recording (chronicle module)

Open: `Chronicle.cpp`, `ChronicleTests.cpp`, `World.cpp` (only `Emit`).

- `World::Emit` calls `Chronicle(event)`, which keeps only notable events:
  scrapped, arrived, a close friendship or a rivalry formed, fired,
  promoted to lead, evicted, a home bought, a unit opened or closed, gang
  events, money created over 10000 in one emission. Drop the oldest beyond
  5000.
- Text comes from `World::Describe`: build a `SimEvent` from an entry.
- Tests: a 30-day city has entries of several kinds, in order, at most
  5000, and they survive a save. Commit.

Done as (chronicle module, one commit): `World::Emit` is no longer `const`
and calls the new private `World::Chronicle` (implemented in `sim/src/Chronicle.cpp`)
for every event; `Chronicle` drops the entry unless its kind is notable,
copies the seven fields (`tick, kind, robot, otherRobot, room, credits,
memory`) into a `ChronicleEntry`, appends and erases from the front past
`kChronicleCap`. Notable: Scrapped, Arrived, BoughtProperty, UnitOpened,
UnitClosed, the five gang kinds, `MoneySupplyChanged` with `credits > 10000`,
and `Remembered` carrying `BecameCloseFriends`, `BecameRivals`, `LostJob`,
`Promoted` or `Evicted`. The "money over 10000" is credits *created*
(the emission event keeps its sign); destroyed money is not kept.
No entry-to-SimEvent helper yet: the entry mirrors `SimEvent`'s fields so
`World::Describe` can rebuild the text when C3 shows it.
Tests in `tests/ChronicleTests.cpp`: seed 5 / 1000 robots / 30 days gives
9 distinct kinds (threshold 4, margin checked by hand), ticks sorted, at
most `kChronicleCap`, no `Departed`/`ArrivedHome`/`WeatherChanged` entries,
and a serialize→deserialize→`FromState` round-trip that compares every
field of every entry. `sim/CMakeLists.txt`, `tests/CMakeLists.txt` and
the `chronicle` module in `scripts/context_budget.py` gained the new
files. Nothing else in the sim reads the chronicle, so behaviour cannot
move: the seed 7 / 2000 robots / 6 days `--all` log is byte-identical to
before the change (last two lines aside), and the two saves differ only
inside the `"chronicle"` block (`[]` becoming its entries) - no save
version bump, the field shipped in C1.

### C3. Showing it (statistics and inspector modules) — split on 2026-11-21

The step names two modules, but its first bullet lives in a third: the
frames are `app/SimulationHost.*`, the `ui` module. MODULES.md ("a task
touching two modules ... is done in steps, one module at a time") and the
B9 precedent cut it into steps of one module each; the original bullets
live on inside C3a-C3c.

### - [x] C3a. The chronicle in the frames (ui module)

Open: `app/SimulationHost.hpp`, `app/SimulationHost.cpp`.

- `WorldFrame` gains `chronicle`: a `shared_ptr<const std::vector<sim::
  ChronicleEntry>>`, not a field of `EconomyFrame`: the chronicle is the
  city's story, not money, and it changes with events, not every ten
  minutes. Copied from the world's list only when it changed.

Done as: `Publish` keeps `chronicle_` plus the pair `(size, newest tick)`
the copy was made from; a notable event appends an entry and the oldest
leave only past `kChronicleCap`, so that pair changes exactly when the
list does and minutes without an event share one copy instead of recopying
up to 5000 entries. No new source files, so `scripts/context_budget.py`
is unchanged; no sim code touched, so no headless comparison applies. As
in B8a no test runs `SimulationHost` (`layout_tests` build `WorldFrame`
by hand), so this step is checked by the build and by C3b and C3c.

### - [x] C3b. The Chronicle tab (statistics module)

Open: `app/Statistics.hpp`, `app/Statistics.cpp` (tab dispatch and one
new Draw function).

- Statistics: a "Chronicle" tab reading `WorldFrame::chronicle`, newest
  first, clipped; every robot, room, building and organization in an
  entry is a link (task A helpers).

Done as (statistics module, one commit): the tab's drawing lives in a
new `app/StatisticsChronicle.cpp` (the `InspectorGang.cpp` precedent),
because `Statistics.cpp` was already 8.7k and taking the tab in place
would put it over the 10k per-file budget; the function is declared
in a new private header `app/StatisticsInternal.hpp` the way
`DrawGangPage` is declared in `InspectorInternal.hpp`. `Statistics.hpp`
itself did not change: the tab dispatch in `DrawStatistics` gained a
"Chronicle" tab item after "Crime" calling `DrawChronicle(frame, city,
&request)`.

- The table reads `WorldFrame::chronicle` backwards — saved oldest
  first, shown newest first — with a frozen header row and
  `ImGuiListClipper`, so all `kChronicleCap` rows are safe. Columns:
  When (`sim::FormatTime`), What, Who, Whom, Room, Building, Owner,
  Credits, table 450 px high as the other statistics tables are.
- Links, all task A helpers, piped into `request` through `follow` as
  the Crime tab does. An entry has no building or organization field,
  so those come from its room: `RoomLink` (its label already carries
  the building's name), `BuildingLink` for the room's building and
  `OrganizationLink` for `EconomyFrame::OwnerOf(room)`. Robots go
  through `AccountLink`, which shows a scrapped one's remembered name
  as plain text; a room that has since closed reads "a room that is
  gone".
- The What column: short kind labels, and for `Remembered` entries the
  memory kind instead ("close friends", "rivals", "lost a job",
  "promoted", "evicted"), since `World::Chronicle` keeps only those;
  bank emissions read by sign ("money created" / "money destroyed").
- `Credits()` is a local copy, as `InspectorOrganization.cpp` and
  `InspectorGang.cpp` already do. `context_budget.py` and the two
  MODULES.md tables gained the new files; ctest 155/155, statistics
  module 18.1k of 25k, new file 2.9k. No sim code touched, so no
  headless comparison applies (as in C3a); the tab is for the user to
  see in the game.

### - [x] C3c. The robot page's History section (inspector module)

Open: `app/Inspector.cpp` (robot page only).

- Robot page: a "History" section with that robot's entries. Commit.

Done as (inspector module, one commit): the section's drawing is a new
`app/InspectorHistory.cpp`, declared as `DrawRobotHistory` in
`app/InspectorInternal.hpp` the way `DrawGangPage` is, because `Inspector.cpp`
was already 9.4k of the 10k per-file budget and could not take the section in
place; the call site in `DrawRobot` is four lines. It goes between "Memories"
and "Recent" - before the event log, whose child window takes all the space
left below it - and takes `&result`, so a click in the section moves the
inspector as the rest of the page does.

- The rows: the entries of `WorldFrame::chronicle` whose `robot` or `otherRobot`
  is this robot, collected newest first by reading the saved list backwards,
  drawn in a frozen-header table `kVisibleRows` (12) rows high with
  `ImGuiListClipper`, so a robot with `kChronicleCap` entries is safe. Columns:
  When (`sim::FormatTime`), What, About whom, Room, Credits. No Building or
  Owner column as the Chronicle tab has: the room's label already carries the
  building's name, and the page is 430 px wide.
- Links: the other robot through `AccountLink(frame, city, ...)`, so a scrapped
  one reads as its remembered name in plain text and an organization as its
  page link; the room through `RoomLink`, or "a room that is gone" when the
  unit closed since. `Counterpart` picks the robot an entry names besides this
  one; no second robot gives an invalid id, which `AccountLink` draws as
  "nobody".
- `WhatName` and `Credits` are local copies of the Chronicle tab's, so one
  entry reads alike in both places; sharing them would put an inspector header
  in the statistics module's context, which the split forbids. C4's
  `ChronicleLists` is where such sharing belongs.
- No sim code touched, so no headless comparison applies (as in C3a and C3b):
  `mechapunk-sim.exe` was not even relinked. ctest 161/161 including
  ContextBudget; the new file is 2.4k, `Inspector.cpp` 9.5k, the inspector
  module 24.9k of 25k.

Warning for C4: the inspector module now stands at 24917 of 25000 tokens, 83
short of its budget. C4 edits `Inspector.cpp`, `InspectorOrganization.cpp` and
the Chronicle tab, so it must first make room - `ChronicleLists.*` gets its
own module (as `organization-lists` and `gang-lists` are) and a page file
moves out of `inspector` - before any History section is added. C4c did this:
`InspectorHistory.cpp` and the new `InspectorChronicle.cpp` are the `chronicle-pages`
module, and `inspector` is back to 23018 of 25000.

### C4. Chronicle details (inspector, statistics) — split on 2026-11-24

The step names two modules and its first bullet lives in a third: `ChronicleLists.*`
and its tests want their own module (as `organization-lists` and `gang-lists` have),
the tab is `statistics`, the pages are `inspector`. Cut into steps of one module and
one commit each, the original bullets living on inside C4a to C4d; C4e is the part
of the step the text could not ask for (see C4a's note).

### - [x] C4a. The chronicle lists (chronicle-lists module)

Open: a new `app/ChronicleLists.hpp/.cpp` and `tests/ChronicleListsTests.cpp`.

- `ChronicleLists` (plain, tested): entries about a robot, a room, a
  building (any of its rooms) and an organization (rooms it owns, robots it
  employs), newest first; and a filter by kind and by day range.
- Tests for every list function; add the new files to the budget script.

Done as (chronicle-lists module, one commit): four list functions
`EntriesAboutRobot`, `EntriesAboutRoom`, `EntriesAboutBuilding`,
`EntriesAboutOrganization`, plus `ChronicleGroup` (the six buttons), `GroupOf`,
`ChronicleFilter` and `FilterChronicle`. Every one returns `const
sim::ChronicleEntry*` pointers into `WorldFrame::chronicle` (the frame keeps them
alive, as `OrganizationLists.cpp` does with loans) and shares one file-local
`Matching` helper that walks the saved list backwards, so every list is newest
first, complete and stable. A building's room ids and an organization's rooms
and workers go into `unordered_set`s used for membership only, never iterated,
so the guide's determinism rule holds; the organization's workers come from
`WorkersOf` in `OrganizationLists.hpp`, the same rule its worker list uses.
The day range is inclusive with a bound of 0 meaning "no bound"
(`sim::DayNumber`), and the name filter lowercases both sides and looks the
robot up in `frame.directory`, so a search finds a scrapped robot's entries.

What the step's text left open, and what was chosen:

- Which kind answers to which button. Gang joining and leaving reads as
  **crime**, the way the Crime tab already tells it; a `Remembered` entry is
  judged by its memory, since `World::Chronicle` keeps only five: friends and
  rivalries **people**, a lost job and a promotion **work**, an eviction
  **homes**. A kind no button names gets `std::nullopt`, so a button on hides
  it - `Departed`, `ArrivedHome`, `WeatherChanged` and the dropped memories.
- **"money it was given" cannot be answered from a saved entry**: `ChronicleEntry`
  keeps no account, and `World::Chronicle` (sim/src/Chronicle.cpp) drops
  `SimEvent::account`, which is where `MoneySupplyChanged` puts it
  (sim/src/Money.cpp:131). An organization's page therefore reaches money only
  through its rooms and its workers; the account is C4e, a save change.
- The tests never skip: the five population tests share one generated city
  (seed 5, 1000 robots, 30 days - the city tests/ChronicleTests.cpp uses), pick
  the id the chronicle names most often and assert it has at least two rows;
  the group and filter tests write their chronicle by hand, so their kinds,
  days and names cannot move with the random stream.

`CMakeLists.txt` gained the file in `mechapunk_layout`, `tests/CMakeLists.txt`
in `layout_tests`, `scripts/context_budget.py` the `chronicle-lists` module
(7560 tokens: 959 + 2054 + 4547) and `docs/MODULES.md` its three tables. ctest
169/169 including ContextBudget. No sim code touched, so no headless comparison
applies (as in C3a, C3b and C3c): `mechapunk-sim.exe` was not even relinked.

### - [x] C4b. The Chronicle tab's filters (statistics module)

Open: `app/StatisticsChronicle.cpp`, `app/Statistics.cpp` (the tab's controls).

- Filter buttons for the six `ChronicleGroup`s, a day range and a search box
  for a robot's name, all fed into `ChronicleFilter` and drawn from
  `FilterChronicle`'s rows instead of reading the vector backwards by hand.
- Keep the frozen header and `ImGuiListClipper`: a filtered list of any length,
  up to `kChronicleCap`, must be safe. The tab's own `WhatName` and `Credits`
  stay where they are.

Done as (statistics module, one commit): the controls are drawn at the top of
`DrawChronicle` and their state lives in `StatisticsView` (`app/Statistics.hpp`),
which is what the statistics window keeps between frames; a function-local
`static` would have hidden that state from the next reader. `DrawChronicle`
therefore gained a `StatisticsView&` first parameter, re-declared in
`app/StatisticsInternal.hpp` (which now includes `Statistics.hpp`), and the
tab dispatch in `Statistics.cpp` passes `view`.

What the step's text left open, and what was chosen:

- One group, not six checkboxes: `ChronicleFilter::group` is a single
  `std::optional<ChronicleGroup>`, so the buttons are one radio group with an
  "All" first option (`ImGui::RadioButton(label, &view.chronicleGroup, n)`,
  0 = every group). The label→group pairs sit in a file-local `kGroups` table
  in the order `ChronicleGroup` declares its values, so no index is cast into
  the enum, and a value outside the table reads as "all" rather than past its
  end.
- The day range is two `ImGui::InputInt` boxes ("From day", "To day"); a bound
  typed as 0 or negative means "no bound", which is what `ChronicleFilter`
  documents for its day numbers.
- The search box is `ImGui::InputText` writing into a `std::array<char, 64>`
  in `StatisticsView`: the Dear ImGui the Conan recipe provides (1.92.8) ships
  no `misc/cpp/imgui_stdlib.h`, so there is no `std::string` overload to call,
  and `IM_COUNTOF` wants a C array, so the length is
  `static_cast<int>(view.chronicleName.size())`; `filter.text` copies the
  buffer.
- A line above the table says how many entries match out of how many are
  saved, and an empty result says what to widen, so a filter that hides
  everything is not mistaken for a chronicle that has nothing in it. The
  controls are drawn before the "Nothing recorded yet" return, so they are
  usable from the first day.
- The table itself changed only in where its rows come from: `FilterChronicle`'s
  pointers (`*rows[row]`, newest first) instead of `entries[size - 1 - row]`.
  The frozen header, the eight columns and `ImGuiListClipper` stay, so a
  filtered list of any length up to `kChronicleCap` is safe; `WhatName` and
  `Credits` did not move.

No new source files, so `scripts/context_budget.py` is unchanged (the statistics
module grew from 18.1k to 19.3k of 25k); `docs/MODULES.md`'s `Statistics` row
now says the tab's controls live in `StatisticsChronicle.cpp`. ctest 169/169
including ContextBudget. No sim code touched, so no headless comparison applies
(as in C3a, C3b and C3c): `mechapunk-sim.exe` was not even relinked. The tab
is for the user to check in the game.

### - [x] C4c. Room and building pages (inspector module)

Open: `app/Inspector.cpp` (room and building pages), `app/InspectorInternal.hpp`,
a new `app/InspectorChronicle.cpp`.

- First make room, as the warning above says: move `InspectorHistory.cpp` out
  of `inspector` into a new `chronicle-pages` module in `scripts/context_budget.py`
  (with the new page file) and re-run `python scripts/context_budget.py` before
  writing any page.
- "What happened here" on the room page and the building page: `EntriesAboutRoom`
  and `EntriesAboutBuilding`, a clipped table `kVisibleRows` (12) rows high,
  every robot and room in a row a link, a scrapped robot its remembered name.

Done as (chronicle-pages module, one commit): the room was made first - `scripts/context_budget.py`
moved `InspectorHistory.cpp` out of `inspector` into a new `chronicle-pages` module
that holds it and the new `app/InspectorChronicle.cpp` plus the headers they lean on
(`InspectorInternal.hpp`, `Links.hpp`, `ChronicleLists.hpp`, `Inspector.hpp`), and
`python scripts/context_budget.py` was run before the page was written: `inspector`
24917 → 23018 of 25000, `chronicle-pages` 8596. No source file moved, so no split
proof applies; the move is only the budget script's membership.

- The sections are `DrawRoomChronicle` and `DrawBuildingChronicle` in the new
  `app/InspectorChronicle.cpp`, declared in `app/InspectorInternal.hpp` the way
  `DrawRobotHistory` is, and called at the end of `DrawRoom` and `DrawBuilding`
  with `&result`, so a click in a section moves the inspector as the rest of the
  page does. `CMakeLists.txt` gained the file in the `mechapunk` executable.
- Both sections draw through one file-local `DrawEntries`, in the Chronicle tab's
  column order: When (`sim::FormatTime`), What, Who, Whom, [Room], Credits, a
  frozen header, `kVisibleRows` (12) rows high and `ImGuiListClipper`, so a list
  of any length up to `kChronicleCap` is safe. Table ids "room history" and
  "building history".
- What the step's text left open, and what was chosen:
  - The room page has no Room column: `EntriesAboutRoom` returns only entries
    whose room is the one already open, so a link to it would reselect the page.
    The step's "every robot and room in a row a link" is met on the building
    page, where the room changes from row to row: `RoomLink`, "a room that is
    gone" for a unit closed since, "-" for room id 0 (an event with no place).
  - Both robots an entry names are links through `AccountLink(frame, city, ...)`,
    so a live robot or organization is clickable, a scrapped one reads as its
    remembered name in plain text and no second robot as "nobody".
  - The building page's section covers the whole building, not the floor the
    slider is on, which is what `EntriesAboutBuilding` answers.
  - `kListFlags`, `kVisibleRows`, `ListSize`, `Credits` and `WhatName` are local
    copies, as `InspectorGang.cpp`, `InspectorOrganization.cpp` and
    `InspectorHistory.cpp` each already have; unifying `WhatName` and `Credits`
    is C4d's.
- No new tests: the rows come from `EntriesAboutRoom` and `EntriesAboutBuilding`,
  already tested in C4a (`ChronicleLists.RoomHistoryNamesItsRoom`,
  `ChronicleLists.BuildingHistoryCoversItsRooms`); the drawing itself needs the
  window. `docs/MODULES.md` gained the `chronicle-pages` row, the
  `InspectorChronicle.cpp` row and a "which module for which task" row. ctest
  169/169 including ContextBudget. No sim code touched, so no headless comparison
  applies (as in C3a, C3b, C3c and C4a, C4b): `mechapunk-sim.exe` was not even
  relinked. The two sections are for the user to check in the game.

### - [x] C4d. Organization page and the robot page's rows (inspector module)

Open: `app/InspectorOrganization.cpp`, `app/InspectorHistory.cpp`.

- The organization page's "History" section: `EntriesAboutOrganization`.
- The robot page's History section reads `EntriesAboutRobot` instead of scanning
  `WorldFrame::chronicle` itself, so its rows match the other pages' - this is
  where C3c's local `WhatName` and `Credits` copies belong to one place.

Done as (chronicle-pages module, one commit): the section is `DrawOrganizationHistory`
in `app/InspectorChronicle.cpp`, next to the room's and the building's sections,
declared in `app/InspectorInternal.hpp` the way they are, and called at the end of
`DrawOrganization` with `&result`, so a click in it moves the inspector as the rest
of the page does. The step's "Open" list names `InspectorOrganization.cpp` for that
call site only: the drawing belongs with the other history sections, in the module
C4c made for them, and `Inspector.cpp` had no room for it anyway.

- The rows are `EntriesAboutOrganization(frame, organization.id)`, already tested
  in C4a (`ChronicleLists.OrganizationHistoryIsItsRoomsAndWorkers`), drawn by the
  file's `DrawEntries` with `showRoom` true: an organization owns rooms in many
  buildings, so the room changes from row to row and is a link ("a room that is
  gone" for a unit closed since, "-" for an event with no place). Table id
  "organization history", frozen header, `kVisibleRows` (12) rows high,
  `ImGuiListClipper`, so `kChronicleCap` rows are safe.
- A line under the section title says what it covers, because that rule is not
  visible from the rows: a robot's eviction or promotion is on the page through
  the employer that owns its workspace.
- The robot page now calls `EntriesAboutRobot`, which is the scan it did by hand
  plus an `IsValid` guard, so its rows cannot drift from the other pages'.
- The copies went to one place: `kListFlags`, `kVisibleRows`, `ListSize`, `Credits`
  and `WhatName` moved out of `InspectorHistory.cpp` and `InspectorChronicle.cpp`
  into a new private header `app/ChroniclePagesInternal.hpp`, the way
  `StatisticsInternal.hpp` is the statistics module's header. `Counterpart` stayed
  in `InspectorHistory.cpp` and `DrawEntries` in `InspectorChronicle.cpp`: each has
  one user (the layout's rule). The statistics tab's copies stayed where C4b left
  them: that file is the `statistics` module, a second module.
- What the step's text left open, and what was chosen:
  - The organization page's own `Credits` ("{} cr", ungrouped) did not move: the
    step asks for one place for the chronicle's copies, and the page's books are
    not chronicle rows.
  - Money an emission gave to the organization itself is still not a row of its
    own: a `ChronicleEntry` saves no account (C4a's note), so the section reaches
    money through its rooms and its workers. That is C4e, a save change, and it
    waits for the user to say whether the page needs it.
- No new tests: the rows come from `EntriesAboutOrganization` and
  `EntriesAboutRobot`, both tested in C4a, and the drawing needs the window (as in
  C4c). `scripts/context_budget.py` gained the header in `chronicle-pages`:
  `inspector` 23018 → 23237 of 25000 (the call site and the declaration),
  `chronicle-pages` 8596 → 8376 (the header's 1059, the two files down 946 and
  489). `docs/MODULES.md` gained the header's row and says the organization page
  has a history section. ctest 169/169 including ContextBudget. No sim code
  touched, so no headless comparison applies (as in C3a, C3b, C3c, C4a, C4b and
  C4c): `mechapunk-sim.exe` was not even relinked. The section is for the user to
  check in the game.

### - [ ] C4e. Money an organization was given (saves module, only if wanted)

C4's bullet "money it was given" needs the account a `MoneySupplyChanged`
event carries, which `ChronicleEntry` does not save (C4a's note). Doing it is a
save change by rule 3: v21, `V20ToV21` adding `account` to every
`world.chronicle[i]`, a new fixture, `CurrentVersionFixtureLoadsAndRoundTrips`
pointing at it and `Version20FixtureMigratesAndRuns`, then `EntriesAboutOrganization`
grows the third rule. Ask the user whether the organization page needs it.


## Done when

- [x] The chronicle is saved, migrated, tested, and shown with links. (C1 to C4d)
- [x] Robots, rooms, buildings and organizations show their own history. (C3c, C4c, C4d)
- [ ] The guide's task table marks C done. (C4e open: ask the user whether the
  organization page needs the account an emission carried, a save change to v21)
