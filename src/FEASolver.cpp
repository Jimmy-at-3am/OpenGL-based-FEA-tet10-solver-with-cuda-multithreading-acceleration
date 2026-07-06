#include "FEASolver.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/IterativeLinearSolvers>

#include <omp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "IElement.h"
#include "IMaterial.h"
#include "TetrahedralElement.h"
#include "Tet10Element.h"

#ifdef HAS_CUDA
#include "CudaSolver.h"
#endif

using namespace Eigen;

// -----------------------------------------------------------------------------
// Tet quality helpers (file-local, used only by diagnostic blocks).
// -----------------------------------------------------------------------------
// Minimum dihedral angle of a tetrahedron, in degrees.
//
// For a tet with vertices {v0,v1,v2,v3}, first computes the 4 unit outward
// face normals n_i (normal of the face opposite vertex i, oriented so that
// n_i · (v_i − v_j) < 0 for any j on that face).
//
// For each of the 6 edges (i,j), the two adjacent faces are those opposite
// the remaining vertices k and l (where {i,j,k,l} = {0,1,2,3}).
// The dihedral angle at edge (i,j) satisfies:
//   cos θ_{ij} = −n̂_k · n̂_l
// The negation arises because outward normals of adjacent faces point away
// from each other, so their dot product = −cos(interior dihedral angle).
//
// Returns θ_min = min_{(i,j)} arccos(−n̂_k · n̂_l).
// Regular tet: θ_min ≈ 70.53°. Slivers: θ_min → 0°.
static double tetMinDihedralDeg(const glm::dvec3& a,
                                const glm::dvec3& b,
                                const glm::dvec3& c,
                                const glm::dvec3& d)
{
    glm::dvec3 verts[4] = { a, b, c, d };
    glm::dvec3 n[4];
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) & 3, k = (i + 2) & 3, l = (i + 3) & 3;
        glm::dvec3 nn = glm::cross(verts[k] - verts[j], verts[l] - verts[j]);
        if (glm::dot(nn, verts[i] - verts[j]) > 0.0) nn = -nn; // outward
        double nl = glm::length(nn);
        n[i] = (nl > 0.0) ? nn / nl : glm::dvec3(0.0);
    }
    const int edges[6][2] = {{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
    double minDeg = 180.0;
    for (int e = 0; e < 6; ++e) {
        int i = edges[e][0], j = edges[e][1];
        int k = -1, ll = -1;
        for (int v = 0; v < 4; ++v)
            if (v != i && v != j) { if (k < 0) k = v; else ll = v; }
        double cosD = -glm::dot(n[k], n[ll]);
        if (cosD >  1.0) cosD =  1.0;
        if (cosD < -1.0) cosD = -1.0;
        const double deg = std::acos(cosD) * (180.0 / 3.14159265358979323846);
        if (deg < minDeg) minDeg = deg;
    }
    return minDeg;
}

// Normalised volume quality measure:
//   q_V = |V_e| / e_max^3
// where V_e = (1/6)|det[b−a, c−a, d−a]| is the tet volume and e_max is the
// length of the longest edge. A regular tetrahedron gives q_V = √2/12 ≈ 0.11785.
// Degenerate flat or needle-like elements give q_V → 0.
static double tetNormalizedVolume(const glm::dvec3& a,
                                  const glm::dvec3& b,
                                  const glm::dvec3& c,
                                  const glm::dvec3& d)
{
    const double V = std::abs(glm::dot(b - a, glm::cross(c - a, d - a))) / 6.0;
    const glm::dvec3 verts[4] = { a, b, c, d };
    double eMax2 = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            const glm::dvec3 e = verts[i] - verts[j];
            const double l2 = glm::dot(e, e);
            if (l2 > eMax2) eMax2 = l2;
        }
    const double eMax3 = std::pow(eMax2, 1.5);
    return (eMax3 > 0.0) ? V / eMax3 : 0.0;
}

// -----------------------------------------------------------------------------
// buildConsistentLoadVector (file-local helper)
// -----------------------------------------------------------------------------
// Converts a total surface traction force F_total (−Y direction) into a
// physically consistent nodal load vector using the tributary-area method.
//
// Mathematical formulation:
//   For a uniform traction t = −F_total / A_patch (N/m²) over the loaded
//   surface patch, the equivalent nodal force at node n is:
//     F_n = t · a_n,   a_n = Σ_{f∋n, f∈patch} A_f / 3
//   where A_f is the area of boundary triangle f and the factor 1/3 distributes
//   each triangle's contribution equally among its three vertices (standard
//   piecewise-constant traction integration rule). This guarantees Σ_n F_n = F_total.
//
// Boundary detection: a tet face (i,j,k) is on the boundary iff it is referenced
// by exactly one tetrahedron, detected via a sorted-key face reference-count map.
//
// Falls back to the uniform F_total/N per-node split (with a warning) when no
// boundary face has all three vertices in loadedNodes (e.g. sphere polar cap).
//
// Outputs:
//   F_out       : size nDOFs, Y-components filled; all other DOFs zero.
//   areaShareOut: size nNodes, per-node area share a_n.
//   totalAreaOut: total loaded patch area A_patch (0 on fallback).
//   triCountOut : number of boundary patch triangles contributing.
// -----------------------------------------------------------------------------
static void buildConsistentLoadVector(
    const FEAModel&         model,
    const std::vector<int>& loadedNodes,
    int                     nNodes,
    double                  forceMagnitude,
    VectorXd&               F_out,
    std::vector<double>&    areaShareOut,
    double&                 totalAreaOut,
    int&                    triCountOut)
{
    const int nDOFs  = nNodes * 3;
    const int nElems = static_cast<int>(model.tetrahedra.size() / 4);

    F_out = VectorXd::Zero(nDOFs);
    areaShareOut.assign(nNodes, 0.0);
    totalAreaOut = 0.0;
    triCountOut  = 0;

    if (loadedNodes.empty()) return;

    // ---- Pass 1: count each tet face by sorted vertex triple --------------
    std::map<std::array<int, 3>, int> faceCount;
    const int tetFaces[4][3] = {{1,2,3},{0,2,3},{0,1,3},{0,1,2}};
    for (int el = 0; el < nElems; ++el) {
        int v[4];
        for (int k = 0; k < 4; ++k)
            v[k] = static_cast<int>(model.tetrahedra[el * 4 + k]);
        for (int f = 0; f < 4; ++f) {
            std::array<int, 3> key = { v[tetFaces[f][0]],
                                       v[tetFaces[f][1]],
                                       v[tetFaces[f][2]] };
            std::sort(key.begin(), key.end());
            ++faceCount[key];
        }
    }

    // ---- Pass 2: area-share accumulation over boundary patch triangles ----
    std::vector<char> isLoaded(nNodes, 0);
    for (int n : loadedNodes) isLoaded[n] = 1;

    for (const auto& kv : faceCount) {
        if (kv.second != 1) continue;                          // not boundary
        const std::array<int, 3>& tri = kv.first;
        if (!(isLoaded[tri[0]] && isLoaded[tri[1]] && isLoaded[tri[2]]))
            continue;                                          // not on load patch

        glm::dvec3 a(model.originalVolumetricPositions[tri[0]]);
        glm::dvec3 b(model.originalVolumetricPositions[tri[1]]);
        glm::dvec3 c(model.originalVolumetricPositions[tri[2]]);
        const double area  = 0.5 * glm::length(glm::cross(b - a, c - a));
        const double share = area / 3.0;

        areaShareOut[tri[0]] += share;
        areaShareOut[tri[1]] += share;
        areaShareOut[tri[2]] += share;
        totalAreaOut += area;
        ++triCountOut;
    }

    // ---- Pass 3: build F from traction * area-share ------------------------
    if (triCountOut > 0 && totalAreaOut > 0.0) {
        const double traction = -forceMagnitude / totalAreaOut; // -Y N/m^2
        for (int n : loadedNodes) {
            F_out(n * 3 + 1) = traction * areaShareOut[n];
        }
    } else {
        // Fallback: loaded slab does not form a boundary surface patch.
        // Happens if the loaded set is a scattered point cloud (e.g. a tiny
        // polar cap where no tet face has all three vertices in the cap).
        std::cout << "[LOAD] Warning: loaded slab is not a closed boundary "
                     "patch; falling back to uniform F/N per-node loading."
                  << std::endl;
        const double fPerNode =
            -forceMagnitude / static_cast<double>(loadedNodes.size());
        for (int n : loadedNodes) F_out(n * 3 + 1) = fPerNode;
    }
}

// -----------------------------------------------------------------------------
// symmetrizeLoadXZ (file-local helper)
// -----------------------------------------------------------------------------
// Projects the Y-component of F_out onto the 4-fold XZ-reflection symmetry
// subspace by averaging over reflection orbits.
//
// For every loaded node n at position (x, z), the three XZ-mirror images are
// located at (−x, z), (x, −z), and (−x, −z) (reflections about the centroid
// planes). If all three mirrors exist within tolerance τ = 1e-4·span_{xz},
// the four nodal forces are replaced by their orbit mean:
//   F_y,n = F_y,n' = F_y,n'' = F_y,n''' = (F_y,n + F_y,n' + F_y,n'' + F_y,n''') / 4
// This enforces 4-fold symmetry while preserving Σ F_y exactly for each
// complete orbit. Nodes with no complete orbit (Steiner points near corners)
// are left unchanged.
static void symmetrizeLoadXZ(VectorXd&               F_out,
                             const std::vector<int>& loadedNodes,
                             const FEAModel&         model,
                             int                     nNodes)
{
    if (loadedNodes.empty()) return;

    double xmn = 1e9, xmx = -1e9, zmn = 1e9, zmx = -1e9;
    for (int i = 0; i < nNodes; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[i];
        if (p.x < xmn) xmn = p.x; if (p.x > xmx) xmx = p.x;
        if (p.z < zmn) zmn = p.z; if (p.z > zmx) zmx = p.z;
    }
    const double cx  = 0.5 * (xmn + xmx);
    const double cz  = 0.5 * (zmn + zmx);
    const double tol = 1e-4 * std::max(xmx - xmn, zmx - zmn);

    auto findNear = [&](double tx, double tz) -> int {
        int best = -1;
        double bd2 = 1e300;
        for (int n : loadedNodes) {
            const glm::vec3& p = model.originalVolumetricPositions[n];
            const double dx = p.x - tx, dz = p.z - tz;
            const double d2 = dx * dx + dz * dz;
            if (d2 < bd2) { bd2 = d2; best = n; }
        }
        return (bd2 <= tol * tol) ? best : -1;
    };

    std::vector<char> done(nNodes, 0);
    int completeOrbits = 0, orphanOrbits = 0;
    double Fbefore = 0.0, Fafter = 0.0;
    for (int n : loadedNodes) Fbefore += F_out(n * 3 + 1);

    for (int n : loadedNodes) {
        if (done[n]) continue;
        const glm::vec3& p = model.originalVolumetricPositions[n];
        const int nx  = findNear(2.0 * cx - p.x, p.z);
        const int nz  = findNear(p.x,            2.0 * cz - p.z);
        const int nxz = findNear(2.0 * cx - p.x, 2.0 * cz - p.z);

        if (nx >= 0 && nz >= 0 && nxz >= 0
            && !done[nx] && !done[nz] && !done[nxz])
        {
            const double avg = 0.25 * (F_out(n   * 3 + 1)
                                     + F_out(nx  * 3 + 1)
                                     + F_out(nz  * 3 + 1)
                                     + F_out(nxz * 3 + 1));
            F_out(n   * 3 + 1) = avg;
            F_out(nx  * 3 + 1) = avg;
            F_out(nz  * 3 + 1) = avg;
            F_out(nxz * 3 + 1) = avg;
            done[n] = done[nx] = done[nz] = done[nxz] = 1;
            ++completeOrbits;
        } else {
            done[n] = 1;
            ++orphanOrbits;
        }
    }

    for (int n : loadedNodes) Fafter += F_out(n * 3 + 1);
    std::cout << "[LOAD] Symmetrize XZ: " << completeOrbits
              << " complete orbits, " << orphanOrbits
              << " orphan (unchanged)."
              << "  sum(F_y) before/after = " << Fbefore << " / " << Fafter
              << std::endl;
}

// -----------------------------------------------------------------------------
// symmetrizeLoadYAxial (file-local helper)
// -----------------------------------------------------------------------------
// Projects the Y-component of F_out onto the Y-axis rotational symmetry
// subspace by binning nodes by (y, r) and replacing each bin with its mean.
//
// Each loaded node is characterised by (y_n, r_n) where
//   r_n = √((x_n − c_x)² + (z_n − c_z)²)   (radial distance from mesh centroid).
// Nodes whose (y, r) values agree within tolerance τ = 1e-3·span form one bin
// (a constant-latitude ring at radius r). All nodes in a bin are assigned the
// bin mean of F_y:
//   F_y,n = (Σ_{m∈bin} F_y,m) / |bin|
// This enforces rotational symmetry about the Y-axis. Total Σ F_y is preserved
// exactly because bin-mean * bin-count = original bin sum.
static void symmetrizeLoadYAxial(VectorXd&               F_out,
                                 const std::vector<int>& loadedNodes,
                                 const FEAModel&         model,
                                 int                     nNodes)
{
    if (loadedNodes.empty()) return;

    double xmn = 1e9, xmx = -1e9, ymn = 1e9, ymx = -1e9, zmn = 1e9, zmx = -1e9;
    for (int i = 0; i < nNodes; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[i];
        if (p.x < xmn) xmn = p.x; if (p.x > xmx) xmx = p.x;
        if (p.y < ymn) ymn = p.y; if (p.y > ymx) ymx = p.y;
        if (p.z < zmn) zmn = p.z; if (p.z > zmx) zmx = p.z;
    }
    const double cx   = 0.5 * (xmn + xmx);
    const double cz   = 0.5 * (zmn + zmx);
    const double span = std::max({xmx - xmn, ymx - ymn, zmx - zmn});
    const double tol  = 1e-3 * span;

    const int N = static_cast<int>(loadedNodes.size());
    std::vector<double> y(N), r(N);
    for (int i = 0; i < N; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[loadedNodes[i]];
        const double dx = p.x - cx, dz = p.z - cz;
        y[i] = p.y;
        r[i] = std::sqrt(dx * dx + dz * dz);
    }

    double Fbefore = 0.0;
    for (int n : loadedNodes) Fbefore += F_out(n * 3 + 1);

    std::vector<char> done(N, 0);
    int numBins = 0;
    for (int i = 0; i < N; ++i) {
        if (done[i]) continue;
        std::vector<int> bin = { i };
        for (int j = i + 1; j < N; ++j) {
            if (done[j]) continue;
            if (std::abs(y[i] - y[j]) < tol && std::abs(r[i] - r[j]) < tol) {
                bin.push_back(j);
            }
        }
        double sum = 0.0;
        for (int k : bin) sum += F_out(loadedNodes[k] * 3 + 1);
        const double avg = sum / static_cast<double>(bin.size());
        for (int k : bin) {
            F_out(loadedNodes[k] * 3 + 1) = avg;
            done[k] = 1;
        }
        ++numBins;
    }

    double Fafter = 0.0;
    for (int n : loadedNodes) Fafter += F_out(n * 3 + 1);
    std::cout << "[LOAD] Symmetrize YAxial: " << N
              << " loaded nodes -> " << numBins << " (y,r) rings."
              << "  sum(F_y) before/after = " << Fbefore << " / " << Fafter
              << std::endl;
}

// -----------------------------------------------------------------------------
// new_TODO_04C: physical-units bridge (model space <-> real SI metres).
// The whole solver reads geometry from model.originalVolumetricPositions, so
// rescaling that one array to metres at the outermost solve entry makes every
// downstream computation (stiffness, loads, stress, fracture) physical without
// touching the assembly code. A saved copy restores the model-space coordinates
// exactly (no float drift across the fracture iteration loop). The guard makes
// the nested solveBrittleFracture -> solveLinearStatic call a no-op.
// -----------------------------------------------------------------------------
bool FEASolver::scaleGeometryToMeters(FEAModel& model) {
    if (m_geomInMeters) return false; // an outer solve already scaled the geometry
    m_geomScale = std::max(1e-12, static_cast<double>(model.modelToMM) * 0.001); // m / model-unit
    m_savedModelPositions = model.originalVolumetricPositions;                    // exact restore
    const float s = static_cast<float>(m_geomScale);
    for (auto& p : model.originalVolumetricPositions) p *= s;
    m_geomInMeters = true;
    return true;
}
void FEASolver::restoreGeometryToModel(FEAModel& model) {
    if (!m_geomInMeters) return;
    model.originalVolumetricPositions = m_savedModelPositions; // exact model-space restore
    m_savedModelPositions.clear();
    m_geomInMeters = false;
    // new_TODO_19E: appliedForces were recorded while the geometry was in
    // METRES; convert the arrow endpoints to model space here — the single
    // owner of the scale for both the linear and (outer) fracture solves.
    // (A conversion at the inner linear writeback missed the fracture path,
    // whose inner solves don't own the scaling — the arrow drew 30x too
    // small; caught by reading the showcase screenshot.)
    if (m_geomScale > 1e-30) {
        const float sInv = static_cast<float>(1.0 / m_geomScale);
        for (auto& fa : model.appliedForces) { fa.start *= sInv; fa.end *= sInv; }
        model.arrowSourceCount = SIZE_MAX;   // force overlay rebuild
    }
}

