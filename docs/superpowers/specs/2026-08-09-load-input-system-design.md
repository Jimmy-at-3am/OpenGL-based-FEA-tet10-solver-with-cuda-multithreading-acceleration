# Load & Boundary-Condition Input System — Design

**Date:** 2026-08-09
**Status:** Approved for implementation
**Scope:** Algorithmic foundation for entering forces, torques, pressure, distributed
forces, fixed supports, and gravity in the PolyFEA pre-processor.

---

## 1. Existing architecture (as inspected)

| Concern | Current state | Consequence for this work |
|---|---|---|
| Build | CMake + Ninja + MSVC, `build.bat build`. Eigen/GLM/miniz/Clipper2/CDT via FetchContent. CUDA + OCCT optional. | New sources join the existing `SOURCES` list; a new test target is added alongside `mesh_diag`. |
| UI | `SimpleUI` — immediate-mode, hit-tested per frame (`button`, `slider`, `vslider`). Single right-hand panel drawn in `main.cpp`. | Extend. Do **not** build a second UI system. |
| Render loop | One loop in `runInteractive()` (`src/main.cpp`). Overlays use dedicated `GL_LINES` VAO/VBOs (`buildForceArrowBuffers`, `buildSlicePreview`, toolpath preview). | Gizmos follow the same overlay pattern. |
| Camera | `Camera` (Z-up CAD orbit). Right-drag orbit, middle-drag pan, scroll zoom. Left-drag is **not** bound to the camera. | Left-click is available for picking, but is claimed through a priority chain, not reserved globally. |
| Object transform | **None.** Every draw call sets `model = glm::mat4(1.0f)`. Geometry is baked into model space by `processRawGeometry` (centred, scaled to a 3-unit bbox diagonal). No move/rotate path exists. | World space ≡ model space today. `LoadCoords` accepts an explicit model matrix (identity in the app, synthetic in tests). |
| Units | Render space = 3-unit normalised. Physical size retained in **mm** (`physicalMinMM`, `physicalMaxMM`, `modelToMM`). Solver rescales geometry to **metres** (`scaleGeometryToMeters`, `m_geomScale`) and works in SI. | One shared `metersPerModelUnit(model)` helper is the single conversion path for adapter *and* solver. |
| Mesh | FE nodes in `originalVolumetricPositions`; `tetrahedra` (4/elem), `tetrahedraQuadratic` (10/elem), `edgeToMidNode`, `hasQuadraticMesh`, `nLinearNodes`. Boundary faces derived by counting tet faces used once (see `buildConsistentLoadVector`). | Same boundary-extraction technique, cached with a BVH. |
| Loads | Hard-coded `FEASolver::LoadType` presets. The solver *scans* for `Z_min`/`X_max`/etc., builds `F` (`VectorXd`, DOF = `node*3+d`), `fixedNodes` (all 3 DOF) and `singleDofFixed` (node, dof), then applies penalty BCs. `appliedForces` is derived from `F` purely for arrow drawing. | **The genuine missing engine interface:** the solver cannot accept an externally-specified load set. This is the one seam we add. |
| Async | `ComputeJob` (worker thread + `atomic` progress/cancel); GL uploads deferred while running. | Solves keep running through `startComputeJob`; the `LoadSet` must outlive the worker. |
| Tests | `ScenarioRunner` (`--run`, `--regress all`) drives the real pipeline. No unit-test target. | Add a GL-free unit-test executable; leave existing scenarios and sentinels untouched. |

**Third-party note:** the README credits nlohmann/json, but it is not fetched and no
header exists in the tree. No JSON dependency is introduced.

---

## 2. Scope

**In scope:** load data model; picking and selection; placement state machine; coordinate
and unit conversion; solver adapter; validation gate; undo/redo; placeholder gizmos;
renderer-facing contract; automated tests.

**Out of scope:** persistence (no file format, no sidecar, no scenario-schema change —
structures are serialisable in principle and round-tripped in memory for tests only);
final colours, typography, icons, shaders, gizmo appearance; modification of the numerical
FEA engine beyond the one load-set seam; any live object move/rotate feature.

