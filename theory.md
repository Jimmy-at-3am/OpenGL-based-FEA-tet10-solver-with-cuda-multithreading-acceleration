# Theoretical Foundations of PolyFEA

This document details the mathematical and algorithmic foundations of the PolyFEA solver. The implementation follows industry-standard finite element methods for structural mechanics, focusing on robust discretization, nonlinear iterative schemes, and high-performance numerical linear algebra.

---

## 1. Solid Mechanics Formulation

### 1.1 Strong Form of Equilibrium
For a solid body defined in a domain $\Omega$ with boundary $\Gamma$, static equilibrium requires that the divergence of the Cauchy stress tensor $\sigma$ plus any body forces $b$ must be zero:

$$ \nabla \cdot \sigma + b = 0 \quad \text{in } \Omega $$

Subject to Dirichlet (displacement) and Neumann (traction) boundary conditions:
$$ u = \bar{u} \quad \text{on } \Gamma_u $$
$$ \sigma \cdot n = \bar{t} \quad \text{on } \Gamma_t $$

### 1.2 Kinematics and Constitutive Law
PolyFEA implements the **small-strain tensor** $\varepsilon$, the symmetric gradient of the displacement field $u$:

$$ \varepsilon = \frac{1}{2}\left( \nabla u + (\nabla u)^T \right) $$

The strain state is represented in **engineering Voigt notation** as a 6-component vector:

$$ \varepsilon = \begin{bmatrix} \varepsilon_{xx} & \varepsilon_{yy} & \varepsilon_{zz} & \gamma_{xy} & \gamma_{yz} & \gamma_{xz} \end{bmatrix}^T $$

where $\gamma_{ij} = 2\varepsilon_{ij}$ are engineering shear strains.

For an isotropic linear elastic material, the stress is related to the strain via Hooke's Law:

$$ \sigma = D \varepsilon $$

Where $D$ is the $6 \times 6$ constitutive matrix defined by Young's Modulus ($E$) and Poisson's ratio ($\nu$):

$$ D = \frac{E}{(1+\nu)(1-2\nu)} \begin{bmatrix} 1-\nu & \nu & \nu & 0 & 0 & 0 \\ \nu & 1-\nu & \nu & 0 & 0 & 0 \\ \nu & \nu & 1-\nu & 0 & 0 & 0 \\ 0 & 0 & 0 & \frac{1-2\nu}{2} & 0 & 0 \\ 0 & 0 & 0 & 0 & \frac{1-2\nu}{2} & 0 \\ 0 & 0 & 0 & 0 & 0 & \frac{1-2\nu}{2} \end{bmatrix} $$

The shear diagonal entries equal the shear modulus $G = E / (2(1+\nu))$, which is the correct value when engineering shear strains are used.

### 1.3 Weak Form (Principle of Virtual Work)
The strong form is recast into the weak form by taking the inner product with an arbitrary virtual displacement test function $v$ and integrating by parts:

$$ \int_{\Omega} \varepsilon(v)^T D\, \varepsilon(u) \, d\Omega = \int_{\Omega} v^T b \, d\Omega + \int_{\Gamma_t} v^T \bar{t} \, d\Gamma $$

This equation states that the internal virtual work must equal the external virtual work.

---

## 2. Finite Element Discretization

### 2.1 The Galerkin Method
The continuous domain $\Omega$ is discretized into a finite number of tetrahedral elements. The continuous displacement field $u(x)$ is approximated using nodal displacements $u_e$ and shape functions $N(x)$:

$$ u(x) \approx \sum_{i=1}^{n} N_i(x)\, u_i = N u_e $$

Applying the strain operator to the shape functions yields the strain-displacement matrix $B$:
$$ \varepsilon(x) = B u_e $$

### 2.2 Element Types and Shape Functions

PolyFEA supports two isoparametric volume elements defined using barycentric coordinates $(L_1, L_2, L_3, L_4)$ with $\sum L_i = 1$.

#### Tet4 — Linear Tetrahedron: B-Matrix via Vandermonde Inversion

For the 4-node linear tetrahedron, each scalar field component is approximated as:
$$ u(x,y,z) = \alpha_1 + \alpha_2 x + \alpha_3 y + \alpha_4 z $$

The shape functions are uniquely determined by requiring $N_i(x_j, y_j, z_j) = \delta_{ij}$. Stacking the interpolation conditions for all four nodes yields the $4 \times 4$ Vandermonde-like system:

