# TODO_24 — Surface-patch application: tangent plane with adjustable side; distributed solver load

## Goal
Each anchor can be toggled between `POINT` and `PATCH` application mode. In `PATCH` mode, a semi-transparent translucent quad is drawn tangent to the surface at the anchor with default extent 5% of bbox diagonal. Four edge arrows let the user resize the rectangle independently along its tangent axes; a corner handle rotates the rectangle within the tangent plane. The solver then distributes the load over all surface nodes that fall under the patch, weighted by tributary area (the same algorithm already used in [theory.md §4.1](theory.md)).

This is the **realistic load model** for engineering: very few real loads are truly point-loaded (point loads produce a stress singularity); a load over a patch better matches a clamp, contact face, or pressure region.

## Files added / modified
- **Modify** `include/LoadAnchor.h` — confirm `applicationMode = POINT|PATCH`, `patchHalfExtents = (hu, hv)`, `patchRotationDeg`.
- **Modify** `include/LoadGizmo.h`, `src/LoadGizmo.cpp` — patch rectangle rendering; edge-arrow + rotation-handle interaction.
- **Modify** `src/SimpleUI.cpp` — anchor panel: `Point | Patch` segmented toggle; if `Patch`: extra fields for `hu`, `hv`, `rotation`, read-only `patch area`, `|F|/A` (or `|T|/A` for torque-patch).
- **Modify** `src/FEASolver.cpp` — load-assembly loop: for each `PATCH`-mode anchor, distribute the load onto surface nodes inside the patch using tributary-area weighting (same code path as the existing whole-surface load).
- **Modify** `src/ShaderSources.h` / `BuiltInShader.cpp` — translucent quad shader with depth-write off; edge-arrow primitives.
- **Modify** `src/FEAModel.cpp` — when an anchor's patch changes, recompute and cache `std::vector<int> patchAffectedNodes` for that anchor (so the solver doesn't re-do the inclusion test every assembly).