**Non-goals that were explicitly rejected:** a parallel UI/renderer/event system; a cached
mutable SI value alongside a display value; a retained raw `LoadSet` pointer on the solver;
an unused public rebake method on `LoadScene`; a live object transform invented to satisfy
`ObjectLocal` semantics.

---

## 3. Module layout

GL- and GLFW-free except where noted, so the whole core is unit-testable.

```
include/loads/LoadTypes.h      — enums, ids, shared value types
include/loads/LoadUnits.h      — unit registry + conversion to SI
include/loads/LoadCoords.h     — screen/world/object/solver transforms (single path)
include/loads/MeshSurface.h    — boundary-face extraction + BVH (cached per meshVersion)
include/loads/MeshPick.h       — ray cast, node/face/region/body selection
include/loads/LoadModel.h      — LoadDefinition, TargetSelection, in-memory serialise
include/loads/LoadResolve.h    — selection ⇄ mesh resolution and remesh re-resolution
include/loads/LoadPlacement.h  — placement state machine + drag→direction math
include/loads/LoadAdapter.h    — LoadDefinition[] → immutable LoadSet (SI)
include/loads/LoadValidation.h — structured validation gate
include/loads/LoadCommands.h   — undo/redo command stack
include/loads/LoadScene.h      — owns loads + selection + undo stack
include/loads/LoadVisual.h     — renderer-facing contract (plain structs)
include/loads/LoadRebake.h     — pure geometry-rebake transformation helper
src/loads/*.cpp                — implementations
src/loads/LoadGizmo.cpp        — placeholder GL_LINES gizmos (GL-touching)
tests/load_tests.cpp           — unit tests (new `load_tests` target, no GL)
```

Engine-side edits, additive only:

- `FEAModel`: `uint64_t meshVersion` bumped wherever volumetric mesh identity changes
  (`generateVolumetricMesh`, `generateMidEdgeNodes`, `meshSlabs`, `meshToolpathSlabs`,
  fracture element removal).
- `FEASolver`: `solveLinearStatic` / `solveNonlinearStatic` / `solveBrittleFracture` accept
  an optional `std::shared_ptr<const LoadSet>` **parameter**. When present, `F`,
  `fixedNodes` and `singleDofFixed` are filled from it and the preset scan is skipped.
  Assembly, penalty BCs, solve cascade and arrow recording are unchanged.

---

## 4. Data model

### 4.1 Authoritative magnitude

One value, SI, no second mutable copy:

```cpp
struct Magnitude {
    double      si    = 0.0;    // authoritative: N, N·m, Pa, m/s²
    UnitId      display;        // presentation tag only
};
```

`display` never participates in computation. UI reads `toDisplay(si, display)` and writes
`si = toSI(text, display)`. There is no cached display-unit number to fall out of sync.

### 4.2 Frame vs direction mode (separated)

```cpp
enum class Frame     { World, ObjectLocal };          // which basis the vector is in
enum class DirMode   { Free, AxisX, AxisY, AxisZ,     // how the user chose it
                       SurfaceNormal, Components };
```

`Frame` and `DirMode` are independent. Sign lives in the vector, not in the mode:
"−X" is `AxisX` with a negated vector, and *Reverse* is an operation (negate), not a mode.
`SurfaceNormal` resolves against the selection's average normal; inward/outward is the sign.

`ObjectLocal` is retained for forward compatibility and is exercised by unit tests with
synthetic matrices. It is **not exposed in the student UI** while no object transform
exists, because it would be an exact duplicate of `World`.

### 4.3 LoadDefinition

```cpp
struct LoadDefinition {
    LoadId            id;              // stable, monotonic
    std::string       name;            // user-visible
    LoadType          type;            // PointForce | Pressure | DistributedForce
                                       // | Torque | FixedSupport | Gravity
    TargetSelection   target;
    glm::dvec3        anchorModel;     // application point / torque centre (model space)
    glm::dvec3        dir;             // unit vector: force dir or torque axis
    Frame             frame;
    DirMode           dirMode;
    Magnitude         magnitude;
    bool              enabled  = true;
    bool              visible  = true;
    Distribution      distribution;    // area-weighted (default) | single-node (advanced)
    LoadParams        params;          // per-type: pressure sign, DOF locks, density ref
    ValidationStatus  status;          // cached result of the last validation pass
};
```

