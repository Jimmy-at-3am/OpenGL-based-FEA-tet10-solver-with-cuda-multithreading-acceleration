# Physics-Safe Load Foundation Implementation Plan

> **Execution rule:** Implement this plan in order, preserving the repository's existing staged and unstaged changes. Use focused tests before the expensive regression suite.

**Goal:** Add a small, testable physics contract that prevents friendly UI language from silently changing load semantics, computes auditable pressure resultants, and exposes those semantics in the existing preset panel without changing current solver behavior.

**Architecture:** Keep physical meaning in a renderer-independent C++ module. The UI translates an existing preset into a `FeatureDefinition`, asks the capability registry how honestly the solver represents it, and displays a compact Physics Receipt. Pressure integration is a pure function over oriented surface patches, so it can be unit-tested now and reused when editable face selections are added later.

**Technology:** C++17, CMake/CTest, standard library only for the new module, existing SimpleUI/OpenGL application for the receipt.

**Design source:** [`docs/design/physics-safe-load-setup.md`](../../design/physics-safe-load-setup.md)

## Scope and safety constraints

- Preserve every existing analysis button and preset result.
- Do not add contact, follower loads, generalized constraints, manufacturing strain, or editable surface selection in this slice.
- Do not label a feature as exact when only its name is familiar; capability is based on the solver formulation.
- Treat input magnitude as total force unless a feature explicitly declares another scope.
- Keep the pressure audit independent from viewport scale and camera orientation.
- Reject invalid or empty pressure patches at the API boundary.
- Do not commit overlapping edits in `CMakeLists.txt` or `src/main.cpp`; both contained user changes before this work. New isolated files may be committed separately.

## Task 1: Establish the failing physics-contract test

**Files:**

- Create: `tests/load_physics_tests.cpp`
- Modify: `CMakeLists.txt`

### Step 1.1: Add a dependency-free test harness

Create a small executable test with helpers for approximate scalar/vector comparisons and exception checks. Cover five behaviors:

1. Uniform pressure on two coplanar patches produces `p * totalArea` in the inward normal direction.
2. Equal pressure on a closed six-face surface has zero net force but nonzero scalar normal load.
3. A symmetric pressure patch has zero moment about the stated reference point.
4. Negative pressure, empty selections, and degenerate area vectors are rejected.
5. The capability table distinguishes exact, idealized, and unsupported semantics.

The tests should call the public API directly, for example:

```cpp
const std::vector<load_physics::SurfacePatch> patches = {
    {{1.0, 0.0, 0.0}, {0.0, -0.5, 0.0}},
    {{1.0, 0.0, 0.0}, {0.0,  0.5, 0.0}},
};

const auto result = load_physics::uniformPressureResultant(
    100.0, patches, true, {0.0, 0.0, 0.0});

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
enum class FrameKind { Global, BuildMaterial, ReferenceGeometry, CurrentBoundary };
enum class EvolutionKind { Dead, Follower };
enum class Capability { Exact, Approximate, Unsupported };

struct FeatureDefinition {
    FeatureKind kind;
    MagnitudeScope scope = MagnitudeScope::TotalAcrossSelection;
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

### Step 2.2: Define the pressure audit data

Use small SI-unit structures:

```cpp
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SurfacePatch {
    Vec3 outwardAreaVectorM2;
    Vec3 centroidM;
};

struct Resultant {
    double scalarNormalLoadN = 0.0;
    Vec3 forceN;
    Vec3 momentNm;
};

Resultant uniformPressureResultant(
    double pressurePa,
    const std::vector<SurfacePatch>& patches,
    bool inward,
    Vec3 referencePointM = {});
```

`outwardAreaVectorM2` is intentionally an oriented vector, not a scalar area. That preserves the cancellation and moment behavior of curved surfaces.

### Step 2.3: Implement the capability rules

Implement one explicit decision table:

- Follower evolution: unsupported.
- Current-boundary frame: unsupported.
- Bearing, remote resultant, prescribed displacement, normal support, cylindrical support, elastic support, and contact: unsupported until the solver owns their equations.
- Point force: approximate because peak stress is singular and mesh-sensitive.
- Pressure in nonlinear or brittle-fracture analysis: unsupported because the current solver does not update the loaded surface and direction.
- Otherwise-supported features in brittle fracture: approximate because element-deletion fracture is mesh-sensitive.
- Dead, reference-configuration pressure in linear static analysis: exact at the load-definition level.
- Patch resultant, boundary traction, body acceleration, and fixed Cartesian constraints: exact at the feature-definition level when their declared frame and evolution are supported.

The word `Exact` here means the requested feature semantics are represented. It does not certify mesh convergence, material calibration, or global model validity.

### Step 2.4: Implement pressure integration and validation

For each patch with outward oriented area vector `A_i` and centroid `x_i`, accumulate:

```text
normal-load scalar += pressure * norm(A_i)
force              += directionSign * pressure * A_i
moment             += (x_i - referencePoint) cross patchForce
```

Use `directionSign = -1` for inward pressure and `+1` for outward pressure. Throw `std::invalid_argument` for non-finite values, negative pressure, an empty patch list, or a zero/degenerate patch area vector.

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

### Step 3.1: Translate existing presets without changing them

Include `LoadPhysics.h`. Map the two point-like presets to `FeatureKind::PointForce`; map the surface tension/compression presets to `FeatureKind::PatchResultant`. Declare every current preset as:

```cpp
load_physics::FeatureDefinition{
    kind,
    load_physics::MagnitudeScope::TotalAcrossSelection,
    load_physics::FrameKind::Global,
    load_physics::EvolutionKind::Dead,
};
```

This records the current behavior; it does not alter the generated nodal force vector.

### Step 3.2: Render a compact, truthful receipt

Place two lines directly below the existing load slider:

```text
INPUT: TOTAL / DEAD / GLOBAL   CONTRACT: EXACT|APPROX
FRACTURE: APPROX - MESH SENSITIVE
```

For a point-like preset, use `APPROX` and add `POINT-STRESS SINGULAR` in the second line. For a distributed preset, state that the region is preset rather than user-editable. Keep the receipt compact enough to fit the existing panel.

### Step 3.3: Keep unsupported features fail-closed

Do not expose controls for any feature whose capability is unsupported. The reusable `canRun()` result is the gate for later editable-feature work; this slice does not create inactive controls that imply unavailable solver support.

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

1. Persistent face/edge/vertex selection identities and remap diagnostics.
2. Editable surface patch selection with area and resultant preview.
3. Reference-configuration traction and pressure assembly with analytic tests.
4. Generalized Dirichlet constraints for prescribed, normal, and cylindrical supports.
5. Body acceleration and centrifugal loading with mass/resultant audit.
6. Remote force/moment distribution with exact resultant preservation.
7. Bearing-load distribution with axis and angular-profile controls.
8. Large-deformation follower loads and tangent consistency.
9. Contact with explicit formulation, stabilization, and convergence diagnostics.
10. Manufacturing-aware material frames, process fields, and fracture validation.

Every later stage must add its solver formulation and verification before its friendly UI control becomes runnable.