// -----------------------------------------------------------------------------
// FEASolver::solveLinearStatic
// -----------------------------------------------------------------------------
// Post-refactor architecture:
//
//   1. Build a concrete IMaterial (LinearElastic) from the solver's E/nu.
//   2. Build a std::vector<std::unique_ptr<IElement>> from the tet mesh.
//   3. The global assembly loop touches ONLY the IElement interface -- it no
//      longer knows that the elements are tet4, nor that the constitutive
//      model is linear elastic. Future hex8 / tendon / hyperelastic element
//      types can plug into the same loop unmodified.
//   4. Boundary conditions, penalty application and the sparse solve path
//      are preserved byte-identical to the pre-refactor baseline.
//
// Threading:
//   The previous version used `#pragma omp parallel for` over the element
//   assembly loop. Per the refactor directive, threading has been removed to
//   guarantee a deterministic, single-threaded execution path for upcoming
//   Newton-Raphson mathematical validations. The old pragma wrote to distinct
//   pre-sized triplet indices, so its removal does not perturb numerical
//   output; it only removes the nondeterministic ordering of *iteration*,
//   which was already irrelevant to the sparse summation.
// -----------------------------------------------------------------------------
bool FEASolver::solveLinearStatic(FEAModel& model, float visualScale,
                                   Eigen::VectorXd* U_out) {
    if (model.tetrahedra.empty()) {
        std::cout << "No tetrahedra available for FEA." << std::endl;
        return false;
    }

    // -------------------------------------------------------------------------
    // Optional Tet10 upgrade: generate mid-edge nodes before DOF sizing.
    // -------------------------------------------------------------------------
    if (useQuadraticElements && !model.hasQuadraticMesh) {
        model.generateMidEdgeNodes();
    }

    // new_TODO_04C: run the FE math in real SI metres (after mid-edge nodes exist
    // so they are scaled too). didScale is false when an outer solve (fracture)
    // already scaled the geometry, so we neither re-scale nor restore here.
    const bool didScale = scaleGeometryToMeters(model);
    const double invLm  = (m_geomScale != 0.0) ? (1.0 / m_geomScale) : 1.0; // model-unit / metre

    const int nElems = static_cast<int>(model.tetrahedra.size() / 4);
    const int nNodes = static_cast<int>(model.originalVolumetricPositions.size());
    const int nDOFs  = nNodes * 3;

    std::cout << "Assembling stiffness matrix for " << nNodes
              << " nodes and " << nElems
              << (useQuadraticElements ? " Tet10" : " Tet4")
              << " tetrahedra..." << std::endl;

    // -------------------------------------------------------------------------
    // Constitutive model (IMaterial). One shared instance for all elements in
    // the current linear-elastic baseline; when heterogeneous materials are
    // introduced this becomes a vector indexed per element.
    // -------------------------------------------------------------------------
    std::unique_ptr<IMaterial> materialOwned;
    if (useFdmAnisotropy && E_z > 0.0) {
        std::cout << "[FDM-ANISO] Transverse isotropic: E_p=" << youngsModulus*1e-9
                  << " GPa  E_z=" << E_z*1e-9 << " GPa  nu_p=" << poissonRatio
                  << "  nu_pz=" << nu_pz << "  G_pz=" << G_pz*1e-9 << " GPa" << std::endl;
        materialOwned = std::make_unique<TransverseIsotropicMaterial>(
            youngsModulus, E_z, poissonRatio, nu_pz, G_pz, buildAxis);
    } else {
        if (useFdmAnisotropy)
            std::cout << "[FDM-ANISO] E_z=0 — falling back to isotropic material." << std::endl;
        materialOwned = std::make_unique<LinearElastic>(youngsModulus, poissonRatio);
    }
    IMaterial& material = *materialOwned;

    // -------------------------------------------------------------------------
    // Build the element list via the IElement interface.
    // -------------------------------------------------------------------------
    std::vector<std::unique_ptr<IElement>> elements;
    if (useMultithreading) {
        // MT: pre-size and fill each slot in parallel. std::make_unique heap
        // allocation is thread-safe in modern libc++/msvcrt; each iteration
        // writes to a unique index.
        elements.resize(nElems);
        if (useQuadraticElements && model.hasQuadraticMesh) {
            #pragma omp parallel for schedule(static)
            for (int el = 0; el < nElems; ++el) {
                std::array<int, Tet10Element::kNumNodes> ids{};
                Eigen::Matrix<double, 3, Tet10Element::kNumNodes> coords;
                for (int i = 0; i < Tet10Element::kNumNodes; ++i) {
                    const int nid = static_cast<int>(
                        model.tetrahedraQuadratic[static_cast<size_t>(el) * 10 + i]);
                    ids[i] = nid;
                    const glm::vec3& p = model.originalVolumetricPositions[nid];
                    coords(0, i) = static_cast<double>(p.x);
                    coords(1, i) = static_cast<double>(p.y);
                    coords(2, i) = static_cast<double>(p.z);
                }
                elements[el] = std::make_unique<Tet10Element>(coords, ids, &material);
            }
        } else {
            #pragma omp parallel for schedule(static)
            for (int el = 0; el < nElems; ++el) {
                std::array<int, TetrahedralElement::kNumNodes> ids{};
                Eigen::Matrix<double, 3, 4> coords;
                for (int i = 0; i < TetrahedralElement::kNumNodes; ++i) {
                    const int nid = static_cast<int>(model.tetrahedra[el * 4 + i]);
                    ids[i] = nid;
                    const glm::vec3& p = model.originalVolumetricPositions[nid];
                    coords(0, i) = static_cast<double>(p.x);
                    coords(1, i) = static_cast<double>(p.y);
                    coords(2, i) = static_cast<double>(p.z);
                }
                elements[el] = std::make_unique<TetrahedralElement>(coords, ids, &material);
            }
        }
    } else {
        // --- ORIGINAL SERIAL PATH (preserved line-by-line) ----------------
        elements.reserve(nElems);

        if (useQuadraticElements && model.hasQuadraticMesh) {
            for (int el = 0; el < nElems; ++el) {
                std::array<int, Tet10Element::kNumNodes> ids{};
                Eigen::Matrix<double, 3, Tet10Element::kNumNodes> coords;
                for (int i = 0; i < Tet10Element::kNumNodes; ++i) {
                    const int nid = static_cast<int>(
                        model.tetrahedraQuadratic[static_cast<size_t>(el) * 10 + i]);
                    ids[i] = nid;
                    const glm::vec3& p = model.originalVolumetricPositions[nid];
                    coords(0, i) = static_cast<double>(p.x);
                    coords(1, i) = static_cast<double>(p.y);
                    coords(2, i) = static_cast<double>(p.z);
                }
                elements.emplace_back(
                    std::make_unique<Tet10Element>(coords, ids, &material));
            }
        } else {
            for (int el = 0; el < nElems; ++el) {
                std::array<int, TetrahedralElement::kNumNodes> ids{};
                Eigen::Matrix<double, 3, 4> coords;
                for (int i = 0; i < TetrahedralElement::kNumNodes; ++i) {
                    const int nid = static_cast<int>(model.tetrahedra[el * 4 + i]);
                    ids[i] = nid;
                    const glm::vec3& p = model.originalVolumetricPositions[nid];
                    coords(0, i) = static_cast<double>(p.x);
                    coords(1, i) = static_cast<double>(p.y);
                    coords(2, i) = static_cast<double>(p.z);
                }
                elements.emplace_back(
                    std::make_unique<TetrahedralElement>(coords, ids, &material));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Global assembly via IElement only. Deterministic, single-threaded.
    //
    // The triplet vector is reserved up-front so push_back never reallocates.
    // Within a single element, all local (row, col) pairs map to distinct
    // global (row, col) pairs (since tet nodes are distinct), therefore the
    // intra-element traversal order does not influence the final sparse
    // matrix values -- Eigen's setFromTriplets sums bucket contents in input
    // order, and element-level order is preserved.
    // -------------------------------------------------------------------------
    std::vector<Triplet<double>> tripletList;
    if (useMultithreading) {
        // MT: pre-sized disjoint write slots per element. Result is
        // numerically identical to serial (setFromTriplets sums duplicates
        // order-invariantly).
        const int    dofsPerElem = elements.empty() ? 12 : elements[0]->NumDOFs();
        const size_t perElem     = static_cast<size_t>(dofsPerElem)
                                 * static_cast<size_t>(dofsPerElem);
        tripletList.assign(static_cast<size_t>(nElems) * perElem,
                           Triplet<double>(0, 0, 0.0));
        std::cout << "[MT] Linear stiffness assembly using "
                  << omp_get_max_threads() << " OpenMP thread(s)." << std::endl;

        #pragma omp parallel
        {
            Eigen::MatrixXd  Ke_thr;
            std::vector<int> gdofs_thr;
            #pragma omp for schedule(dynamic)
            for (int el = 0; el < nElems; ++el) {
                // Respect fracture element mask: skip dead elements.
                if (!m_fractureAlive.empty() && !m_fractureAlive[el]) continue;

                const IElement* elem = elements[el].get();
                const int       nd   = elem->NumDOFs();

                elem->GetGlobalDOFs(gdofs_thr);
                elem->ComputeStiffnessMatrix(Ke_thr);

                const size_t base = static_cast<size_t>(el) * perElem;
                for (int r = 0; r < nd; ++r) {
                    const int row = gdofs_thr[r];
                    for (int c = 0; c < nd; ++c) {
                        tripletList[base + static_cast<size_t>(r) * dofsPerElem + c]
                            = Triplet<double>(row, gdofs_thr[c], Ke_thr(r, c));
                    }
                }
            }
        }
    } else {
        // --- ORIGINAL SERIAL PATH (preserved line-by-line) ----------------
        const int dofsPerElem = elements.empty() ? 12 : elements[0]->NumDOFs();
        tripletList.reserve(static_cast<size_t>(nElems)
                            * static_cast<size_t>(dofsPerElem * dofsPerElem));

        Eigen::MatrixXd  Ke;     // reused dense-output buffer at interface boundary
        std::vector<int> gdofs;  // reused global-DOF buffer

        for (int el = 0; el < nElems; ++el) {
            // Respect fracture element mask: skip dead elements.
            if (!m_fractureAlive.empty() && !m_fractureAlive[el]) continue;

            const IElement* elem = elements[el].get();
            const int nd = elem->NumDOFs();

            elem->GetGlobalDOFs(gdofs);
            elem->ComputeStiffnessMatrix(Ke);

            for (int r = 0; r < nd; ++r) {
                const int row = gdofs[r];
                for (int c = 0; c < nd; ++c) {
                    tripletList.emplace_back(row, gdofs[c], Ke(r, c));
                }
            }
        }
    }

    SparseMatrix<double> K(nDOFs, nDOFs);
    K.setFromTriplets(tripletList.begin(), tripletList.end());

    // IMMEDIATELY free the massive triplet array and element cache before the solver runs
    // to prevent System RAM from maxing out (e.g., Tet10 triplets can use >10GB for 1M elements).
    std::vector<Triplet<double>>().swap(tripletList);
    std::vector<std::unique_ptr<IElement>>().swap(elements);

    // Regularize island DOFs when running inside the fracture loop.
    // Killed elements leave their nodes with zero-diagonal rows in K, making K
    // singular and causing GPU Cholesky (and CPU LDLT) to fail. Setting K[i,i]=1.0
    // for these DOFs gives them U=0 (physically correct: no stiffness → no movement)
    // and keeps K positive definite throughout all fracture iterations.
    if (!m_fractureAlive.empty()) {
        const VectorXd diag = K.diagonal();
        for (int i = 0; i < nDOFs; ++i) {
            if (diag[i] == 0.0) K.coeffRef(i, i) = 1.0;
        }
    }

    VectorXd F = VectorXd::Zero(nDOFs);

    // -------------------------------------------------------------------------
    // Auto boundary conditions:
    //   SurfaceCompressionY : fix Y_min face, distribute -Y consistent load.
    //   PointForceZ         : fix Z_min face, apply a single -Z point force
    //                         at the node closest to the (x,y) centroid of
    //                         the Z_max face.
    // -------------------------------------------------------------------------
    double minX = 1e9, maxX = -1e9;
    double minY = 1e9, maxY = -1e9;
    double minZ = 1e9, maxZ = -1e9;
    for (int i = 0; i < nNodes; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[i];
        if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
        if (p.z < minZ) minZ = p.z; if (p.z > maxZ) maxZ = p.z;
    }

    std::vector<int> fixedNodes;
    std::vector<int> loadedNodes;
    std::vector<std::pair<int,int>> singleDofFixed; // (nodeIdx, dofComponent) for 3-2-1 minimal BCs
    int pointLoadNode = -1;

    if (loadType == LoadType::CantileverBendingZ) {
        const double tolX = (maxX - minX) * 0.02; // 2% tolerance on X_min/X_max
        std::vector<int> tipCandidates;
        double sumY = 0.0, sumZ = 0.0;
        int    tipCount = 0;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.x <= minX + tolX) fixedNodes.push_back(i);
            if (p.x >= maxX - tolX) {
                tipCandidates.push_back(i);
                sumY += p.y; sumZ += p.z; ++tipCount;
            }
        }
        if (tipCount > 0) {
            const double cy = sumY / tipCount, cz = sumZ / tipCount;
            double bestD = 1e300;
            for (int n : tipCandidates) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                const double dy = p.y - cy, dz = p.z - cz;
                const double d2 = dy*dy + dz*dz;
                if (d2 < bestD) { bestD = d2; pointLoadNode = n; }
            }
        }
        if (pointLoadNode >= 0) loadedNodes.push_back(pointLoadNode);
        std::cout << "BC: fixed " << fixedNodes.size() << " nodes at X_min. ";
        if (pointLoadNode >= 0) {
            const glm::vec3& p = model.originalVolumetricPositions[pointLoadNode];
            std::cout << "Point load at node " << pointLoadNode
                      << " pos=(" << p.x << "," << p.y << "," << p.z << ").";
        } else {
            std::cout << "No X_max node found.";
        }
        std::cout << std::endl;
    } else if (loadType == LoadType::PointForceZ) {
        const double tolZ = (maxZ - minZ) * 0.02; // 2% tolerance on Z_min/Z_max
        std::vector<int> topCandidates;
        double sumX = 0.0, sumY = 0.0;
        int    topCount = 0;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.z <= minZ + tolZ) fixedNodes.push_back(i);
            if (p.z >= maxZ - tolZ) {
                topCandidates.push_back(i);
                sumX += p.x; sumY += p.y; ++topCount;
            }
        }
        if (topCount > 0) {
            const double cx = sumX / topCount, cy = sumY / topCount;
            double bestD = 1e300;
            for (int n : topCandidates) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                const double dx = p.x - cx, dy = p.y - cy;
                const double d2 = dx*dx + dy*dy;
                if (d2 < bestD) { bestD = d2; pointLoadNode = n; }
            }
        }
        if (pointLoadNode >= 0) loadedNodes.push_back(pointLoadNode);
        std::cout << "BC: fixed " << fixedNodes.size() << " nodes at Z_min. ";
        if (pointLoadNode >= 0) {
            const glm::vec3& p = model.originalVolumetricPositions[pointLoadNode];
            std::cout << "Point load at node " << pointLoadNode
                      << " pos=(" << p.x << "," << p.y << "," << p.z << ").";
        } else {
            std::cout << "No Z_max node found.";
        }
        std::cout << std::endl;
    } else if (loadType == LoadType::TensionX ||
               loadType == LoadType::TensionY ||
               loadType == LoadType::TensionZ) {
        const int ta = (loadType == LoadType::TensionX) ? 0
                     : (loadType == LoadType::TensionY) ? 1 : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const double tolA = (bmax - bmin) * 0.02;

        std::vector<int> faceMin, faceMax;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) faceMin.push_back(i);
            if (coord >= bmax - tolA) faceMax.push_back(i);
        }
        loadedNodes = faceMax;

        // Mesh centroid for anchor node selection.
        double cx=0, cy=0, cz=0;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            cx+=p.x; cy+=p.y; cz+=p.z;
        }
        cx/=nNodes; cy/=nNodes; cz/=nNodes;

        // 3-2-1 minimal constraints: nodeA (all 3 DOF) + nodeB (1 DOF) + nodeC (1 DOF).
        const int pb = (ta+1)%3; // first perp axis
        const int pc = (ta+2)%3; // second perp axis

        auto getCoord = [&](int ni, int ax) -> double {
            const glm::vec3& p = model.originalVolumetricPositions[ni];
            return (ax==0)?(double)p.x:(ax==1)?(double)p.y:(double)p.z;
        };

        int nodeA=-1; double dA=1e300;
        int nodeB=-1; double dB=-1;
        int nodeC=-1; double dC=-1;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double d2=(p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy)+(p.z-cz)*(p.z-cz);
            if (d2 < dA) { dA=d2; nodeA=i; }
        }
        const double Apb = nodeA>=0 ? getCoord(nodeA, pb) : 0.0;
        const double Apc = nodeA>=0 ? getCoord(nodeA, pc) : 0.0;
        for (int i=0; i<nNodes; ++i) {
            if (i==nodeA) continue;
            const double offPb = std::abs(getCoord(i, pb) - Apb);
            if (offPb > dB) { dB=offPb; nodeB=i; }
        }
        for (int i=0; i<nNodes; ++i) {
            if (i==nodeA || i==nodeB) continue;
            const double offPc = std::abs(getCoord(i, pc) - Apc);
            if (offPc > dC) { dC=offPc; nodeC=i; }
        }

        if (nodeA >= 0) fixedNodes.push_back(nodeA);
        if (nodeB >= 0) singleDofFixed.push_back({nodeB, pc});
        if (nodeC >= 0) singleDofFixed.push_back({nodeC, pb});

        std::cout << "BC [Tension" << "XYZ"[ta] << "]: anchor=" << nodeA
                  << "  rot-nodes=" << nodeB << "," << nodeC
                  << "  faces: -=" << faceMin.size() << " +=" << faceMax.size() << " nodes." << std::endl;
    } else if (loadType == LoadType::FacePull ||
               loadType == LoadType::FaceBend) {
        // new_TODO_19C: clamp ALL DOF on the min-face of faceAxis (statically
        // well-posed — no rigid-body modes, deliberately avoiding the Tension*
        // single-node-anchor blowup documented 2026-07-02), load the max-face.
        const int ta = (faceAxis >= 0 && faceAxis <= 2) ? faceAxis : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        // Tight tolerance: face nodes sit exactly on the bounding planes for
        // both slab meshes and TetGen box facets; 0.5% must stay below the
        // slab-boundary spacing so the clamp grabs ONE plane of nodes.
        const double tolA = (bmax - bmin) * 0.005;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) fixedNodes.push_back(i);
            else if (coord >= bmax - tolA) loadedNodes.push_back(i);
        }
        std::cout << "BC [Face" << (loadType == LoadType::FacePull ? "Pull" : "Bend")
                  << ' ' << "XYZ"[ta] << "]: clamped " << fixedNodes.size()
                  << " nodes at min-face, loading " << loadedNodes.size()
                  << " nodes at max-face." << std::endl;
    } else if (loadType == LoadType::ThreePointBend) {
        // new_TODO_19D bending: geometry is in METRES here (scale-solve path).
        const int a = (faceAxis >= 0 && faceAxis <= 2) ? faceAxis : 2;
        const int d = bendDir;
        const int e = 3 - a - d;
        if (d < 0 || d > 2 || d == a) {
            std::cout << "ThreePointBend: invalid bendDir/axis" << std::endl;
            return false;
        }
        auto coordOf = [&](int ni, int ax) -> double {
            const glm::vec3& p = model.originalVolumetricPositions[ni];
            return (ax == 0) ? (double)p.x : (ax == 1) ? (double)p.y : (double)p.z;
        };
        double aMin = 1e300, aMax = -1e300, dMin = 1e300, dMax = -1e300;
        for (int i = 0; i < nNodes; ++i) {
            aMin = std::min(aMin, coordOf(i, a)); aMax = std::max(aMax, coordOf(i, a));
            dMin = std::min(dMin, coordOf(i, d)); dMax = std::max(dMax, coordOf(i, d));
        }
        const double aC = 0.5 * (aMin + aMax);
        const double halfSpan = 0.5 * bendSpanMM * 1e-3;
        // Surface bands: bottom/top 15% of the section depth (covers the curved
        // shaft surface without grabbing core nodes).
        const double surfTol = 0.15 * (dMax - dMin);
        auto band = [&](double target, bool bottom) -> std::vector<int> {
            // Snap to the nearest surface NODE LINE, then take a narrow
            // ±0.6 mm strip around it (a roller/nose line). The earlier
            // width-doubling search widened to several mm on sparse-ring
            // meshes and produced 90+ node shelf supports — partial clamps
            // that choked the mid-span moment (standing shaft silent at 2x
            // its F x L anchor; caught 2026-07-03 by exactly that oracle).
            auto onSurf = [&](int i) {
                const double dc = coordOf(i, d);
                return bottom ? (dc <= dMin + surfTol) : (dc >= dMax - surfTol);
            };
            double bestDa = 1e300, lineA = target;
            for (int i = 0; i < nNodes; ++i) {
                if (!onSurf(i)) continue;
                const double da = std::abs(coordOf(i, a) - target);
                if (da < bestDa) { bestDa = da; lineA = coordOf(i, a); }
            }
            std::vector<int> out;
            for (int i = 0; i < nNodes; ++i)
                if (onSurf(i) && std::abs(coordOf(i, a) - lineA) <= 0.6e-3)
                    out.push_back(i);
            return out;
        };
        std::vector<int> supA = band(aC - halfSpan, true);
        std::vector<int> supB = band(aC + halfSpan, true);
        loadedNodes           = band(aC, false);
        if (supA.empty() || supB.empty() || loadedNodes.empty()) {
            std::cout << "ThreePointBend: empty support/load band (span too "
                         "large for the mesh?)" << std::endl;
            return false;
        }
        // Rollers: support nodes pinned in d and e; support A also pins the
        // beam axis (removes the last rigid mode). Slightly stiffer than ideal
        // rollers, identical for both test articles -> ordering-invariant.
        for (int n : supA) {
            singleDofFixed.push_back({n, d});
            singleDofFixed.push_back({n, e});
            singleDofFixed.push_back({n, a});
        }
        for (int n : supB) {
            singleDofFixed.push_back({n, d});
            singleDofFixed.push_back({n, e});
        }
        double mA = 0.0, mB = 0.0;
        for (int n : supA) mA += coordOf(n, a);
        for (int n : supB) mB += coordOf(n, a);
        mA /= supA.size(); mB /= supB.size();
        lastEffectiveSpanMM = (mB - mA) * 1000.0;
        std::cout << "BC [3PointBend axis=" << "XYZ"[a] << " load=-" << "XYZ"[d]
                  << "]: supports " << supA.size() << "+" << supB.size()
                  << " nodes at " << mA * 1000.0 << "/" << mB * 1000.0
                  << " mm (span req " << bendSpanMM << " -> eff "
                  << lastEffectiveSpanMM << " mm), load band "
                  << loadedNodes.size() << " nodes at mid-span top." << std::endl;
    } else {
        const double tolY = (maxY - minY) * 0.05;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.y <= minY + tolY) fixedNodes.push_back(i);
            if (p.y >= maxY - tolY) loadedNodes.push_back(i);
        }
        std::cout << "BC: Fixed " << fixedNodes.size() << " nodes at Y_min. Loaded "
                  << loadedNodes.size() << " nodes at Y_max." << std::endl;
    }

    model.appliedForces.clear();
    model.nodalForceMagnitudes.assign(nNodes, 0.0f);
    model.nodalDisplacementMagnitudes.assign(nNodes, 0.0f);
    model.showAppliedForceField = false;
    model.totalAppliedForce = static_cast<float>(std::abs(forceMagnitude));

    if (loadedNodes.empty()) {
        std::cout << "No load nodes detected for automatic force application." << std::endl;
        return false;
    }

    // -------------------------------------------------------------------------
    // Build external force vector F.
    // -------------------------------------------------------------------------
    if (loadType == LoadType::CantileverBendingZ) {
        // Transverse bending load: -Z force at X_max tip
        F(pointLoadNode * 3 + 2) = -forceMagnitude;
        model.nodalForceMagnitudes[pointLoadNode] = static_cast<float>(std::abs(forceMagnitude));
        model.appliedForcePerNode = static_cast<float>(std::abs(forceMagnitude));

        const glm::vec3 pos = model.originalVolumetricPositions[pointLoadNode];
        const float arrowLen = static_cast<float>(maxX - minX) * 0.25f;
        model.appliedForces.push_back({ pos + glm::vec3(0, 0, arrowLen), pos });

        std::cout << "Applied transverse point force " << -forceMagnitude
                  << " N (-Z) at node " << pointLoadNode << "." << std::endl;
    } else if (loadType == LoadType::PointForceZ) {
        // Single concentrated -Z force at pointLoadNode. No triangulation
        // noise: the load vector has exactly one non-zero entry.
        F(pointLoadNode * 3 + 2) = -forceMagnitude;
        model.nodalForceMagnitudes[pointLoadNode]
            = static_cast<float>(std::abs(forceMagnitude));
        model.appliedForcePerNode = static_cast<float>(std::abs(forceMagnitude));

        const glm::vec3 pos = model.originalVolumetricPositions[pointLoadNode];
        const float arrowLen = static_cast<float>(maxZ - minZ) * 0.25f;
        // Arrow shows force origin (external source) -> application point:
        // tail is offset in +Z, tip is at the node, so the visual direction
        // matches the physical -Z push.
        model.appliedForces.push_back({ pos + glm::vec3(0, 0, arrowLen), pos });

        std::cout << "Applied point force " << -forceMagnitude
                  << " N (-Z) at node " << pointLoadNode << "." << std::endl;
    } else if (loadType == LoadType::TensionX ||
               loadType == LoadType::TensionY ||
               loadType == LoadType::TensionZ) {
        const int ta = (loadType == LoadType::TensionX) ? 0
                     : (loadType == LoadType::TensionY) ? 1 : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const double tolA = (bmax - bmin) * 0.02;

        std::vector<int> faceMinT, faceMaxT;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) faceMinT.push_back(i);
            if (coord >= bmax - tolA) faceMaxT.push_back(i);
        }

        const double fMax = faceMaxT.empty() ? 0.0 : forceMagnitude / static_cast<double>(faceMaxT.size());
        const double fMin = faceMinT.empty() ? 0.0 : forceMagnitude / static_cast<double>(faceMinT.size());
        for (int n : faceMaxT) {
            F(n*3+ta) += fMax;
            model.nodalForceMagnitudes[n] = static_cast<float>(fMax);
        }
        for (int n : faceMinT) {
            F(n*3+ta) -= fMin;
            model.nodalForceMagnitudes[n] = static_cast<float>(fMin);
        }
        model.appliedForcePerNode = static_cast<float>(fMax);

        const float arrowLen = static_cast<float>(bmax - bmin) * 0.2f;
        glm::vec3 arrowDir(0,0,0); ((float*)(&arrowDir))[ta] = arrowLen;
        for (int n : faceMaxT) {
            glm::vec3 pos = model.originalVolumetricPositions[n];
            model.appliedForces.push_back({ pos, pos + arrowDir });
        }
        for (int n : faceMinT) {
            glm::vec3 pos = model.originalVolumetricPositions[n];
            model.appliedForces.push_back({ pos, pos - arrowDir });
        }
        std::cout << "Tension" << "XYZ"[ta] << ": distributed " << forceMagnitude
                  << " N over " << faceMaxT.size() << "(+)/" << faceMinT.size() << "(-) nodes" << std::endl;
    } else if (loadType == LoadType::FacePull ||
               loadType == LoadType::FaceBend) {
        // new_TODO_19C: equal nodal split of the total force over the loaded
        // face. Equal split on Tet10 corner+midside nodes is not a consistent
        // load vector, but the SAME scheme is used for every mesh, so the
        // standing-vs-lying ORDERING comparison is scheme-invariant; the
        // resultant is exact by construction (asserted via appliedForceN).
        const int ta = (faceAxis >= 0 && faceAxis <= 2) ? faceAxis : 2;
        int fd = (loadType == LoadType::FacePull) ? ta : bendDir;
        if (fd < 0 || fd > 2 || (loadType == LoadType::FaceBend && fd == ta)) {
            std::cout << "FaceBend: invalid bendDir " << bendDir
                      << " (must be 0..2 and != faceAxis)" << std::endl;
            return false;
        }
        // Distribution set: on quadratic meshes load ONLY the mid-edge nodes
        // of the face — the consistent load vector for uniform traction on a
        // quadratic triangle is corner 0 / midside 1/3, and equal shares on
        // corners produce a local bulge that inflates maxDispMM ~60% (caught
        // by the PL/AE oracle scenario 2026-07-02). Tet4 meshes use all face
        // nodes (equal split IS the consistent rule there).
        std::vector<int> distNodes;
        if (useQuadraticElements) {
            for (int n : loadedNodes)
                if (n >= model.nLinearNodes) distNodes.push_back(n);
        }
        if (distNodes.empty()) distNodes = loadedNodes;
        const double share = forceMagnitude / static_cast<double>(distNodes.size());
        double appliedSum = 0.0;
        for (int n : distNodes) {
            F(n * 3 + fd) += share;
            appliedSum += share;
            model.nodalForceMagnitudes[n] = static_cast<float>(std::abs(share));
        }
        lastAppliedForceN = appliedSum;   // exact resultant along the load axis
        model.appliedForcePerNode = static_cast<float>(std::abs(share));

        // One resultant arrow at the loaded-face centroid (tail offset against
        // the force direction so the arrow points the way the force acts) —
        // 19E renders these; anchor/direction come from the SAME node set the
        // solver loads, never a separate guess.
        glm::vec3 cent(0.0f);
        for (int n : loadedNodes) cent += model.originalVolumetricPositions[n];
        cent /= static_cast<float>(loadedNodes.size());
        const double bminA = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmaxA = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const float arrowLen = static_cast<float>(bmaxA - bminA) * 0.25f;
        glm::vec3 dirv(0.0f);
        ((float*)(&dirv))[fd] = (forceMagnitude >= 0.0 ? 1.0f : -1.0f);
        if (loadType == LoadType::FacePull)   // tension: arrow points OUTWARD
            model.appliedForces.push_back({ cent, cent + dirv * arrowLen });
        else                                   // transverse push: tip at the face
            model.appliedForces.push_back({ cent - dirv * arrowLen, cent });

        std::cout << "Face" << (loadType == LoadType::FacePull ? "Pull" : "Bend")
                  << ": distributed " << forceMagnitude << " N along "
                  << (forceMagnitude >= 0.0 ? '+' : '-') << "XYZ"[fd]
                  << " over " << loadedNodes.size() << " max-face nodes"
                  << std::endl;
    } else if (loadType == LoadType::ThreePointBend) {
        const int d = bendDir;
        // Same consistent-load rule as FacePull: quadratic meshes load only
        // mid-edge nodes (equal corner shares create local artifacts).
        std::vector<int> distNodes;
        if (useQuadraticElements) {
            for (int n : loadedNodes)
                if (n >= model.nLinearNodes) distNodes.push_back(n);
        }
        if (distNodes.empty()) distNodes = loadedNodes;
        const double share = forceMagnitude / static_cast<double>(distNodes.size());
        double appliedSum = 0.0;
        for (int n : distNodes) {
            F(n * 3 + d) -= share;                      // push toward -d
            appliedSum += share;
            model.nodalForceMagnitudes[n] = static_cast<float>(std::abs(share));
        }
        lastAppliedForceN = appliedSum;                  // magnitude convention
        model.appliedForcePerNode = static_cast<float>(std::abs(share));

        glm::vec3 cent(0.0f);
        for (int n : loadedNodes) cent += model.originalVolumetricPositions[n];
        cent /= static_cast<float>(loadedNodes.size());
        glm::vec3 dirv(0.0f);
        ((float*)(&dirv))[d] = -1.0f;
        const float arrowLen = 0.25f * static_cast<float>(
            (d == 0 ? maxX - minX : d == 1 ? maxY - minY : maxZ - minZ));
        model.appliedForces.push_back({ cent - dirv * arrowLen, cent });

        std::cout << "3PointBend: " << forceMagnitude << " N toward -"
                  << "XYZ"[d] << " over " << distNodes.size()
                  << " mid-span nodes" << std::endl;
    } else {
        // Legacy surface compression: consistent nodal loads from surface-
        // triangle integration. Correctly distributes force by tributary area.
        std::vector<double> areaShare;
        double patchArea = 0.0;
        int    triCount  = 0;
        buildConsistentLoadVector(model, loadedNodes, nNodes, forceMagnitude,
                                  F, areaShare, patchArea, triCount);

        switch (loadSymmetry) {
            case LoadSymmetry::XZReflection:
                symmetrizeLoadXZ(F, loadedNodes, model, nNodes);
                break;
            case LoadSymmetry::YAxial:
                symmetrizeLoadYAxial(F, loadedNodes, model, nNodes);
                break;
            case LoadSymmetry::None:
            default:
                break;
        }

        const float arrowLength = static_cast<float>(maxY - minY) * 0.15f;
        double maxNodalF  = 0.0;
        double sumNodalF  = 0.0;
        int    countNodalF = 0;
        for (int node : loadedNodes) {
            const double fy = std::abs(F(node * 3 + 1));
            if (fy > maxNodalF) maxNodalF = fy;
            sumNodalF += fy;
            ++countNodalF;
        }
        model.appliedForcePerNode = countNodalF > 0
            ? static_cast<float>(sumNodalF / countNodalF)
            : 0.0f;

        for (int node : loadedNodes) {
            const double fy = std::abs(F(node * 3 + 1));
            glm::vec3 pos = model.originalVolumetricPositions[node];
            const float scaled = maxNodalF > 0.0
                ? arrowLength * static_cast<float>(fy / maxNodalF)
                : arrowLength;
            model.appliedForces.push_back({ pos + glm::vec3(0, scaled, 0), pos });
            model.nodalForceMagnitudes[node] = static_cast<float>(fy);
        }

        std::cout << "Applied consistent nodal loads over " << triCount
                  << " boundary triangles (total area " << patchArea << ")." << std::endl;
    }

    // ---- new_TODO_19C-b: interlayer weld ties (new_TODO_06 registry) ----
    // Toolpath slabs have independent node rings per slab; each tie couples a
    // slab-s top node to its containing triangle on slab s+1's bottom ring via
    // penalty springs on the constraint u_top - sum(bary_i * u_bot_i) per DOF:
    //   K(top,top)+=kt, K(top,bi)-=kt*w_i, K(bi,bj)+=kt*w_i*w_j.
    // kt = alpha * E_z * A_tie / t (06 spec, alpha~100 => ~1% added stack
    // compliance, folded into the analytic pull oracle). Geometry is in SI
    // metres here (scaled solve path), so areas/thickness convert from mm.
    if (model.hasLayerStack() && !model.layers->interfaces.empty()) {
        const double Etie = (useFdmAnisotropy && E_z > 0.0) ? E_z : youngsModulus;
        std::vector<Eigen::Triplet<double>> tieTrip;
        long nTies = 0;
        for (const auto& wi : model.layers->interfaces) {
            if (wi.ties.empty()) continue;
            const double A_m2 = (double)wi.areaMM2 * 1e-6 /
                                std::max<size_t>(1, wi.ties.size());
            const double t_m  = std::max(1e-9, (double)wi.thicknessMM * 1e-3);
            const double kt   = (double)model.layers->tieAlpha * Etie * A_m2 / t_m;
            for (const auto& tie : wi.ties) {
                if (tie.released) continue;   // new_TODO_06 delamination hook
                ++nTies;
                const int top = static_cast<int>(tie.nodeTop);
                for (int d = 0; d < 3; ++d) {
                    tieTrip.emplace_back(top * 3 + d, top * 3 + d, kt);
                    for (int i = 0; i < 3; ++i) {
                        const int bi = static_cast<int>(tie.triBottom[i]) * 3 + d;
                        const double wi_ = tie.bary[i];
                        tieTrip.emplace_back(top * 3 + d, bi, -kt * wi_);
                        tieTrip.emplace_back(bi, top * 3 + d, -kt * wi_);
                        for (int j = 0; j < 3; ++j)
                            tieTrip.emplace_back(
                                bi, static_cast<int>(tie.triBottom[j]) * 3 + d,
                                kt * wi_ * tie.bary[j]);
                    }
                }
            }
        }
        if (!tieTrip.empty()) {
            Eigen::SparseMatrix<double> T(K.rows(), K.cols());
            T.setFromTriplets(tieTrip.begin(), tieTrip.end());
            K += T;
            std::cout << "[TIES] assembled " << nTies << " weld ties over "
                      << model.layers->interfaces.size() << " interfaces\n";
        }
    }

    // Apply fixed constraints via penalty method.
    double maxDiagonal = K.diagonal().maxCoeff();
    double penalty = maxDiagonal * 1e7;

    for (int node : fixedNodes) {
        K.coeffRef(node * 3 + 0, node * 3 + 0) += penalty;
        K.coeffRef(node * 3 + 1, node * 3 + 1) += penalty;
        K.coeffRef(node * 3 + 2, node * 3 + 2) += penalty;
        F(node * 3 + 0) = 0.0;
        F(node * 3 + 1) = 0.0;
        F(node * 3 + 2) = 0.0;
    }
    for (auto& [node, dof] : singleDofFixed) {
        K.coeffRef(node*3+dof, node*3+dof) += penalty;
        F(node*3+dof) = 0.0;
    }

    VectorXd U;

    // ---------------------------------------------------------------
    // GPU path: cuSOLVER sparse Cholesky on GPU
    // ---------------------------------------------------------------