`LoadParams` is a plain tagged struct (not a variant) so serialisation stays trivial:
pressure sign, fixed-support per-DOF locks (`lockX/Y/Z`), gravity density source, and the
advanced single-node flag.

### 4.4 TargetSelection — remesh strategy

Selections are authoritative as **geometry**, never as volumetric indices. Two strategies,
in preference order, with the chosen one recorded on the selection so re-resolution is
consistent and validation can report honestly.

**Primary — original-surface reference.** `surfaceVertices`/`surfaceIndices` are the
invariant across a re-tet: TetGen is re-run on the *same* surface when quality/volume
settings change, so surface triangle index + barycentric coordinate survives remeshing by
construction, while volumetric boundary triangles and node indices do not. Used whenever
the volumetric mesh derives from the loaded surface.

**Fallback — anchor cloud.** A set of per-face anchors (centroid, unit normal, area) plus
adjacency, with independent distance and normal-angle tolerances. Required for the G-code
toolpath lane, where the volume is built from `ToolpathSections` and the "surface" is only
a bbox shell, and for slab meshes whose boundary is stair-stepped away from the input
surface. Multiple anchors — never one anchor plus a radius — so painted and disconnected
selections survive.

```cpp
enum class SelectionKind { Point, Faces, ConnectedRegion, WholeBody };
enum class AnchorStrategy { OriginalSurface, AnchorCloud };

struct TargetSelection {
    SelectionKind    kind;
    AnchorStrategy   strategy;
    // Primary
    std::vector<SurfaceRef> surfaceRefs;   // {triIndex, bary}
    // Fallback
    std::vector<FaceAnchor> anchors;       // {centroid, normal, area}
    std::vector<std::pair<uint32_t,uint32_t>> adjacency;
    double           distanceTol;          // absolute, model units
    double           normalAngleTolDeg;    // region regrow + match gate
    // Resolved cache (never authoritative)
    Resolved         cache;                // nodes, faces, area, centroid, normal
    uint64_t         resolvedMeshVersion = 0;
    bool             valid = false;
};
```

Re-resolution runs when `resolvedMeshVersion != model.meshVersion`, and applies
**ambiguity checks**: near-tie anchor matches, best match beyond tolerance, resolved area
drifting more than a threshold from the original, and a connected selection resolving to a
disconnected set. Any of these invalidates the selection with a repair suggestion rather
than silently binding to different geometry.

### 4.5 Serialisability

Every struct is POD-ish and version-tagged, with in-memory `serialize`/`deserialize` free
functions used only by round-trip tests. **No file format, no I/O, no UI.**

---

## 5. Units

One module, one direction of travel. Every value converts to SI exactly once at the input
boundary and is stored as SI thereafter.

| Quantity | Accepted display units | SI base |
|---|---|---|
| Force | N, kN, lbf | N |
| Torque | N·mm, N·m | N·m |
| Pressure | Pa, kPa, MPa | Pa |
| Length | mm, m | m |
| Density | kg/m³, g/cm³ | kg/m³ |
| Acceleration | m/s², g | m/s² |

Rules enforced in code and tests: pressure is never stored or assembled as a total force;
torque units are never inferred (`N·mm` vs `N·m` is explicit); NaN, infinity, empty and
unparseable input are rejected at parse time, before reaching the model; every conversion
is round-trip tested.

---

## 6. Coordinates

`LoadCoords` is a set of pure functions taking an **explicit** model matrix. The app passes
identity; tests pass synthetic matrices.

```
screen ──inverse(proj·view)──▶ world ray
world  ──inverse(M)──────────▶ object/model
model  ──×metersPerModelUnit▶ solver (SI metres)
```

`metersPerModelUnit(model) = model.modelToMM / 1000.0` is defined **once** and used by both
the adapter and the solver, so the two can never drift.