$$ P = \begin{bmatrix} 1 & 1 & 1 & 1 \\ x_1 & x_2 & x_3 & x_4 \\ y_1 & y_2 & y_3 & y_4 \\ z_1 & z_2 & z_3 & z_4 \end{bmatrix} $$

The shape function coefficient matrix is $C = P^{-1}$ (size $4 \times 4$). The spatial derivatives of shape function $N_i$ are then read directly from columns of $C$:

$$ \beta_i = C_{1i} = \frac{\partial N_i}{\partial x}, \quad \gamma_i = C_{2i} = \frac{\partial N_i}{\partial y}, \quad \delta_i = C_{3i} = \frac{\partial N_i}{\partial z} $$

These are **constant** throughout the element (Tet4 is a constant-strain element). The element volume is recovered from:

$$ V_e = \frac{|\det(P)|}{6} $$

The $6 \times 12$ strain-displacement matrix $B$ is assembled in engineering Voigt notation as:

$$
B = \begin{bmatrix}
\beta_1 & 0 & 0 & \beta_2 & 0 & 0 & \beta_3 & 0 & 0 & \beta_4 & 0 & 0 \\
0 & \gamma_1 & 0 & 0 & \gamma_2 & 0 & 0 & \gamma_3 & 0 & 0 & \gamma_4 & 0 \\
0 & 0 & \delta_1 & 0 & 0 & \delta_2 & 0 & 0 & \delta_3 & 0 & 0 & \delta_4 \\
\gamma_1 & \beta_1 & 0 & \gamma_2 & \beta_2 & 0 & \gamma_3 & \beta_3 & 0 & \gamma_4 & \beta_4 & 0 \\
0 & \delta_1 & \gamma_1 & 0 & \delta_2 & \gamma_2 & 0 & \delta_3 & \gamma_3 & 0 & \delta_4 & \gamma_4 \\
\delta_1 & 0 & \beta_1 & \delta_2 & 0 & \beta_2 & \delta_3 & 0 & \beta_3 & \delta_4 & 0 & \beta_4
\end{bmatrix}
$$

where each triplet of columns corresponds to the $(u, v, w)$ displacement DOFs of one node. Because $B$ is constant, the stiffness matrix evaluates analytically:

$$ K_e = V_e \, B^T D B $$

#### Tet10 — Quadratic Tetrahedron: Isoparametric Mapping

The 10-node quadratic tetrahedron uses reference barycentric coordinates $(\xi, \eta, \zeta)$ with $L_0 = 1 - \xi - \eta - \zeta$, $L_1 = \xi$, $L_2 = \eta$, $L_3 = \zeta$. Node layout follows the ABAQUS convention:

| Nodes | Type | Shape function |
|---|---|---|
| 0–3 | Corner | $N_i = L_i(2L_i - 1)$ |
| 4 (edge 0–1) | Mid-edge | $N_4 = 4L_0 L_1$ |
| 5 (edge 1–2) | Mid-edge | $N_5 = 4L_1 L_2$ |
| 6 (edge 0–2) | Mid-edge | $N_6 = 4L_0 L_2$ |
| 7 (edge 0–3) | Mid-edge | $N_7 = 4L_0 L_3$ |
| 8 (edge 1–3) | Mid-edge | $N_8 = 4L_1 L_3$ |
| 9 (edge 2–3) | Mid-edge | $N_9 = 4L_2 L_3$ |

Because $B$ varies linearly with position, Tet10 correctly captures bending strains and avoids the shear locking pathology inherent in Tet4 under bending-dominated loads.

**Isoparametric Jacobian.** The mapping from reference to physical coordinates requires computing the $3 \times 3$ Jacobian matrix at each integration point:

$$ J = \frac{\partial N}{\partial \xi}^T X_{\text{nodes}} $$

where $\partial N / \partial \xi$ is the $10 \times 3$ matrix of shape function gradients with respect to $(\xi, \eta, \zeta)$, and $X_{\text{nodes}}$ is the $10 \times 3$ matrix of physical nodal coordinates. Physical gradients are obtained by the chain rule:

$$ \frac{\partial N}{\partial x} = \frac{\partial N}{\partial \xi} J^{-T} $$

The $6 \times 30$ B matrix at each Gauss point is assembled from the physical gradients $\partial N_i / \partial x$, $\partial N_i / \partial y$, $\partial N_i / \partial z$ in exactly the same engineering Voigt pattern as for Tet4. The element stiffness contribution at that point is weighted by $|\det(J)|$:

$$ K_e = \sum_{g=1}^{n_g} w_g \,|\det J_g|\; B_g^T D B_g $$