#ifdef HAS_CUDA
    if (useGPU) {
        std::cout << "[GPU] Attempting GPU solve via cuSOLVER..." << std::endl;
        std::cout << "[GPU] Device: " << cuda_solver::getGpuInfo() << std::endl;

        // cuSOLVER needs CSR. Eigen SparseMatrix is CSC by default.
        // For a symmetric matrix K, CSC(K) == CSR(K^T) == CSR(K).
        // So we can pass the raw arrays directly. But we must ensure
        // K is compressed first.
        Eigen::SparseMatrix<double> Kcsr = K;  // copy (or makeCompressed)
        Kcsr.makeCompressed();

        auto tGpu0 = std::chrono::high_resolution_clock::now();
        bool gpuOK = cuda_solver::solveOnGpu(Kcsr, F, U);
        auto tGpu1 = std::chrono::high_resolution_clock::now();
        double gpuMs = std::chrono::duration<double, std::milli>(tGpu1 - tGpu0).count();

        if (gpuOK) {
            std::cout << "[GPU] Solve succeeded in " << gpuMs << " ms" << std::endl;
        } else {
            std::cout << "[GPU] GPU solve failed. Falling back to CPU..." << std::endl;
            useGPU = false; // fall through to CPU path below
        }
    }

    if (!useGPU || U.size() != K.rows())
