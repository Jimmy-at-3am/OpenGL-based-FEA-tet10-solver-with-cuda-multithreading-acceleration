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

### 7.3 GPU Direct Solver — cuSOLVER Sparse Cholesky

When `useGPU = true`, PolyFEA solves $K u = F$ on the GPU using NVIDIA's **cuSOLVER** library, which performs a **sparse Cholesky factorization** ($K = LL^T$). This is a direct solver, not an iterative one.

The complete pipeline:

1. **CSC → CSR format.** Eigen stores sparse matrices in Compressed Sparse Column (CSC) format. For a symmetric matrix $K$, the column pointers, row indices, and values of CSC$(K)$ are identical to CSR$(K^T) =$ CSR$(K)$. The raw arrays are therefore transferred to GPU VRAM without any format conversion.

2. **Symbolic analysis.** cuSOLVER analyzes the sparsity pattern of $K$ to determine the nonzero structure of the Cholesky factor $L$, applying a reordering strategy (e.g., Approximate Minimum Degree) to minimize fill-in.

3. **Numerical factorization.** The full $K = LL^T$ factorization is computed on the GPU in VRAM.

4. **Triangular solves.** Forward substitution solves $Lz = F$, then backward substitution solves $L^T u = z$, giving the solution $u = K^{-1} F$. Both passes execute on the GPU.

5. **Result transfer.** The solution vector $u$ is copied back from VRAM to host memory.

If the GPU solve fails (e.g., unsupported GPU, factorization failure), `useGPU` is set to false and the solver falls back to the CPU cascade (PCG → LDL$^T$) for all remaining load steps.

**Key distinction from CPU path.** The CPU uses LDL$^T$ (which does not require strict positive definiteness), while cuSOLVER's Cholesky requires $K$ to be strictly positive definite. After penalty enforcement, $K$ is SPD, so this requirement is always satisfied.

---

## 8. Mesh Quality Metrics

PolyFEA computes two per-element quality metrics to identify degenerate (sliver) tetrahedra that degrade solver accuracy.

### 8.1 Minimum Dihedral Angle

The minimum dihedral angle is the smallest of the six edge dihedral angles of the tetrahedron. For a tetrahedron with vertices $\{v_0, v_1, v_2, v_3\}$:

1. Compute the four outward-pointing unit face normals $\hat{n}_0, \hat{n}_1, \hat{n}_2, \hat{n}_3$, where $\hat{n}_i$ is the normal of the face opposite vertex $v_i$, oriented so that $\hat{n}_i \cdot (v_i - v_j) < 0$ (pointing away from the interior).

2. For each of the six edges $(i, j)$, the dihedral angle $\theta_{ij}$ is the angle between the two faces meeting at that edge, i.e., the faces opposite vertices $k$ and $l$ (where $\{i,j,k,l\} = \{0,1,2,3\}$). Using outward normals:
$$ \cos\theta_{ij} = -\hat{n}_k \cdot \hat{n}_l $$
The negation arises because outward normals of adjacent faces point away from each other — their dot product equals $-\cos(\text{dihedral angle})$.

3. The minimum dihedral angle is:
$$ \theta_{\min} = \min_{(i,j)} \arccos(-\hat{n}_k \cdot \hat{n}_l) $$

A regular tetrahedron has $\theta_{\min} \approx 70.53°$. Elements with $\theta_{\min} < 10°$ are classified as slivers and severely impair the condition number of $K$.

### 8.2 Normalized Volume

The normalized volume quality measure divides the element volume by the cube of its longest edge:

$$ q_V = \frac{|V_e|}{e_{\max}^3} $$

where $e_{\max}$ is the length of the longest edge. For a regular tetrahedron, $q_V = \sqrt{2}/12 \approx 0.11785$. Sliver elements with flat or needle-like geometry approach $q_V \approx 0$. Elements with $q_V < 10^{-3}$ are flagged in diagnostic output.

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
