# Physics-Safe Load Foundation Implementation Plan

> **Execution rule:** Implement this plan in order, preserving the repository's existing staged and unstaged changes. Use focused tests before the expensive regression suite.

**Goal:** Add a small, testable physics contract that prevents friendly UI language from silently changing load semantics, computes auditable pressure resultants, and exposes those semantics in the existing preset panel without changing current solver behavior.

**Architecture:** Keep physical meaning in a renderer-independent C++ module. The UI translates an existing preset into a `FeatureDefinition`, asks the capability registry how honestly the solver represents it, and displays a compact Physics Receipt. Pressure integration is a pure function over oriented planar surface facets, so it can be unit-tested now and reused when editable face selections are added later.

**Technology:** C++17, CMake/CTest, standard library only for the new module, existing SimpleUI/OpenGL application for the receipt.

**Execution status:** Implemented and automatically verified on 2026-08-30. Live
OpenGL screenshot capture remained unavailable with Windows helper error
`0x80004002`; executable character-width checks cover the receipt-fit risk.

**Design source:** [`docs/design/physics-safe-load-setup.md`](../../design/physics-safe-load-setup.md)

## Scope and safety constraints

- Preserve every existing analysis button and solver vector; fail closed only
  where a mode is proven to dispatch to a different physical problem.
- Do not add contact, follower loads, generalized constraints, manufacturing strain, or editable surface selection in this slice.
- Do not label a feature as exact when only its name is familiar; capability is based on the solver formulation.
- Treat input magnitude as total force unless a feature explicitly declares another scope.
- Keep the pressure audit independent from viewport scale and camera orientation.
- Reject invalid or empty pressure-facet lists at the API boundary.
- Preserve legacy tension launch behavior, but disclose that its current 3+1+1
  support has only five scalar constraints and leaves at least one rigid mode.
- Do not commit overlapping edits in `CMakeLists.txt` or `src/main.cpp`; both contained user changes before this work. New isolated files may be committed separately.

## Task 1: Establish the failing physics-contract test

**Files:**

- Create: `tests/load_physics_tests.cpp`
- Modify: `CMakeLists.txt`

### Step 1.1: Add a dependency-free test harness

Create a small executable test with helpers for approximate scalar/vector comparisons and exception checks. Cover seven behaviors:

1. Uniform pressure on two coplanar facets produces `p * totalArea` in the inward normal direction.
2. Equal pressure on a closed six-face surface has zero net force but nonzero scalar normal load.
3. A triangulated multi-normal surface has the expected vector force and moment
   about a shifted reference point.
4. A pressure facet has the expected moment and reverses with typed direction.
5. Negative pressure, empty selections, degenerate data, non-finite data, and
   invalid enum values fail closed.
6. The capability table distinguishes exact, idealized, and unsupported semantics.
7. Every legacy preset exposes its real magnitude scope, distribution, support,
   mode coverage, and receipt-width budget.

The tests should call the public API directly, for example:

```cpp
const std::vector<load_physics::SurfaceFacet> facets = {
    {{1.0, 0.0, 0.0}, {0.0, -0.5, 0.0}},
    {{1.0, 0.0, 0.0}, {0.0,  0.5, 0.0}},
};

const auto result = load_physics::uniformPressureResultant(
    100.0,
    facets,
    load_physics::PressureDirection::Inward,
    {0.0, 0.0, 0.0});

expectNear(result.scalarNormalLoadN, 200.0);
expectVecNear(result.forceN, {-200.0, 0.0, 0.0});
expectVecNear(result.momentNm, {0.0, 0.0, 0.0});
```

### Step 1.2: Register only the focused test target

Add `enable_testing()`, an executable named `load_physics_tests`, its include directory, and an `add_test` entry. During the red phase, point it at the test source only so the absent production contract is the reason compilation fails.

### Step 1.3: Verify the red phase

Run:

