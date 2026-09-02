#pragma once

#include "FEAModel.h"
#include <Eigen/Core>
#include <atomic>

// Control parameters for the Newton-Raphson nonlinear static solver.
// Defaults are chosen so that a linear showcase runs in a single load step
// and converges in exactly one NR iteration (validation protocol).
struct NRParams {
    int    numLoadSteps  = 1;
    int    maxIterations = 25;
    double residualTolL2 = 1.0e-6;
};

// Parameters for geometry-aware element selection (Tet4 vs Tet10).
struct GeometryAnalysisParams {
    float curvatureAngleThreshold = 15.0f; // degrees: normals differing by more than this are "curved"
    float highCurvatureFracLimit  = 0.25f; // fraction of vertices with high curvature to trigger Tet10
};

class FEASolver {
public:
    double youngsModulus = 2.0e11; // 200 GPa (Steel)
    double poissonRatio = 0.3;     // Steel
    double forceMagnitude = 1.0e8; // 100 MN total force for visible result
    double fractureStress = 5.0e7; // 50 MPa (typical FDM plastic)

    // When true, solveNonlinearStatic prints a read-only diagnostic block
    // (bbox, octant/quadrant node census, fixed/loaded-set centroids, load
    // resultant + moment about origin, slab-vs-surface size ratios, and a
    // consistent-load comparison) to discriminate topology bias vs BC vs
    // force misalignment as the source of asymmetric results.
    bool verboseDiagnostics = false;

    // Post-processing applied to the consistent nodal load vector before the
    // linear solve. Eliminates spurious directional bias introduced by mesh
    // triangulation asymmetry when the physical problem is known to have a
    // particular symmetry class.
    //
    //   None         : leave consistent loads as computed (default).
    //   XZReflection : average F_y over each 4-node orbit
    //                  { (x,z), (-x,z), (x,-z), (-x,-z) } of every loaded
    //                  node -> enforces 4-fold symmetry about the Y-axis.
    //                  Recommended for cube / box geometries.
    //   YAxial       : bin loaded nodes by (y, r=sqrt(x^2+z^2)) and set
    //                  each node's F_y to its bin mean -> enforces full
    //                  rotational symmetry about the Y-axis. Recommended
    //                  for sphere / cylinder geometries.
    //
    // Both modes preserve the total applied force exactly. Nodes whose
    // symmetry orbit is incomplete (e.g. TetGen Steiner points with no
    // mirror image) are left unchanged and counted in the log output.
    enum class LoadSymmetry { None, XZReflection, YAxial };
    LoadSymmetry loadSymmetry = LoadSymmetry::None;

    // Selects the boundary-condition and force-application scheme:
    //
    //   SurfaceCompressionY : fix nodes at Y_min, distribute -Y consistent
    //                         nodal loads over the Y_max boundary patch.
    //                         Legacy compression test.
    //
    //   PointForceZ         : fix nodes at Z_min, apply a single concentrated
    //                         force in the -Z direction at the boundary node
    //                         with maximum Z coordinate closest to the (x,y)
    //                         centroid of the Z_max face. This eliminates
    //                         all surface-patch triangulation noise since
    //                         the load vector has exactly one non-zero
    //                         entry, making mesh-induced asymmetry directly
    //                         observable in the displacement response.
    //   CantileverBendingZ  : fix nodes at X_min, apply a concentrated
    //                         force in the -Z direction at the centroid of X_max.
    //                         Ideal for validating against Euler-Bernoulli formulas.
    //   FacePull / FaceBend : well-posed face presets — clamp ALL
    //                         DOF on the min-face of `faceAxis` (no rigid
    //                         modes, unlike the Tension* single-node anchor),
    //                         equal-split the total force over the max-face
    //                         nodes: FacePull along +faceAxis, FaceBend along
    //                         `bendDir` (transverse, face-shear bending).
    //   ThreePointBend      : classic 3-point bend test.
    //                         Beam axis = faceAxis; load direction = bendDir.
    //                         Two support bands on the bendDir-MIN surface at
    //                         beamCentre ± span/2 (rollers: bendDir + third
    //                         axis pinned; one support also pins the beam
    //                         axis), load band on the bendDir-MAX surface at
    //                         mid-span pushing toward -bendDir. Bands select
    //                         nodes adaptively (tolerance doubles until >= 6
    //                         nodes) — the EFFECTIVE span (mean support node
    //                         positions) is reported for the F x L oracle.
    enum class LoadType { SurfaceCompressionY, PointForceZ, CantileverBendingZ,
                         TensionX, TensionY, TensionZ,
                         FacePull, FaceBend, ThreePointBend };
    LoadType loadType = LoadType::PointForceZ;
    int faceAxis = 2;  // FacePull/FaceBend: face axis; ThreePointBend: beam axis
    int bendDir  = 0;  // FaceBend/ThreePointBend: force direction (!= faceAxis)
    double bendSpanMM = 50.0;         // ThreePointBend: requested support span
    double lastEffectiveSpanMM = 0.0; // ThreePointBend: measured node-band span
    // equilibrium telemetry (FacePull/FaceBend only): the exact
    // force sum applied along the load direction, and the clamp reaction
    // recovered from the penalty method (R = penalty * u_clamped per DOF).
    double lastAppliedForceN  = 0.0;
    double lastReactionSumN   = 0.0;