**4-Point Gauss Quadrature Rule.** PolyFEA uses the symmetric 4-point Hammer rule on the reference tetrahedron. The integration points and weights are:

| Point | $(\xi,\, \eta,\, \zeta)$ | Weight $w_g$ |
|---|---|---|
| 1 | $(a,\, b,\, b)$ | $1/24$ |
| 2 | $(b,\, a,\, b)$ | $1/24$ |
| 3 | $(b,\, b,\, a)$ | $1/24$ |
| 4 | $(b,\, b,\, b)$ | $1/24$ |

where $a = (5 + 3\sqrt{5})/20 \approx 0.58541$ and $b = (5 - \sqrt{5})/20 \approx 0.13820$. The total weight $4/24 = 1/6$ equals the volume of the reference tetrahedron, ensuring the quadrature integrates a constant integrand correctly. This rule integrates polynomials of degree $\leq 2$ exactly — sufficient for Tet10, since $B$ is degree 1 in reference coordinates and $B^T D B$ is therefore degree 2.

---

## 3. Global Stiffness Assembly

The global stiffness matrix $K$ (size $n_\text{DOF} \times n_\text{DOF}$) is assembled from element contributions via the **triplet list method**:

1. For each element, compute the local stiffness $K_e$ and retrieve its global DOF indices.
2. Emit $(i, j, K_e^{ij})$ triplets into a pre-reserved flat array.
3. Call `Eigen::SparseMatrix::setFromTriplets`, which sums duplicate $(i,j)$ entries — correctly accumulating the shared-node contributions from all incident elements. Summation is **order-invariant**, so the final matrix is bit-identical regardless of element traversal order.

**Parallel assembly.** When multithreading is enabled, the triplet array is pre-sized to `nElems × nDOFs_per_elem²` slots, and each element is assigned a disjoint contiguous block. OpenMP threads write into their own blocks with no synchronization, and `setFromTriplets` gathers the contributions afterward. This strategy eliminates lock contention while producing a numerically identical result to the serial path.

**Memory management.** After `setFromTriplets` completes, the triplet vector is immediately deallocated (via `std::vector::swap` with an empty vector). For a 1M-element Tet10 mesh the triplet array can exceed 10 GB; releasing it before the linear solve is essential to avoid out-of-memory conditions.

---

## 4. Load Application

### 4.1 Consistent Nodal Load Vector

Distributing a total surface traction $\bar{t}$ over a boundary patch as equivalent nodal forces requires computing the **tributary area** of each loaded node, rather than applying a naive $F_\text{total}/N$ per-node split which ignores the non-uniform triangle sizes produced by mesh generation.

**Algorithm:**

1. **Boundary face identification.** Iterate over all element faces (each tetrahedron has 4 triangular faces). A face is on the mesh boundary if and only if it is shared by exactly one tetrahedron. This is detected by building a sorted-key face map: for every tet face $(i,j,k)$, store the sorted triple $\{i,j,k\}$ as a key and increment its reference count. Keys with count $= 1$ are boundary faces.

2. **Loaded patch selection.** Boundary faces whose three vertices all belong to the loaded node set are identified as the load patch.

3. **Tributary area accumulation.** For each load-patch triangle with area $A_f$, each of its three vertices receives an area share of $A_f / 3$ (the standard piecewise-constant traction integration rule). The per-node area share $a_n = \sum_f A_f / 3$ approximates the Voronoi area of node $n$ on the surface patch.

4. **Equivalent nodal force.** The applied traction magnitude is $t = -F_\text{total} / A_\text{patch}$ (N/m²). The nodal force at node $n$ is:
$$ F_n = t \cdot a_n $$
   This guarantees that $\sum_n F_n = F_\text{total}$ exactly.

If no boundary face has all three vertices in the loaded set (e.g., a scattered node selection on a sphere polar cap), the method falls back to the uniform $F_\text{total}/N$ split with a logged warning.

### 4.2 Load Symmetrization

Mesh generators (such as TetGen) do not in general produce mirror-symmetric node distributions. For problems with a known geometric symmetry, the triangulation-induced asymmetry in the consistent load vector can be corrected by projecting the load onto the physical symmetry subspace. PolyFEA implements two symmetrization schemes, both of which exactly preserve the total applied force.

