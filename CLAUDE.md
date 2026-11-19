# Mechapunk

A cyberpunk robot city simulation (C++23, own engine NEE). Read
`docs/IMPLEMENTATION_GUIDE.md` first: how to work with a small context,
build and test commands, the rules that must never break (determinism,
money adds up, save migrations, no teleporting robots) and the task list.
Each task has its own file in `docs/tasks/`, cut into steps of one module
and one commit each. `ROADMAP.md` has the milestones and known gaps.

The code is split into modules that each fit a small model's context: see
`docs/MODULES.md` for which files to open for a task, and open only those.

Quick reference:
- Build: `cmake --build build/build/Release`; test: `ctest --test-dir build/build/Release`.
- Headless check: `build/build/Release/mechapunk-sim.exe --seed 1 --robots 10000 --days 40 --quiet`.
- Any change to saved state: bump the save version, add a frozen migration,
  a new fixture and tests (guide, rule 3).
- Commit game work to `master` and push; NEE only through pull requests.
- Do not drive or screenshot the game window unless the user says the
  machine is free.
- Keep modules small: `python scripts/context_budget.py` (also in ctest) must
  pass; split files by topic as `docs/MODULES.md` describes.
