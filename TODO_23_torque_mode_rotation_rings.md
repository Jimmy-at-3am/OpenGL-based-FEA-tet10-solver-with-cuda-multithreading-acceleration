# TODO_23 — Torque (twist) mode: rotation rings replace translation arrows

## Goal
Each anchor can be toggled between `FORCE` and `TORQUE` type via a side-panel toggle. In `TORQUE` mode, the gizmo shows 3 coloured rotation rings (around the local U / V / N axes) in place of the translation arrows. Dragging along a ring increases the torque component about that axis. Numeric panel header switches from `Force [N]` to `Torque [N·m]`.

## Files added / modified
- **Modify** `include/LoadAnchor.h` — `type` already an enum; just confirm `TORQUE` case is exercised.
- **Modify** `include/LoadGizmo.h`, `src/LoadGizmo.cpp` — torque-ring rendering; ring-drag manipulation math.
- **Modify** `src/SimpleUI.cpp` — `Force` / `Torque` segmented toggle in the anchor panel; relabel fields.
- **Modify** `src/ShaderSources.h` / `BuiltInShader.cpp` — torus / ring primitive shader (extruded circle ribbon).
- **Modify** `src/FEASolver.cpp` — accept and apply torques (see "Solver integration" below).

## Algorithm summary
**Ring rendering:**
```text
For selected anchor a with type==TORQUE:
    for axis k in {u, v, n}:
        ring_axis      = local axis k
        ring_plane     = the plane perpendicular to ring_axis at anchor.position
        ring_radius    = anchorScale * 1.2
        ring_thickness = 0.05 * anchorScale + magnitudeScale * |T·axis|
        draw torus(centre=anchor.position, normal=ring_axis,
                   inner=ring_radius - ring_thickness/2,
                   outer=ring_radius + ring_thickness/2,
                   colour=axisColour(k))
        draw arc-arrow at +90° around ring (chirality indicator) so user sees sign convention
```

**Ring-drag interaction:**
```text
on mouse-down on ring k:
    dragAxis = k
    dragStartScreen = mouseScreen
    dragStartTorqueAxis = T · axis_k
    anchorScreen = project(anchor.position)
    // tangent direction at the click point, in screen space:
    p0_world = ring-surface point under cursor
    p0_screen = project(p0_world)
    tangent_screen = perpendicular_in_plane(anchor_screen - p0_screen, ring_axis_screen)

on mouse-move while dragging:
    deltaPx = dot(mouseScreen - dragStartScreen, tangent_screen)
    T_k = dragStartTorqueAxis + deltaPx * torqueSensitivity
    anchor.vector = build vector with the k-th component = T_k and other components preserved
```

`torqueSensitivity` = `maxExpectedTorque / 200` per pixel; default `maxExpectedTorque = 100 N·m`.

**Toggle behaviour** (panel):
```text
[ Force ]  ( Torque )    <- segmented control; clicking switches a.type
```
When toggling Force → Torque on an anchor with non-zero force, the magnitude is *preserved* numerically (i.e. `|F| in N` becomes `|T| in N·m`); the unit changes, the value doesn't. This is intentional — the user nearly always re-edits the value anyway, and discarding it on toggle is surprising. A panel info icon next to the value text explains the unit change.

**Solver integration** (point-torque math):
Tet elements have only translational DOFs. A point torque at a single node is mathematically a *couple* and must be distributed onto a small region. Two implementation choices:

1. **Single-node couple** (cheapest): apply the torque as an equivalent force pair to the 1-ring of nearby nodes on the same surface. Force pair: $F_\perp = \tau / r_{\text{eff}}$ where $r_{\text{eff}}$ is the radius of gyration of the 1-ring. Sum of forces zero; net moment matches $\tau$ to the leading order. Simple, $O(1)$ per torque.

2. **Distributed-couple via patch** (more accurate): treat the point-torque as a patch-torque with a default small patch radius ($r = 2\,\text{anchorScale}$). Defer to TODO_24's machinery.

**Ship option 1 in TODO_23**; let TODO_24's patch mode replace it with option 2 when the user opts into a patch.

## Multithreading
None new. Solver integration is in the existing per-element load-assembly loop and inherits its OMP parallelisation.

## Numerical care points
- **Couple-to-force-pair r_eff selection** — small `r_eff` produces large nodal forces that locally distort the mesh; clamp `r_eff >= 1.5 * h_local`. If no neighbour nodes are within reach, emit a warning in console and zero out the torque (rather than producing nonsense forces).
- **Ring chirality** — the arc-arrow indicator MUST follow the right-hand rule about the ring's axis. Easy to mis-orient; sanity check: a $+T_z$ on the top face of the cube should make the right-hand-rule curl visibly counterclockwise from above.
- **Toggle preserving magnitude** — store the value in a single `vector` and a `magnitudeOverride` field but never auto-rescale on toggle. Document in code.
- **Numeric-panel unit display** — units must update with the type. Easy mistake: panel header switches but text-field suffix doesn't.

## Screen-visible acceptance check

**Setup**
1. Run TODO_22 acceptance check (anchor #1 selected, gizmo visible with arrows).

**Action**
1. In the right-side panel, click `Torque` in the segmented `Force | Torque` toggle.
2. Drag the green (V-axis) ring counterclockwise (as viewed from outside).
3. Click `Force` in the toggle to switch back.

**Expected visible**
- After step 1: arrows disappear; 3 coloured rings appear at anchor #1 (red around U, green around V, blue around N). Panel header reads `Anchor #1 [Torque]`; field labels now `Tx`, `Ty`, `Tz` with unit `N·m`. Magnitude value preserved (e.g. `Ty: -3.1` was `Fy: -3.1`).
- After step 2: green ring visibly thickens; `Ty` panel field updates live, e.g. from `-3.1` to `47.2`. The arc-arrow chirality on the ring matches right-hand-rule (verify by viewing the cube from $+y$ — counterclockwise drag should increase $T_y$).
- After step 3: rings replaced by arrows; values preserved; panel header back to `Force`.

**Failure modes**
- Rings drawn outside the screen near the anchor → ring radius computed wrong (must scale with bbox).
- Ring chirality mismatched (clockwise drag increases $T_v$) → tangent-direction sign flipped in screen-space calculation.
- Toggling Force→Torque zeroes the value → magnitude not preserved across the toggle.
- Solver crashes on Solve with a torque present → no `r_eff` clamp; small mesh, couple-force-pair too large.

**Pass criterion**
Toggle switches gizmo glyphs cleanly; ring drag updates torque component with correct chirality; unit labels update; solver accepts the torque without crash.

## Regression sentinel
`regression/torque_gizmo_cube.txt` — programmatic test placing a torque anchor, asserting that `solveLinearStatic` succeeds and produces nonzero displacement consistent with right-hand-rule rotation about the chosen axis.

## Depends on
TODO_22.

## Estimated effort
2.5 days.