#endif
    if (useMultithreading) {
        // MT: iterative CG with Jacobi preconditioner. Eigen parallelizes
        // SpMV and dot products via OpenMP, so iterations keep all cores
        // busy. Jacobi (diagonal) preconditioning is a natural fit for our
        // penalty-BC matrix because the huge diagonal entries at fixed
        // nodes get scaled to ~1, drastically improving conditioning.
        // Falls back to serial SimplicialLDLT if CG fails to converge.
        Eigen::setNbThreads(omp_get_max_threads());
        const int maxIters = std::max(nDOFs, 5000);
        const double cgTol = 1e-10;
        std::cout << "[MT] Solving via ConjugateGradient<Jacobi> on "
                  << omp_get_max_threads() << " thread(s)  (maxIters="
                  << maxIters << ", tol=" << cgTol << ")..." << std::endl;

        auto tSolve0 = std::chrono::high_resolution_clock::now();
        ConjugateGradient<SparseMatrix<double>, Lower|Upper,
                          DiagonalPreconditioner<double>> cg;
        cg.setMaxIterations(maxIters);
        cg.setTolerance(cgTol);
        cg.compute(K);

        bool cgOK = false;
        if (cg.info() == Success) {
            U = cg.solve(F);
            cgOK = (cg.info() == Success);
        }
        auto tSolve1 = std::chrono::high_resolution_clock::now();
        const double solveMs = std::chrono::duration<double, std::milli>(
                                   tSolve1 - tSolve0).count();

        if (cgOK) {
            std::cout << "[MT] CG converged in " << cg.iterations()
                      << " iters, est. rel-error = " << cg.error()
                      << "  [" << solveMs << " ms]" << std::endl;
        } else {
            std::cout << "[MT] CG did not converge (iters=" << cg.iterations()
                      << ", err=" << cg.error()
                      << "). Falling back to serial SimplicialLDLT..."
                      << std::endl;
            SimplicialLDLT<SparseMatrix<double>> ldlt;
            ldlt.compute(K);
            if (ldlt.info() != Success) {
                std::cout << "Matrix decomposition failed!" << std::endl;
                if (didScale) restoreGeometryToModel(model);
                return false;
            }
            U = ldlt.solve(F);
            if (ldlt.info() != Success) {
                std::cout << "Solving failed!" << std::endl;
                if (didScale) restoreGeometryToModel(model);
                return false;
            }
        }
    } else {
        // --- ORIGINAL SERIAL PATH (preserved line-by-line; only the
        //     `VectorXd U` declaration is hoisted above this branch) -----
        std::cout << "Solving linear system using SimplicialLDLT..." << std::endl;
        SimplicialLDLT<SparseMatrix<double>> solver;
        solver.compute(K);
        if (solver.info() != Success) {
            std::cout << "Matrix decomposition failed!" << std::endl;
            if (didScale) restoreGeometryToModel(model);
            return false;
        }

        U = solver.solve(F);
        if (solver.info() != Success) {
            std::cout << "Solving failed!" << std::endl;
            if (didScale) restoreGeometryToModel(model);
            return false;
        }
    }

    std::cout << "\n================ BENCHMARK RESULTS ================\n";
    if (loadType == LoadType::CantileverBendingZ) {
        double L = maxX - minX;
        double b = maxY - minY; // width (Y-axis)
        double h = maxZ - minZ; // height (Z-axis, parallel to force)
        double I = (b * h * h * h) / 12.0;
        double theoretical = (forceMagnitude * L * L * L) / (3.0 * youngsModulus * I);
        double actual = U.cwiseAbs().maxCoeff();
        double error = std::abs(actual - theoretical) / theoretical * 100.0;
        int nElements = model.hasQuadraticMesh ? static_cast<int>(model.tetrahedraQuadratic.size() / 10) 
                                               : static_cast<int>(model.tetrahedra.size() / 4);

        std::cout << "- Mesh Quality : " << model.params.tetQuality << "\n"
                  << "- Max Volume   : " << model.params.maxVolPercent << " %\n"
                  << "- Nodes        : " << model.originalVolumetricPositions.size() << "\n"
                  << "- Elements     : " << nElements << "\n"
                  << "- Analytical   : " << theoretical << " m\n"
                  << "- FEA Output   : " << actual << " m\n"
                  << "- Error        : " << error << " %\n";
    } else {
        std::cout << "Solved! Max displacement: " << U.cwiseAbs().maxCoeff() << std::endl;
    }
    std::cout << "===================================================\n" << std::endl;

    if (U_out) *U_out = U;

    // new_TODO_19D: clamp-reaction recovery for the face presets. With the
    // penalty BC method the reaction at a clamped DOF is penalty * u (the
    // spring force that holds the node at ~0). Summed along the load axis it
    // must balance the applied resultant; the residual also carries the
    // penalty method's own roundoff, reported rather than hidden.
    if (loadType == LoadType::FacePull || loadType == LoadType::FaceBend) {
        const int fd = (loadType == LoadType::FacePull)
                           ? ((faceAxis >= 0 && faceAxis <= 2) ? faceAxis : 2)
                           : bendDir;
        double rsum = 0.0;
        for (int n : fixedNodes) rsum += penalty * U(n * 3 + fd);
        lastReactionSumN = rsum;
    } else if (loadType == LoadType::ThreePointBend) {
        // Support reactions live on singleDofFixed entries along the load axis.
        double rsum = 0.0;
        for (auto& [node, dof] : singleDofFixed)
            if (dof == bendDir) rsum += penalty * U(node * 3 + dof);
        lastReactionSumN = std::abs(rsum);   // magnitude convention (load is -d)
    }

    // new_TODO_04C: U is now the PHYSICAL displacement in metres. Record the real
    // max nodal displacement (mm) for the report, then convert the (exaggerated)
    // displacement back to model space so the deformed overlay renders in the
    // 3-unit render frame the camera expects.
    model.physicalMaxDispMM = static_cast<float>(U.cwiseAbs().maxCoeff() * 1000.0);

    model.deformedPositions.resize(nNodes);
    model.nodalDispMM.resize(nNodes);

    // Per-node displacement update is embarrassingly parallel: each iteration
    // touches a unique index in deformedPositions and nodalDisplacementMagnitudes.
    // The `if(useMultithreading)` OMP clause collapses to serial execution in
    // the master thread when the flag is off, so serial behaviour is unchanged.
    #pragma omp parallel for schedule(static) if(useMultithreading)
    for (int i = 0; i < nNodes; ++i) {
        // TRUE physical displacement (mm), no exaggeration — probe truth
        // source (new_TODO_19C; probes previously read the x10 render overlay).
        model.nodalDispMM[i] = glm::vec3(
            static_cast<float>(U(i * 3 + 0) * 1000.0),
            static_cast<float>(U(i * 3 + 1) * 1000.0),
            static_cast<float>(U(i * 3 + 2) * 1000.0));
        glm::vec3 dispMeters(
            static_cast<float>(U(i * 3 + 0) * visualScale),
            static_cast<float>(U(i * 3 + 1) * visualScale),
            static_cast<float>(U(i * 3 + 2) * visualScale)
        );
        glm::vec3 dispModel = dispMeters * static_cast<float>(invLm); // metres -> model units
        // originalVolumetricPositions is in metres here; *invLm returns it to model space.
        model.deformedPositions[i] =
            model.originalVolumetricPositions[i] * static_cast<float>(invLm) + dispModel;
        model.nodalDisplacementMagnitudes[i] = glm::length(dispModel);
    }

    model.hasDeformation = true;
    model.showDeformedMesh = true;
    model.needsUpdate = true;
    // Restore model-space coordinates BEFORE buildBuffers so the rendered mesh and
    // any later geometry reads see the original 3-unit frame (no-op if an outer
    // fracture solve owns the scaling).
    if (didScale) restoreGeometryToModel(model);   // (also converts appliedForces)
    model.buildBuffers();

    std::cout << "Updated mesh with deformations (scaled " << visualScale << "x)" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// FEASolver::solveNonlinearStatic
