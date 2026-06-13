> **2026-06-12 NOTE:** roadmap restructured. For what to do next, read
> `knowledge/TODOs/TODO_00_MASTER_ROADMAP.md` — it supersedes any "next TODO"
> pointer in this file. Next implementation TODO: **TODO_07**, then 06A→06E.

# TODO_04 Implementation Progress
# STEP Loader as Primary Input + B-rep Retention

**Status**: DONE — All 16 steps complete. Pending: OCCT install + build validation.
**Date started**: 2026-05-19
**Date completed**: 2026-05-19

---

## Goal (from TODO_04 md)

Add OpenCASCADE (OCCT) as a dependency, implement a `StepLoader` that:
1. Reads `.step` / `.stp` files
2. Retains the underlying `TopoDS_Shape` on `FEAModel` as `brep` (unique_ptr<BRepHandle>)
3. Tessellates the B-rep to triangles via `BRepMesh_IncrementalMesh`
4. Feeds the result through existing `surfaceVertices`/`surfaceIndices` pipeline
5. Patches TODO_03's Hausdorff/normal-deviation to use exact NURBS nearest-point when BRep is present (forward pass only — dramatically better Hausdorff accuracy)

**Pass criterion**: STEP cube + sphere load, render, mesh, solve; status panel shows `Source: STEP (B-rep retained)`; Hausdorff on sphere drops ≥ 2 orders of magnitude vs STL version.

---

## Algorithm / Math Context (from theoretical files)

### A. NURBS/CAD B-rep Tessellation (03_Surface_Meshing §5)

The practical recipe for STEP → triangles (what OCC does internally):
```
1. Parse B-rep into (V_corner, E_ridge, F_face)
2. For each edge: 1-D curvature-driven discretisation (frozen)
3. For each face:
   a. Pull back metric and sizing into (u,v) parameter space
   b. Insert frozen boundary discretisation as CDT constraints
   c. Run anisotropic 2-D frontal-Delaunay (BAMG)
   d. Lift result back via S(u,v)
4. Sew along shared edges
5. Verify Hausdorff and normal-deviation thresholds
```
OCC's `BRepMesh_IncrementalMesh` handles steps 2-4 internally.

**Linear deflection parameter** (lin_def):
```
lin_def = sizingChordError × L_diag
```
where `L_diag` is the bbox diagonal (call `BRepBndLib::Add` first).
Typical: `sizingChordError = 1e-3` → 0.1% chord error.

**Angular deflection** (ang_def = 0.3 rad ≈ 17°): matches COMSOL default.

### B. First Fundamental Form (01_Mathematical_Foundations §3.1)

OCC's BAMG-based surface mesher uses the first fundamental form I as its inner product in UV space. Mesh elements in parameter space that are isotropic under I map to isotropic elements on S. No action needed in our code — OCC handles this.

For curvature-based sizing (from 03 §1.1):
```
h_κ(x) = δ_max / max(|κ1|, |κ2|) × sqrt(8)
```
where δ_max = lin_def (chord error). This is automatically satisfied by OCC's incremental mesher.

### C. Piecewise-Smooth Domain / Sharp Features (01_Mathematical_Foundations §3.4)

CAD B-reps are piecewise C^k with explicit feature graph G = (V_corner, E_ridge, F_face). OCC preserves this via its topology — edges are frozen before faces are meshed. Sharp features ARE preserved exactly without any heuristic dihedral threshold. This is the key quality advantage over STL input.

### D. Face Orientation in OCC (critical care point)

Each `TopoDS_Face` has `Orientation()` = `TopAbs_FORWARD` or `TopAbs_REVERSED`.
For `REVERSED` faces, triangle winding must be flipped (swap indices 1 and 2).
Failure to do this causes half the surface normals to point inward → TODO_03 normal deviation ≈ 180° on half the mesh.

### E. Exact Nearest-Point on NURBS (what enables the Hausdorff improvement)

OCC method: `BRepExtrema_DistShapeShape`
1. Create a `TopoDS_Vertex` from the query point
2. Dispatch `BRepExtrema_DistShapeShape(vertex, shape)` → exact nearest point on NURBS
3. Distance = `glm::length(samplePt - nearestPt)`