## Algorithm summary
**Patch rendering** (in anchor's local tangent frame $(\hat u, \hat v, \hat n)$):
```text
For each anchor a with applicationMode == PATCH:
    R = rotationMatrix(a.patchRotationDeg about hat_n)
    u_axis = R * hat_u
    v_axis = R * hat_v
    half_u = a.patchHalfExtents.u
    half_v = a.patchHalfExtents.v
    corners = anchor.position
              ± half_u * u_axis ± half_v * v_axis     (4 corners on tangent plane)
    draw translucent quad through 4 corners (semi-transparent blue, alpha=0.3)
    draw 4 edge arrows at midpoints, pointing outward along (±u_axis, ±v_axis)
        each arrow drag scales the corresponding half-extent
    draw 1 rotation handle as a small ring at corner (anchor.position + half_u * u_axis + half_v * v_axis)
```

**Edge-arrow drag:**
```text
on mouse-down on edge arrow (say +u edge):
    dragArrow = (+u)
    dragStartExtent = a.patchHalfExtents.u
    dragRefScreen = mouseScreen

on mouse-move:
    deltaWorld = unprojectScreenDeltaAlongAxis(mouseScreen, dragRefScreen, u_axis)
    a.patchHalfExtents.u = max(minHalfExtent, dragStartExtent + deltaWorld)
    invalidate patchAffectedNodes cache
```

`minHalfExtent` = `2 * h_local` (twice the local mesh size) to ensure the patch always covers at least a small ring of surface nodes.

**Rotation-handle drag:**
```text
on mouse-down on rotation ring at corner:
    dragMode = "rotate"
    refAngle = atan2(currentVector_in_tangent_plane)

on mouse-move:
    newAngle = atan2(currentVector_in_tangent_plane)
    a.patchRotationDeg += (newAngle - refAngle) * 180/pi
    invalidate cache
```

**Solver integration — distributing the load:**
```text
recomputePatchAffectedNodes(anchor):
    patchAffectedNodes = []
    for each surface vertex v_i:
        d_world = v_i.position - anchor.position
        if dot(d_world, hat_n) > 2 * h_local: continue   // not near the tangent plane
        d_tangent = (dot(d_world, u_axis), dot(d_world, v_axis))
        d_tangent_unrotated = rotate(d_tangent, -patchRotationDeg)
        if abs(d_tangent_unrotated.x) <= half_u AND abs(d_tangent_unrotated.y) <= half_v:
            patchAffectedNodes.append(v_i.index)
    Cache.

In solver's load-assembly pass (similar to existing tributary-area logic from theory.md §4.1):
    For each anchor a:
        if a.applicationMode == POINT:
            apply a.vector to a's single closest surface node (existing point-load path)
        else: // PATCH
            compute total tributary area: A = sum over patchAffectedNodes of (Voronoi-area on surface)
            traction t = a.vector / A      // N/m^2  for force; N for torque per length
            for each node n in patchAffectedNodes:
                F_n = t * voronoiArea_on_surface(n)
                add F_n to global F at n's DOFs
            // for torque patches: distribute moment as a force-pair using the patch's geometric centre vs offset.
```

This **exactly reuses the algorithm in [theory.md §4.1](theory.md)** that the existing whole-surface load distribution uses — meaning the math is already validated; only the `patchAffectedNodes` filter is new.

**Patch torque** distribution (when `a.type == TORQUE` AND `a.applicationMode == PATCH`):
treat the patch's nodes as a rigid body; resolve the torque $\tau$ into in-plane force pairs about the patch centre. For each node $n$ in the patch at offset $r_n$ from the anchor in the tangent plane: force component $F_n = (\tau \times r_n) / |r_n|^2 \cdot \text{tribArea}_n / \sum \text{tribArea}$. Net force zero; moment about the patch centre matches $\tau$.

## Multithreading
- `recomputePatchAffectedNodes`: per-vertex parallel-for over surface vertices.
- Solver-side load assembly: inherits existing OMP parallel-for in `FEASolver.cpp` (the existing code already handles tributary-area distribution multithreadedly).

## Numerical care points
- **Patch centred slightly above surface** — the tangent plane is at the anchor's *surface* point; if the patch's $\hat n$ direction is curved, the plane diverges from the true surface for points far from the centre. Solver code should accept the projection error gracefully — the existing tributary-area code already handles this since it uses **surface-mesh Voronoi areas**, not in-plane areas.
- **Empty patch** — if `patchAffectedNodes` is empty (the patch is smaller than a single triangle), fall back to a single-nearest-node POINT load and warn in console.
- **Patch overlapping multiple disjoint surface regions** — e.g. on a thin plate, a patch placed on one face might also overlap the opposite face's nodes if `2*h_local > plate_thickness`. Guard with `d_world · hat_n > 0` (only include nodes whose offset has a positive projection onto the patch normal).
- **Cache invalidation** — `patchAffectedNodes` is invalidated when (1) the anchor moves, (2) `patchHalfExtents` or `patchRotationDeg` change, (3) the mesh is regenerated. Forgetting (3) means the solver applies the load to stale node IDs.

## Cross-references
- **TODO_19 (AMR cycle):** after each refinement step, `loadAnchors[i].surfaceFaceId` and the cached `patchAffectedNodes` are invalidated. `AdaptiveSolver` MUST call `anchor.reproject(newSurfaceBVH)` and `anchor.invalidatePatchCache()` for every anchor between cycles. Add to TODO_19's "solution transfer" step.
- **TODO_17 (Boundary layer):** the BL extruder may insert new surface nodes above the original surface; patch anchors continue to anchor at the original surface position, which is correct.

## Screen-visible acceptance check

**Setup**
1. Run TODO_22 acceptance check (anchor #1 selected, with `Fz = -100`).
2. Have a cube mesh loaded.

**Action**
1. In the right-side panel, click `Patch` in the `Point | Patch` segmented toggle.
2. Observe the translucent quad appearing on the cube's top face.
3. Drag the +u edge arrow outward — observe the rectangle resize and the area readout update.
4. Drag the corner rotation ring → rotate the patch 30° in-plane.
5. Click `Solve Linear Static` (existing button).

**Expected visible**
- After step 1: a semi-transparent blue rectangle (alpha 0.3) appears tangent to the cube's top face at anchor #1, centred on the anchor, default size 5% of bbox diagonal per side. 4 edge arrows + 1 corner rotation handle drawn. Panel shows: `Patch hu: 0.05`, `Patch hv: 0.05`, `Patch area: 0.010 m²`, `Traction |F|/A: 10000 N/m²`.
- After step 3: rectangle grows along +u; `hu` field updates live; `Patch area` rises; traction drops.
- After step 4: rectangle visibly rotates around its centre; corner handle stays at the rotating corner.
- After step 5: standard solve runs. In the displacement viz, the deformation pattern is visibly *spread out* over the patch (not concentrated at a single point as a POINT load would be). Existing `appliedForces` arrow viz shows multiple small arrows across the patch nodes.

**Failure modes**
- Patch appears offset from the surface (e.g. floating 1 cm above) → tangent plane uses wrong normal, or the anchor's stored normal is stale.
- Patch resizing in only one direction → edge-arrow drag projects onto wrong axis.
- Solve crashes with "patch nodes empty" → patch smaller than nearest triangle; missing fallback to point load.
- Solve runs but displacement looks identical to point load → patch nodes computed but load not distributed (still applied at single node).

**Pass criterion**
Patch visible, resizable, rotatable; solve completes; displacement field shows distributed load characteristic (smoother, lower-magnitude peak than the point-load version).

## Regression sentinel
`regression/patch_load_cube.txt` — places a 0.1 m × 0.1 m patch on the cube top; asserts (a) `patchAffectedNodes.size() > 1`, (b) `sum(F_n) == anchor.vector` to $10^{-6}$ relative.

## Depends on
TODO_22 (anchor selection + gizmo infra), TODO_23 (Force/Torque toggle + torque math reused for patch torque).

## Estimated effort
3.5 days.