// -----------------------------------------------------------------------------
// Incremental load-stepping Newton-Raphson driver.
//
// Algorithm (per load step s = 1..N, lambda = s/N):
//
//   F_ext_s = lambda * F_ext_full
//   for iter = 0, 1, 2, ...:
//       F_int = assemble_internal_forces(u)     (via IElement::ComputeInternalForceVector)
//       R     = F_ext_s - F_int
//       R(fixed DOFs) = 0                       (Dirichlet enforced on residual)
//       print ||R||_2
//       if iter >= 1 and ||R||_2 < tol: break   (converged; iter 0 is never converged)
//       K     = assemble_tangent(u)             (via IElement::ComputeTangentStiffness)
//       K    += penalty on fixed DOFs           (same penalty method as linear path)
//       du    = solve(K, R)
//       u    += du
//
// For a linear-elastic baseline at load step 1:
//   iter 0: F_int = 0, R = F_ext,  ||R|| = ||F_ext||      (not converged)
//           Solve K du = F_ext => du = K^-1 F_ext; u <- du.
//   iter 1: F_int = K*u = F_ext, R = 0, ||R|| = 0 < tol.  CONVERGED in 1 iter.
//
// This is the validation protocol: the NR loop handles a linear problem in
// exactly one iteration while printing ||R||_2 at step 0 and step 1.
// -----------------------------------------------------------------------------
bool FEASolver::solveNonlinearStatic(FEAModel& model, float visualScale, NRParams params) {
    if (model.tetrahedra.empty()) {
        std::cout << "No tetrahedra available for FEA." << std::endl;
        return false;
    }
    if (params.numLoadSteps < 1)  params.numLoadSteps  = 1;
    if (params.maxIterations < 1) params.maxIterations = 1;

    // ---- Optional Tet10 upgrade ------------------------------------------------
    if (useQuadraticElements && !model.hasQuadraticMesh) {
        model.generateMidEdgeNodes();
    }

    const int nElems = static_cast<int>(model.tetrahedra.size() / 4);
    const int nNodes = static_cast<int>(model.originalVolumetricPositions.size());
    const int nDOFs  = nNodes * 3;

    std::cout << "[NR] Assembling problem for " << nNodes
              << " nodes / " << nElems
              << (useQuadraticElements ? " Tet10" : " Tet4")
              << " tetrahedra / "
              << params.numLoadSteps << " load step(s)." << std::endl;

    // ---- Constitutive + elements (reference configuration) --------------------
    LinearElastic material(youngsModulus, poissonRatio);

    std::vector<std::unique_ptr<IElement>> elements;
    elements.reserve(nElems);

    if (useQuadraticElements && model.hasQuadraticMesh) {
        for (int el = 0; el < nElems; ++el) {
            std::array<int, Tet10Element::kNumNodes> ids{};
            Eigen::Matrix<double, 3, Tet10Element::kNumNodes> coords;
            for (int i = 0; i < Tet10Element::kNumNodes; ++i) {
                const int nid = static_cast<int>(
                    model.tetrahedraQuadratic[static_cast<size_t>(el) * 10 + i]);
                ids[i] = nid;
                const glm::vec3& p = model.originalVolumetricPositions[nid];
                coords(0, i) = static_cast<double>(p.x);
                coords(1, i) = static_cast<double>(p.y);
                coords(2, i) = static_cast<double>(p.z);
            }
            elements.emplace_back(
                std::make_unique<Tet10Element>(coords, ids, &material));
        }
    } else {
        for (int el = 0; el < nElems; ++el) {
            std::array<int, TetrahedralElement::kNumNodes> ids{};
            Eigen::Matrix<double, 3, 4> coords;
            for (int i = 0; i < TetrahedralElement::kNumNodes; ++i) {
                const int nid = static_cast<int>(model.tetrahedra[el * 4 + i]);
                ids[i] = nid;
                const glm::vec3& p = model.originalVolumetricPositions[nid];
                coords(0, i) = static_cast<double>(p.x);
                coords(1, i) = static_cast<double>(p.y);
                coords(2, i) = static_cast<double>(p.z);
            }
            elements.emplace_back(
                std::make_unique<TetrahedralElement>(coords, ids, &material));
        }
    }

    // ---- Auto boundary conditions (branch on loadType) ------------------------
    double minX = 1e9, maxX = -1e9;
    double minY = 1e9, maxY = -1e9;
    double minZ = 1e9, maxZ = -1e9;
    for (int i = 0; i < nNodes; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[i];
        if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
        if (p.z < minZ) minZ = p.z; if (p.z > maxZ) maxZ = p.z;
    }

    std::vector<int> fixedNodes, loadedNodes;
    std::vector<std::pair<int,int>> singleDofFixed;
    int pointLoadNode = -1;

    if (loadType == LoadType::PointForceZ) {
        const double tolZ = (maxZ - minZ) * 0.02;
        std::vector<int> topCandidates;
        double sumX = 0.0, sumY = 0.0;
        int    topCount = 0;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.z <= minZ + tolZ) fixedNodes.push_back(i);
            if (p.z >= maxZ - tolZ) {
                topCandidates.push_back(i);
                sumX += p.x; sumY += p.y; ++topCount;
            }
        }
        if (topCount > 0) {
            const double cx = sumX / topCount, cy = sumY / topCount;
            double bestD = 1e300;
            for (int n : topCandidates) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                const double dx = p.x - cx, dy = p.y - cy;
                const double d2 = dx*dx + dy*dy;
                if (d2 < bestD) { bestD = d2; pointLoadNode = n; }
            }
        }
        if (pointLoadNode >= 0) loadedNodes.push_back(pointLoadNode);

        std::cout << "[NR] BC: fixed " << fixedNodes.size() << " nodes at Z_min. ";
        if (pointLoadNode >= 0) {
            const glm::vec3& p = model.originalVolumetricPositions[pointLoadNode];
            std::cout << "Point load at node " << pointLoadNode
                      << " pos=(" << p.x << "," << p.y << "," << p.z << ").";
        } else {
            std::cout << "No Z_max node found.";
        }
        std::cout << std::endl;
    } else if (loadType == LoadType::TensionX ||
               loadType == LoadType::TensionY ||
               loadType == LoadType::TensionZ) {
        const int ta = (loadType == LoadType::TensionX) ? 0
                     : (loadType == LoadType::TensionY) ? 1 : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const double tolA = (bmax - bmin) * 0.02;

        std::vector<int> faceMin, faceMax;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) faceMin.push_back(i);
            if (coord >= bmax - tolA) faceMax.push_back(i);
        }
        loadedNodes = faceMax;

        double cx=0, cy=0, cz=0;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            cx+=p.x; cy+=p.y; cz+=p.z;
        }
        cx/=nNodes; cy/=nNodes; cz/=nNodes;

        const int pb = (ta+1)%3;
        const int pc = (ta+2)%3;
        auto getCoordNR = [&](int ni, int ax) -> double {
            const glm::vec3& p = model.originalVolumetricPositions[ni];
            return (ax==0)?(double)p.x:(ax==1)?(double)p.y:(double)p.z;
        };

        int nodeA=-1; double dA=1e300;
        int nodeB=-1; double dB=-1;
        int nodeC=-1; double dC=-1;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double d2=(p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy)+(p.z-cz)*(p.z-cz);
            if (d2 < dA) { dA=d2; nodeA=i; }
        }
        const double Apb = nodeA>=0 ? getCoordNR(nodeA, pb) : 0.0;
        const double Apc = nodeA>=0 ? getCoordNR(nodeA, pc) : 0.0;
        for (int i=0; i<nNodes; ++i) {
            if (i==nodeA) continue;
            const double offPb = std::abs(getCoordNR(i, pb) - Apb);
            if (offPb > dB) { dB=offPb; nodeB=i; }
        }
        for (int i=0; i<nNodes; ++i) {
            if (i==nodeA || i==nodeB) continue;
            const double offPc = std::abs(getCoordNR(i, pc) - Apc);
            if (offPc > dC) { dC=offPc; nodeC=i; }
        }

        if (nodeA >= 0) fixedNodes.push_back(nodeA);
        if (nodeB >= 0) singleDofFixed.push_back({nodeB, pc});
        if (nodeC >= 0) singleDofFixed.push_back({nodeC, pb});

        std::cout << "[NR] BC [Tension" << "XYZ"[ta] << "]: anchor=" << nodeA
                  << "  rot-nodes=" << nodeB << "," << nodeC
                  << "  faces: -=" << faceMin.size() << " +=" << faceMax.size() << " nodes." << std::endl;
    } else {
        const double tolY = (maxY - minY) * 0.05;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.y <= minY + tolY) fixedNodes.push_back(i);
            if (p.y >= maxY - tolY) loadedNodes.push_back(i);
        }
        std::cout << "[NR] BC: fixed " << fixedNodes.size()
                  << " nodes at Y_min, loaded " << loadedNodes.size()
                  << " nodes at Y_max." << std::endl;
    }

    if (loadedNodes.empty()) {
        std::cout << "[NR] No load nodes detected." << std::endl;
        return false;
    }

    // -------------------------------------------------------------------------
    // Diagnostic pass (read-only). Discriminates between:
    //   H1 TetGen topology bias  - non-mirror-symmetric node distribution
    //   H2 BC misalignment       - fixed/loaded slab catches asymmetric subset
    //   H3 Force misalignment    - uniform F/N per-node load ignores tributary
    //                              area, produces non-zero moment about origin
    // No solver state is touched; printed only when verboseDiagnostics = true.
    // -------------------------------------------------------------------------
    if (verboseDiagnostics) {
        std::cout << "\n[DIAG] =============== asymmetry diagnostic ===============" << std::endl;

        // ---- Bounding box + centers ----
        double minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9;
        double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
            if (p.z < minZ) minZ = p.z; if (p.z > maxZ) maxZ = p.z;
            sumX += p.x; sumY += p.y; sumZ += p.z;
        }
        const double cx = 0.5 * (minX + maxX);
        const double cy = 0.5 * (minY + maxY);
        const double cz = 0.5 * (minZ + maxZ);
        const double mx = sumX / nNodes, my = sumY / nNodes, mz = sumZ / nNodes;
        const double bbLx = maxX - minX, bbLy = maxY - minY, bbLz = maxZ - minZ;
        const double bbDiag = std::sqrt(bbLx*bbLx + bbLy*bbLy + bbLz*bbLz);

        std::cout << "[DIAG] bbox  min = (" << minX << ", " << minY << ", " << minZ << ")" << std::endl;
        std::cout << "[DIAG] bbox  max = (" << maxX << ", " << maxY << ", " << maxZ << ")" << std::endl;
        std::cout << "[DIAG] bboxCenter = (" << cx << ", " << cy << ", " << cz
                  << ")   y-mid = " << cy << "  (=0 if Y-centered)" << std::endl;
        std::cout << "[DIAG] arithMean  = (" << mx << ", " << my << ", " << mz
                  << ")   offset/bbDiag = (" << (mx-cx)/bbDiag << ", "
                  << (my-cy)/bbDiag << ", " << (mz-cz)/bbDiag << ")" << std::endl;

        // ---- H1: octant census of volumetric nodes about bbox center ----
        int oct[8] = {0,0,0,0,0,0,0,0};
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            int ix = (p.x >= cx), iy = (p.y >= cy), iz = (p.z >= cz);
            oct[(ix<<2) | (iy<<1) | iz]++;
        }
        std::cout << "[DIAG] H1 octant census (-x-y-z .. +x+y+z): ";
        for (int k = 0; k < 8; ++k) std::cout << oct[k] << " ";
        std::cout << std::endl;

        // ---- H1: top-slab and bottom-slab xz-quadrant census ----
        auto quadCensus = [&](const std::vector<int>& nodes, const char* label) {
            int q[4] = {0,0,0,0};
            for (int n : nodes) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                int ix = (p.x >= cx), iz = (p.z >= cz);
                int idx = (ix<<1) | iz;
                q[idx]++;
            }
            std::cout << "[DIAG] H1 " << label << " xz-quadrants (-x-z,-x+z,+x-z,+x+z): "
                      << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << std::endl;
        };
        quadCensus(loadedNodes, "loaded-slab");
        quadCensus(fixedNodes,  "fixed-slab ");

        // ---- H2: fixed/loaded-set centroids and extents ----
        auto setStats = [&](const std::vector<int>& nodes, const char* label) {
            if (nodes.empty()) return;
            double sx=0, sy=0, sz=0;
            double xmn=1e9,xmx=-1e9,zmn=1e9,zmx=-1e9;
            for (int n : nodes) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                sx+=p.x; sy+=p.y; sz+=p.z;
                if (p.x<xmn) xmn=p.x; if (p.x>xmx) xmx=p.x;
                if (p.z<zmn) zmn=p.z; if (p.z>zmx) zmx=p.z;
            }
            const double n = static_cast<double>(nodes.size());
            std::cout << "[DIAG] H2 " << label << " centroid = ("
                      << sx/n << ", " << sy/n << ", " << sz/n
                      << ")   xz-extent = (" << (xmx-xmn) << ", " << (zmx-zmn)
                      << ")   N = " << nodes.size() << std::endl;
        };
        setStats(loadedNodes, "loaded");
        setStats(fixedNodes,  "fixed ");

        // Slab span vs bbox span -- small for sphere polar caps, ~1 for cube faces
        auto spanRatio = [&](const std::vector<int>& nodes) -> double {
            if (nodes.empty()) return 0.0;
            double xmn=1e9,xmx=-1e9,zmn=1e9,zmx=-1e9;
            for (int n : nodes) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                if (p.x<xmn) xmn=p.x; if (p.x>xmx) xmx=p.x;
                if (p.z<zmn) zmn=p.z; if (p.z>zmx) zmx=p.z;
            }
            const double sx = (xmx-xmn) / (bbLx > 0 ? bbLx : 1.0);
            const double sz = (zmx-zmn) / (bbLz > 0 ? bbLz : 1.0);
            return 0.5 * (sx + sz);
        };
        std::cout << "[DIAG] H2 loaded-slab xz-span / bbox xz-span = "
                  << spanRatio(loadedNodes)
                  << "   (~1.0 for cube face, <<1 for sphere polar cap)" << std::endl;
        std::cout << "[DIAG] H2  fixed-slab xz-span / bbox xz-span = "
                  << spanRatio(fixedNodes) << std::endl;

        // ---- H3 + D1 (surface-compression only) -------------------------------
        // These metrics all assume a distributed surface load and are
        // meaningless for a single-node point force.
        if (loadType == LoadType::SurfaceCompressionY) {
        // ---- H3: resultant and moment of the currently-applied load ----
        glm::dvec3 Fsum(0.0), Msum(0.0);
        const double fPerNode = -forceMagnitude / static_cast<double>(loadedNodes.size());
        for (int n : loadedNodes) {
            glm::dvec3 x = glm::dvec3(model.originalVolumetricPositions[n]);
            glm::dvec3 f(0.0, fPerNode, 0.0);
            Fsum += f;
            Msum += glm::cross(x, f); // moment about origin
        }
        std::cout << "[DIAG] H3 F_sum = (" << Fsum.x << ", " << Fsum.y << ", " << Fsum.z
                  << ")   (expect (0, -forceMagnitude, 0))" << std::endl;
        std::cout << "[DIAG] H3 M_sum about origin = (" << Msum.x << ", " << Msum.y << ", " << Msum.z
                  << ")   |M|/(|F|*bbDiag) = "
                  << glm::length(Msum) / (std::abs(forceMagnitude) * bbDiag + 1e-30)
                  << "   (0 means centered load)" << std::endl;

        // ---- H3 Metric I: consistent nodal loads from boundary triangles ------
        // Boundary patch triangles only (tet faces referenced by exactly one
        // tet, whose 3 vertices are all in loadedNodes). Internal faces are
        // filtered out to avoid double-counting area.
        std::vector<char> isLoaded(nNodes, 0);
        for (int n : loadedNodes) isLoaded[n] = 1;

        std::map<std::array<int,3>, int> faceCountDiag;
        const int tetFacesDiag[4][3] = {{1,2,3},{0,2,3},{0,1,3},{0,1,2}};
        for (int el = 0; el < nElems; ++el) {
            int v[4];
            for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(model.tetrahedra[el*4 + k]);
            for (int f = 0; f < 4; ++f) {
                std::array<int,3> key = { v[tetFacesDiag[f][0]],
                                          v[tetFacesDiag[f][1]],
                                          v[tetFacesDiag[f][2]] };
                std::sort(key.begin(), key.end());
                ++faceCountDiag[key];
            }
        }

        std::vector<double> areaShare(nNodes, 0.0);
        std::vector<double> patchAreas;   // D1: per-triangle areas
        double totalArea = 0.0;
        int triCount = 0;
        // D1: dump first `D1_DUMP_LIMIT` loaded-patch triangles verbatim.
        constexpr int D1_DUMP_LIMIT = 12;
        std::cout << "[DIAG] D1 loaded patch triangles (first "
                  << D1_DUMP_LIMIT << "):" << std::endl;
        for (const auto& kv : faceCountDiag) {
            if (kv.second != 1) continue; // interior face
            const std::array<int,3>& tri = kv.first;
            if (!(isLoaded[tri[0]] && isLoaded[tri[1]] && isLoaded[tri[2]])) continue;
            glm::dvec3 pa(model.originalVolumetricPositions[tri[0]]);
            glm::dvec3 pb(model.originalVolumetricPositions[tri[1]]);
            glm::dvec3 pc(model.originalVolumetricPositions[tri[2]]);
            const double area = 0.5 * glm::length(glm::cross(pb - pa, pc - pa));
            const double share = area / 3.0;
            areaShare[tri[0]] += share;
            areaShare[tri[1]] += share;
            areaShare[tri[2]] += share;
            totalArea += area;
            if (triCount < D1_DUMP_LIMIT) {
                std::cout << "[DIAG]   tri " << triCount
                          << " nodes=(" << tri[0] << "," << tri[1] << "," << tri[2] << ")"
                          << "  A=(" << pa.x << "," << pa.y << "," << pa.z << ")"
                          << "  B=(" << pb.x << "," << pb.y << "," << pb.z << ")"
                          << "  C=(" << pc.x << "," << pc.y << "," << pc.z << ")"
                          << "  area=" << area << std::endl;
            }
            patchAreas.push_back(area);
            ++triCount;
        }
        if (!patchAreas.empty()) {
            double amn = 1e300, amx = -1e300, amean = 0.0;
            for (double a : patchAreas) {
                if (a < amn) amn = a; if (a > amx) amx = a; amean += a;
            }
            amean /= patchAreas.size();
            double avar = 0.0;
            for (double a : patchAreas) avar += (a - amean) * (a - amean);
            const double astd = std::sqrt(avar / patchAreas.size());
            std::cout << "[DIAG] D1 patch-area stats: min/mean/max = "
                      << amn << " / " << amean << " / " << amx
                      << "   stdev = " << astd
                      << "   cv = " << (amean > 0 ? astd / amean : 0.0)
                      << "   (cv > ~0.01 => non-uniform triangulation)" << std::endl;
        }
        if (triCount > 0 && totalArea > 0.0) {
            const double traction = -forceMagnitude / totalArea; // N / m^2 in -Y
            double fmn = 1e300, fmx = -1e300, fmean = 0.0;
            int contributing = 0;
            for (int n : loadedNodes) {
                if (areaShare[n] <= 0.0) continue;
                const double fc = std::abs(traction * areaShare[n]);
                if (fc < fmn) fmn = fc;
                if (fc > fmx) fmx = fc;
                fmean += fc; contributing++;
            }
            if (contributing > 0) fmean /= contributing;
            const double uniform = std::abs(fPerNode);
            std::cout << "[DIAG] H3 top-face triangles reconstructed: " << triCount
                      << "   total area = " << totalArea << std::endl;
            std::cout << "[DIAG] H3 uniform per-node load         = " << uniform << std::endl;
            std::cout << "[DIAG] H3 consistent per-node load min/mean/max = "
                      << fmn << " / " << fmean << " / " << fmx
                      << "   spread = " << (fmn > 0 ? fmx/fmn : 0.0) << "x" << std::endl;
        } else {
            std::cout << "[DIAG] H3 no top-face triangles reconstructed "
                         "(loaded slab does not form a closed surface patch -- "
                         "typical for sphere polar caps; symptom of under-resolved Neumann BC)."
                      << std::endl;
        }
        } // end surface-compression guard (H3 + D1 block)

        // ---- D4: mesh quality histogram (slivers + min dihedral) --------------
        int    sliverCountNorm = 0;   // |V|/edge_max^3 < 1e-3
        int    sliverCountDihe = 0;   // min dihedral < 10 deg
        double qnMin = 1e300, qnMax = -1e300, qnMean = 0.0;
        double qdMin = 1e300, qdMax = -1e300, qdMean = 0.0;
        int worstIdx = -1; double worstDihe = 181.0;
        for (int el = 0; el < nElems; ++el) {
            int v[4];
            for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(model.tetrahedra[el*4 + k]);
            glm::dvec3 pa(model.originalVolumetricPositions[v[0]]);
            glm::dvec3 pb(model.originalVolumetricPositions[v[1]]);
            glm::dvec3 pc(model.originalVolumetricPositions[v[2]]);
            glm::dvec3 pd(model.originalVolumetricPositions[v[3]]);
            const double qn = tetNormalizedVolume(pa, pb, pc, pd);
            const double qd = tetMinDihedralDeg(pa, pb, pc, pd);
            if (qn < 1e-3)  ++sliverCountNorm;
            if (qd < 10.0)  ++sliverCountDihe;
            if (qn < qnMin) qnMin = qn; if (qn > qnMax) qnMax = qn; qnMean += qn;
            if (qd < qdMin) qdMin = qd; if (qd > qdMax) qdMax = qd; qdMean += qd;
            if (qd < worstDihe) { worstDihe = qd; worstIdx = el; }
        }
        if (nElems > 0) { qnMean /= nElems; qdMean /= nElems; }
        std::cout << "[DIAG] D4 tet quality ("
                  << nElems << " tets):" << std::endl;
        std::cout << "[DIAG]   normalized volume  min/mean/max = "
                  << qnMin << " / " << qnMean << " / " << qnMax
                  << "   (regular tet ~0.118)" << std::endl;
        std::cout << "[DIAG]   min dihedral (deg) min/mean/max = "
                  << qdMin << " / " << qdMean << " / " << qdMax
                  << "   (regular tet ~70.5)" << std::endl;
        std::cout << "[DIAG]   slivers: "
                  << sliverCountNorm << " by normVol<1e-3, "
                  << sliverCountDihe << " by minDihedral<10deg"
                  << "   (" << 100.0 * sliverCountDihe / std::max(1, nElems) << "% of mesh)"
                  << std::endl;
        if (worstIdx >= 0) {
            int v[4];
            for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(model.tetrahedra[worstIdx*4 + k]);
            std::cout << "[DIAG]   worst-dihedral tet idx=" << worstIdx
                      << "  angle=" << worstDihe << " deg"
                      << "  nodes=(" << v[0] << "," << v[1] << "," << v[2] << "," << v[3] << ")"
                      << std::endl;
        }

        std::cout << "[DIAG] ===================================================\n" << std::endl;
    }

    // ---- Full external load vector (applied fully at lambda = 1) --------------
    // Branch on loadType:
    //   SurfaceCompressionY : consistent nodal loads from boundary triangles
    //                         (tributary-area weighting).
    //   PointForceZ         : single -Z entry at pointLoadNode; no
    //                         triangulation noise.
    VectorXd F_ext_full = VectorXd::Zero(nDOFs);
    double   patchArea = 0.0;
    int      triCount  = 0;

    if (loadType == LoadType::PointForceZ) {
        F_ext_full(pointLoadNode * 3 + 2) = -forceMagnitude;
    } else if (loadType == LoadType::TensionX ||
               loadType == LoadType::TensionY ||
               loadType == LoadType::TensionZ) {
        const int ta = (loadType == LoadType::TensionX) ? 0
                     : (loadType == LoadType::TensionY) ? 1 : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const double tolA = (bmax - bmin) * 0.02;
        std::vector<int> faceMinT, faceMaxT;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) faceMinT.push_back(i);
            if (coord >= bmax - tolA) faceMaxT.push_back(i);
        }
        const double fMaxNR = faceMaxT.empty() ? 0.0 : forceMagnitude / static_cast<double>(faceMaxT.size());
        const double fMinNR = faceMinT.empty() ? 0.0 : forceMagnitude / static_cast<double>(faceMinT.size());
        for (int n : faceMaxT) F_ext_full(n*3+ta) += fMaxNR;
        for (int n : faceMinT) F_ext_full(n*3+ta) -= fMinNR;
    } else {
        std::vector<double> areaShare;
        buildConsistentLoadVector(model, loadedNodes, nNodes, forceMagnitude,
                                  F_ext_full, areaShare, patchArea, triCount);

        // Optional: enforce a known symmetry class on the load vector to
        // cancel out mesh-triangulation-induced directional bias.
        switch (loadSymmetry) {
            case LoadSymmetry::XZReflection:
                symmetrizeLoadXZ(F_ext_full, loadedNodes, model, nNodes);
                break;
            case LoadSymmetry::YAxial:
                symmetrizeLoadYAxial(F_ext_full, loadedNodes, model, nNodes);
                break;
            case LoadSymmetry::None:
            default:
                break;
        }
    }

    // ---- D2: 4-fold symmetry test on the ASSEMBLED consistent load vector --
    // For a centered mesh and symmetric physical problem each loaded node's
    // consistent Fy should equal that of its (x,-z), (-x,z), (-x,-z) mirrors.
    // Any nonzero defect is a triangulation (mesh) asymmetry on that axis.
    // Only meaningful for distributed loads; skipped for PointForceZ.
    if (verboseDiagnostics && !loadedNodes.empty()
        && loadType == LoadType::SurfaceCompressionY) {
        // Bounding-box center in x,z (independent of prior diagnostic scope).
        double xmn = 1e9, xmx = -1e9, zmn = 1e9, zmx = -1e9;
        for (int i = 0; i < nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            if (p.x < xmn) xmn = p.x; if (p.x > xmx) xmx = p.x;
            if (p.z < zmn) zmn = p.z; if (p.z > zmx) zmx = p.z;
        }
        const double cxD = 0.5 * (xmn + xmx);
        const double czD = 0.5 * (zmn + zmx);
        const double tolD = 1e-4 * std::max(xmx - xmn, zmx - zmn);

        // Nearest-neighbor search among loaded nodes for each mirror image.
        auto findMirror = [&](double tx, double tz) -> int {
            int   best = -1;
            double bd2 = 1e300;
            for (int n : loadedNodes) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                const double dx = p.x - tx, dz = p.z - tz;
                const double d2 = dx*dx + dz*dz;
                if (d2 < bd2) { bd2 = d2; best = n; }
            }
            return (bd2 <= tolD * tolD) ? best : -1;
        };

        int    pairedX = 0, pairedZ = 0, pairedDiag = 0;
        double defMeanX = 0, defMaxX = 0;
        double defMeanZ = 0, defMaxZ = 0;
        double defMeanD = 0, defMaxD = 0;
        double fMeanAbs = 0;
        int    nLoadedWithForce = 0;
        for (int n : loadedNodes) {
            const double fy = std::abs(F_ext_full(n * 3 + 1));
            if (fy > 0.0) { fMeanAbs += fy; ++nLoadedWithForce; }
        }
        if (nLoadedWithForce > 0) fMeanAbs /= nLoadedWithForce;

        if (fMeanAbs > 0.0) {
            for (int n : loadedNodes) {
                const glm::vec3& p = model.originalVolumetricPositions[n];
                const double xr = 2.0 * cxD - p.x;
                const double zr = 2.0 * czD - p.z;
                const double fy = F_ext_full(n * 3 + 1);

                const int nx  = findMirror(xr,  p.z);
                const int nz  = findMirror(p.x, zr);
                const int nxz = findMirror(xr,  zr);

                if (nx  >= 0) {
                    const double d = std::abs(fy - F_ext_full(nx  * 3 + 1)) / fMeanAbs;
                    defMeanX += d; if (d > defMaxX) defMaxX = d; ++pairedX;
                }
                if (nz  >= 0) {
                    const double d = std::abs(fy - F_ext_full(nz  * 3 + 1)) / fMeanAbs;
                    defMeanZ += d; if (d > defMaxZ) defMaxZ = d; ++pairedZ;
                }
                if (nxz >= 0) {
                    const double d = std::abs(fy - F_ext_full(nxz * 3 + 1)) / fMeanAbs;
                    defMeanD += d; if (d > defMaxD) defMaxD = d; ++pairedDiag;
                }
            }
            auto rpt = [](int paired, double sum, double mx, const char* axis) {
                if (paired == 0) {
                    std::cout << "[DIAG] D2 " << axis << " reflection: no mirror pairs found." << std::endl;
                    return;
                }
                std::cout << "[DIAG] D2 " << axis << " reflection: pairs=" << paired
                          << "  mean defect=" << (sum / paired)
                          << "  max defect=" << mx
                          << "   (>~0.01 => asymmetric triangulation on this axis)"
                          << std::endl;
            };
            std::cout << "\n[DIAG] ============ D2 4-fold load symmetry ============" << std::endl;
            rpt(pairedX,    defMeanX, defMaxX, "x -> -x   ");
            rpt(pairedZ,    defMeanZ, defMaxZ, "z -> -z   ");
            rpt(pairedDiag, defMeanD, defMaxD, "(x,z)->-(x,z)");
            std::cout << "[DIAG] ===================================================\n" << std::endl;
        }
    }

    // ---- UI/visualization metadata --------------------------------------------
    model.appliedForces.clear();
    model.nodalForceMagnitudes.assign(nNodes, 0.0f);
    model.nodalDisplacementMagnitudes.assign(nNodes, 0.0f);
    model.showAppliedForceField = false;
    model.totalAppliedForce     = static_cast<float>(std::abs(forceMagnitude));

    if (loadType == LoadType::PointForceZ) {
        const float arrowLen = static_cast<float>(maxZ - minZ) * 0.25f;
        const glm::vec3 pos = model.originalVolumetricPositions[pointLoadNode];
        model.appliedForces.push_back({ pos + glm::vec3(0, 0, arrowLen), pos });
        model.nodalForceMagnitudes[pointLoadNode]
            = static_cast<float>(std::abs(forceMagnitude));
        model.appliedForcePerNode = static_cast<float>(std::abs(forceMagnitude));
        std::cout << "[NR] Point force " << -forceMagnitude
                  << " N (-Z) at node " << pointLoadNode << "." << std::endl;
    } else if (loadType == LoadType::TensionX ||
               loadType == LoadType::TensionY ||
               loadType == LoadType::TensionZ) {
        const int ta = (loadType == LoadType::TensionX) ? 0
                     : (loadType == LoadType::TensionY) ? 1 : 2;
        const double bmin = (ta==0)?minX:(ta==1)?minY:minZ;
        const double bmax = (ta==0)?maxX:(ta==1)?maxY:maxZ;
        const double tolA = (bmax - bmin) * 0.02;
        std::vector<int> faceMinV, faceMaxV;
        for (int i=0; i<nNodes; ++i) {
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const double coord = (ta==0)?(double)p.x:(ta==1)?(double)p.y:(double)p.z;
            if (coord <= bmin + tolA) faceMinV.push_back(i);
            if (coord >= bmax - tolA) faceMaxV.push_back(i);
        }
        const double fPerNodeV = forceMagnitude / static_cast<double>(std::max(1, (int)faceMaxV.size()));
        model.appliedForcePerNode = static_cast<float>(fPerNodeV);
        for (int n : faceMaxV) model.nodalForceMagnitudes[n] = static_cast<float>(fPerNodeV);
        for (int n : faceMinV) model.nodalForceMagnitudes[n] = static_cast<float>(fPerNodeV);
        const float arrowLen = static_cast<float>(bmax - bmin) * 0.2f;
        glm::vec3 arrowDir(0,0,0); ((float*)(&arrowDir))[ta] = arrowLen;
        for (int n : faceMaxV) {
            glm::vec3 pos = model.originalVolumetricPositions[n];
            model.appliedForces.push_back({ pos, pos + arrowDir });
        }
        for (int n : faceMinV) {
            glm::vec3 pos = model.originalVolumetricPositions[n];
            model.appliedForces.push_back({ pos, pos - arrowDir });
        }
        std::cout << "[NR] Tension" << "XYZ"[ta] << ": distributed " << forceMagnitude
                  << " N over " << faceMaxV.size() << "(+)/" << faceMinV.size() << "(-) nodes" << std::endl;
    } else {
        const float arrowLength = static_cast<float>(maxY - minY) * 0.15f;
        double maxNodalF = 0.0, sumNodalF = 0.0;
        for (int n : loadedNodes) {
            const double fy = std::abs(F_ext_full(n * 3 + 1));
            if (fy > maxNodalF) maxNodalF = fy;
            sumNodalF += fy;
        }
        model.appliedForcePerNode = loadedNodes.empty()
            ? 0.0f
            : static_cast<float>(sumNodalF / static_cast<double>(loadedNodes.size()));
        for (int node : loadedNodes) {
            const double fy = std::abs(F_ext_full(node * 3 + 1));
            glm::vec3 pos = model.originalVolumetricPositions[node];
            const float scaled = maxNodalF > 0.0
                ? arrowLength * static_cast<float>(fy / maxNodalF)
                : arrowLength;
            model.appliedForces.push_back({ pos + glm::vec3(0, scaled, 0), pos });
            model.nodalForceMagnitudes[node] = static_cast<float>(fy);
        }

        std::cout << "[NR] Consistent load applied over " << triCount
                  << " boundary triangles (patch area " << patchArea << ")." << std::endl;
    }

    // ---- NR state -------------------------------------------------------------
    VectorXd u     = VectorXd::Zero(nDOFs);
    VectorXd F_int = VectorXd::Zero(nDOFs);
    VectorXd R     = VectorXd::Zero(nDOFs);
    VectorXd du    = VectorXd::Zero(nDOFs);

    // Scratch containers used by both assembly phases below. Declared at the
    // outer scope so per-iteration heap churn is avoided in the serial path;
    // the parallel path shadows these with thread-local copies inside each
    // `#pragma omp parallel` block, leaving these untouched when threading
    // is active.
    Eigen::MatrixXd              Ke;
    Eigen::VectorXd              ue, fe;
    std::vector<int>             gdofs;
    std::vector<Triplet<double>> tripletList;

    if (useMultithreading) {
        std::cout << "[NR][MT] Using " << omp_get_max_threads()
                  << " OpenMP thread(s) for element assembly." << std::endl;
    }

    // ---- Incremental load stepping -------------------------------------------
    for (int step = 1; step <= params.numLoadSteps; ++step) {
        const double lambda = static_cast<double>(step)
                            / static_cast<double>(params.numLoadSteps);
        VectorXd F_ext = lambda * F_ext_full;

        std::cout << "[NR] --- Load step " << step << "/" << params.numLoadSteps
                  << "  (lambda = " << lambda << ") ---" << std::endl;

        bool converged = false;
        for (int iter = 0; iter < params.maxIterations; ++iter) {
            // ------ Assemble F_int(u) via IElement interface -------------------
            // Parallel path: each thread owns private ue/fe/gdofs buffers;
            // scatter into the shared F_int uses #pragma omp atomic so the
            // final accumulation is identical up to FP summation reordering.
            // Serial path (useMultithreading == false): OpenMP runs the loop
            // in the master thread with no fork/join, atomic is uncontended,
            // behaviour matches the pre-parallel baseline.
            F_int.setZero();
            if (useMultithreading) {
                #pragma omp parallel
                {
                    Eigen::VectorXd  ue_thr, fe_thr;
                    std::vector<int> gdofs_thr;
                    #pragma omp for schedule(dynamic)
                    for (int el = 0; el < nElems; ++el) {
                        const IElement* elem = elements[el].get();
                        const int       nd   = elem->NumDOFs();
                        elem->GetGlobalDOFs(gdofs_thr);

                        ue_thr.resize(nd);
                        for (int r = 0; r < nd; ++r) ue_thr(r) = u(gdofs_thr[r]);

                        elem->ComputeInternalForceVector(ue_thr, fe_thr);

                        for (int r = 0; r < nd; ++r) {
                            #pragma omp atomic
                            F_int(gdofs_thr[r]) += fe_thr(r);
                        }
                    }
                }
            } else {
                for (int el = 0; el < nElems; ++el) {
                    const IElement* elem = elements[el].get();
                    const int nd = elem->NumDOFs();
                    elem->GetGlobalDOFs(gdofs);

                    ue.resize(nd);
                    for (int r = 0; r < nd; ++r) ue(r) = u(gdofs[r]);

                    elem->ComputeInternalForceVector(ue, fe);

                    for (int r = 0; r < nd; ++r) F_int(gdofs[r]) += fe(r);
                }
            }

            // ------ Residual + Dirichlet enforcement ---------------------------
            R = F_ext - F_int;
            for (int node : fixedNodes) {
                R(node * 3 + 0) = 0.0;
                R(node * 3 + 1) = 0.0;
                R(node * 3 + 2) = 0.0;
            }
            for (auto& [node, dof] : singleDofFixed) R(node*3+dof) = 0.0;
            const double residualNorm = R.norm();

            std::cout << "[NR]   step " << step
                      << "  iter " << iter
                      << "  ||R||_2 = " << residualNorm << std::endl;

            // Convergence check (never on iter 0 so the initial residual is
            // always printed -- this is the validation hook the protocol asks
            // for: step 0 AND step 1 residuals are logged).
            if (iter >= 1 && residualNorm < params.residualTolL2) {
                std::cout << "[NR]   converged in " << iter
                          << " iteration(s)." << std::endl;
                converged = true;
                break;
            }

            // ------ Assemble tangent K(u) via IElement interface ---------------
            if (useMultithreading) {
                // MT: pre-sized disjoint write slots per element; identical
                // numerical result as serial (setFromTriplets sums duplicates
                // order-invariantly).
                const int    dpeNR     = elements.empty() ? 12 : elements[0]->NumDOFs();
                const size_t perElemNR = static_cast<size_t>(dpeNR)
                                       * static_cast<size_t>(dpeNR);
                tripletList.assign(static_cast<size_t>(nElems) * perElemNR,
                                   Triplet<double>(0, 0, 0.0));

                #pragma omp parallel
                {
                    Eigen::MatrixXd  Ke_thr;
                    Eigen::VectorXd  ue_thr;
                    std::vector<int> gdofs_thr;
                    #pragma omp for schedule(dynamic)
                    for (int el = 0; el < nElems; ++el) {
                        const IElement* elem = elements[el].get();
                        const int       nd   = elem->NumDOFs();
                        elem->GetGlobalDOFs(gdofs_thr);

                        ue_thr.resize(nd);
                        for (int r = 0; r < nd; ++r) ue_thr(r) = u(gdofs_thr[r]);

                        elem->ComputeTangentStiffness(ue_thr, Ke_thr);

                        const size_t base = static_cast<size_t>(el) * perElemNR;
                        for (int r = 0; r < nd; ++r) {
                            const int row = gdofs_thr[r];
                            for (int c = 0; c < nd; ++c) {
                                tripletList[base + static_cast<size_t>(r) * dpeNR + c]
                                    = Triplet<double>(row, gdofs_thr[c], Ke_thr(r, c));
                            }
                        }
                    }
                }
            } else {
                // --- ORIGINAL SERIAL PATH (preserved line-by-line) --------
                tripletList.clear();
                {
                    const int dpe = elements.empty() ? 12 : elements[0]->NumDOFs();
                    tripletList.reserve(static_cast<size_t>(nElems)
                                        * static_cast<size_t>(dpe * dpe));
                }
                for (int el = 0; el < nElems; ++el) {
                    const IElement* elem = elements[el].get();
                    const int nd = elem->NumDOFs();
                    elem->GetGlobalDOFs(gdofs);

                    ue.resize(nd);
                    for (int r = 0; r < nd; ++r) ue(r) = u(gdofs[r]);

                    elem->ComputeTangentStiffness(ue, Ke);

                    for (int r = 0; r < nd; ++r) {
                        const int row = gdofs[r];
                        for (int c = 0; c < nd; ++c) {
                            tripletList.emplace_back(row, gdofs[c], Ke(r, c));
                        }
                    }
                }
            }

            SparseMatrix<double> K(nDOFs, nDOFs);
            K.setFromTriplets(tripletList.begin(), tripletList.end());

            // IMMEDIATELY free the massive triplet array before the sparse solve
            // to prevent System RAM from maxing out during Tet10 simulations.
            std::vector<Triplet<double>>().swap(tripletList);

            // Penalty Dirichlet (same as linear path)
            const double maxDiagonal = K.diagonal().maxCoeff();
            const double penalty     = maxDiagonal * 1e7;
            for (int node : fixedNodes) {
                K.coeffRef(node * 3 + 0, node * 3 + 0) += penalty;
                K.coeffRef(node * 3 + 1, node * 3 + 1) += penalty;
                K.coeffRef(node * 3 + 2, node * 3 + 2) += penalty;
            }
            for (auto& [node, dof] : singleDofFixed)
                K.coeffRef(node*3+dof, node*3+dof) += penalty;

            bool solved = false;

#ifdef HAS_CUDA
            if (useGPU) {
                Eigen::SparseMatrix<double> Kcsr = K;
                Kcsr.makeCompressed();
                
                auto tGpu0 = std::chrono::high_resolution_clock::now();
                if (cuda_solver::solveOnGpu(Kcsr, R, du)) {
                    auto tGpu1 = std::chrono::high_resolution_clock::now();
                    double gpuMs = std::chrono::duration<double, std::milli>(tGpu1 - tGpu0).count();
                    std::cout << "[NR][GPU] Tangent solve succeeded in " << gpuMs << " ms" << std::endl;
                    solved = true;
                } else {
                    std::cout << "[NR][GPU] GPU solve failed. Falling back to CPU..." << std::endl;
                    useGPU = false; // Turn off for remaining iterations to avoid repeating failures
                }
            }
#endif

            if (!solved) {
                if (useMultithreading) {
                    // MT: CG with Jacobi preconditioner. Eigen's SpMV and dot
                    // products parallelize internally when OpenMP is enabled.
                    // Falls back to serial SimplicialLDLT if CG fails.
                    Eigen::setNbThreads(omp_get_max_threads());
                    const int    maxIters = std::max(nDOFs, 5000);
                    const double cgTol    = 1e-10;

                auto tSolve0 = std::chrono::high_resolution_clock::now();
                ConjugateGradient<SparseMatrix<double>, Lower|Upper,
                                  DiagonalPreconditioner<double>> cg;
                cg.setMaxIterations(maxIters);
                cg.setTolerance(cgTol);
                cg.compute(K);

                bool cgOK = false;
                if (cg.info() == Success) {
                    du = cg.solve(R);
                    cgOK = (cg.info() == Success);
                }
                auto tSolve1 = std::chrono::high_resolution_clock::now();
                const double solveMs = std::chrono::duration<double, std::milli>(
                                           tSolve1 - tSolve0).count();

                if (cgOK) {
                    std::cout << "[NR][MT] CG converged in " << cg.iterations()
                              << " iters, err=" << cg.error()
                              << "  [" << solveMs << " ms]" << std::endl;
                } else {
                    std::cout << "[NR][MT] CG did not converge (iters="
                              << cg.iterations() << ", err=" << cg.error()
                              << "). Falling back to serial SimplicialLDLT..."
                              << std::endl;
                    SimplicialLDLT<SparseMatrix<double>> solver;
                    solver.compute(K);
                    if (solver.info() != Success) {
                        std::cout << "[NR] Tangent decomposition failed." << std::endl;
                        return false;
                    }
                    du = solver.solve(R);
                    if (solver.info() != Success) {
                        std::cout << "[NR] Linear solve failed." << std::endl;
                        return false;
                    }
                }
            } else {
                // --- ORIGINAL SERIAL PATH (preserved line-by-line) --------
                SimplicialLDLT<SparseMatrix<double>> solver;
                solver.compute(K);
                if (solver.info() != Success) {
                    std::cout << "[NR] Tangent decomposition failed." << std::endl;
                    return false;
                }

                du = solver.solve(R);
                if (solver.info() != Success) {
                    std::cout << "[NR] Linear solve failed." << std::endl;
                    return false;
                }
            }
            } // end if (!solved)

            u += du;
        }

        if (!converged) {
            std::cout << "[NR] step " << step
                      << " failed to converge within " << params.maxIterations
                      << " iterations." << std::endl;
            return false;
        }
    }

    std::cout << "[NR] Solved. Max displacement: "
              << u.cwiseAbs().maxCoeff() << std::endl;

    // ---- D3: top-5 displacement outliers ------------------------------------
    // Prints the 5 nodes with largest |u|, along with:
    //   - whether they lie on the mesh surface (>=1 incident boundary face)
    //   - whether they are in the loaded/fixed set
    //   - number of incident tets
    //   - minimum dihedral angle across incident tets (sliver indicator)
    // Helps explain isolated "pop-up" vertices in the displacement colormap.
    if (verboseDiagnostics) {
        // Build node-to-tets and boundary-face sets once.
        std::vector<std::vector<int>> nodeTets(nNodes);
        for (int el = 0; el < nElems; ++el)
            for (int k = 0; k < 4; ++k)
                nodeTets[static_cast<int>(model.tetrahedra[el*4 + k])].push_back(el);

        std::map<std::array<int,3>, int> faceCountD3;
        const int tetFacesD3[4][3] = {{1,2,3},{0,2,3},{0,1,3},{0,1,2}};
        for (int el = 0; el < nElems; ++el) {
            int v[4];
            for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(model.tetrahedra[el*4 + k]);
            for (int f = 0; f < 4; ++f) {
                std::array<int,3> key = { v[tetFacesD3[f][0]],
                                          v[tetFacesD3[f][1]],
                                          v[tetFacesD3[f][2]] };
                std::sort(key.begin(), key.end());
                ++faceCountD3[key];
            }
        }
        std::vector<char> isSurface(nNodes, 0);
        for (const auto& kv : faceCountD3) {
            if (kv.second != 1) continue;
            isSurface[kv.first[0]] = 1;
            isSurface[kv.first[1]] = 1;
            isSurface[kv.first[2]] = 1;
        }
        std::vector<char> isLoadedD3(nNodes, 0), isFixedD3(nNodes, 0);
        for (int n : loadedNodes) isLoadedD3[n] = 1;
        for (int n : fixedNodes)  isFixedD3[n]  = 1;

        // Sort nodes by |u| descending, keep top 5.
        std::vector<std::pair<double,int>> magIdx(nNodes);
        double sumMag = 0.0;
        for (int i = 0; i < nNodes; ++i) {
            const double ux = u(i*3 + 0), uy = u(i*3 + 1), uz = u(i*3 + 2);
            const double m  = std::sqrt(ux*ux + uy*uy + uz*uz);
            magIdx[i] = { m, i };
            sumMag += m;
        }
        const double meanMag = (nNodes > 0) ? (sumMag / nNodes) : 0.0;
        std::partial_sort(magIdx.begin(), magIdx.begin() + std::min(5, nNodes),
                          magIdx.end(),
                          [](const std::pair<double,int>& A, const std::pair<double,int>& B) {
                              return A.first > B.first;
                          });

        std::cout << "\n[DIAG] ============ D3 top-5 displacement outliers ============" << std::endl;
        std::cout << "[DIAG] mean |u| across all nodes = " << meanMag << std::endl;

        // ---- per-group |u| statistics (loaded / surface / interior) ----------
        auto groupStats = [&](auto pred, const char* label) {
            std::vector<double> ms;
            ms.reserve(nNodes);
            for (int i = 0; i < nNodes; ++i) {
                if (!pred(i)) continue;
                const double ux = u(i*3+0), uy = u(i*3+1), uz = u(i*3+2);
                ms.push_back(std::sqrt(ux*ux + uy*uy + uz*uz));
            }
            if (ms.empty()) {
                std::cout << "[DIAG]   " << label << ": (empty)" << std::endl;
                return;
            }
            double mn = ms[0], mx = ms[0], sm = 0.0;
            for (double m : ms) { if (m<mn) mn=m; if (m>mx) mx=m; sm += m; }
            const double avg = sm / ms.size();
            double var = 0.0;
            for (double m : ms) var += (m - avg) * (m - avg);
            const double sd = std::sqrt(var / ms.size());
            std::sort(ms.begin(), ms.end());
            const double p99 = ms[static_cast<size_t>(0.99 * (ms.size() - 1))];
            std::cout << "[DIAG]   " << label << "  N=" << ms.size()
                      << "  min/mean/max=" << mn << " / " << avg << " / " << mx
                      << "  stdev=" << sd << "  p99=" << p99
                      << "  spread(max/mean)=" << (avg > 0 ? mx / avg : 0.0)
                      << "x" << std::endl;
        };
        groupStats([&](int i){ return isLoadedD3[i] != 0; },                     "LOADED     ");
        groupStats([&](int i){ return isFixedD3[i]  != 0; },                     "FIXED      ");
        groupStats([&](int i){ return isSurface[i] && !isLoadedD3[i] && !isFixedD3[i]; },
                   "SURFACE-free");
        groupStats([&](int i){ return !isSurface[i]; },                          "INTERIOR   ");

        const int top = std::min(5, nNodes);
        for (int rank = 0; rank < top; ++rank) {
            const int i = magIdx[rank].second;
            const double m = magIdx[rank].first;
            const glm::vec3& p = model.originalVolumetricPositions[i];
            const int nIncident = static_cast<int>(nodeTets[i].size());
            double minDihe = 180.0;
            for (int el : nodeTets[i]) {
                int v[4];
                for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(model.tetrahedra[el*4 + k]);
                const double d = tetMinDihedralDeg(
                    glm::dvec3(model.originalVolumetricPositions[v[0]]),
                    glm::dvec3(model.originalVolumetricPositions[v[1]]),
                    glm::dvec3(model.originalVolumetricPositions[v[2]]),
                    glm::dvec3(model.originalVolumetricPositions[v[3]]));
                if (d < minDihe) minDihe = d;
            }
            const char* role = isLoadedD3[i] ? "LOADED"
                             : isFixedD3[i]  ? "FIXED "
                             : "interior-dof";
            const char* loc  = isSurface[i]  ? "SURFACE" : "interior-vol";
            std::cout << "[DIAG]   #" << (rank + 1)
                      << "  node " << i
                      << "  pos=(" << p.x << "," << p.y << "," << p.z << ")"
                      << "  |u|=" << m
                      << "  ratio/mean=" << (meanMag > 0 ? m / meanMag : 0.0)
                      << "  " << role << "  " << loc
                      << "  tets=" << nIncident
                      << "  minDihedral=" << minDihe << " deg" << std::endl;
        }
        std::cout << "[DIAG] =========================================================\n"
                  << std::endl;
    }

    // ---- Push displacements back into the model -------------------------------
    model.deformedPositions.resize(nNodes);
    // NR path is not unit-scaled (documented 04C limitation): clear the
    // physical-mm probe field rather than fill it with wrong units.
    model.nodalDispMM.clear();
    // Per-node update is embarrassingly parallel; serial behaviour unchanged
    // when useMultithreading == false (OMP collapses to master thread).
    #pragma omp parallel for schedule(static) if(useMultithreading)
    for (int i = 0; i < nNodes; ++i) {
        glm::vec3 displacement(
            static_cast<float>(u(i * 3 + 0) * visualScale),
            static_cast<float>(u(i * 3 + 1) * visualScale),
            static_cast<float>(u(i * 3 + 2) * visualScale)
        );
        model.deformedPositions[i] = model.originalVolumetricPositions[i] + displacement;
        model.nodalDisplacementMagnitudes[i] = glm::length(displacement);
    }

    model.hasDeformation   = true;
    model.showDeformedMesh = true;
    model.needsUpdate      = true;
    model.buildBuffers();

    std::cout << "[NR] Updated mesh with deformations (scaled "
              << visualScale << "x)" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// solveAdaptive – geometry-aware element selection + linear solve