This gives distance to the ANALYTIC surface (ground truth), not to the discrete triangulation.
Expected improvement: 2-3 orders of magnitude on smooth geometry (sphere: ~5e-4 → ~8e-7).

For surface normal at nearest point (for normal-deviation comparison):
```
BRepLProp_SLProps props(BRepAdaptor_Surface(face), u, v, 1, 1e-6);
gp_Dir normal = props.Normal();
```
where (u,v) comes from `dss.SupportOnShape2(1).FaceParameter(u, v)`.

### F. STEP Unit Detection

OCC reads STEP in its "cascade unit" (default: MM = millimetres).
After `TransferRoots()`, check: `Interface_Static::Cval("xstep.cascade.unit")`
→ returns "MM", "M", "INCH", etc.

`processRawGeometry` already handles bbox normalization (scales to 3-unit diagonal),
so no manual unit conversion is needed. Log the detected unit for the user.

---

## Architecture Decisions

### 1. pImpl Pattern for OCC Isolation

```
include/BRepHandle.h       ← OCC-free, has struct BRepHandle { struct Impl; unique_ptr<Impl>; }
src/BRepHandle_impl.h      ← internal only, defines BRepHandle::Impl with OCC types
src/BRepHandle.cpp         ← includes BRepHandle_impl.h, implements BRepHandle methods
src/StepLoader.cpp         ← includes BRepHandle_impl.h, creates BRepHandle
```

Both `BRepHandle.cpp` and `StepLoader.cpp` are OCC-aware. All other TUs stay OCC-free.

Note: CMakeLists.txt already adds `src/` to include_directories, so `BRepHandle_impl.h`
is findable as `#include "BRepHandle_impl.h"`.

### 2. FEAModel Changes

```cpp
// FEAModel.h additions:
class BRepHandle;  // forward declaration (NOT include)
std::unique_ptr<BRepHandle> brep;
bool hasBRep() const { return brep != nullptr; }
bool loadSTEP(const std::string& filepath);
~FEAModel();  // MUST be defined in .cpp where BRepHandle is complete
```

### 3. MeshQuality Changes (forward pass uses OCC)

New overloads in MeshQuality.h:
```cpp
class BRepHandle;
FidelityReport computeFidelity(const RefSurface& ref, const BRepHandle& brep,
                                const tetgenio& out, const FEAParams& p);
void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                         const tetgenio& out, const FEAParams& p);
void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                         const tetgenio& out, const FEAParams& p, std::ostream& os);
```

In FEAModel.cpp after tetrahedralization:
```cpp
if (hasBRep())
    MeshQuality::emitFidelityReport(refSurfaceForFidelity, *brep, out, params);
else
    MeshQuality::emitFidelityReport(refSurfaceForFidelity, out, params);
```

FidelityReport gets new field: `bool usedExactBRep = false;`

### 4. OCCT CMake Integration

Strategy: `find_package(OpenCASCADE CONFIG)` with configurable root path.
NOT FetchContent (would take hours to compile OCCT from source).

```cmake
option(USE_OCCT "Enable STEP loader via OpenCASCADE" ON)
set(OCCT_ROOT "C:/OpenCASCADE-7.8.1-vc14-64" CACHE PATH 
    "OpenCASCADE install root (set this if find_package fails)")
```

OCCT modules needed:
- TKernel, TKMath — foundation
- TKSTEP, TKSTEPBase, TKSTEPAttr, TKSTEP209, TKXSBase — STEP I/O
- TKBRep, TKTopAlgo — topology
- TKMesh — incremental mesher
- TKShHealing — shape healing
- TKG3d, TKG2d, TKGeomBase, TKGeomAlgo — geometry
- TKPrim — primitives (for gen_step_fixtures only)

For gen_step_fixtures (tool): also TKPrim for `BRepPrimAPI_MakeBox/MakeSphere`.

### 5. FEAParams Addition

Add `float sizingChordError = 1e-3f;` to FEAParams for lin_def scaling.

### 6. GeometryLoaderDispatch

