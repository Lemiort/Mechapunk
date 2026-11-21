# Task G: unions

Status: after gangs (B). Save change.

## Steps (money, careers)

- [ ] G1. State: `Union { EntityId employer; std::uint32_t members; bool
  striking; std::uint32_t lowPayNights; }` in `WorldState`, save change.
- [ ] G2. Workers of one employer form a union when its pay percent stays
  under 90% for 7 nights (`SettleEconomy`'s pay step in `Settlement.cpp`).
- [ ] G3. If pay is cut again, the union strikes for a day: its members do
  not commute (`ScheduleNextCommute` in `Behaviour.cpp`); the employer
  earns nothing that day and then raises pay. Statistics and inspector
  lines.
- [ ] G4. Union details (inspector, statistics). Follow the guide's
  section 9 (detail pages): a union page with its employer
  (`OrganizationLink`), members (clipped table: name, job room, level,
  pay), the employer's pay percent over the last 30 days (a plot from
  `DailyStats` or the union's own history), strikes so far and whether it
  strikes now. Links back: the organization page shows its union, a
  member's robot page shows "Union: <UnionLink>". Plain, tested list
  functions in `app/UnionLists.*`.
