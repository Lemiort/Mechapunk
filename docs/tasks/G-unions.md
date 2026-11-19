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