**XZ Reflection (4-fold symmetry).** For every loaded node $n$ at position $(x, z)$, the algorithm finds its three mirror images at $(-x, z)$, $(x, -z)$, and $(-x, -z)$ by nearest-neighbor search within a tolerance $\tau = 10^{-4} \cdot \text{span}_{xz}$. If all three mirrors exist and are unprocessed, the four nodal forces are replaced by their mean:
$$ F_{y,n} = F_{y,n'} = F_{y,n''} = F_{y,n'''} = \frac{1}{4}\left(F_{y,n} + F_{y,n'} + F_{y,n''} + F_{y,n'''}\right) $$
Nodes whose complete orbit cannot be found (Steiner points near corners) are left unchanged.

**Y-Axial Symmetry (rotational).** Loaded nodes are binned by $(y, r)$ where $r = \sqrt{(x-c_x)^2 + (z-c_z)^2}$ is the radial distance from the mesh centroid, using a relative tolerance $\tau = 10^{-3} \cdot \text{span}$. All nodes in the same $(y, r)$ ring are assigned the bin mean of $F_y$. This enforces rotational symmetry about the $Y$-axis and is recommended for spherical and cylindrical geometries.

---

## 5. Boundary Condition Enforcement

PolyFEA uses the **Penalty Method** to enforce Dirichlet boundary conditions (fixed supports). A large penalty stiffness $\alpha$ is added to the diagonal entries corresponding to the fixed DOFs:

$$ K_{ii} \leftarrow K_{ii} + \alpha, \qquad \alpha = 10^7 \times \max\!\left(\operatorname{diag}(K)\right) $$

Setting $\alpha$ relative to the existing diagonal — rather than to an absolute value — makes the penalty scale automatically with the material stiffness and mesh size. This preserves the **symmetric positive-definite (SPD)** structure of $K$ and the performance characteristics of the solvers that exploit it. The Jacobi preconditioner in the CG path normalizes these inflated diagonal entries to $\approx 1$, largely recovering the pre-penalty condition number of the reduced (constrained) system.

---

## 6. Nonlinear Analysis — Incremental Newton-Raphson

For nonlinear materials or large deformations, equilibrium is formulated as a root-finding problem for the residual force vector:

$$ R(u) = F_{\text{ext}} - F_{\text{int}}(u) = 0 $$

PolyFEA implements an **incremental load-stepping Newton-Raphson** algorithm. The total external load $F_\text{ext}$ is applied in $N$ equal increments (load steps). At load step $s$, the fraction of load applied is:

$$ \lambda_s = \frac{s}{N}, \qquad F_{\text{ext},s} = \lambda_s \, F_{\text{ext}} $$

Within each load step, the standard Newton-Raphson iteration proceeds:

1. **Internal force assembly.** Compute $F_{\text{int}}(u_k)$ by gathering the element displacement vector $u_e$ from the global $u$, evaluating $f_e = K_e u_e$ at each element via the `IElement` interface, and scattering the contributions into the global vector.

2. **Residual.** Compute $R = F_{\text{ext},s} - F_{\text{int}}$. Zero the residual at all fixed DOFs to enforce Dirichlet conditions on the increment.

3. **Convergence check.** If $\|R\|_2 < \tau_\text{tol}$ and at least one NR iteration has been completed, declare convergence. The first iteration ($k = 0$) is never declared converged — this ensures $\|R\|$ at $k=0$ is always printed, providing the validation hook (for a linear material this initial residual equals $\|F_\text{ext}\|$ and drops to machine zero after one iteration).

4. **Tangent stiffness.** Assemble $K_T(u_k)$ via `IElement::ComputeTangentStiffness` (identical to the linear assembly procedure in Section 3). Apply penalty Dirichlet on the tangent.

5. **Linear step.** Solve $K_T(u_k)\, \Delta u_k = R(u_k)$ using one of the solvers in Section 7.

6. **State update.** $u_{k+1} = u_k + \Delta u_k$.

**Validation property.** For a linear-elastic material with $N = 1$ load step: at $k=0$, $F_\text{int} = 0$ so $R = F_\text{ext}$; the NR step solves $K\, \Delta u = F_\text{ext}$ giving $u = K^{-1} F_\text{ext}$. At $k=1$, $F_\text{int} = Ku = F_\text{ext}$, so $R = 0$ and convergence is declared. The nonlinear solver reduces to the linear solver in exactly one iteration.

---

## 7. Linear Solvers

The core bottleneck of any FEA engine is solving the massive sparse linear system $K u = F$. PolyFEA implements an automatic fallback cascade across three distinct solver backends.

### 7.1 CPU Direct Solver — Sparse LDL$^T$ Factorization