// ---------------------------------------------------------------------------
// Analyzes surface curvature of the volumetric mesh to decide whether to use
// Tet4 (linear) or Tet10 (quadratic) elements. Then delegates to
// solveLinearStatic with the auto-selected element type.
// ---------------------------------------------------------------------------
bool FEASolver::solveAdaptive(FEAModel& model, float visualScale) {
    if (!model.hasVolumetricMesh || model.tetrahedra.empty()) {
        std::cout << "[Adaptive] No volumetric mesh. Generate mesh first." << std::endl;
        return false;
    }

    const int nVerts = static_cast<int>(model.volumetricVertices.size());
    const auto& verts = model.volumetricVertices;
    const auto& tets  = model.tetrahedra;
    const int nTets   = static_cast<int>(tets.size() / 4);

    int highCurvCount = 0;
    const float angleThreshold = geoParams.curvatureAngleThreshold * 3.14159265f / 180.0f;

    // Build per-vertex average normal
    std::vector<glm::vec3> avgNormal(nVerts, glm::vec3(0.0f));
    std::vector<int> normalCount(nVerts, 0);

    for (int t = 0; t < nTets; ++t) {
        unsigned int n0 = tets[t * 4 + 0], n1 = tets[t * 4 + 1];
        unsigned int n2 = tets[t * 4 + 2], n3 = tets[t * 4 + 3];
        // 4 faces: (n0,n1,n2), (n0,n1,n3), (n0,n2,n3), (n1,n2,n3)
        unsigned int faces[4][3] = {
            {n0, n1, n2}, {n0, n1, n3}, {n0, n2, n3}, {n1, n2, n3}
        };
        for (auto& f : faces) {
            glm::vec3 e0 = verts[f[1]].position - verts[f[0]].position;
            glm::vec3 e1 = verts[f[2]].position - verts[f[0]].position;
            glm::vec3 cr = glm::cross(e0, e1);
            float len = glm::length(cr);
            if (len < 1e-12f) continue;
            glm::vec3 n = cr / len;
            for (int vi : {(int)f[0], (int)f[1], (int)f[2]}) {
                avgNormal[vi] += n;
                normalCount[vi]++;
            }
        }
    }

    for (int i = 0; i < nVerts; ++i) {
        if (normalCount[i] < 2) continue;
        float len = glm::length(avgNormal[i]);
        if (len > 1e-12f) avgNormal[i] /= len;
    }

    // Second pass: check angular deviation
    std::vector<bool> counted(nVerts, false);
    for (int t = 0; t < nTets; ++t) {
        unsigned int n0 = tets[t * 4 + 0], n1 = tets[t * 4 + 1];
        unsigned int n2 = tets[t * 4 + 2], n3 = tets[t * 4 + 3];
        unsigned int faces[4][3] = {
            {n0, n1, n2}, {n0, n1, n3}, {n0, n2, n3}, {n1, n2, n3}
        };
        for (auto& f : faces) {
            glm::vec3 e0 = verts[f[1]].position - verts[f[0]].position;
            glm::vec3 e1 = verts[f[2]].position - verts[f[0]].position;
            glm::vec3 cr = glm::cross(e0, e1);
            float len = glm::length(cr);
            if (len < 1e-12f) continue;
            glm::vec3 n = cr / len;
            for (int vi : {(int)f[0], (int)f[1], (int)f[2]}) {
                if (counted[vi]) continue;
                float d = glm::dot(n, avgNormal[vi]);
                d = std::max(-1.0f, std::min(1.0f, d));
                if (std::acos(d) > angleThreshold) {
                    highCurvCount++;
                    counted[vi] = true;
                }
            }
        }
    }

    float curvFrac = nVerts > 0 ? static_cast<float>(highCurvCount) / nVerts : 0.0f;
    bool useTet10 = curvFrac > geoParams.highCurvatureFracLimit;

    std::cout << "[Adaptive] Curvature analysis: " << (curvFrac * 100.0f)
              << "% high-curvature vertices (threshold=" << geoParams.highCurvatureFracLimit * 100.0f << "%)"
              << std::endl;
    std::cout << "[Adaptive] Auto-selected: " << (useTet10 ? "Tet10 (quadratic)" : "Tet4 (linear)")
              << std::endl;

    useQuadraticElements = useTet10;
    return solveLinearStatic(model, visualScale);
}