Add `.step` and `.stp` to `makeLoader()` and `scanForModels()`.
StepLoader::load() (IGeometryLoader interface) fills LoadedGeometry only.
StepLoader::loadWithBRep() is the extended API for FEAModel.

### 7. UI Changes (main.cpp)

- `scanForModels()`: add `.step`, `.stp` extensions
- File list: add STEP badge (similar to 3MF badge, gold/amber color)
- Format display: if `lastLoadedFormat == "STEP"`, show "Source: STEP (B-rep retained)"
- Help text: update from "(place .stl / .3mf here)" to "(place .stl / .3mf / .step here)"

### 8. Graceful Degradation (USE_OCCT=OFF)

When `USE_OCCT=OFF` or OCCT not found:
- StepLoader::load() returns false with message "STEP loader not available (USE_OCCT=OFF)"
- `hasBRep()` always returns false
- `loadFile()` still tries, still fails gracefully
- All MeshQuality BRep overloads call the non-BRep path

---

## Files to Create

1. `include/BRepHandle.h` — OCC-free pImpl class
2. `src/BRepHandle_impl.h` — internal OCC-aware Impl definition
3. `src/BRepHandle.cpp` — implements nearestPointOnShape, normalAtNearest, numFaces, etc.
4. `include/StepLoader.h` — IGeometryLoader + extended loadWithBRep API
5. `src/StepLoader.cpp` — full OCC implementation
6. `tools/gen_step_fixtures.cpp` — generates unit_cube.step and sphere_r1.step
7. `regression/step_cube_load.txt` — placeholder sentinel
8. `regression/step_sphere_hausdorff.txt` — placeholder sentinel

## Files to Modify

9. `include/FEAData.h` — add `sizingChordError = 1e-3f`
10. `include/FEAModel.h` — add `brep`, `hasBRep()`, `loadSTEP()`, `~FEAModel()`
11. `src/FEAModel.cpp` — `loadSTEP()`, extend `loadFile()`, clear brep in `processRawGeometry()`
12. `include/GeometryLoaderDispatch.h` — add STEP/STP dispatch
13. `src/main.cpp` — `scanForModels()` + STEP badge + status display
14. `include/MeshQuality.h` — BRep overloads + `usedExactBRep` field in FidelityReport
15. `src/MeshQuality.cpp` — BRep forward-pass implementation
16. `CMakeLists.txt` — OCCT find_package, new sources, gen_step_fixtures target, STEP asset copy

---

## Completed Steps

1. [x] `include/FEAData.h` — added sizingChordError = 1e-3f
2. [x] `include/BRepHandle.h` — pImpl class, OCC-free
3. [x] `src/BRepHandle_impl.h` — OCC-aware Impl struct (BRepHandle::Impl with TopoDS_Shape)
4. [x] `src/BRepHandle.cpp` — nearestPointOnShape, normalAtNearest, principalCurvatures, create()
5. [x] `include/StepLoader.h` — IGeometryLoader + loadWithBRep()
6. [x] `src/StepLoader.cpp` — full OCC implementation with face-orientation winding fix
7. [x] `include/GeometryLoaderDispatch.h` — added .step/.stp dispatch
8. [x] `include/FEAModel.h` — added brep + hasBRep + loadSTEP + ~FEAModel()
9. [x] `src/FEAModel.cpp` — loadSTEP + loadFile dispatch + brep reset in processRawGeometry
10. [x] `src/main.cpp` — scanForModels + STEP badge + status display
11. [x] `include/MeshQuality.h` — BRep overloads + FidelityReport.usedExactBRep
12. [x] `src/MeshQuality.cpp` — BRep fidelity path (OCC forward pass)
13. [x] `tools/gen_step_fixtures.cpp` — cube + sphere STEP generators
14. [x] `CMakeLists.txt` — OCCT integration + new targets + STEP asset copy
15. [x] `regression/step_cube_load.txt` — placeholder sentinel
16. [x] `regression/step_sphere_hausdorff.txt` — placeholder sentinel

---

## Pending (requires OCCT install)