The primary CPU solver (Eigen `SimplicialLDLT`) decomposes the symmetric global stiffness matrix $K$ into a lower triangular matrix $L$ and a diagonal matrix $D$:

$$ K = L D L^T $$

Forward and backward substitution then solve for $u$ in $O(n)$ operations given the factorization. This algorithm is numerically robust — the LDL$^T$ decomposition does not require square roots and tolerates mild numerical indefiniteness — but its memory cost scales as $O(N^{4/3})$ to $O(N^2)$ due to fill-in in $L$ for 3D volumetric meshes. It is the default serial solver path.

### 7.2 CPU Iterative Solver — Preconditioned Conjugate Gradient (PCG)

For large meshes with multithreading enabled, PolyFEA uses the **Conjugate Gradient** method (Eigen `ConjugateGradient`), a Krylov-subspace iterative method that minimizes the quadratic energy functional:

$$ f(u) = \frac{1}{2} u^T K u - u^T F $$

Because CG performance degrades rapidly with condition number $\kappa(K)$, PolyFEA applies a **Jacobi (diagonal) preconditioner** $M = \operatorname{diag}(K)$. Each preconditioned CG step solves the transformed system $M^{-1}K u = M^{-1}F$, scaling each equation by the reciprocal of its diagonal. This directly counters the ill-conditioning introduced by the penalty method: the enormous diagonal entries at fixed DOFs are immediately divided to $\approx 1$, dramatically reducing $\kappa(M^{-1}K)$.

When Eigen is compiled with OpenMP support, its internal SpMV and dot-product kernels parallelize automatically across all available threads, using the same thread count set via `omp_get_max_threads()`. If CG fails to converge within the iteration budget, the solver falls back to serial SimplicialLDLT.

### 7.3 GPU Iterative Solver — cuSPARSE Preconditioned Conjugate Gradient

When `useGPU = true`, PolyFEA solves $K u = F$ on the GPU using a **Jacobi-preconditioned Conjugate Gradient** (PCG) algorithm built on NVIDIA's **cuSPARSE** sparse matrix–vector product (SpMV) plus custom CUDA kernels for the vector operations. This is an *iterative* solver, identical in mathematics to the CPU CG path of Section 7.2 but executed entirely in VRAM.

**Why PCG and not sparse Cholesky.** A GPU sparse Cholesky factorization (`cusolverSpDcsrlsvchol`) produces a dense fill-in factor: for a large 3D stiffness matrix with tens of millions of nonzeros this can require 5–20× the input memory (gigabytes), readily exceeding the available VRAM. PCG never factorizes — it needs only $O(\text{nnz})$ memory at all times (the original matrix plus a handful of length-$n$ work vectors), so no fill-in ever occurs and the entire iteration stays resident on the device.

The complete pipeline:

1. **CSC → CSR transfer.** Eigen stores sparse matrices in Compressed Sparse Column (CSC) format. For a symmetric matrix $K$, CSC$(K)$ is bit-identical to CSR$(K)$, so the raw column-pointer, row-index, and value arrays are uploaded to VRAM without any format conversion.

2. **Jacobi preconditioner.** The diagonal $\operatorname{diag}(K)$ is extracted and both $K$ and $F$ are scaled by $1/\operatorname{diag}(K)$. This mirrors the CPU path's diagonal preconditioner and immediately normalizes the enormous penalty-method diagonal entries at fixed DOFs to $\approx 1$, sharply reducing the condition number that governs CG convergence.

3. **PCG iteration.** Each step performs one cuSPARSE SpMV $Ap = K p$ plus device-kernel dot products and AXPY updates. Iteration continues until the residual norm satisfies tolerance or the iteration budget is exhausted.

4. **Result transfer.** The converged solution vector $u$ is copied back from VRAM to host memory.

If the GPU solve fails (e.g., unsupported GPU, allocation failure, or non-convergence), `useGPU` is set to false and the solver falls back to the CPU cascade (CG → LDL$^T$) for all remaining load steps.

**Key distinction from CPU path.** The GPU and CPU CG paths solve the same preconditioned system; the GPU path simply keeps every iteration in device memory, eliminating the per-iteration PCIe round-trips that would otherwise dominate runtime. As an SPD-only Krylov method, CG requires $K$ to be symmetric positive-definite — guaranteed here by penalty enforcement (Section 5).

---

## 8. Mesh Quality Metrics