Direction semantics: a `World` direction is stored as a world vector and is invariant under
camera motion — camera rotation changes only the view matrix and can never alter committed
load data. An `ObjectLocal` direction is stored in object coordinates and converted through
`M` at use time.

**Drag→direction stability.** Screen drag is projected onto a plane through the anchor. The
plane normal is the camera forward, *unless* the intended direction is near-parallel to the
view axis, in which case the solver falls back to the best-conditioned of the two remaining
camera-basis planes. Direction is renormalised every update; zero-length results are
rejected and leave the previous direction in place.

---

## 7. Picking and selection

**Boundary extraction.** Faces used by exactly one tet, as `buildConsistentLoadVector`
already does. For a Tet10 mesh the boundary face is a 6-node T6; picking tessellates it as
a flat corner triangle (geometry is straight-sided, so this is exact), while load
assembly uses the quadratic shape functions (§8).

**Acceleration.** A median-split BVH over boundary triangles, cached and rebuilt only when
`meshVersion` changes. Reuses the BVH approach already proven in `MeshQuality.cpp`.

**Operations.** Click (node or face), shift-click add/remove, drag/brush paint, connected-
region flood fill bounded by `normalAngleTolDeg`, whole-body, clear. Hovered / active /
committed target states are distinct and exposed to the renderer.

Section view is respected: clipped-away geometry is not pickable, matching what the student
can actually see.

---

## 8. Load-type algorithms

Throughout, `wᵢ` denotes a **strictly positive** tributary weight and `Nᵢ` a shape function.

### 8.1 Tet10 consistent loads

For uniform pressure on a straight-sided T6 face of area `A`, Gauss integration of
`∫Nᵢ dA` gives **0 at each corner and A/3 at each midside** — not an equal six-way split.
Pressure and distributed-force assembly integrate the quadratic shape functions and load
midside nodes accordingly. Tet4 faces use the familiar `A/3` per corner.

The existing engine presets are **not** retrofitted — doing so would move regression
sentinels. Correct quadratic lumping lives in the new adapter, which is the only path
user-defined loads take.

### 8.2 Tributary weights (distinct from consistent weights)

Consistent weights answer "convert a distributed traction to nodal forces". Tributary
weights answer "how should a constrained optimisation weight each node", and must be
strictly positive. Using consistent T6 weights here would divide by zero at corners and
pin corner forces to zero.

Tributary split: connect the three midsides into four medial sub-triangles (each `A/4`),
distribute each sub-triangle's area equally to its three vertices. Result per T6 face:

- corner node: `A/12`
- midside node: `A/4`
- total: `3·(A/12) + 3·(A/4) = A` ✓, all strictly positive ✓

### 8.3 Point / localised force

Beginner default spreads the force over a small surface patch around the pick point
(area-weighted, total preserved **exactly**). True single-node loading is an advanced
option that raises a mesh-dependence warning: peak stress at a mathematical point load is
singular and mesh-refinement-dependent. The user-entered total is never silently altered.

### 8.4 Pressure

Acts on the selected region along its normal, sign selectable inward/outward. Stored as
pressure (Pa) — never as a total force. Consistent nodal loads from `p·∫Nᵢ dA` per face,
quadratic-aware. Reports the approximate resultant `p × A` through the visual interface as
information only.

### 8.5 Distributed force

A specified **total** resultant shared over the region, allocated by area weighting (not by
dividing by triangle or node count). The assembled nodal forces are asserted to reproduce
the requested total within tolerance.

### 8.6 Torque

Not a hand-made force pair. Requires a finite selected region and solves a weighted
minimum-norm problem:

```
minimise   ½ Σᵢ (1/wᵢ)|fᵢ|²
subject to Σᵢ fᵢ = 0            (3 rows — zero net force)
           Σᵢ rᵢ × fᵢ = T        (3 rows — exact requested moment)
```

With `Σf = 0` the moment is reference-point invariant, so the right-hand-rule sign is
unambiguous. Stationarity gives `f = W Cᵀλ` with `W = blockdiag(wᵢI₃)` and `Gλ = b`,
`G = C W Cᵀ` (6×6).

