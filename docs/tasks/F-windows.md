# Task F: see-through windows

Status: optional. The user wants more detail, never less.

## Steps

- [ ] F1 (view-core). Lit windows by real occupancy: a window glows when its
  apartment's residents are home and awake (`EconomyFrame::owners` and
  `RobotFrame::apartment`, `activity`). Today it is a fixed pattern
  (`rg -n "window" app/CityLights.cpp app/CityStatic.cpp`).
- [ ] F2 (view-interiors). Window panes of the open building are glass
  (`SubmitTransparentMesh`), so its interior shows through.
- [ ] F3. Later: a cube map or a simple room box behind lit windows of
  closed buildings.