    // When true, both solveLinearStatic and solveNonlinearStatic will:
    //   1. Call model.generateMidEdgeNodes() to create Tet10 connectivity.
    //   2. Build Tet10Element instances (30-DOF quadratic tets) instead of
    //      TetrahedralElement (12-DOF linear tets).
    //   3. Use the full (corner + mid-edge) node set for DOF sizing.
    // The flag must be set BEFORE calling either solve method.
    bool useQuadraticElements = false;

    // When true, parallelize the per-element compute/assemble loops inside
    // solveLinearStatic and solveNonlinearStatic with OpenMP. The outer
    // algorithm (Newton-Raphson, load stepping, assemble -> solve) is
    // UNCHANGED; only per-element work is distributed across threads.
    // Triplet lists use pre-sized indices so the final SparseMatrix is
    // identical to the serial path (Eigen::setFromTriplets is order-
    // invariant for sums). F_int scatter uses #pragma omp atomic so the
    // final vector is also identical up to FP summation reordering.
    // The flag must be set BEFORE calling either solve method.
    bool useMultithreading = false;

    // When true, the linear system K*U = F is solved on the GPU using
    // Jacobi-preconditioned CG with NVIDIA cuSPARSE and cuBLAS instead of
    // Eigen's CPU solvers. Requires a CUDA-capable
    // GPU. Falls back to CPU if GPU solve fails.
    bool useGPU = false;

    // --- Async job hooks (UI progress panel) ---------------------------------
    // When non-null, the solver writes a monotonically increasing 0..1 fraction
    // to *progressOut at stage boundaries, and polls *cancelRequested at safe
    // checkpoints (assembly chunks, fracture/NR iterations). A cancelled solve
    // restores the model geometry and returns false. Both default to null so
    // the headless harness and existing call sites are unaffected.
    std::atomic<float>* progressOut     = nullptr;
    std::atomic<bool>*  cancelRequested = nullptr;

    // When true AND the loaded material provides E_z / nu_pz / G_pz, the
    // solver instantiates a TransverseIsotropicMaterial (XY = strong plane,
    // Z = build/weak axis) and solveBrittleFracture uses three independent
    // max-stress checks instead of isotropic von Mises.
    // Has no effect when E_z == 0 (non-anisotropic material loaded).
    // Selects the weak (build) axis for TransverseIsotropicMaterial and for the
    // buildAxis-aware fracture criterion in solveBrittleFracture.
    //   0 = X is the build/weak axis  (YZ is the strong in-plane)
    //   1 = Y is the build/weak axis  (XZ is the strong in-plane)  ← default
    //   2 = Z is the build/weak axis  (XY is the strong in-plane)
    int    buildAxis                 = 1;

    bool   useFdmAnisotropy         = false;
    // Transverse isotropic constants (set by the UI from the .mat file).
    double E_z                       = 0.0;  // through-thickness modulus
    double nu_pz                     = 0.0;  // out-of-plane Poisson
    double G_pz                      = 0.0;  // transverse shear modulus
    // Mode-specific fracture thresholds (Pa).
    double fractureStress_intralayer  = 0.0;  // in-plane von Mises criterion
    double fractureStress_interlayer  = 0.0;  // |sigma_zz| tensile limit
    double fractureShear_interlayer   = 0.0;  // sqrt(tau_xz^2 + tau_yz^2) limit

    // Solves the one-shot linear static problem K u = F:
    //   1. Assembles K = Σ_e A_e^T K_e A_e via triplet list (Tet4 or Tet10).
    //   2. Builds F according to the loadType policy (point force or consistent
    //      nodal loads from tributary-area integration).
    //   3. Enforces Dirichlet BCs: K_ii += α, F_i = 0 for each fixed DOF i,
    //      where α = 1e7 · max(diag(K)).
    //   4. Solves via GPU PCG+Jacobi → CPU PCG+Jacobi → CPU LDL^T.
    //   5. For CantileverBendingZ, prints analytical Euler-Bernoulli δ_max
    //      and FEA percentage error.
    // On success writes u to *U_out (if non-null) and updates model positions.
    bool solveLinearStatic(FEAModel& model, float visualScale = 1.0f,
                           Eigen::VectorXd* U_out = nullptr);