**Conditioning.** `C`'s force rows are dimensionless and its moment rows carry length, so
`G` is dimensionally inhomogeneous and its condition number would inflate as `L²` with part
size alone. The constraint *system* is rescaled — `C′ = SC`, `b′ = Sb` with
`S = diag(1,1,1,1/L,1/L,1/L)` and `L` the region's radius of gyration
`√(Σwᵢrᵢ²/Σwᵢ)` — giving the congruence `G′ = S G Sᵀ`, which stays symmetric PSD.
Because `S` is invertible the feasible set is unchanged, so this is a diagnostics-only
reformulation that cannot alter the physical answer. Dividing by `Σwᵢ` makes `G′`
dimensionless.

**Rejection, not projection.** An unattainable torque is refused, never silently projected:

1. Decompose `T` against `G′`'s SVD. If `‖T_unattainable‖ / ‖T‖` exceeds tolerance →
   error naming the deficient axis.
2. If attainable but ill-conditioned, nodal forces grow as `1/σ_min`. Reject on force
   amplification `‖f‖/‖T‖` beyond a physical threshold, reporting the amplification
   (a student cannot interpret a bare condition number).

Degenerate cases this catches: a single node (`f ≡ 0` under `Σf = 0`), collinear nodes (no
moment about their own line), and sliver regions.

### 8.7 Fixed support

One or more regions. Beginner behaviour constrains all translational DOFs — the only DOFs
solid tets have. No rotational DOFs are invented. Advanced per-axis locks (`lockX/Y/Z`) map
onto the solver's existing `singleDofFixed` (node, dof) mechanism.

### 8.8 Gravity

Whole-body, requiring density. Consistent nodal body force by integrating `ρ·g` over each
element. Defaults to Earth gravity in the project's SI base. Rejected or warned when
density is missing or of unknown units, direction is zero-length, or no valid body force
can be formed.

---

## 9. Solver adapter and engine seam

The adapter produces an **immutable** `LoadSet` in SI at node level:

```cpp
struct LoadSet {
    std::vector<std::pair<int, glm::dvec3>>  nodalForces;    // node → N
    std::vector<int>                         fixedNodes;     // all 3 DOF
    std::vector<std::pair<int,int>>          singleDofFixed; // (node, dof)
    // provenance for reporting
    double totalForceN = 0.0;
    glm::dvec3 totalMomentNm{0.0};
};
```

Areas and volumes are computed in metres via the shared `metersPerModelUnit`, so values are
already SI when the solver scales geometry.

**Lifetime.** The set is passed as a `std::shared_ptr<const LoadSet>` **parameter** to each
solve invocation and captured by the worker-job closure — no retained raw pointer, no
dangling risk across the async boundary.

---

## 10. Validation gate

Structured results, not raw solver codes:

```cpp
struct ValidationIssue {
    Severity     severity;   // Info | Warning | Error
    std::string  message;    // plain language, classroom-facing
    std::string  detail;     // technical, for logs
    LoadId       loadId;     // 0 = model-wide
    std::string  suggestion; // corrective action
};
```

Covers: no loads; loads but no supports; residual rigid-body motion; zero magnitude; zero
direction; invalid or deleted target; target invalidated by remeshing; gravity without
density; pressure without valid area; invalid units; non-finite values; torque without a
valid axis; unattainable or ill-conditioned torque; point-load singularity warning.

**Rigid-body check by rank, not by presence.** Build the 6-column rigid-mode matrix `R`
(3 translations, 3 rotations `u = ω × x`), restrict to constrained DOFs, normalise each
column to unit norm (rotation columns scale with length; translations do not), and require
`rank(R_constrained) == 6` at a relative singular-value threshold. Applied **per connected
component** — a fracture-separated or weak-tie toolpath island can float free while the
global rank is 6. Checking only "are some nodes fixed" would pass a model the penalty
solver then answers with garbage.

Example primary message: *"Your part can still move as one whole object. Hold at least one
surface before simulating."*

---

## 11. Placement state machine

Pure logic driven by events, no GLFW types, fully unit-testable.

```
Idle → SelectingLoadType → SelectingTarget → SettingDirection
     → SettingMagnitude → Previewing → Committed
plus: EditingExistingLoad, Cancelled, Invalid
```