```powershell
cmake --build build --target load_physics_tests
```

Expected: compilation fails because `LoadPhysics.h` does not yet exist. Confirm that the failure is caused by the missing requested feature, not by malformed test code or unrelated configuration.

## Task 2: Implement the renderer-independent load semantics

**Files:**

- Create: `include/LoadPhysics.h`
- Create: `src/LoadPhysics.cpp`
- Modify: `CMakeLists.txt`

### Step 2.1: Define the public vocabulary

Declare these types in namespace `load_physics`:

```cpp
enum class AnalysisMode {
    LinearStatic,
    NonlinearStatic,
    BrittleFracture,
};

enum class FeatureKind {
    PointForce,
    PatchResultant,
    BoundaryTraction,
    Pressure,
    BearingLoad,
    RemoteResultant,
    BodyAcceleration,
    FixedConstraint,
    PrescribedDisplacement,
    NormalSupport,
    CylindricalSupport,
    ElasticSupport,
    Contact,
};

enum class MagnitudeScope { TotalAcrossSelection, PerRegion };
enum class DistributionKind {
    Unspecified,
    ConcentratedNode,
    ConsistentArea,
    LinearFacetTributary,
    EqualNode,
};
enum class FrameKind { Global, BuildMaterial, ReferenceGeometry, CurrentBoundary };
enum class EvolutionKind { Dead, Follower };
enum class Capability { Exact, Approximate, Unsupported };

struct FeatureDefinition {
    FeatureKind kind;
    MagnitudeScope scope = MagnitudeScope::TotalAcrossSelection;
    DistributionKind distribution = DistributionKind::Unspecified;
    FrameKind frame = FrameKind::Global;
    EvolutionKind evolution = EvolutionKind::Dead;
};

struct CapabilityResult {
    Capability status;
    const char* reason;
    bool canRun() const noexcept { return status != Capability::Unsupported; }
};
```

Also declare `capabilityName`, `featureName`, and `assess` so the UI never owns a second, drifting interpretation table.

Add a separate `PresetKind`/`PresetDescription` adapter for the six existing UI
presets. This is required because abstract feature compatibility alone cannot
detect a preset-specific dispatch error. The audit found that nonlinear
`CantileverBendingZ` falls through to the Y-compression branch, so
`assessPreset(CantileverBendingZ, NonlinearStatic)` must be unsupported.

`PresetDescription` also owns `PresetSupportKind` and a compact support summary.
Full-face Cartesian clamps are disclosed directly. Tension X/Y/Z use the legacy
3+1+1 five-DOF gauge; because changing solver constraints is outside this slice,
all three axes remain runnable but `Approximate` in every mode, with the remaining
rigid-mode count stated in the receipt only as a lower bound (`>=1`).

### Step 2.2: Define the pressure audit data

Use small SI-unit structures:

```cpp
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SurfaceFacet {
    Vec3 outwardAreaVectorM2;
    Vec3 centroidM;
};

struct Resultant {
    double scalarNormalLoadN = 0.0;
    Vec3 forceN;
    Vec3 momentNm;
};

enum class PressureDirection { Inward, Outward };

Resultant uniformPressureResultant(
    double pressurePa,
    const std::vector<SurfaceFacet>& facets,
    PressureDirection direction,
    Vec3 referencePointM = {});
```

`SurfaceFacet` is one planar, constant-normal facet. A curved boundary must be
passed as its constituent facets; aggregating it to one area vector and centroid
would lose scalar-area and moment information. `outwardAreaVectorM2` is
intentionally an oriented vector, not a scalar area, so the facet sum preserves
force cancellation and moment behavior on triangulated curved surfaces.
`PressureDirection::{Inward, Outward}` is typed so a positional Boolean cannot
silently reverse every patch force.

### Step 2.3: Implement the capability rules

Implement one explicit decision table:

- Follower evolution: unsupported.
- Current-boundary frame: unsupported.
- Bearing, remote resultant, body acceleration, prescribed displacement, normal support, cylindrical support, elastic support, and contact: unsupported until the solver owns their equations.
- Point force: approximate because peak stress is singular and mesh-sensitive.
- Pressure: auditable but unsupported for solving until a boundary adapter assembles it.
- A patch resultant with unspecified distribution: unsupported.
- A consistent-area patch resultant: exact at the input-definition level.
- A linear-facet tributary resultant: approximate on a Tet10 face because the
  legacy helper loads corner nodes but not quadratic midside nodes.
- An equal-node patch resultant: approximate because remeshing changes its spatial distribution.
- Otherwise-supported features in brittle fracture: approximate because element-deletion fracture is mesh-sensitive.
- Fixed Cartesian constraints: exact at the feature-definition level when their declared frame is supported.
- A preset using the current five-DOF tension gauge: approximate in every mode;
  preserve launch behavior but disclose at least one remaining rigid mode.
- Unknown enum/preset values: unsupported; unknown pressure direction: reject
  with `std::invalid_argument` instead of choosing a default direction.

The word `Exact` here means the requested feature semantics are represented. It does not certify mesh convergence, material calibration, or global model validity.

### Step 2.4: Implement pressure integration and validation

For each planar facet with outward oriented area vector `A_i` and centroid `x_i`, accumulate:

```text
normal-load scalar += pressure * norm(A_i)
force              += directionSign * pressure * A_i
moment             += (x_i - referencePoint) cross patchForce
```

Use `directionSign = -1` for `PressureDirection::Inward` and `+1` for
`PressureDirection::Outward`. Throw `std::invalid_argument` for non-finite
values, negative pressure, an empty facet list, or a zero/degenerate facet area
vector. Do not pass one aggregate record for a curved patch.

### Step 2.5: Link the production implementation

Add `src/LoadPhysics.cpp` to both the application source list and `load_physics_tests`.

### Step 2.6: Verify the green phase

Run:

```powershell
cmake --build build --target load_physics_tests
.\build\load_physics_tests.exe
ctest --test-dir build -R load_physics_tests --output-on-failure
```

Expected: the executable reports all focused checks passed, and CTest reports one passing test.

## Task 3: Add the Physics Receipt to the existing preset panel

**Files:**

- Modify: `src/main.cpp`

### Step 3.1: Translate and audit the existing presets without changing their solver vectors

Include `LoadPhysics.h`. Map the six `FEASolver::LoadType` entries to the six
`PresetKind` entries in one paired option table with the visible label, so three
parallel arrays cannot drift independently. The adapter resolves:

- cantilever and point Z as total, dead, global, concentrated-node forces;
- surface compression Y as one total face resultant with linear corner-triangle
  tributary distribution (not quadratic Tet10 consistent loading);
- tension X/Y/Z as `+/- F` on two faces, with entered magnitude per face and equal-node distribution.

The adapter also states the automatic support: X-, Y-, or Z-min Cartesian clamp
for the first three presets, and the legacy five-DOF gauge for tension. The latter
is a warning/approximation, not a new solver restraint.

The current surface helper falls back to equal-node loading when its bbox slab
does not resolve complete boundary triangles. Because the pure preset gate has
no resolved mesh patch, it must report this preset as approximate and disclose
both possibilities. The normal path reconstructs only three-corner faces and
loads their corner nodes, so it is `LinearFacetTributary`; Tet10 midside nodes do
not receive the consistent constant-traction weights.

The adapter, not the UI, owns the capability verdict. In particular, it blocks
nonlinear cantilever because that solver branch currently changes the physical
setup to Y compression. It marks tension approximate because 3+1+1 constraints
leave at least one rigid mode, while preserving the existing runnable workflow.

The underlying feature declarations use:

```cpp
load_physics::FeatureDefinition{
    kind,
    scope,
    distribution,
    load_physics::FrameKind::Global,
    load_physics::EvolutionKind::Dead,
};
```

