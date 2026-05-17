# PolyFEA — Volumetric Finite Element Solver

![C++](https://img.shields.io/badge/C++-17-blue?logo=cplusplus)
![CUDA](https://img.shields.io/badge/CUDA-Accelerated-76B900?logo=nvidia)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3+-red?logo=opengl)
![License](https://img.shields.io/badge/License-MIT-green)
![Status](https://img.shields.io/badge/Status-Active-brightgreen)

A high-performance 3D finite element analysis engine built from scratch in C++.
Solves structural deformation problems on arbitrary geometry using adaptive
tetrahedral meshes, a real-time OpenGL visualiser, and a fully custom
CUDA-accelerated solver — no commercial FEA libraries used.

**Author:** Jimmy Han — [@Jimmy-at-3am](https://github.com/Jimmy-at-3am)

---

## Table of Contents
- [What it does](#what-it-does)
- [Development history](#development-history)
- [Architecture](#architecture)
- [Mathematical foundation](#mathematical-foundation)
- [Solver pipeline](#solver-pipeline)
- [Visualiser](#visualiser)
- [Building from source](#building-from-source)
- [Examples](#examples)
- [Roadmap](#roadmap)
- [Technical acknowledgements](#technical-acknowledgements)

---

## What it does

PolyFEA takes an arbitrary STL mesh, tetrahedralizes it into a volumetric finite
element model, applies structural loads and boundary conditions, and solves for
nodal displacements and reaction forces. Results are visualised in real time via
a hardware-accelerated OpenGL renderer with scalar heatmaps.

**Current capabilities:**
- STL import with feature-preserving mesh decimation via `meshoptimizer`
- 3D Delaunay tetrahedralization via `TetGen`
- Linear tetrahedral elements (Tet4) and quadratic tetrahedral elements (Tet10)
- Automatic adaptive promotion from Tet4 → Tet10 on curved geometry
- Linear static analysis and full Newton-Raphson nonlinear static analysis
- Point force and surface compression boundary conditions
- Load symmetry enforcement across XZ reflection planes and polar radii
- Three solver backends: CUDA PCG, CPU Conjugate Gradient, CPU direct LDLT
- Automatic fallback cascade between all three solvers
- Real-time OpenGL 3.3+ visualiser with displacement and force heatmaps
- OpenMP multithreaded stiffness matrix assembly

---

## Development history

This project was built incrementally, each stage solving a real engineering
problem encountered in the previous one:

| Stage | What was built | Problem it solved |
|-------|---------------|-------------------|
| 1 | STL mesh importer + vertex extraction | Needed geometry input from real CAD files |
| 2 | Surface mesh decimation (`meshoptimizer`) | Raw STL files had pathologically dense polar caps on spherical geometry |
| 3 | Regional surface force analysis | First attempt at distributed loading |
| 4 | Switched to point force analysis | Regional forces produced uneven load distribution and unsolvable matrix conditions |
| 5 | Nonlinear Newton-Raphson solver | Linear solver could not handle large-deformation problems |
| 6 | OpenMP multithreaded assembly | Single-threaded stiffness assembly was the primary bottleneck at scale |
| 7 | Selective material properties (`IMaterial`) | Needed to model different materials without rewriting the assembly loop |
| 8 | CUDA PCG solver | CPU solvers were too slow for large meshes; PCIe bottleneck eliminated by keeping iteration in VRAM |
| 9 | Adaptive meshing (current) | Tet4 elements produced inaccurate strain gradients on curved surfaces |

---

## Architecture

```
PolyFEA/
├── include/
│   ├── IElement.h        # Abstract element interface (Tet4, Tet10 implement this)
│   ├── FEASolver.h       # Solver orchestration, load types, solver selection
│   └── FEAModel.h        # Mesh data, deformed positions, GPU buffer flags
├── src/
│   ├── FEAModel.cpp      # Mesh construction, TetGen interface, meshoptimizer pipeline
│   ├── FEASolver.cpp     # Stiffness assembly (OpenMP), boundary conditions, Newton-Raphson
│   ├── cuda_solver/      # Custom PCG implementation in CUDA (cuSPARSE + device kernels)
│   └── main.cpp          # OpenGL render loop, GLFW/GLAD, SimpleUI overlay
├── assets/               # Example STL files and result screenshots
├── lib/                  # Third-party compiled dependencies
├── CMakeLists.txt
└── build.bat
```

**Key design principle:** Element mathematics are fully separated from global
assembly via the `IElement` interface. Adding a new element type (Hex8, shells)
requires no modification to the multithreaded assembly loop in `FEASolver.cpp`.

---

## Mathematical foundation

### Governing equation

For a linear elastic solid under static equilibrium:

```
∇·σ + b = 0    (equilibrium in domain Ω)
u = ū           (displacement BC on Γ_u)
σ·n = t̄         (traction BC on Γ_t)
```

### Weak form

```
∫ ε(v) : C : ε(u) dΩ = ∫ v·b dΩ + ∫ v·t̄ dΓ
```

### Discretised system

```
[K] {u} = {F}
```

Where `[K]` is the global sparse stiffness matrix assembled from element
contributions using isoparametric shape functions over tetrahedral domains.

### Constitutive model

Isotropic linear elasticity. The 6×6 constitutive matrix `D` is built from
Young's Modulus `E` and Poisson's Ratio `ν` via the `IMaterial` / `LinearElastic`
interface. Material properties are dynamically loaded from external `.mat` 
configuration files (e.g., `steel.mat`), allowing users to simulate different 
materials without recompiling or modifying the assembly pipeline.

### Boundary conditions

Constrained DOFs are enforced via the **Penalty Method**:

```
K_ii += 10^7 × max(diag(K))
```

This avoids matrix restructuring while maintaining numerical conditioning.

### Nonlinear solver

For large-deformation problems, `solveNonlinearStatic` implements incremental
Newton-Raphson load stepping:

```
K_T(u) Δu = λ·F_ext − F_int(u)
```

The tangent stiffness `K_T` and internal force array are recomputed each
iteration until the L₂ residual norm satisfies convergence tolerance.

---

## Solver pipeline

### Mesh preparation

1. Parse STL file → extract raw surface vertices and triangles
2. Run `meshopt_simplify` for feature-preserving decimation — locks sharp
   boundary edges, removes pathologically dense regions (spherical caps)
3. Pass cleaned surface to `TetGen` for 3D Delaunay tetrahedralization
4. Adaptive check: compute vertex-normal deviations via `GeometryAnalysisParams`.
   If curvature exceeds threshold → promote mesh topology from Tet4 → Tet10,
   generating mid-edge nodes automatically

### Point force deformation (step by step)

1. Scan `model.originalVolumetricPositions` — identify all nodes on the Z_min
   face within geometric tolerance (fixed boundary)
2. Compute (X, Y) centroid of Z_max boundary — find the single nearest node
3. Assemble global force vector `F` with `forceMagnitude` applied in −Z at
   that node only (eliminates surface-triangulation noise from area averaging)
4. Assemble global tangent stiffness `K` via OpenMP parallel loop over elements
5. Lock Z_min DOFs via Penalty Method
6. Solve `K·U = F` via selected backend (CUDA → CPU CG → CPU LDLT cascade)
7. Scale `U` by visual multiplier → write to `model.deformedPositions` →
   set `model.needsUpdate` → GPU re-uploads buffer on next frame

### Solver backend selection

| Backend | Method | Best for |
|---------|--------|----------|
| CUDA PCG | Preconditioned Conjugate Gradient (custom CUDA kernels + cuSPARSE SpMV) | Large meshes — entire iteration stays in VRAM |
| CPU CG | `Eigen::ConjugateGradient` + Jacobi preconditioner + OpenMP | Mid-size meshes, no GPU available |
| CPU Direct | `Eigen::SimplicialLDLT` (sparse Cholesky) | Small meshes or ill-conditioned matrices |

The solver automatically falls back down this chain. If CUDA runs out of VRAM,
it catches the failure and continues on CPU CG. If CG fails to converge, it
falls back to the exact direct solver.

**Memory optimisation:** The triplet vector used to build the sparse matrix
is explicitly deallocated immediately after `setFromTriplets` — on large
meshes this recovers up to ~15 GB of system RAM before the solve begins.

---

## Visualiser

Built on OpenGL 3.3+ with GLFW and GLAD.

- **Frame loop:** Uncapped render loop tied to monitor refresh rate
- **Buffer strategy:** Vertex buffers are strictly static — `glBufferSubData`
  is called only when `model.needsUpdate` is flagged by the solver, never
  every frame
- **Rendered entities:**
  - Volumetric mesh with transparency toggle
  - Scalar heatmap via `updateScalarFieldData` — paints nodal displacement
    magnitudes or force magnitudes using a dynamic gradient shader
  - Force arrow vectors (`ForceArrow`) drawn at load application sites
  - Schema axes and boundary grid
  - `SimpleUI` C++ immediate-mode overlay rendered without CAD viewport overlap

---

## Building from source

### Prerequisites

- Visual Studio 2019 or 2022 with **C++ Desktop** and **C++ CMake tools** workloads
- CUDA Toolkit 11+ (optional — CPU fallback is automatic; pass `-DUSE_CUDA=OFF` to skip)
- OpenGL 3.3+ capable GPU

Everything else (Eigen, miniz, meshoptimizer) is fetched automatically by CMake at configure time.

### Windows

```bat
git clone --recurse-submodules https://github.com/Jimmy-at-3am/OpenGL-based-FEA-tet10-solver-with-cuda-multithreading-acceleration.git
cd OpenGL-based-FEA-tet10-solver-with-cuda-multithreading-acceleration
build.bat build
```

> **Note:** `--recurse-submodules` is required — it downloads `meshoptimizer` alongside the repo.
> If you already cloned without it, run `git submodule update --init` inside the repo folder.

For a CPU-only build (no CUDA required):

```bat
build.bat configure -DUSE_CUDA=OFF
build.bat build
```

---

## Examples

### Benchmark — cantilever beam under point load

Validation against the classical Euler–Bernoulli analytical solution for tip
deflection of a cantilever beam:

| Parameter | Value |
|-----------|-------|
| Geometry | 5.0 × 1.0 × 1.0 m rectangular beam |
| Material | Steel — E = 200 GPa, ν = 0.30 |
| Load | 100 MN point force at free end (−Z) |

```
δ = FL³ / 3EI      (Euler–Bernoulli beam theory)
Analytical result: δ = 0.25 m
```

Five meshes of the same geometry were tested by varying TetGen's mesh quality
constraint and maximum element volume parameter independently. This isolates
the effect of element shape quality versus mesh density on solver accuracy.

| Run | Mesh Quality | Max Volume | Nodes | Elements | FEA Output | Error |
|-----|-------------|------------|-------|----------|------------|-------|
| 1   | 1.4         | 0.1%       | 81    | 24       | 0.2421 m   | 3.171% |
| 2   | 3.0         | 0.001%     | 1808  | 994      | 0.2566 m   | 2.631% |
| 3   | **3.0**     | **0.01007%** | **294** | **122** | **0.2506 m** | **0.246%** ✅ |
| 4   | 2.088       | 0.01007%   | 318   | 139      | 0.2520 m   | 0.790% |
| 5   | 3.0         | 0.004629%  | 426   | 192      | 0.2537 m   | 1.483% |

*(Add heatmap screenshot here — assets/cantilever_benchmark.png)*

**Key finding — mesh quality dominates over mesh density:**
Run 2 uses 1808 nodes (the densest mesh) yet produces 2.631% error. Run 3 uses
only 294 nodes but achieves 0.246% error — the best result. The difference is
the maximum volume constraint: Run 2's extremely tight volume cap (0.001%)
forces TetGen to generate many small, poorly-shaped elements to satisfy the
constraint, degrading aspect ratios and hurting accuracy more than the extra
nodes help.

Comparing Runs 3 and 4 directly isolates the quality parameter effect: both
share identical Max Volume (0.01007%) and nearly identical node counts (294 vs
318), yet quality 3.0 achieves 0.246% versus quality 2.088 at 0.790% — a 3×
accuracy improvement from shape quality alone.

This is consistent with established tetrahedral FEA theory: element aspect
ratio is the primary driver of numerical accuracy, not node count.

---

## Roadmap

- [x] STL mesh import and surface decimation
- [x] 3D Delaunay tetrahedralization (TetGen)
- [x] Linear Tet4 elements
- [x] Point force static analysis
- [x] Newton-Raphson nonlinear solver
- [x] OpenMP multithreaded assembly
- [x] Selective material properties (IMaterial interface)
- [x] CUDA PCG accelerated solver
- [x] Adaptive Tet4 → Tet10 mesh promotion
- [ ] Von Mises stress and principal stress output
- [ ] Distributed load and pressure boundary conditions
- [ ] Thermal analysis coupling (heat conduction FEA)
- [ ] Additional element types (Hex8, shell elements)
- [ ] Export to VTK / ParaView format

---

## Technical acknowledgements

- **[TetGen](http://www.tetgen.org)** — Hang Si, WIAS Berlin. 3D Delaunay tetrahedralization.
- **[meshoptimizer](https://github.com/zeux/meshoptimizer)** — Arseny Kapoulkine. Feature-preserving mesh decimation.
- **[Eigen](https://eigen.tuxfamily.org)** — Sparse matrix storage, SimplicialLDLT, ConjugateGradient solvers.
- **[GLFW](https://www.glfw.org)** and **[GLAD](https://glad.dav1d.de)** — OpenGL context and extension loading.
- **[CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit)** / **cuSPARSE** — NVIDIA. GPU sparse matrix-vector products.

---

*Built independently as part of ongoing engineering research.
Started January 2026 — actively developed.*