    // Solves the nonlinear static problem R(u) = F_ext − F_int(u) = 0 using
    // incremental Newton-Raphson with load stepping.
    //
    // Outer loop: s = 1..N,  λ_s = s/N,  F_ext,s = λ_s · F_ext_full.
    // Inner NR loop per step:
    //   F_int = Σ_e A_e^T f_{int,e}(u_e)
    //   R = F_ext,s − F_int;  R[fixed DOFs] = 0
    //   if ||R||_2 < τ and iter ≥ 1: converged
    //   K_T = Σ_e A_e^T K_{T,e}(u_e) A_e + penalty(fixed DOFs)
    //   Δu = solve(K_T, R);   u += Δu
    //
    // For a linear-elastic material converges in exactly 1 NR iteration per step.
    bool solveNonlinearStatic(FEAModel& model,
                              float    visualScale = 1.0f,
                              NRParams params      = {});

    // Geometry analysis parameters exposed as UI sliders.
    GeometryAnalysisParams geoParams;

    // Curvature-driven element-order selection followed by a linear static solve.
    //
    // 1. For each surface vertex v, compute θ_v = max angle between the averaged
    //    vertex normal n_v and each incident face normal n_f.
    //    Mark v high-curvature if θ_v > curvatureAngleThreshold.
    // 2. If (N_high-curvature / N_surface) > highCurvatureFracLimit: use Tet10
    //    (calls generateMidEdgeNodes, x_m = (x_a+x_b)/2 per edge); else Tet4.
    // 3. Delegates to solveLinearStatic with the selected element order.
    // Element order is global — the entire mesh uses Tet4 or Tet10, not a mix.
    bool solveAdaptive(FEAModel& model, float visualScale = 1.0f);

    // Brittle element-deletion fracture.
    //
    // Algorithm (each iteration):
    //   1. Assemble K over alive elements only (elements where model.elementAlive[el] != 0).
    //   2. Solve K u = F.
    //   3. Recover per-element von Mises stress via ComputeStressVoigt.
    //   4. Mark elements with σ_vm > fractureStress as dead.
    //   5. Repeat until no new deaths (stable) or maxIters reached.
    //
    // Results are written to model.elementAlive / elementFailureIter and the
    // mesh is visualised via buildBuffers() each iteration so progress is visible.
    bool solveBrittleFracture(FEAModel& model,
                              float     visualScale = 1.0f,
                              int       maxIters    = 50);

private:
    // When non-empty, the assembly loop in solveLinearStatic skips elements
    // where m_fractureAlive[el] == 0.  Set by solveBrittleFracture.
    std::vector<uint8_t> m_fractureAlive;

    // Progress sub-range mapping: reportProgress(f) writes lo + (hi-lo)*f, so
    // an outer loop (fracture iterations) can hand each nested linear solve a
    // slice of the overall bar. Defaults cover the whole bar.
    double m_progLo = 0.0, m_progHi = 1.0;
    void reportProgress(double f) const;
    bool isCancelled() const { return cancelRequested && cancelRequested->load(); }

    // Indentation level for the SolverStatus viewport overlay. A bare
    // solveLinearStatic reports its stages at depth 1 (under the job header);
    // solveBrittleFracture bumps this so each nested solve nests under its
    // "ITERATION k / n" row, exactly like COMSOL's nested progress tree.
    int m_statusDepth = 1;

    // Generic applied-load visualization: rebuilds model.appliedForces from the
    // ACTUAL assembled external force vector F — every arrow sits at a loaded
    // node and points along that node's true force direction, with length
    // scaled by |F_node|. Replaces the per-preset hand-drawn arrows (which
    // could disagree with the load actually applied). Arrows are anchored
    // outside the body: pulls point away from the surface, pushes end at it.
    void recordAppliedForceArrows(FEAModel& model, const Eigen::VectorXd& F,
                                  int nNodes) const;
    // Adds restraint reactions (-penalty*u at constrained DOFs) to the applied
    // load vector before generating arrows. This visualizes the complete
    // external force system: load nose/traction plus clamps or roller supports.
    void recordExternalForceArrows(
        FEAModel& model, const Eigen::VectorXd& appliedF,
        const Eigen::VectorXd& displacement, double penalty,
        const std::vector<int>& fixedNodes,
        const std::vector<std::pair<int,int>>& singleDofFixed,
        int nNodes) const;

    // physical-units bridge. The renderer keeps geometry in the
    // 3-unit model space, but the FE math must run in real SI (metres, N, Pa) so
    // stress/displacement and the fracture comparison respect the ACTUAL part
    // size. scaleGeometryToMeters() rescales originalVolumetricPositions to metres
    // (factor modelToMM/1000) at the outermost solve entry and saves the model-
    // space copy; restoreGeometryToModel() puts it back exactly. The re-entrancy
    // guard makes the nested solveBrittleFracture -> solveLinearStatic call a
    // no-op so coordinates are never double-scaled. m_geomScale is metres per
    // model-unit (used to convert the metre displacement back to model space for
    // the deformed overlay).
    bool                   m_geomInMeters = false;
    double                 m_geomScale    = 1.0;   // metres per model-space unit
    std::vector<glm::vec3> m_savedModelPositions;  // exact model-space restore
    bool scaleGeometryToMeters(FEAModel& model);   // returns true if it scaled
    void restoreGeometryToModel(FEAModel& model);
};