This records the current behavior; it does not alter the generated nodal force vector.

### Step 3.2: Render a compact, truthful receipt

Use short slider labels that remain visible beside `1000.000` (`TIP F`,
`POINT F`, `FACE F`, or `F / FACE`). Declare total versus per-face scope on the
`LOAD` line, then place a compact four-line receipt below it, plus a fifth line
only when a mode is blocked:

```text
LOAD: TOTAL F|+/-F/FACE / DEAD / GLOBAL
DIST: POINT SINGULAR|BBOX: CORNER-TRI OR EQUAL-NODE
SUPPORT: AXIS-MIN CLAMP|5-DOF; >=1 RIGID MODE FREE
L EXACT|APPROX | NL EXACT|APPROX|BLOCKED | FR MESH-DEP
BLOCK: SHORT REASON
```

Keep each receipt line short enough for the existing 275 px column; do not rely
on clipping to hide the qualifier. `Exact` means the input contract is
represented; it does not claim stress convergence.

### Step 3.3: Keep unsupported features fail-closed

Do not expose controls for new features whose capability is unsupported. For an
existing analysis button, keep its layout position but pass `!canRun()` to the
existing disabled-state parameter. A blocked button cannot create a solver
object. The receipt must show the short reason next to that disabled mode.

### Step 3.4: Build the application

Run:

```powershell
.\build.bat build
```

Expected: the existing application target compiles and stages successfully with no new warnings or linker errors attributable to the physics contract.

## Task 4: Verify no current analysis behavior changed

**Files:**

- Test only; no source changes expected.

### Step 4.1: Re-run the focused contract tests

Run:

```powershell
ctest --test-dir build -R load_physics_tests --output-on-failure
```

### Step 4.2: Run the existing regression suite

From the build directory, run:

```powershell
.\FEAPreProcessor.exe --regress all
```

Compare the pass/fail summary with the pre-change baseline. A pre-existing numerical fallback or fracture-severing message is acceptable only when its case still exits successfully and matches the baseline behavior.

### Step 4.3: Inspect the patch for accidental scope expansion

Run:

```powershell
git diff --check
git diff -- include/LoadPhysics.h src/LoadPhysics.cpp tests/load_physics_tests.cpp CMakeLists.txt src/main.cpp
git status --short
```

Confirm that:

- no solver assembly or existing preset force generation changed;
- no renderer code entered the new physics module;
- no existing user change was reverted;
- the UI claims match the registry; and
- unsupported features remain absent from the controls.

## Task 5: Handoff and deferred chain

**Files:**

- Update documentation only if verification reveals a mismatch.

### Step 5.1: Report the implemented boundary

State clearly that this slice adds a semantics/audit foundation and a visible receipt, not the full editable load/support workflow.

### Step 5.2: Preserve the next implementation order

Continue only after this foundation is accepted, in this sequence:

1. Complete the tension gauge with an independent sixth scalar constraint,
   runtime `rank(CR)`, and a canonical rank/null-mode test; only then label it
   3-2-1.
2. Persistent face/edge/vertex selection identities and remap diagnostics.
3. Editable surface patch selection with area and resultant preview.
4. Reference-configuration traction and pressure assembly with analytic tests,
   including actual Tet4/Tet10 nodal-vector checks and quadratic midside weights.
5. Generalized Dirichlet constraints for prescribed, normal, and cylindrical supports.
6. Body acceleration and centrifugal loading with mass/resultant audit.
7. Remote force/moment distribution with exact resultant preservation.
8. Bearing-load distribution with axis and angular-profile controls.
9. Large-deformation follower loads and tangent consistency.
10. Contact with explicit formulation, stabilization, and convergence diagnostics.
11. Manufacturing-aware material frames, process fields, and fracture validation.

Every later stage must add its solver formulation and verification before its friendly UI control becomes runnable.