// =============================================================================
// FEASolver::solveBrittleFracture
// =============================================================================
// Brittle element-deletion fracture loop.
//
// Each iteration:
//   1. Assemble K over alive elements (m_fractureAlive mask applied in the
//      assembly loops of solveLinearStatic).
//   2. Solve K u = F.
//   3. Recover per-element von Mises stress from B and D.
//   4. Kill elements with σ_vm > fractureStress.
//   5. Repeat until stable or maxIters.
//
// The GPU path is inherited from solveLinearStatic automatically.
// =============================================================================
bool FEASolver::solveBrittleFracture(FEAModel& model, float visualScale, int maxIters)
{
    if (model.tetrahedra.empty()) {
        std::cout << "[FRACTURE] No tetrahedra. Run GENERATE 3D MESH first." << std::endl;
        return false;
    }

    const int nElems = static_cast<int>(model.tetrahedra.size() / 4);

    // Initialise element-alive mask (all alive at the start).
    if (static_cast<int>(model.elementAlive.size()) != nElems)
        model.elementAlive.assign(nElems, 1);
    if (static_cast<int>(model.elementFailureIter.size()) != nElems)
        model.elementFailureIter.assign(nElems, -1);
    model.elementFailureMode.assign(nElems, 0);
    // new_TODO_03: per-element von Mises captured at the iteration each element
    // dies (visualization only; reset to 0 = alive at the start of every run).
    model.elementVonMisesAtDeath.assign(nElems, 0.0f);

    // Constitutive model used for stress recovery.
    std::unique_ptr<IMaterial> materialOwned;
    const bool useFdmFracture = useFdmAnisotropy && E_z > 0.0;
    if (useFdmFracture) {
        materialOwned = std::make_unique<TransverseIsotropicMaterial>(
            youngsModulus, E_z, poissonRatio, nu_pz, G_pz, buildAxis);
        std::cout << "[FRACTURE-FDM] Mode-aware fracture: interlayer-tens="
                  << fractureStress_interlayer*1e-6 << " MPa  interlayer-shear="
                  << fractureShear_interlayer*1e-6 << " MPa  intralayer="
                  << fractureStress_intralayer*1e-6 << " MPa" << std::endl;
    } else {
        materialOwned = std::make_unique<LinearElastic>(youngsModulus, poissonRatio);
    }
    IMaterial& material = *materialOwned;

    // solveLinearStatic permanently sets useGPU=false if a GPU solve fails,
    // which would silently disable GPU for all subsequent fracture iterations.
    // Save the user's intent and restore it at the top of each iteration so that
    // each iteration retries GPU (the K matrix changes each iteration as elements
    // are killed, and earlier failures due to singularity are fixed by the island-
    // node regularization added above).
    const bool savedUseGPU  = useGPU;
    const bool savedUseMT   = useMultithreading;

    // Auto-match element order to whatever was generated. When the user
    // previously ran a Tet10 solve, generateMidEdgeNodes() has expanded
    // originalVolumetricPositions to include mid-edge nodes. Running the
    // fracture loop with useQuadraticElements=false (the default) would then
    // assemble Tet4 stiffness into a matrix sized for ALL nodes (corner +
    // mid-edge), inflating the DOF count 2-3x and making each LDLT factorization
    // orders of magnitude slower. It also causes stress recovery to use the wrong
    // element type, giving incorrect von Mises / FDM fracture values.
    if (model.hasQuadraticMesh)
        useQuadraticElements = true;

    // isTet10 must be derived AFTER the auto-detection above.
    const bool isTet10 = useQuadraticElements && model.hasQuadraticMesh;

    // new_TODO_04C: scale to real SI metres for the whole fracture loop. The
    // precomputed per-element D*B below and the nested solveLinearStatic both read
    // the now-metre originalVolumetricPositions, so recovered von Mises stress is
    // physical Pa and the comparison vs fractureStress respects the actual part
    // size (a 4 mm and a 400 mm part no longer fracture identically). The node set
    // is final here (mid-edge nodes already exist whenever isTet10), so the saved
    // model-space copy restores exactly.
    const bool didScale = scaleGeometryToMeters(model);

    // Force multithreaded CG for every fracture iteration regardless of the UI
    // toggle. Serial LDLT (the non-MT path) is O(n^{1.5}) and memory-bandwidth
    // bound — it appears as "low CPU usage" while doing nothing useful on the
    // other cores. CG with Jacobi preconditioning parallelises via OpenMP SpMV,
    // uses all cores, and converges faster for the near-diagonal systems that
    // arise when many elements are killed (F_i = 1 for dead-node DOFs).
    useMultithreading = true;
    Eigen::setNbThreads(omp_get_max_threads());

    // -------------------------------------------------------------------------
    // Precompute per-element average D*B matrix ONCE.
    // Node coordinates never change in brittle fracture (no geometry update),
    // so the strain-displacement matrix B_e is the same every iteration.
    // Storing avg_DB = (1/nGauss)*Σ_gp D*B_gp eliminates repeated Jacobian
    // inversion and Gauss-point integration from the inner stress-recovery loop.
    // -------------------------------------------------------------------------
    using DB4  = Eigen::Matrix<double, 6, 12>;
    using DB10 = Eigen::Matrix<double, 6, 30>;
    std::vector<DB4>  elDB4;
    std::vector<DB10> elDB10;

    std::cout << "[FRACTURE] Precomputing element stress matrices..." << std::endl;
    if (isTet10) {
        elDB10.resize(nElems);
        #pragma omp parallel for schedule(static)
        for (int el = 0; el < nElems; ++el) {
            std::array<int, Tet10Element::kNumNodes> ids{};
            Eigen::Matrix<double, 3, Tet10Element::kNumNodes> coords;
            for (int i = 0; i < Tet10Element::kNumNodes; ++i) {
                const int nid = static_cast<int>(
                    model.tetrahedraQuadratic[static_cast<size_t>(el)*10 + i]);
                ids[i] = nid;
                const glm::vec3& p = model.originalVolumetricPositions[nid];
                coords(0,i) = p.x; coords(1,i) = p.y; coords(2,i) = p.z;
            }
            Tet10Element elem(coords, ids, &material);
            elDB10[el] = elem.ComputeAvgDB();
        }
    } else {
        elDB4.resize(nElems);
        #pragma omp parallel for schedule(static)
        for (int el = 0; el < nElems; ++el) {
            std::array<int, TetrahedralElement::kNumNodes> ids{};
            Eigen::Matrix<double, 3, 4> coords;
            for (int i = 0; i < TetrahedralElement::kNumNodes; ++i) {
                const int nid = static_cast<int>(model.tetrahedra[el*4 + i]);
                ids[i] = nid;
                const glm::vec3& p = model.originalVolumetricPositions[nid];
                coords(0,i) = p.x; coords(1,i) = p.y; coords(2,i) = p.z;
            }
            TetrahedralElement elem(coords, ids, &material);
            elDB4[el] = elem.ComputeAvgDB();
        }
    }

    for (int iter = 0; iter < maxIters; ++iter) {
        useGPU = savedUseGPU; // re-enable GPU for each iteration

        const int aliveNow = static_cast<int>(
            std::count(model.elementAlive.begin(), model.elementAlive.end(), uint8_t(1)));
        std::cout << (useFdmFracture ? "[FRACTURE-FDM]" : "[FRACTURE]")
                  << " === Iteration " << iter + 1 << "/" << maxIters
                  << "  (" << aliveNow << "/" << nElems << " elements alive) ===" << std::endl;

        // Feed the alive mask to solveLinearStatic's assembly loops.
        m_fractureAlive = model.elementAlive;

        Eigen::VectorXd U;
        bool ok = solveLinearStatic(model, visualScale, &U);

        m_fractureAlive.clear();

        if (!ok || U.size() == 0) {
            // new_TODO_19C-b: a singular system AFTER elements have already
            // failed means the crack SEVERED the part — a freed chunk has
            // rigid modes. That is the correct terminal state of a break
            // test (the 19D standing-vs-lying acceptance depends on reaching
            // it), so stop gracefully with the accumulated fracture state
            // instead of erroring. Iteration 1 failing = genuine setup
            // problem, still a hard failure.
            bool anyFailed = false;
            for (uint8_t a : model.elementAlive) if (!a) { anyFailed = true; break; }
            if (iter > 1 && anyFailed) {
                std::cout << "[FRACTURE] Solver singular at iteration " << iter
                          << " after prior element failures -> part SEVERED; "
                             "stopping with accumulated crack state." << std::endl;
                useMultithreading = savedUseMT;
                if (didScale) restoreGeometryToModel(model);
                return true;
            }
            std::cout << "[FRACTURE] Solver failed at iteration " << iter << "." << std::endl;
            useMultithreading = savedUseMT;
            if (didScale) restoreGeometryToModel(model);
            return false;
        }

        // ---- Per-element stress recovery (precomputed D*B, always parallel) ----
        std::vector<float>   sigmaVM(nElems, 0.0f);
        std::vector<uint8_t> failModeBuf(nElems, 0);

        #pragma omp parallel for schedule(static)
        for (int el = 0; el < nElems; ++el) {
            if (!model.elementAlive[el]) continue;

            Eigen::Matrix<double,6,1> s;
            if (isTet10) {
                Eigen::Matrix<double, 30, 1> ue;
                for (int n = 0; n < Tet10Element::kNumNodes; ++n) {
                    const int nid = static_cast<int>(
                        model.tetrahedraQuadratic[static_cast<size_t>(el)*10 + n]);
                    ue(n*3+0) = U(nid*3+0);
                    ue(n*3+1) = U(nid*3+1);
                    ue(n*3+2) = U(nid*3+2);
                }
                s = elDB10[el] * ue;
            } else {
                Eigen::Matrix<double, 12, 1> ue;
                for (int n = 0; n < TetrahedralElement::kNumNodes; ++n) {
                    const int nid = static_cast<int>(model.tetrahedra[el*4 + n]);
                    ue(n*3+0) = U(nid*3+0);
                    ue(n*3+1) = U(nid*3+1);
                    ue(n*3+2) = U(nid*3+2);
                }
                s = elDB4[el] * ue;
            }

            const double sx=s(0), sy=s(1), sz=s(2), txy=s(3), tyz=s(4), txz=s(5);
            sigmaVM[el] = static_cast<float>(std::sqrt(
                0.5*((sx-sy)*(sx-sy)+(sy-sz)*(sy-sz)+(sz-sx)*(sz-sx))
                + 3.0*(txy*txy + tyz*tyz + txz*txz)));

            if (useFdmFracture) {
                // Three independent max-stress criteria in principal material axes.
                // Indices depend on buildAxis (which axis is the weak build direction).
                double interTens, interShear, vmInPlane;
                if (buildAxis == 0) {
                    // X is weak; YZ is the strong in-plane
                    interTens  = std::max(sx, 0.0);
                    interShear = std::sqrt(txy*txy + txz*txz);
                    vmInPlane  = std::sqrt(sy*sy - sy*sz + sz*sz + 3.0*tyz*tyz);
                } else if (buildAxis == 1) {
                    // Y is weak; XZ is the strong in-plane
                    interTens  = std::max(sy, 0.0);
                    interShear = std::sqrt(txy*txy + tyz*tyz);
                    vmInPlane  = std::sqrt(sx*sx - sx*sz + sz*sz + 3.0*txz*txz);
                } else {
                    // Z is weak; XY is the strong in-plane
                    interTens  = std::max(sz, 0.0);
                    interShear = std::sqrt(tyz*tyz + txz*txz);
                    vmInPlane  = std::sqrt(sx*sx - sx*sy + sy*sy + 3.0*txy*txy);
                }

                uint8_t mode = 0;
                if      (interTens  > fractureStress_interlayer) mode = 1;
                else if (interShear > fractureShear_interlayer)  mode = 2;
                else if (vmInPlane  > fractureStress_intralayer) mode = 3;
                failModeBuf[el] = mode;
            }
        }

        // ---- Kill overstressed elements ----
        int killed = 0;
        int killInterTens = 0, killInterShear = 0, killIntralayer = 0;
        float maxSVM = 0.0f;

        for (int el = 0; el < nElems; ++el) {
            if (sigmaVM[el] > maxSVM) maxSVM = sigmaVM[el];
            if (!model.elementAlive[el]) continue;

            bool kill = false;
            if (useFdmFracture) {
                kill = (failModeBuf[el] != 0);
                if (kill) {
                    model.elementFailureMode[el] = failModeBuf[el];
                    if      (failModeBuf[el] == 1) ++killInterTens;
                    else if (failModeBuf[el] == 2) ++killInterShear;
                    else                           ++killIntralayer;
                }
            } else {
                kill = (static_cast<double>(sigmaVM[el]) > fractureStress);
            }

            if (kill) {
                model.elementAlive[el]       = 0;
                model.elementFailureIter[el] = iter;
                model.elementVonMisesAtDeath[el] = sigmaVM[el]; // new_TODO_03: stress at death
                ++killed;
            }
        }

        const int alive = static_cast<int>(
            std::count(model.elementAlive.begin(), model.elementAlive.end(), uint8_t(1)));

        if (useFdmFracture) {
            std::cout << "[FRACTURE-FDM] iter " << iter
                      << ": killed " << killed
                      << " (interTens=" << killInterTens
                      << " interShear=" << killInterShear
                      << " intralayer=" << killIntralayer << ")"
                      << "  max σ_vm=" << maxSVM*1e-6 << " MPa"
                      << "  alive=" << alive << "/" << nElems << std::endl;
        } else {
            std::cout << "[FRACTURE] iter " << iter
                      << ": " << killed << " elements killed"
                      << "  max σ_vm=" << maxSVM*1e-6 << " MPa"
                      << "  alive=" << alive << "/" << nElems << std::endl;
        }

        // Redraw with the updated alive mask so the user sees progression.
        model.needsUpdate = true;
        model.buildBuffers();

        if (killed == 0) {
            std::cout << (useFdmFracture ? "[FRACTURE-FDM]" : "[FRACTURE]")
                      << " Converged (no new failures) after "
                      << iter+1 << " iteration(s)." << std::endl;
            break;
        }
    }

    if (useFdmFracture) {
        int totInterTens = 0, totInterShear = 0, totIntralayer = 0;
        for (int el = 0; el < nElems; ++el) {
            switch (model.elementFailureMode[el]) {
                case 1: ++totInterTens;   break;
                case 2: ++totInterShear;  break;
                case 3: ++totIntralayer;  break;
            }
        }
        std::cout << "[FRACTURE-FDM] mode totals: interTens=" << totInterTens
                  << "  interShear=" << totInterShear
                  << "  intralayer=" << totIntralayer << std::endl;
    }

    useMultithreading = savedUseMT;
    if (didScale) restoreGeometryToModel(model);
    return true;
}