- `Esc` cancels an incomplete placement; committed loads are never touched.
- Right-click cancels where consistent with existing controls (it also orbits, so cancel
  binds to right-click *release without drag*).
- Delete/Backspace removes the selected committed load.
- Preview state is separate from committed solver data until confirmation.
- Undo/redo covers creation, deletion, target, direction and magnitude changes.

**Input priority chain:** `UI → gizmo handle → placement picking → camera`. Each layer may
consume the event; anything unconsumed falls through. Left-click is not globally reserved,
and camera orbit/pan/zoom behaviour is unchanged. Dragging a gizmo consumes the drag, so it
cannot also rotate the camera.

---

## 12. Undo/redo

Command stack with `apply`/`revert` per command type (create, delete, retarget, redirect,
re-magnitude, enable/disable). Commands store before/after value snapshots, not references
into the load list, so replay is order-safe. Redo is cleared on a new command.

---

## 13. Renderer-facing contract

`LoadVisual.h` exposes plain structs, no colours, no shaders, no geometry detail:

- force arrow: origin, direction, magnitude, state
- torque: centre, axis, sign, magnitude, state
- pressure: region faces, normals, magnitude, resultant
- distributed force: region, direction, total
- fixed support: region
- gravity: direction, enabled
- per-item `VisualState { Hover, Selected, Preview, Committed, Disabled, Invalid }`
- suggested screen-space size / normalised visual scale
- label text and formatted unit value
- `HitId` per draggable gizmo component

Command interface for the visual agent: begin/update/end/cancel drag, change magnitude,
change direction mode, reverse direction, change target, commit, delete, undo, redo.

Placeholder gizmos in `LoadGizmo.cpp` draw with the existing `GL_LINES` overlay pattern so
the feature is usable immediately and the frontend agent replaces appearance only.

---

## 14. Geometry rebake (documented contract, no live feature)

Rebaking vertices does **not** move anchors — anchors are stored coordinates, so a rotated
mesh would re-resolve them onto the wrong surface silently. Any future rebake path must:

1. transform anchors by `M`
2. transform `ObjectLocal` directions by `M`
3. leave `World` directions unchanged
4. bump `meshVersion`
5. re-resolve all selections

Steps 1–3 are pure and ship as a tested free function in `LoadRebake.h` taking
`(loads, M) → loads`. Steps 4–5 mutate model state and remain documented requirements on
the future call site rather than being hidden inside a "pure" helper. Nothing calls this
today.

---

## 15. Tests

**Unit** (`load_tests`, no GL): screen→mesh picking; face and region selection; coordinate
conversions with synthetic model matrices; direction normalisation and reversal; force
preservation under camera transformation; torque axis and right-hand-rule sign; torque
constraint scaling, rank and rejection of unattainable/ill-conditioned requests; N·mm ⇄ N·m
and all unit round-trips; pressure area→resultant; Tet4 and **Tet10** consistent load
lumping; tributary-weight positivity and sum; distributed-force total preservation;
fixed-support translation constraints; rigid-body rank test including per-component
floating islands; gravity/density validation; in-memory serialisation round-trip;
undo/redo; invalid-target detection; remesh re-resolution under both strategies including
ambiguity rejection; solver-adapter output; geometry-rebake pure transform.

**Manual:** cantilever (hold one end, push the other); uniform pressure on a face; shaft/tab
under torque; distributed surface force; gravity with and without density; rotated camera;
remeshed model. The rotated-*object* application test is omitted — the feature does not
exist; object-transform correctness is covered by synthetic-matrix unit tests.

**Regression:** existing `scenarios/*.json` and `regression/*.txt` must remain green and
unmodified.

---

## 16. Known limitations (to be confirmed at implementation)

- `ObjectLocal` is inert in the app until an object transform exists.
- Torque on a single node or a collinear region is rejected by design, not approximated.
- Original-surface anchoring is unavailable for G-code toolpath meshes; those fall back to
  the anchor cloud, which is less robust under aggressive re-slicing.
- Point-load singularity is warned about, not solved — the underlying mesh dependence is
  physical, not a bug.