PolyFEA computes five per-element shape measures to identify degenerate (sliver) tetrahedra that degrade solver accuracy. All are evaluated in double precision and guarded against degenerate denominators. Exact arithmetic predicates (Shewchuk's `orient3d`/`insphere`, bootstrapped via `initExactPredicates`) underpin the robustness of the volume sign tests. The `MeshQuality` module aggregates them into per-mesh histograms, percentile summaries (p05/p50/p95/p99), and a worst-$N$ element list, and classifies each element as inverted (scaled Jacobian $\le 0$), severe sliver ($\theta_{\min} < 5°$), or sliver ($\theta_{\min} < $ `tetMinDihedralDeg`).

### 8.1 Knupp/Pebay Shape

The Knupp shape metric is the primary scalar quality measure, normalized so a regular tetrahedron returns exactly $1$. With edge vectors $e_1 = p_1 - p_0$, $e_2 = p_2 - p_0$, $e_3 = p_3 - p_0$:

$$ q_{\text{shape}} = 2^{1/3}\, \frac{3\,(\det)^{2/3}}{\|e_1\|^2 + \|e_2\|^2 + \|e_3\|^2}, \qquad \det = (e_1 \times e_2)\cdot e_3 = 6 V_e $$

The leading factor $2^{1/3}$ rescales $q_{\text{shape}}(\text{regular}) = 1$. If $\det < 10^{-30}$ (degenerate or inverted) the metric returns $0$. Results are clamped to $[0, 1]$.

### 8.2 Minimum and Maximum Dihedral Angle

The six edge dihedral angles bound the angular distortion of the tetrahedron. For an edge running $a \to b$ with the two opposite vertices $c$ and $d$, the dihedral is the angle between the two faces meeting at that edge. Writing $e = b - a$, the two in-plane vectors normal to the edge are $n_1 = e \times (c-a)$ and $n_2 = e \times (d-a)$, giving:

$$ \cos\theta = \frac{n_1 \cdot n_2}{\|n_1\|\,\|n_2\|} $$

The cosine is clamped to $[-1, 1]$ before $\arccos$ to absorb floating-point noise. The smallest and largest of the six $\theta$ values are reported as $\theta_{\min}$ and $\theta_{\max}$. A regular tetrahedron has every dihedral $\approx 70.53°$. Elements with $\theta_{\min}$ below `tetMinDihedralDeg` (default 10°, matching ANSYS Fluent's sliver threshold) are flagged as slivers; below 5° (NAFEMS/COMSOL hard-reject) they are severe slivers.

### 8.3 Scaled Jacobian

The scaled Jacobian measures corner orthogonality and detects inverted elements. At each of the four corners a local Jacobian is formed from the three outgoing edges (in a fixed cyclic order so all four corner determinants carry the sign of the tetrahedron's signed volume); the corner value is:

$$ sJ_{\text{corner}} = \sqrt{2}\,\frac{(e_1 \times e_2)\cdot e_3}{\|e_1\|\,\|e_2\|\,\|e_3\|} $$

The $\sqrt{2}$ factor normalizes a regular tetrahedron to $+1$. The element value is the minimum over the four corners. **A non-positive minimum scaled Jacobian indicates an inverted (tangled) element** and is the FAIL criterion for the mesh — unlike slivers, which only WARN.

### 8.4 Radius Ratio

The radius ratio compares the inscribed and circumscribed sphere radii:

$$ \rho = \frac{3\,r_{\text{in}}}{R_{\text{circ}}}, \qquad r_{\text{in}} = \frac{3 V_e}{A_{\text{total}}} $$

where $A_{\text{total}}$ is the sum of the four face areas and $R_{\text{circ}} = \|x_c - p_0\|$ is found from the circumcenter, computed via the cofactor form $x_c = \frac{1}{2\det}\left(\|e_1\|^2\,(e_2\times e_3) + \|e_2\|^2\,(e_3\times e_1) + \|e_3\|^2\,(e_1\times e_2)\right)$. The factor $3$ normalizes $\rho(\text{regular}) = 1$; sliver and needle elements approach $\rho \to 0$. Result clamped to $[0, 1]$.

### 8.5 Equiangular Skew (volume-based)

The skew measures how much smaller the element is than the regular tetrahedron inscribed in the same circumscribed sphere of radius $R$:

$$ S = \frac{V_{\text{equi}} - V_e}{V_{\text{equi}}}, \qquad V_{\text{equi}} = \frac{8\sqrt{3}}{27}\,R^3 $$

$S = 0$ for a regular tetrahedron (where $V_e = V_{\text{equi}}$) and $S \to 1$ for a degenerate sliver. Result clamped to $[0, 1]$ and reported as a four-bin histogram alongside the Knupp histogram.

### 8.6 Tet10 Isoparametric Variants

For quadratic meshes the Knupp shape and scaled Jacobian are re-evaluated isoparametrically at the four points of the standard Gauss rule (Section 2.2), using the $3\times3$ physical Jacobian $J$ assembled from the 10 nodal positions. The shape-function reference gradients used here are kept bit-for-bit identical to those in `Tet10Element` to ensure the quality report reflects the same mapping the solver integrates over.

### 8.7 Geometric Fidelity — Hausdorff Distance and Normal Deviation

The per-element metrics above measure element *shape*; they say nothing about how faithfully the volumetric mesh boundary reproduces the *input geometry*. PolyFEA therefore also computes a geometric fidelity report comparing the post-tetrahedralization boundary surface against a reference snapshot of the input surface (`RefSurface`, captured before TetGen runs).

**Symmetric Hausdorff distance.** Two directed passes are evaluated:

$$ d(A \to B) = \max_{a \in A}\, \min_{b \in B} \|a - b\|, \qquad d_H = \max\big(d(A\to B),\, d(B\to A)\big) $$

The forward pass samples the volumetric boundary faces (a 4-point quadrature per triangle) and measures their distance to the reference surface; the reverse pass does the opposite. Both directions use a median-split bounding-volume hierarchy (BVH) over the triangle set for $O(\log n)$ nearest-triangle queries. The report records both the maximum Hausdorff distance and its 95th percentile (which suppresses single-point outliers from Steiner vertices), normalized against the mesh bounding-box diagonal.

**Normal deviation.** At each sample the angle between the volumetric-boundary normal and the reference-surface normal at the nearest point is recorded; the p50/p95/p99/max of this distribution quantify how well surface orientation is preserved. A face-orientation bug manifests unmistakably as $\approx 180°$ deviation.

A mesh **passes** fidelity when the Hausdorff distance is below 1 % of the bounding-box diagonal and the p95 normal deviation is below 15°.

**Exact B-rep nearest-point (STEP input).** When the model retains an analytic B-rep (Section 11), the forward pass replaces the BVH-on-triangulation query with OpenCASCADE's `BRepExtrema_DistShapeShape`, which returns the exact nearest point on the underlying NURBS surface rather than on its discrete approximation. On smooth geometry this improves Hausdorff accuracy by 2–3 orders of magnitude (e.g., a sphere drops from $\sim\!5\times10^{-4}$ to $\sim\!8\times10^{-7}$), and the report flags `usedExactBRep`. The reverse pass continues to use the triangulation BVH.

---

## 9. Adaptive Element Selection

To balance accuracy against computational cost, PolyFEA implements a curvature-driven strategy that selects the element order (Tet4 or Tet10) for the entire mesh based on the geometry of the input surface.

**Algorithm:**

1. **Face normal computation.** For every boundary triangle, compute the unit outward face normal:
$$ \hat{n}_f = \frac{\vec{e}_1 \times \vec{e}_2}{\|\vec{e}_1 \times \vec{e}_2\|} $$

2. **Vertex normal averaging.** For each surface vertex, accumulate and average the face normals of all incident boundary triangles to produce a smooth vertex normal $\hat{n}_v$ approximating the underlying surface curvature.

3. **Per-vertex curvature classification.** For each surface vertex $v$, compute the deviation angle between $\hat{n}_v$ and each incident face normal:
$$ \theta_v = \max_f \arccos(\hat{n}_f \cdot \hat{n}_v) $$
If $\theta_v$ exceeds the user-configurable `curvatureAngleThreshold` (default 15°), vertex $v$ is marked as high-curvature.

4. **Global element-order selection.** Compute the fraction of high-curvature surface vertices:
$$ f_{\text{curved}} = \frac{N_{\text{high-curvature}}}{N_{\text{surface}}} $$
If $f_{\text{curved}} > \text{highCurvatureFracLimit}$ (default 0.25), the solver upgrades the **entire mesh** to Tet10 by calling `model.generateMidEdgeNodes()`, which inserts midpoint nodes on every tetrahedral edge at positions $\frac{1}{2}(x_i + x_j)$, and then constructs Tet10 element instances referencing the 10-node connectivity. Otherwise, the mesh uses Tet4 throughout.

This is a **global** decision — the solver does not mix Tet4 and Tet10 elements within a single assembly, as doing so would require incompatible DOF numbering. The threshold fraction provides a meaningful trigger: a flat box has $f_{\text{curved}} \approx 0$ and uses efficient Tet4, while a sphere has $f_{\text{curved}} \approx 1$ and uses accurate Tet10.

---

## 10. Analytical Validation (Euler-Bernoulli Beam Theory)

To rigorously validate solver accuracy, PolyFEA includes an automated analytical benchmark using classical **Euler-Bernoulli Beam Theory**.

For a cantilever beam fixed at $X_{\min}$ and subjected to a transverse point load $F$ at the free end $X_{\max}$, the theoretical maximum tip deflection is:

$$ \delta_{\max} = \frac{F L^3}{3 E I} $$

Where the area moment of inertia $I$ for a solid rectangular cross-section bending about the neutral axis is:

$$ I = \frac{b h^3}{12} $$

The `FEASolver` automatically extracts $L$, $b$, $h$ from the AABB (Axis-Aligned Bounding Box) of the generated mesh, computes $I$, and prints the analytical $\delta_{\max}$ alongside the numerical $\|U\|_\infty$, reporting a percentage error to quantify mesh convergence:

$$ \text{error} = \frac{|\delta_\text{FEA} - \delta_\text{analytical}|}{\delta_\text{analytical}} \times 100\% $$

This validation is activated by the `CantileverBendingZ` load type, which fixes nodes at $X_{\min}$, applies a single concentrated $-Z$ force at the node closest to the centroid of the $X_{\max}$ face, and compares the maximum nodal displacement magnitude against $\delta_{\max}$.

---

## 11. Geometry Input Pipeline

PolyFEA accepts three input formats, dispatched by file extension through a single `IGeometryLoader` interface (`GeometryLoaderDispatch`). Every loader produces a common `LoadedGeometry` (surface vertices + triangle indices) which `processRawGeometry` then normalizes — translating the centroid to the origin and uniformly scaling so the bounding-box diagonal spans 3 units — before tetrahedralization. Because normalization is unconditional, no per-format unit conversion is required.

### 11.1 STL — Triangulated Surface

The baseline format. Raw triangle soup is parsed, welded into an indexed mesh, and (optionally) decimated by `meshoptimizer`'s feature-preserving simplification to remove pathologically dense regions (e.g., spherical polar caps) while locking sharp boundary edges.

### 11.2 3MF — Compressed Mesh Container

3MF models are ZIP archives containing an XML mesh payload. The loader unzips the `3D/3dmodel.model` part (via `miniz`) and parses its `<vertices>` / `<triangles>` elements into the same `LoadedGeometry` structure. Unlike STL, 3MF carries a well-defined vertex index list, so no welding pass is needed.

### 11.3 STEP — Analytic B-rep with Retained NURBS

STEP (`.step` / `.stp`) input is handled via **OpenCASCADE (OCCT)** and is the highest-fidelity path because it retains the exact boundary representation rather than discarding it after tessellation.

**Pipeline:**

1. **Read and heal.** `STEPControl_Reader` transfers the file into a `TopoDS_Shape`. `ShapeFix_Shape` then repairs open edges, inconsistent vertex tolerances, and bad face orientations that STEP exporters commonly emit. The cascade unit (`xstep.cascade.unit`) is read for logging only.

2. **Tessellate.** `BRepMesh_IncrementalMesh` triangulates the healed shape with a curvature-driven discretization controlled by two parameters:
$$ \text{lin\_def} = \texttt{sizingChordError} \times L_{\text{diag}}, \qquad \text{ang\_def} = 0.3\ \text{rad} \approx 17° $$
   where `sizingChordError` defaults to $10^{-3}$ (0.1 % chord error) and $L_{\text{diag}}$ is the shape's bounding-box diagonal. The mesher freezes a 1-D discretization of each edge first, then meshes each face in $(u,v)$ parameter space under the first fundamental form, guaranteeing that sharp feature edges are preserved *exactly* — without any heuristic dihedral threshold.

3. **Harvest triangles, respecting face orientation.** Each `TopoDS_Face` carries an orientation flag. For `TopAbs_REVERSED` faces the triangle winding is flipped (swap two indices); omitting this would invert half the surface normals.

4. **Retain the B-rep.** The healed `TopoDS_Shape` is kept live on `FEAModel` as `brep` (a `unique_ptr<BRepHandle>`). OCCT is isolated behind this pImpl handle so only two translation units compile against its headers; all other code, and the entire build when `USE_OCCT=OFF`, sees an inline stub. The retained B-rep enables the exact-NURBS fidelity pass of Section 8.7 via `BRepExtrema_DistShapeShape` (nearest point) and `BRepLProp_SLProps` (surface normal at that point).
