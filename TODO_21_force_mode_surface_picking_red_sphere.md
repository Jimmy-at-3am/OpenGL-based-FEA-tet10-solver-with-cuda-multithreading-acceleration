# TODO_21 — Force mode + ray-picked surface cursor + click-drop anchor

## Goal
A `Force Mode` button enters a modal interaction state. While in the mode, a small red sphere hovers on the model surface following the mouse cursor (snapped to the nearest surface point). A left-click drops a permanent **load anchor** at that surface point. `Esc` exits the mode. Multiple anchors persist on screen between mode toggles. Foundation for TODO_22 (gizmo), TODO_23 (torque), TODO_24 (patch mode).

## Files added / modified
- **Add** `include/LoadAnchor.h` — struct holding `{ position, surfaceNormal, surfaceTangentU, surfaceTangentV, type=FORCE|TORQUE, applicationMode=POINT|PATCH, vector, magnitudeOverride, patchHalfExtents, patchRotation, surfaceFaceId }`.
- **Add** `include/LoadGizmo.h`, `src/LoadGizmo.cpp` — central data model + ray-picking + the `RAYPICK_SPHERE` rendering pass.
- **Add** `include/RayPicker.h`, `src/RayPicker.cpp` — Möller–Trumbore ray-triangle intersection, BVH lookup, returns nearest hit.
- **Modify** `include/FEAModel.h` — add `std::vector<LoadAnchor> loadAnchors`.
- **Modify** `src/SimpleUI.cpp` — `Force Mode` toggle button + status indicator "Force mode active (Esc to exit)".
- **Modify** `src/main.cpp` — input dispatcher dispatches mouse-move + click events to `LoadGizmo` when in force mode; ESC handler.
- **Modify** `src/ShaderSources.h` / `BuiltInShader.cpp` — instanced sphere primitive (reuses corner-sphere shader from TODO_09 if landed; otherwise add).

## Algorithm summary
**Ray-picking** ([Möller-Trumbore 1997](https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm)):

```text
For each mouse-move event in Force Mode:
    ray.origin = camera position
    ray.direction = unproject(mouseX, mouseY, camera) — normalised
    hit = RayPicker::nearestSurfaceHit(ray, surfaceBVH)
    if hit.valid:
        cursor.position = hit.point
        cursor.normal   = surfaceVertices[hit.tri].interpolatedNormal(hit.uvw)
        cursor.show     = true
    else:
        cursor.show = false

For each click event in Force Mode:
    if cursor.show:
        LoadAnchor a;
        a.position = cursor.position
        a.surfaceNormal = cursor.normal
        (a.surfaceTangentU, a.surfaceTangentV) = orthonormalFrameFromNormal(a.surfaceNormal)
        a.type = FORCE   // default; TODO_23 lets user toggle
        a.vector = a.surfaceNormal * defaultMagnitude   // default = -10 N along inward normal
        a.applicationMode = POINT
        a.surfaceFaceId = hit.tri
        model.loadAnchors.push_back(a)
        log "[Load] anchor #N at (x,y,z) normal (nx,ny,nz)"
```

**Tangent frame** (used by TODO_22..24): from a unit normal $\hat n$, build $(\hat u, \hat v)$ via Gram-Schmidt against world up; if $|n \cdot \text{up}| > 0.99$ use world X instead. Both tangents stored in the anchor so downstream gizmos don't recompute.

**BVH for surface triangles:** use meshoptimizer's spatial-sort helper (already a dep) to construct a per-load-mode-entry BVH over the boundary triangles of the current mesh. Rebuilt whenever the mesh changes (cheap: $O(N \log N)$ on $\sim 10^4$–$10^5$ tris).

## Multithreading
Single-ray-per-frame; serial is fast enough ($\sim 1$ ms on 100 K tris with BVH). BVH build is one-shot at mode entry; could parallelise but unnecessary. `useMultithreading` flag has no effect here.

## Numerical care points
- **Self-intersection on click** — never trigger another click event during the click frame; debounce with a frame-level "click consumed" flag.
- **Camera-inside-mesh handling** — if `ray.origin` is inside the volume, `nearestSurfaceHit` returns the back-face. Allow this (it's the user's intent) but tint the cursor a darker red to signal "back face".
- **BVH staleness** — every time `model.tetrahedra` or `model.originalVolumetricPositions` changes (re-mesh, ODT smoothing, AMR step), invalidate the BVH and rebuild on next mode entry.
- **Frame-pacing** — ray-pick must run **after** view matrix update, before draw, every frame the mode is active. Don't pick on mouse-move events alone — that's coarser than render frame rate.

## Screen-visible acceptance check

**Setup**
1. Build & launch.
2. Click `Generate Cube` (existing) then `Generate 3D Mesh`.

**Action**
1. Click the new `Force Mode` button. Status banner reads `Force mode active (Esc to exit)`.
2. Move the mouse around over the cube — observe the red sphere.
3. Left-click 3 different surface points.
4. Press `Esc`.

**Expected visible**
- A small red sphere (radius ~ 1% of bbox diagonal) appears at the mouse position on the surface and tracks smoothly as the mouse moves over the visible faces.
- The sphere disappears when the mouse is off the model.
- After each click: a slightly larger dark-red sphere remains at the click location; console: `[Load] anchor #1 placed at (0.50, 0.50, 1.00) normal (0.00, 0.00, 1.00)` etc.
- 3 persistent anchors visible after the 3 clicks.
- Pressing `Esc` hides the tracking-red-sphere but leaves the dropped anchors visible.
- Re-entering `Force Mode` and clicking again adds anchor #4 without removing 1–3.

**Failure modes**
- Tracking sphere flickers / appears far from surface → BVH rebuilt with wrong vertex array (using volumetric vertices instead of surface vertices) OR camera-ray unprojection has a sign error (test by clicking near image centre — should hit near bbox centre).
- Sphere passes through model on back-facing surfaces → BVH only stores front-facing triangles; ensure both winding directions are included.
- No anchor persists after click → click event consumed by another UI element; verify input dispatch order (Force-mode handler before generic UI).
- Multiple anchors stack at same position on a single click → click event fired twice per render frame (debounce missing).

**Pass criterion**
3 anchors visible at 3 clicked positions; tracking sphere visibly hugs the surface during mouse motion; `Esc` exits cleanly with anchors retained.

## Regression sentinel
`regression/load_anchors_cube.txt` — script that programmatically places 3 anchors at fixed surface-coordinate positions and asserts they appear in `model.loadAnchors`. Manual visual check still required (UI responsiveness).

## Depends on
None — independent UX/data-model work. (Anchors are stored only; nothing consumes them yet.)

## Estimated effort
3 days (most is the input-dispatch state machine + BVH; ray-tri intersection itself is trivial).
