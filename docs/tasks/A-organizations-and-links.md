# Task A: organizations in the inspector, and every name a link

Status: open. No save change: everything shown is already in `WorldFrame`.

## What the user asked for

"Interface improvements: add a list of property and workers to an
organisation. Also every reference (name, organisation, building) should be
clickable, so we can switch the inspector to that object."

So:
1. The inspector can show an **organization**: its facts and books, its
   owners, **every room it owns** (grouped by building), and **every robot
   working for it**.
2. **Every** robot name, organization name, building name and room name in
   the inspector and the statistics window is a link. Clicking it switches
   the inspector to that object (a robot, an organization, a building, a
   room). Double-clicking a robot also moves the camera to it, as today.

## Read first

- `docs/IMPLEMENTATION_GUIDE.md` sections 1 to 3.
- `docs/MODULES.md`: modules `ui` and `statistics`. Step 1 splits `ui`
  so the inspector becomes its own module.
- How links work today, in `app/Inspector.cpp` (find with `rg -n`):
  `RoomLink`, `ShowRoom`, `ShowRobot`, `RobotLink`. A link is an
  `ImGui::Selectable`; clicking returns an `InspectorRequest` (a
  `Selection` plus `jumpToRobot`), which `main.cpp` applies with
  `ApplySelection` (see the block calling `DrawStatistics` and
  `DrawInspector`). `Selection` is in `app/CityView.hpp`.
- The data: `EconomyFrame` in `app/SimulationHost.hpp` has
  `organizations` (sorted by id), `loans`, `owners` (owner of every room,
  by room id; organizations and robots) and `OwnerOf(room)`. A robot's
  employer is `OwnerOf(robot.workspace)`. `SimulationHost::AccountName`
  names a robot or an organization; `SimulationHost::FindRobot` finds a
  living robot.

## Steps

Each step is one commit. Build everything and run `ctest` after each.

### - [x] A1. Move links into their own files (inspector module)

Open: `app/Inspector.cpp`, `app/Inspector.hpp`, `scripts/context_budget.py`,
root `CMakeLists.txt`.

- Create `app/Links.hpp` and `app/Links.cpp` (part of the `mechapunk`
  executable). Move `ShowRoom`, `ShowRobot`, `RoomLink` and `RobotLink`
  there unchanged, plus `RoomLabel` and `KindLabel` if the links need them,
  and declare them in `Links.hpp`. `Inspector.cpp` includes `Links.hpp`.
- In `scripts/context_budget.py`, split module `ui` into `ui`
  (`app/main.cpp`, `app/SimulationHost.hpp`, `app/SimulationHost.cpp`) and
  a new `inspector` (`app/Inspector.hpp`, `app/Inspector.cpp`,
  `app/Links.hpp`, `app/Links.cpp`, `app/SimulationHost.hpp`,
  `app/CityView.hpp`). Update the table in `docs/MODULES.md` (its
  `--markdown` output) and the "Which module" rows for the inspector.
- Nothing visible changes. Commit: "Move inspector links into their own file".

### - [ ] A2. Links for every kind of object (inspector module)

Open: `app/Links.hpp`, `app/Links.cpp`, `app/CityView.hpp` (only
`Selection`), `app/Inspector.cpp`, `app/main.cpp` (only the block that
applies requests).

- `Selection` gains `sim::EntityId organization;` with a comment: the
  organization shown in the inspector; the camera and the cutaway ignore it.
- New helpers in `Links`, all returning `std::optional<InspectorRequest>`
  and all labelled `"<text>##<kind><id>"` so equal names never clash:
  - `BuildingLink(city, building, prefix)`: selects the building
    (`Selection{.building = id, .floor = 0}`).
  - `OrganizationLink(frame, organization, prefix)`: selects
    `Selection{.organization = id}`.
  - `AccountLink(frame, city, account, prefix)`: a robot (via `RobotLink`
    and `ShowRobot`), an organization, or plain disabled text "nobody" for
    none. Remembered scrapped robots (`FindRobotInfo` but not
    `FindRobot`) are plain text: there is nothing to show.
  - Make `RoomLink` return `std::optional<InspectorRequest>` too, so every
    link has the same shape.
  - Draw links in one colour (a `kLink` light blue pushed around the
    `Selectable`) with the tooltip "Show in the inspector", so players see
    what is clickable.
- `DrawInspector`: an organization selection is a valid selection (the
  early return checks it), and shows the organization page from step A3
  (until then, its name and "coming soon" are fine within this step).
- `main.cpp`: `ApplySelection` must keep `organization`. Selecting a
  robot, room or building clears it; selecting an organization clears the
  robot and the room but keeps the open building, so the city view does not
  jump.
- Commit: "Add links to buildings, organizations and any account".

### - [ ] A3. Who owns what and who works where (layout library, testable)

Open: `app/SimulationHost.hpp` (only `RobotFrame` and `EconomyFrame`),
`sim/include/mechapunk/sim/City.hpp` (only `RoomIndex`), root
`CMakeLists.txt`, `tests/CMakeLists.txt`, `scripts/context_budget.py`.

Lists are built by plain functions, so they can be tested without a window:

- New `app/OrganizationLists.hpp/.cpp`, added to the `mechapunk_layout`
  library (it already links the simulation and the engine):
  ```cpp
  // A room an account owns, with its building, for listing.
  struct OwnedRoom { sim::EntityId building; std::uint32_t floor{}; sim::EntityId room; };
  // Every room `owner` owns, by building id, then floor, then room id.
  std::vector<OwnedRoom> RoomsOwnedBy(const EconomyFrame& economy,
                                      const sim::RoomIndex& rooms, sim::EntityId owner);
  // Every living robot whose job room `employer` owns, by robot id.
  std::vector<const RobotFrame*> WorkersOf(const WorldFrame& frame, sim::EntityId employer);
  // Robots and organizations owing `bank`, largest debt first, then by id.
  std::vector<const sim::Loan*> LoansFrom(const EconomyFrame& economy, sim::EntityId bank);
  ```
  Iterate `owners` (an unordered map) into a vector and **sort**: the order
  on screen must not jump between frames.
- New `tests/OrganizationListsTests.cpp` in `layout_tests`: build a
  `WorldFrame` from a generated city (`sim::World::Generate({.seed = 3,
  .robots = 300})`, then fill `RobotFrame`s and `EconomyFrame::owners`
  the way `SimulationHost::Publish` does, or call it if it is free of
  threads) and check: the Habitat Trust owns apartments and they are sorted;
  City Hall owns the dorms and police stations; every robot with a job is
  a worker of exactly one organization; an unknown id gives empty lists.
- Add the new files to modules in `context_budget.py` (`inspector`).
- Commit: "List an organization's property, workers and loans".

### - [ ] A4. The organization page (inspector module)

Open: `app/Inspector.cpp` (only `DrawInspector`), `app/Links.hpp`,
`app/OrganizationLists.hpp`, `app/Statistics.cpp` (only
`DrawOrganizations`, to reuse its fact rows).

- New `app/InspectorOrganization.cpp` with
  `std::optional<InspectorRequest> DrawOrganization(const Selection&, const
  WorldFrame&, const CityView&)`, declared in a small
  `app/InspectorInternal.hpp` (shared by the `Inspector*.cpp` files only).
  Keep `Inspector.cpp` from growing.
- Sections, top to bottom:
  1. Title: the name; kind and sector ("bank", "central bank",
     "corporation, industry", "City Hall").
  2. Facts: credits; what it owes and to whom (`AccountLink` to the
     lender); pay percent for industry; yesterday's revenue, wages,
     supplies, upkeep, dividends, repaid (as in the Organizations tab).
  3. Owners: each owner as an `AccountLink` with its share in percent.
  4. **Property**: "N rooms in M buildings", then one
     `ImGui::TreeNode` per building (label: `BuildingLink` text and room
     count), and inside it a table: room (`RoomLink`), kind
     (`KindLabel`), floor, rent or price, residents or staff now. Rooms of
     a whole building at once are many (the Habitat Trust owns thousands):
     use `ImGuiListClipper` for any list over 50 rows.
  5. **Workers**: "N workers", then a table: name (`RobotLink`; a click
     selects, a double-click jumps), job room (`RoomLink`), shift
     (`sim::JobShift` start and end as clock times), job level, what it is
     doing now. Clipped like the property list.
  6. For banks: loans given (`LoansFrom`), each borrower an `AccountLink`
     with the amount owed; and the number of robots saving with it.
- Commit: "Show organizations in the inspector with property and workers".

### - [ ] A5. Make every reference a link (inspector and statistics)

Open: `app/Inspector.cpp`, `app/Statistics.cpp`, `app/Links.hpp`. Work one
file at a time if the context is tight (two commits are fine).

Find every reference printed as plain text:
`rg -n "AccountName|NameOf|Name\(frame|RoomLabel|->name|\.name" app/Inspector.cpp app/Statistics.cpp`
and turn each into the matching link. Known places:
- Robot page: where it is now ("Resting at home", working at, visiting:
  the room as `RoomLink`); savings bank (`OrganizationLink`); the lender
  of its loan; its landlord ("Rents its home from"); the other apartments it
  owns (list them, each a `RoomLink`, instead of "Owns N other
  apartments"); each organization it owns part of (`OrganizationLink`,
  one per line or comma-separated `SameLine` links); its employer next to
  "Work:" (`OrganizationLink`); robots named in memories.
- Room page: the owner (any account, not only robots: `AccountLink`); the
  building it is in (`BuildingLink`); the operator of a venue.
- Building page: the owners of its rooms, summed ("Owned by: Habitat Trust
  (24 rooms), Rex-41 (1 room)", each a link).
- Statistics, Organizations tab: clicking a name now opens the organization
  in the inspector (return an `InspectorRequest`); remove the details pane
  under the table, since the inspector shows it; owners stay links.
  Central bank tab: the account of each emission is an `AccountLink`.
- Keep `DrawStatistics`'s contract: it returns the request; `main.cpp`
  applies it like the inspector's.
- Commit: "Make every name in the inspector and statistics a link".

## Done when

- [ ] Clicking any robot, organization, building or room name anywhere in
  the inspector or the statistics window shows it in the inspector.
- [ ] An organization's page lists its rooms by building and its workers,
  with counts, and stays smooth with the Habitat Trust (thousands of rooms).
- [ ] `layout_tests` cover the lists; all tests and `ContextBudget` pass.
- [ ] The guide's task table marks A done; `docs/MODULES.md` lists the new
  files.
- [ ] The user has checked it in the game (ask them; do not drive the
  window yourself).