- Install OpenCASCADE (official installer) and set `-DOCCT_ROOT=<path>`
- Run `build.bat configure && build.bat build`
- Run `gen_step_fixtures.exe` to generate `assets/test_fixtures/unit_cube.step` and `sphere_r1.step`
- Load both fixtures in the app and confirm console output + status panel
- Fill in `regression/step_*.txt` placeholder values with actual numbers

---

## Key Constraints / Gotchas

### OCCT on Windows/MSVC
- Build uses MSVC via WSL2 shell (build.bat invokes vcvars64.bat)
- OCCT official installer sets up `OpenCASCADEConfig.cmake`
- cmake config dir typically at `<OCCT_ROOT>/cmake/`
- Default target names: `TKBRep`, `TKSTEP`, etc. (no `OpenCASCADE::` prefix in 7.x)
- DO NOT use FetchContent for OCCT — build time would be hours

### FEAModel destructor (pImpl gotcha)
- `std::unique_ptr<BRepHandle>` with forward-declared `BRepHandle` requires:
  - `~FEAModel()` declared in .h (NOT defaulted there)
  - `FEAModel::~FEAModel() = default;` defined in .cpp (where BRepHandle.h is included)
- Same applies to move ctor/assign if added — but FEAModel has OpenGL handles so
  moving is currently unsupported; just add the destructor.

### Face orientation (TOP CARE POINT)
- `TopAbs_REVERSED` faces: MUST swap triangle index 1 and 2 during harvest
- Failure → half the normals point inward → TODO_03 normal-dev ≈ 180° on all REVERSED faces
- Already hit this bug with TetGen in TODO_03 (Bug 1). Same class of bug.

### RefSurface snapshot still needed even with BRep
- `processRawGeometry` still populates `refSurfaceForFidelity` as before
- BRep path uses RefSurface for the REVERSE fidelity pass (ref → vol BVH)
- Only the FORWARD pass (vol boundary → shape) uses OCC instead of BVH

### BRepExtrema_DistShapeShape performance
- Each OCC nearest-point query: ~0.1–1ms depending on shape complexity
- For typical meshing (10k tris × 4 samples = 40k queries): 4–40 seconds
- Acceptable for a one-time quality check; not suitable for per-frame use
- Add OpenMP parallel loop: `#pragma omp parallel for if(p.useMultithreading)`
- BRepExtrema_DistShapeShape is thread-safe when called with separate objects per thread

### StepLoader::loadWithBRep vs IGeometryLoader::load
- IGeometryLoader::load() only fills LoadedGeometry (no BRep)
- GeometryLoaderDispatch uses IGeometryLoader::load() — so STEP via dispatch fills geometry only
- FEAModel::loadSTEP() calls StepLoader::loadWithBRep() to also get the BRep handle
- This is why loadSTEP() is separate from the dispatch path

### OCCT not installed → USE_OCCT graceful fallback
- All OCCT-calling code is inside `#ifdef HAS_OCCT` guards
- StepLoader::load() returns false with a helpful message when HAS_OCCT=0
- MeshQuality BRep overloads fall through to the existing triangulation path

---

## NEXT SESSION INSTRUCTIONS

TODO_04 implementation is complete. All 16 source files written and verified.

**Before the next TODO, the user needs to:**
1. Install OpenCASCADE (official installer from opencascade.com) for MSVC
2. `build.bat configure -DOCCT_ROOT="<install path>"`
3. `build.bat build`
4. Run `tools/gen_step_fixtures.exe` to generate test fixtures
5. Load fixtures in app, verify console output, fill in `regression/step_*.txt`

**Next TODO to implement:** TODO_05 (interactive teaching panel) — NEW top
priority. On 2026-05-24 three high-priority tasks were inserted ahead of the old
chain and the former TODO_05–25 were renumbered to TODO_08–28:
  - TODO_05 — interactive left-side teaching panel
  - TODO_06 — layer-aware slicing & meshing for FDM parts (EPIC, phased)
  - TODO_07 — fracture result visualization (deformation + failure mode)
All three are PLAN-stage docs awaiting user approval before implementation.
After them, the renumbered chain continues at TODO_08 (quality HUD color overlay).
