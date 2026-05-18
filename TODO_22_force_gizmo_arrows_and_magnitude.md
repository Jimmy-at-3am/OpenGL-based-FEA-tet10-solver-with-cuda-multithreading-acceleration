# TODO_22 — Force gizmo: 3 translation arrows + magnitude scale handle + numeric panel

## Goal
Clicking an existing anchor (placed by TODO_21) selects it and reveals a 3-axis manipulation gizmo: three colored arrows (red X, green Y, blue Z in the anchor's local tangent frame), a spherical handle at each arrow's tip for axis-only nudging, and a single uniform-scale handle at the anchor centre. Dragging any handle live-updates the force vector and displays `(Fx, Fy, Fz, |F|)` in a side panel.

## Files added / modified
- **Modify** `include/LoadGizmo.h`, `src/LoadGizmo.cpp` — add `GizmoState { selectedAnchor, hoveredHandle, dragAxis, dragStartPos }`; manipulation routines.
- **Modify** `src/SimpleUI.cpp` — anchor-selected side panel: textfields for $F_x, F_y, F_z$ + read-only `|F|`; reset / delete buttons.
- **Modify** `src/main.cpp` — extend mouse-event dispatch with selection (click on anchor in any mode) and drag-with-axis-lock.
- **Modify** `src/ShaderSources.h` / `BuiltInShader.cpp` — arrow primitive (cylinder shaft + cone tip) instanced shader; gizmo handles always rendered on top (no depth test).

## Algorithm summary
**Local axes:** the gizmo's X axis = anchor.surfaceTangentU, Y = anchor.surfaceTangentV, Z = anchor.surfaceNormal. The user can toggle to world axes via a panel checkbox — default is local since "force along the inward normal" is the common case.

**Rendering:**
```text
For each anchor a in model.loadAnchors:
    if a is selectedAnchor:
        draw arrow along a.surfaceTangentU  (red)   length = anchorScale + |F·u| * lengthPerN
        draw arrow along a.surfaceTangentV  (green) length = anchorScale + |F·v| * lengthPerN
        draw arrow along a.surfaceNormal    (blue)  length = anchorScale + |F·n| * lengthPerN
        draw small sphere at each arrow tip (the per-axis tweak handle)
        draw central sphere of radius scaledByMagnitude (uniform-scale handle)
        draw thin centre-line connecting anchor to arrow tip
```

`anchorScale` = 5% of bbox diagonal — base length so an unset force is still visible. `lengthPerN` chosen so the default force (10 N) renders at 1.5× anchor scale.

**Interaction state machine:**
```text
on mouse-down at pixel p:
    handle = pickGizmoHandle(p)         // ray-pick against handle spheres + arrow shafts
    if handle.valid:
        dragAxis = handle.axisLocal     // u, v, n, or "uniform"
        dragStartPos = a.vector
        dragRefScreen = p
    else if pickAnchorBase(p) returns some anchor a':
        selectedAnchor = a'
    else:
        selectedAnchor = none

on mouse-move while dragging:
    if dragAxis is one of u/v/n:
        // project mouse delta onto the screen-space axis direction
        deltaPx = dotProduct(currentScreenPx - dragRefScreen, screenAxis(dragAxis))
        F_component = dragStartPos · dragAxis + deltaPx * sensitivity
        a.vector += dragAxis * (F_component - dragStartPos · dragAxis)
    else if dragAxis == "uniform":
        // radial drag: positive outward, negative inward
        radiusBefore = |dragRefScreen - anchorScreen|
        radiusNow    = |currentScreenPx - anchorScreen|
        scale = radiusNow / radiusBefore
        a.vector = dragStartPos * scale

on mouse-up: dragAxis = none
```

`sensitivity` is in N per pixel; default = `maxExpectedForce / 200` so a half-screen drag swings full range. User can override in a settings panel.

**Numeric panel** (lives in SimpleUI's right panel when an anchor is selected):
```text
Anchor #2 [Force]
  Fx: [   124.7] N      (text-field, editable, syncs to vector)
  Fy: [    -3.1] N
  Fz: [   -50.0] N
  |F|: 134.7 N          (read-only)
  Direction: (0.925, -0.023, -0.371)
  [Reset]  [Delete]
```

Edits in the text fields override `a.vector` directly; the gizmo redraws to match.

## Multithreading
All work on the main thread (GL context + UI events). No new threading.

## Numerical care points
- **Pixel-to-world scaling drift** — when the camera zooms, `sensitivity` should remain constant in world units, not pixels. Multiply by `worldUnitsPerPixelAtAnchorDepth` (computed from camera + anchor depth).
- **Local-axis stability** — `surfaceTangentU/V` from TODO_21 must remain stable as the anchor sits on the same triangle. Never recompute the tangent frame mid-drag (would cause coupling between Fx and Fy).
- **Float overflow on text-field edit** — clamp magnitudes to `[1e-6, 1e9]` N; reject NaN / Inf paste.
- **Handle pick precedence** — when a handle and an anchor base overlap on screen (zoomed out): pick the handle first if the gizmo is already shown for that anchor; pick the anchor base if no gizmo is shown. Otherwise selections become impossible to escape.

## Screen-visible acceptance check

**Setup**
1. Run TODO_21 acceptance check (3 anchors placed on the cube).
2. Stay in or exit Force Mode (gizmo works in both — selection is always available).

**Action**
1. Click anchor #1.
2. Observe gizmo + numeric panel.
3. Drag the red (X) arrow's tip handle outward.
4. Drag the central uniform-scale handle outward.
5. Type `-100` into the `Fz` text field; press Enter.
6. Click empty space.

**Expected visible**
- After step 2: 3 perpendicular coloured arrows + spherical handles + central scale sphere appear at anchor #1. Right-side panel shows `Anchor #1 [Force]`, `Fx: 0.0`, `Fy: 0.0`, `Fz: -10.0`, `|F|: 10.0`.
- After step 3: red arrow visibly lengthens; `Fx` updates live as the mouse moves (e.g., `Fx: 47.2`).
- After step 4: all three arrows scale uniformly; `|F|` climbs proportionally; `Fx/Fy/Fz` ratios preserved.
- After step 5: Z arrow snaps to new length matching $F_z = -100$ N; `|F|` recomputed.
- After step 6: gizmo disappears; numeric panel hides; anchor sphere remains.

**Failure modes**
- Arrows always world-aligned (red along world X) → tangent frame ignored; gizmo using world basis instead of `(u, v, n)`.
- Drag direction flipped (push left, force grows right) → screen-axis projection sign inverted.
- Arrows occluded by mesh interior → depth-test not disabled for gizmo render pass.
- `|F|` panel value lags by 1 frame → text-field update path not invoked until next event; bind directly.
- Selecting anchor #2 leaves anchor #1's gizmo visible → `selectedAnchor` not single-valued.

**Pass criterion**
All three axes drag smoothly with live numeric updates; uniform-scale preserves direction; text-field edit is reflected in the arrow drawing; deselecting hides gizmo cleanly.

## Regression sentinel
`regression/force_gizmo_cube.txt` — programmatic test that places an anchor, sets `vector = (0, 0, -10)` via API, then sets `Fz = -100` and asserts `|F| == 100`. UI responsiveness still requires manual check.

## Depends on
TODO_21.

## Estimated effort
3 days (handle picking + axis-aligned drag math is the bulk; rendering is mechanical).
