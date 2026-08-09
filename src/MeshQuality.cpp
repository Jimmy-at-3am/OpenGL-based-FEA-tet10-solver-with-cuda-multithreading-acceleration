// =============================================================================
//  MeshQuality.cpp -- per-element tetrahedral quality metrics.
//
//  Implements:
//    * initExactPredicates(maxCoord)
//    * computeReport(tetgenio, FEAParams)          -- Tet4 path
//    * emitReport(tetgenio, FEAParams[, ostream])  -- Tet4 path
//    * computeReportTet10(positions, connectivity, FEAParams)
//    * emitReportTet10(positions, connectivity, FEAParams[, ostream])
//
//  Numerical care-points:
//    #1  exactinit() must be called before any orient3d/insphere use.
//    #2  Guard Knupp denominator: if (det < 1e-30) return 0.
//   #16  Use std::clamp(x, -1, 1) before acos for dihedrals.
//        Equiangular skew is volume-based and normalised to [0,1] (not degrees).
//        Tet10 shape-gradient code is duplicated locally to keep mesh_diag
//        free of Eigen; do NOT pull in Tet10Element.h here.
// =============================================================================
#include "MeshQuality.h"
#include "BRepHandle.h"     // OCC-free; needed for the BRep overload signatures

#include "tetgen.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace MeshQuality {

namespace {

// --------------------------------------------------------------------------
//  Care-point #1: exactinit() bootstrap.
//  We call exactinit() at most once per process.  TetGen itself also calls
//  exactinit() from inside tetrahedralize(); doing it a second time here is
//  cheap (just recomputes a few floating-point constants) but we guard with
//  a once_flag to keep ordering deterministic.
// --------------------------------------------------------------------------
std::once_flag g_exactinit_once;

// --------------------------------------------------------------------------
//  Shape measures (all double precision).
// --------------------------------------------------------------------------

// Knupp/Pebay tetrahedral shape, normalised so a regular tet returns 1.0.
//
//   q = K * 3 * (det)^(2/3) / ||edges||_F^2
//
// where  K     = 2^(1/3)  (rescales the formula so q(regular) == 1)
//        det   = ((p1-p0) cross (p2-p0)) . (p3-p0)  == 6 * signed_vol
//        ||.|| = sum of squared edge lengths from p0 to p1, p2, p3
//
// Care-point #2: if det < 1e-30 (degenerate or inverted), return 0.
double knuppShape(const glm::dvec3& p0,
                  const glm::dvec3& p1,
                  const glm::dvec3& p2,
                  const glm::dvec3& p3)
{
    const glm::dvec3 e1 = p1 - p0;
    const glm::dvec3 e2 = p2 - p0;
    const glm::dvec3 e3 = p3 - p0;

    const double det = glm::dot(glm::cross(e1, e2), e3);
    if (det < 1e-30) return 0.0;                       // care-point #2

    const double frob2 = glm::dot(e1, e1)
                       + glm::dot(e2, e2)
                       + glm::dot(e3, e3);
    if (frob2 < 1e-30) return 0.0;

    // 2^(1/3) -- normalises q(regular tet) to exactly 1.
    constexpr double K = 1.2599210498948731647672106072782;

    // (det^2)^(1/3) == |det|^(2/3); det is guaranteed > 0 by the guard above.
    const double q = K * 3.0 * std::cbrt(det * det) / frob2;

    // Clamp to [0, 1] -- floating-point noise on a near-regular tet can push
    // q a hair above 1.0; the histogram top bucket is closed at 1.00 so this
    // matters for binning.
    if (q < 0.0) return 0.0;
    if (q > 1.0) return 1.0;
    return q;
}

// Dihedral angle range (min and max in degrees) over the 6 edges of a tet.
//
// For an edge from a -> b, with the two opposite vertices c and d, the
// interior dihedral satisfies
//     cos(theta) = (e x (c-a)) . (e x (d-a)) / (|.| * |.|)
// with e = (b - a).
//
// Care-point #16: clamp the cosine into [-1, 1] before acos.
// Returns {0, 0} for a fully degenerate tet (all faces collapsed).
struct DihedralRange { double minDeg; double maxDeg; };

DihedralRange dihedralRangeDeg(const glm::dvec3& p0,
                               const glm::dvec3& p1,
                               const glm::dvec3& p2,
                               const glm::dvec3& p3)
{
    static constexpr int kEdges[6][4] = {
        {0, 1, 2, 3},
        {0, 2, 1, 3},
        {0, 3, 1, 2},
        {1, 2, 0, 3},
        {1, 3, 0, 2},
        {2, 3, 0, 1},
    };
    const glm::dvec3* P[4] = {&p0, &p1, &p2, &p3};

    constexpr double kRadToDeg = 57.29577951308232087679815481410517;

    double minDeg = 180.0;
    double maxDeg =   0.0;
    for (const auto& E : kEdges) {
        const glm::dvec3& a = *P[E[0]];
        const glm::dvec3& b = *P[E[1]];
        const glm::dvec3& c = *P[E[2]];
        const glm::dvec3& d = *P[E[3]];

        const glm::dvec3 e  = b - a;
        const glm::dvec3 n1 = glm::cross(e, c - a);
        const glm::dvec3 n2 = glm::cross(e, d - a);

        const double l1 = glm::length(n1);
        const double l2 = glm::length(n2);
        if (l1 < 1e-30 || l2 < 1e-30) {
            return {0.0, 0.0};
        }

        double cosTh = glm::dot(n1, n2) / (l1 * l2);
        cosTh = std::clamp(cosTh, -1.0, 1.0); // care-point #16
        const double deg = std::acos(cosTh) * kRadToDeg;
        if (deg < minDeg) minDeg = deg;
        if (deg > maxDeg) maxDeg = deg;
    }
    return {minDeg, maxDeg};
}

// Source-compatible wrapper used by the existing call sites in computeReport.
inline double minDihedralDeg(const glm::dvec3& p0,
                             const glm::dvec3& p1,
                             const glm::dvec3& p2,
                             const glm::dvec3& p3)
{
    return dihedralRangeDeg(p0, p1, p2, p3).minDeg;
}

// Minimum corner scaled Jacobian for the tet.  Regular tet returns +1.0;
// an inverted tet returns a negative value (and the verdict logic counts
// any element with min-sJ <= 0 as "inverted").
//
// At each of the 4 corners we form a local Jacobian J_i whose columns are
// the three edges leaving that corner *in a fixed cyclic order chosen so
// that det(J_i) carries the same sign as the signed volume of the tet*.
// The corner sJ is sqrt(2) * det(J_i) / (|e1|*|e2|*|e3|); the leading
// sqrt(2) factor normalises q(regular) to 1.
double scaledJacobianMin(const glm::dvec3& p0,
                         const glm::dvec3& p1,
                         const glm::dvec3& p2,
                         const glm::dvec3& p3)
{
    // NOTE: kSqrt2 is declared inside the lambda body, not as a function-local
    // constexpr that the lambda would otherwise have to capture.  MSVC (cl 19.50)
    // raises C3493 in the latter form even with C++17's "no-capture-needed"
    // rule; declaring the constant lambda-locally side-steps it.
    auto cornerSJ = [](const glm::dvec3& e1,
                       const glm::dvec3& e2,
                       const glm::dvec3& e3) -> double
    {
        constexpr double kSqrt2 = 1.4142135623730950488016887242097;
        const double det = glm::dot(glm::cross(e1, e2), e3);
        const double L1  = glm::length(e1);
        const double L2  = glm::length(e2);
        const double L3  = glm::length(e3);
        const double denom = L1 * L2 * L3;
        if (denom < 1e-30) return 0.0;
        return kSqrt2 * det / denom;
    };

    // Cyclic orderings chosen so that all 4 corner dets share the sign of
    // the signed tetrahedron volume:
    //   corner 0:  edges to 1, 2, 3
    //   corner 1:  edges to 0, 3, 2
    //   corner 2:  edges to 3, 0, 1
    //   corner 3:  edges to 0, 2, 1
    const double s0 = cornerSJ(p1 - p0, p2 - p0, p3 - p0);
    const double s1 = cornerSJ(p0 - p1, p3 - p1, p2 - p1);
    const double s2 = cornerSJ(p3 - p2, p0 - p2, p1 - p2);
    const double s3 = cornerSJ(p0 - p3, p2 - p3, p1 - p3);
    return std::min({s0, s1, s2, s3});
}

// Radius ratio: rho = 3 * r_in / R_circum, range [0, 1] (1 == regular tet).
//
// r_in    = 3 * V / A_total   (in-sphere radius)
// R_circum = circumscribed-sphere radius
//
// Guards: V < 1e-30 -> return 0;  A_total < 1e-30 -> return 0.
double radiusRatio(const glm::dvec3& p0,
                   const glm::dvec3& p1,
                   const glm::dvec3& p2,
                   const glm::dvec3& p3)
{
    const glm::dvec3 e1 = p1 - p0;
    const glm::dvec3 e2 = p2 - p0;
    const glm::dvec3 e3 = p3 - p0;

    const double vol6 = std::abs(glm::dot(glm::cross(e1, e2), e3)); // 6*V
    if (vol6 < 1e-30) return 0.0;
    const double V = vol6 / 6.0;

    // Face areas (norm of cross product / 2 per face).
    const glm::dvec3 f0 = glm::cross(p2 - p1, p3 - p1); // face opposite p0
    const glm::dvec3 f1 = glm::cross(p0 - p2, p3 - p2); // face opposite p1
    const glm::dvec3 f2 = glm::cross(p0 - p3, p1 - p3); // face opposite p2
    const glm::dvec3 f3 = glm::cross(p1 - p0, p2 - p0); // face opposite p3
    const double A_total = 0.5 * (glm::length(f0) + glm::length(f1)
                                + glm::length(f2) + glm::length(f3));
    if (A_total < 1e-30) return 0.0;

    // r_in = 3V / A_total
    const double r_in = 3.0 * V / A_total;

    // R_circum: for a tet with edge-vectors e1,e2,e3 from p0, using the
    // formula R = |e1||e2||e3||e4||e5||e6| / (8*|6V|) is complex; simpler:
    // R = |e1 x e2||e3| / (2 * |det|)  (one valid form) --  but the robust
    // general formula is R = |(b-a)||(c-a)||(d-a)| * ... which reduces to:
    //
    //   R = sqrt( |b-a|^2 * |c-a|^2 * |d-a|^2 ) / sqrt(...) -- complicated.
    //
    // Use the standard formula via the opposite-edge product sum:
    //   24*V*R = |e01||e23| * |e01+e23| + |e02||e13| * ... (Euler formula)
    // Instead, use the simpler direct approach:
    //   R = |x| where x solves 2*J^T*x = [|e1|^2, |e2|^2, |e3|^2]
    //   i.e. x = 0.5 * J^{-T} * [|e1|^2, |e2|^2, |e3|^2]
    {
        const double d11 = glm::dot(e1, e1);
        const double d22 = glm::dot(e2, e2);
        const double d33 = glm::dot(e3, e3);
        const double d12 = glm::dot(e1, e2);
        const double d13 = glm::dot(e1, e3);
        const double d23 = glm::dot(e2, e3);

        // det(J) = vol6  (already computed).
        const double detSq = vol6 * vol6;
        if (detSq < 1e-60) return 0.0;

        // Cofactor matrix of J (= det * J^{-T}):
        //   C = | (e2 x e3)  (e3 x e1)  (e1 x e2) |^T  -- columns are cofactors
        // circumcenter from p0: x_c = 0.5 * J^{-T} * [d11, d22, d33]
        //                          = 0.5 / det(J) * C^T * [d11, d22, d33]
        const glm::dvec3 C0 = glm::cross(e2, e3); // cofactor col 0
        const glm::dvec3 C1 = glm::cross(e3, e1); // cofactor col 1
        const glm::dvec3 C2 = glm::cross(e1, e2); // cofactor col 2

        // x_c = (d11*C0 + d22*C1 + d33*C2) * 0.5 / vol6
        const glm::dvec3 xc = (d11 * C0 + d22 * C1 + d33 * C2) * (0.5 / vol6);
        const double R_circum = glm::length(xc);
        if (R_circum < 1e-30) return 0.0;

        const double rho = 3.0 * r_in / R_circum;
        // Clamp: floating-point can push slightly above 1 on a regular tet.
        return std::clamp(rho, 0.0, 1.0);

        (void)d12; (void)d13; (void)d23; // suppress unused-variable warnings
    }
}

// Volume-based equiangular skew for a tetrahedron, in [0, 1].
//
//   V_equi(R) = (8*sqrt(3)/27) * R^3   (volume of a regular tet with same
//                                        circumscribed sphere radius R)
//   S = (V_equi - V) / V_equi
//
// S = 0 for a regular tet (V == V_equi); S -> 1 for a degenerate sliver.
// R_circum < 1e-30 -> return 1.0 (fully degenerate).
// Care: result clamped to [0, 1] against float noise.
double equiangularSkew(const glm::dvec3& p0,
                       const glm::dvec3& p1,
                       const glm::dvec3& p2,
                       const glm::dvec3& p3)
{
    const glm::dvec3 e1 = p1 - p0;
    const glm::dvec3 e2 = p2 - p0;
    const glm::dvec3 e3 = p3 - p0;

    const double vol6 = std::abs(glm::dot(glm::cross(e1, e2), e3));
    const double V    = vol6 / 6.0;

    // Circumradius via cofactor formula (same as in radiusRatio above).
    const double d11 = glm::dot(e1, e1);
    const double d22 = glm::dot(e2, e2);
    const double d33 = glm::dot(e3, e3);
    const glm::dvec3 C0 = glm::cross(e2, e3);
    const glm::dvec3 C1 = glm::cross(e3, e1);
    const glm::dvec3 C2 = glm::cross(e1, e2);

    if (vol6 < 1e-30) return 1.0; // degenerate -> worst skew

    const glm::dvec3 xc = (d11 * C0 + d22 * C1 + d33 * C2) * (0.5 / vol6);
    const double R = glm::length(xc);
    if (R < 1e-30) return 1.0;

    // V_equi = 8*sqrt(3)/27 * R^3
    constexpr double k = 8.0 * 1.7320508075688772935 / 27.0; // 8*sqrt(3)/27
    const double V_equi = k * R * R * R;
    if (V_equi < 1e-30) return 1.0;

    const double S = (V_equi - V) / V_equi;
    return std::clamp(S, 0.0, 1.0);
}

// --------------------------------------------------------------------------
// Tet10 isoparametric quality -- evaluated at the 4-point Gauss rule.
//
// Shape gradients are duplicated here (NOT from Tet10Element.h) so that
// MeshQuality.cpp remains free of Eigen and IMaterial dependencies.
// These must stay bit-for-bit identical to EvalShapeGradients in
// Tet10Element.cpp.  If you update one, update both.
// --------------------------------------------------------------------------
namespace tet10 {

// 4-point Gauss rule for the reference tetrahedron (same constants as
// Tet10Element.cpp).
constexpr double GP_A = 0.5854101966249685;
constexpr double GP_B = 0.1381966011250105;

struct Vec3d { double x, y, z; };

// Shape-function reference-space gradients (10 x 3) at reference point.
// Row i = [dNi/dxi, dNi/deta, dNi/dzeta].
inline void evalShapeGrad(double xi, double eta, double zeta,
                          double dN[10][3])
{
    const double L0 = 1.0 - xi - eta - zeta;
    const double L1 = xi;
    const double L2 = eta;
    const double L3 = zeta;

    dN[0][0] = -(4.0*L0-1.0); dN[0][1] = -(4.0*L0-1.0); dN[0][2] = -(4.0*L0-1.0);
    dN[1][0] =  (4.0*L1-1.0); dN[1][1] =  0.0;           dN[1][2] =  0.0;
    dN[2][0] =  0.0;           dN[2][1] =  (4.0*L2-1.0); dN[2][2] =  0.0;
    dN[3][0] =  0.0;           dN[3][1] =  0.0;           dN[3][2] =  (4.0*L3-1.0);

    dN[4][0] =  4.0*(L0-L1);  dN[4][1] = -4.0*L1;        dN[4][2] = -4.0*L1;
    dN[5][0] =  4.0*L2;       dN[5][1] =  4.0*L1;        dN[5][2] =  0.0;
    dN[6][0] = -4.0*L2;       dN[6][1] =  4.0*(L0-L2);   dN[6][2] = -4.0*L2;
    dN[7][0] = -4.0*L3;       dN[7][1] = -4.0*L3;        dN[7][2] =  4.0*(L0-L3);
    dN[8][0] =  4.0*L3;       dN[8][1] =  0.0;            dN[8][2] =  4.0*L1;
    dN[9][0] =  0.0;           dN[9][1] =  4.0*L3;        dN[9][2] =  4.0*L2;
}

// Compute the physical Jacobian J (3x3) at a Gauss point from the 10
// node positions.  Returns det(J).
// nodes[i] = {x, y, z} for node i (0-based, corner-first ordering matching
// Tet10Element: corners 0-3, mid-edges 4-9).
double computeJacobian(const Vec3d nodes[10],
                       double xi, double eta, double zeta,
                       double J[3][3])
{
    double dN[10][3];
    evalShapeGrad(xi, eta, zeta, dN);

    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            J[r][c] = 0.0;

    // J[r][c] = sum_i  dN[i][r] * nodes[i][c]
    for (int i = 0; i < 10; ++i) {
        const double nx = nodes[i].x, ny = nodes[i].y, nz = nodes[i].z;
        J[0][0] += dN[i][0] * nx;  J[0][1] += dN[i][0] * ny;  J[0][2] += dN[i][0] * nz;
        J[1][0] += dN[i][1] * nx;  J[1][1] += dN[i][1] * ny;  J[1][2] += dN[i][1] * nz;
        J[2][0] += dN[i][2] * nx;  J[2][1] += dN[i][2] * ny;  J[2][2] += dN[i][2] * nz;
    }

    return J[0][0]*(J[1][1]*J[2][2]-J[1][2]*J[2][1])
          -J[0][1]*(J[1][0]*J[2][2]-J[1][2]*J[2][0])
          +J[0][2]*(J[1][0]*J[2][1]-J[1][1]*J[2][0]);
}

// Frobenius norm squared of a 3x3 matrix.
inline double frobSq(const double J[3][3])
{
    double s = 0.0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            s += J[r][c] * J[r][c];
    return s;
}

// Row norms product of a 3x3 matrix (for scaled Jacobian denominator).
inline double rowNormProduct(const double J[3][3])
{
    double n0 = std::sqrt(J[0][0]*J[0][0]+J[0][1]*J[0][1]+J[0][2]*J[0][2]);
    double n1 = std::sqrt(J[1][0]*J[1][0]+J[1][1]*J[1][1]+J[1][2]*J[1][2]);
    double n2 = std::sqrt(J[2][0]*J[2][0]+J[2][1]*J[2][1]+J[2][2]*J[2][2]);
    return n0 * n1 * n2;
}

} // namespace tet10

// Minimum scaled Jacobian of a Tet10 element, evaluated at the 4 Gauss points.
// mid-edge nodes expected at indices 4-9 (same ordering as Tet10Element.cpp).
// Returns the minimum sJ value over all 4 Gauss points; regular tet -> 1.0.
double tet10ScaledJacobianMin(const glm::dvec3 nodes[10])
{
    static const double gps[4][3] = {
        {tet10::GP_A, tet10::GP_B, tet10::GP_B},
        {tet10::GP_B, tet10::GP_A, tet10::GP_B},
        {tet10::GP_B, tet10::GP_B, tet10::GP_A},
        {tet10::GP_B, tet10::GP_B, tet10::GP_B},
    };

    tet10::Vec3d n10[10];
    for (int i = 0; i < 10; ++i)
        n10[i] = {nodes[i].x, nodes[i].y, nodes[i].z};

    constexpr double kSqrt2 = 1.4142135623730950488016887242097;
    double minSJ = 1e300;
    for (const auto& gp : gps) {
        double J[3][3];
        const double detJ = tet10::computeJacobian(n10, gp[0], gp[1], gp[2], J);
        const double denom = tet10::rowNormProduct(J);
        if (denom < 1e-30) { minSJ = 0.0; continue; }
        const double sJ = kSqrt2 * detJ / denom;
        if (sJ < minSJ) minSJ = sJ;
    }
    return minSJ;
}

// Isoparametric Knupp shape of a Tet10 element, averaged over 4 Gauss points.
// Regular tet (mid-edge nodes at linear midpoints) -> 1.0.
double tet10IsoparametricKnupp(const glm::dvec3 nodes[10])
{
    static const double gps[4][3] = {
        {tet10::GP_A, tet10::GP_B, tet10::GP_B},
        {tet10::GP_B, tet10::GP_A, tet10::GP_B},
        {tet10::GP_B, tet10::GP_B, tet10::GP_A},
        {tet10::GP_B, tet10::GP_B, tet10::GP_B},
    };

    tet10::Vec3d n10[10];
    for (int i = 0; i < 10; ++i)
        n10[i] = {nodes[i].x, nodes[i].y, nodes[i].z};

    constexpr double K = 1.2599210498948731647672106072782; // 2^(1/3)
    double sum = 0.0;
    for (const auto& gp : gps) {
        double J[3][3];
        const double detJ = tet10::computeJacobian(n10, gp[0], gp[1], gp[2], J);
        if (detJ < 1e-30) continue; // care-point #2
        const double frob2 = tet10::frobSq(J);
        if (frob2 < 1e-30) continue;
        double q = K * 3.0 * std::cbrt(detJ * detJ) / frob2;
        q = std::clamp(q, 0.0, 1.0);
        sum += q;
    }
    return std::clamp(sum / 4.0, 0.0, 1.0);
}

// --------------------------------------------------------------------------
// Histogram bin selectors.
// --------------------------------------------------------------------------

// Knupp shape bin: 0:[0.00,0.20) 1:[0.20,0.50) 2:[0.50,0.80) 3:[0.80,1.00]
// Histogram bin selector for Knupp shape.
inline int knuppBinIndex(double q)
{
    // 0: [0.00, 0.20)  1: [0.20, 0.50)  2: [0.50, 0.80)  3: [0.80, 1.00]
    if (q < 0.20) return 0;
    if (q < 0.50) return 1;
    if (q < 0.80) return 2;
    return 3;
}

// Skew bin: 0:[0.00,0.25) 1:[0.25,0.50) 2:[0.50,0.80) 3:[0.80,1.00]
inline int skewBinIndex(double s)
{
    if (s < 0.25) return 0;
    if (s < 0.50) return 1;
    if (s < 0.80) return 2;
    return 3;
}

// --------------------------------------------------------------------------
// Percentile helper (O(N) via nth_element).
// For < 100 elements p99 collapses to max -- documented in the spec.
// --------------------------------------------------------------------------
MetricPercentiles computePercentiles(std::vector<double> v)
{
    if (v.empty()) return {};
    const std::size_t n = v.size();
    auto pick = [&](double pct) -> double {
        const std::size_t k = std::min(n - 1,
            static_cast<std::size_t>(std::floor(pct * static_cast<double>(n - 1) + 0.5)));
        std::nth_element(v.begin(), v.begin() + k, v.end());
        return v[k];
    };
    MetricPercentiles out;
    out.p05 = pick(0.05);
    out.p50 = pick(0.50);
    out.p95 = pick(0.95);
    out.p99 = pick(0.99);
    return out;
}

// Median via nth_element (modifies the input vector).  Returns 0 for an
// empty input.  For even N we average the two middle samples for stability.
double medianInPlace(std::vector<double>& v)
{
    const std::size_t n = v.size();
    if (n == 0) return 0.0;
    const std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double upper = v[mid];
    if (n & 1u) return upper;
    // Even count -- the lower middle is the max of the left partition.
    const double lower = *std::max_element(v.begin(), v.begin() + mid);
    return 0.5 * (lower + upper);
}

// =============================================================================
// BVH for nearest-triangle queries (Hausdorff + normal deviation).
// =============================================================================

// Axis-aligned bounding box (double precision).
struct AABB3d {
    glm::dvec3 lo{ 1e300,  1e300,  1e300};
    glm::dvec3 hi{-1e300, -1e300, -1e300};

    void expandPoint(const glm::dvec3& p) { lo = glm::min(lo,p); hi = glm::max(hi,p); }
    void expandBox  (const AABB3d& o)     { lo = glm::min(lo,o.lo); hi = glm::max(hi,o.hi); }

    // Squared distance from p to the nearest point in/on this AABB; 0 if inside.
    double sqDistToPoint(const glm::dvec3& p) const {
        double sq = 0.0;
        for (int i = 0; i < 3; ++i) {
            double d = 0.0;
            if      (p[i] < lo[i]) d = lo[i] - p[i];
            else if (p[i] > hi[i]) d = p[i]  - hi[i];
            sq += d * d;
        }
        return sq;
    }
};

// Closest point on triangle (a,b,c) to query p.
// Ericson "Real-Time Collision Detection" §5.1.5.
// Returns { sqDist, closestPt, baryU, baryV, baryW }  (u+v+w == 1).
struct CPResult { double sqDist; glm::dvec3 pt; double u, v, w; };

CPResult closestPointOnTri(const glm::dvec3& p,
                            const glm::dvec3& a,
                            const glm::dvec3& b,
                            const glm::dvec3& c)
{
    const glm::dvec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = glm::dot(ab,ap), d2 = glm::dot(ac,ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        const glm::dvec3 pp = a;
        return { glm::dot(p-pp,p-pp), pp, 1.0, 0.0, 0.0 };
    }
    const glm::dvec3 bp = p - b;
    const double d3 = glm::dot(ab,bp), d4 = glm::dot(ac,bp);
    if (d3 >= 0.0 && d4 <= d3) {
        const glm::dvec3 pp = b;
        return { glm::dot(p-pp,p-pp), pp, 0.0, 1.0, 0.0 };
    }
    const double vc = d1*d4 - d3*d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const double t = d1 / (d1 - d3);
        const glm::dvec3 pp = a + t * ab;
        return { glm::dot(p-pp,p-pp), pp, 1.0-t, t, 0.0 };
    }
    const glm::dvec3 cp = p - c;
    const double d5 = glm::dot(ab,cp), d6 = glm::dot(ac,cp);
    if (d6 >= 0.0 && d5 <= d6) {
        const glm::dvec3 pp = c;
        return { glm::dot(p-pp,p-pp), pp, 0.0, 0.0, 1.0 };
    }
    const double vb = d5*d2 - d1*d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const double t = d2 / (d2 - d6);
        const glm::dvec3 pp = a + t * ac;
        return { glm::dot(p-pp,p-pp), pp, 1.0-t, 0.0, t };
    }
    const double va    = d3*d6 - d5*d4;
    const double denom = 1.0 / (va + vb + vc);
    const double v = vb * denom, w = vc * denom;
    const glm::dvec3 pp = a + v * ab + w * ac;
    return { glm::dot(p-pp,p-pp), pp, 1.0-v-w, v, w };
}

// Median-split BVH over a triangle mesh for nearest-point queries.
// Caller must keep 'positions' and 'triIndices' alive for the lifetime of
// nearest() calls (raw pointers to external, const data).
struct TriBVH {
    struct Node {
        AABB3d box;
        int    leftChild  = -1; // -1 if leaf
        int    rightChild = -1;
        int    primStart  =  0;
        int    primCount  =  0; // > 0 : leaf; 0 : internal
    };
    struct NearResult {
        double     sqDist = 1e300;
        glm::dvec3 pt    {0.0, 0.0, 0.0};
        glm::dvec3 normal{0.0, 0.0, 1.0}; // interpolated if vertNormals is non-empty
        int        triIdx = -1;
    };

    std::vector<Node>       nodes;
    std::vector<int>        prims;       // triangle indices in build order
    const glm::dvec3*       positions  = nullptr;
    const int*              triIndices = nullptr; // 3 ints per triangle
    std::vector<glm::dvec3> vertNormals; // may be empty (no normal interpolation)

    void build(const glm::dvec3* verts, const int* tris, int nTris,
               const std::vector<glm::dvec3>& vNrm = {})
    {
        positions   = verts;
        triIndices  = tris;
        vertNormals = vNrm;
        prims.resize(nTris);
        for (int i = 0; i < nTris; ++i) prims[i] = i;
        nodes.clear();
        nodes.reserve(std::max(1, 2 * nTris)); // no realloc during recursion
        if (nTris > 0) buildNode(0, nTris);
    }

    NearResult nearest(const glm::dvec3& query) const {
        NearResult best;
        if (!nodes.empty()) nearestRec(0, query, best.sqDist, best);
        return best;
    }

private:
    // Returns index of new node in nodes[].  Accesses nodes[] only by index
    // after recursive calls so vector reallocation does not invalidate it.
    int buildNode(int first, int count)
    {
        const int idx = static_cast<int>(nodes.size());
        nodes.emplace_back();

        AABB3d box;
        for (int i = first; i < first + count; ++i) {
            const int t = prims[i];
            box.expandPoint(positions[triIndices[t*3+0]]);
            box.expandPoint(positions[triIndices[t*3+1]]);
            box.expandPoint(positions[triIndices[t*3+2]]);
        }
        nodes[idx].box       = box;
        nodes[idx].primStart = first;
        nodes[idx].primCount = count;

        if (count <= 4) return idx; // leaf

        const glm::dvec3 ext = box.hi - box.lo;
        int axis = (ext.y > ext.x) ? 1 : 0;
        if (ext.z > ext[axis]) axis = 2;

        const int mid = first + count / 2;
        std::nth_element(prims.begin() + first,
                         prims.begin() + mid,
                         prims.begin() + first + count,
                         [&](int a, int b) {
                             auto cen = [&](int t) {
                                 return (positions[triIndices[t*3+0]][axis]
                                       + positions[triIndices[t*3+1]][axis]
                                       + positions[triIndices[t*3+2]][axis]) / 3.0;
                             };
                             return cen(a) < cen(b);
                         });

        const int li = buildNode(first,      mid - first);
        const int ri = buildNode(mid, first + count - mid);
        nodes[idx].leftChild  = li;
        nodes[idx].rightChild = ri;
        nodes[idx].primCount  = 0; // internal
        return idx;
    }

    void nearestRec(int nodeIdx, const glm::dvec3& q,
                    double& bestSq, NearResult& best) const
    {
        const Node& nd = nodes[nodeIdx];
        if (nd.box.sqDistToPoint(q) >= bestSq) return;

        if (nd.primCount > 0) {
            for (int i = nd.primStart; i < nd.primStart + nd.primCount; ++i) {
                const int t = prims[i];
                const CPResult r = closestPointOnTri(
                    q,
                    positions[triIndices[t*3+0]],
                    positions[triIndices[t*3+1]],
                    positions[triIndices[t*3+2]]);
                if (r.sqDist < bestSq) {
                    bestSq      = r.sqDist;
                    best.sqDist = r.sqDist;
                    best.pt     = r.pt;
                    best.triIdx = t;
                    if (!vertNormals.empty()) {
                        const glm::dvec3& na = vertNormals[triIndices[t*3+0]];
                        const glm::dvec3& nb = vertNormals[triIndices[t*3+1]];
                        const glm::dvec3& nc = vertNormals[triIndices[t*3+2]];
                        const glm::dvec3  n  = r.u*na + r.v*nb + r.w*nc;
                        const double      len = glm::length(n);
                        best.normal = (len > 1e-12) ? n / len : glm::dvec3(0.0, 0.0, 1.0);
                    }
                }
            }
        } else {
            nearestRec(nd.leftChild,  q, bestSq, best);
            nearestRec(nd.rightChild, q, bestSq, best);
        }
    }
};

} // anonymous namespace

// -----------------------------------------------------------------------------
//  Public: initExactPredicates
// -----------------------------------------------------------------------------
void initExactPredicates(double maxCoord)
{
    std::call_once(g_exactinit_once, [maxCoord]() {
        // Shewchuk's signature in this fork:
        //   exactinit(verbose, noexact, nofilter, maxx, maxy, maxz)
        // Pass the same bound on each axis -- a conservative envelope is
        // enough to choose the right error tolerances.
        const double mc = (maxCoord > 0.0) ? maxCoord : 1.0;
        ::exactinit(/*verbose=*/0, /*noexact=*/0, /*nofilter=*/0, mc, mc, mc);
    });
}

// -----------------------------------------------------------------------------
//  Public: computeReport
// -----------------------------------------------------------------------------
QualityReport computeReport(const tetgenio& out, const FEAParams& p)
{
    QualityReport rpt;
    rpt.radiusEdgeBound     = static_cast<double>(p.tetRadiusEdge);
    rpt.minDihedralBoundDeg = static_cast<double>(p.tetMinDihedralDeg);
    rpt.hardMinDihedralFailDeg = 5.0;

    const int nTets = out.numberoftetrahedra;
    rpt.numElements = nTets;
    if (nTets <= 0 || out.tetrahedronlist == nullptr || out.pointlist == nullptr) {
        // Nothing to score -- return a default (PASS, all zeros) report.
        return rpt;
    }

    // Care-point #1: make sure exact predicates are alive before we touch
    // any geometry that might be fed into orient3d (we don't call orient3d
    // ourselves here, but downstream MeshQuality TODOs will, and TetGen has
    // typically already called exactinit -- this is the belt-and-braces
    // bootstrap requested by the spec).
    {
        const REAL* pl = out.pointlist;
        double maxCoord = 1.0;
        for (int i = 0; i < out.numberofpoints; ++i) {
            const double x = std::fabs(static_cast<double>(pl[i * 3 + 0]));
            const double y = std::fabs(static_cast<double>(pl[i * 3 + 1]));
            const double z = std::fabs(static_cast<double>(pl[i * 3 + 2]));
            if (x > maxCoord) maxCoord = x;
            if (y > maxCoord) maxCoord = y;
            if (z > maxCoord) maxCoord = z;
        }
        initExactPredicates(maxCoord);
    }

    // Per-element output buffers -- threads write to disjoint indices.
    std::vector<double>     knuppArr(nTets, 0.0);
    std::vector<double>     dihedralArr(nTets, 0.0);    // per-tet min-dihedral
    std::vector<double>     dihedralMaxArr(nTets, 0.0); // per-tet max-dihedral
    std::vector<double>     sJacArr(nTets, 0.0);
    std::vector<double>     radiusRatioArr(nTets, 0.0);
    std::vector<double>     skewArr(nTets, 0.0);
    std::vector<glm::dvec3> centroidArr(nTets, glm::dvec3(0.0));

    const REAL* PL    = out.pointlist;
    const int*  TL    = out.tetrahedronlist;
    const int   firstN = out.firstnumber;       // usually 0; account for it anyway

    const int verticesPerTet = out.numberofcorners > 0 ? out.numberofcorners : 4;
    // We only need the first 4 corners (linear tet) regardless of high-order
    // mid-edge nodes the user may have requested.
    const bool isTet10 = (verticesPerTet == 10);
    rpt.isTet10 = isTet10;

    const bool useMT = p.useMultithreading;

    // Histogram: thread-local bins, merged serially after the loop.
    // Layout: indices [0..3] = Knupp bins, [4..7] = Skew bins.
    // We bound the thread count even outside OpenMP so the merge logic
    // is identical in both paths.
    int nThreads = 1;
#ifdef _OPENMP
    if (useMT) nThreads = omp_get_max_threads();
#endif
    if (nThreads < 1) nThreads = 1;

    std::vector<std::array<std::size_t, 8>> tlBins(
        nThreads, std::array<std::size_t, 8>{0u,0u,0u,0u,0u,0u,0u,0u});

#ifdef _OPENMP
    #pragma omp parallel if (useMT) num_threads(nThreads)
    {
        const int tid = omp_get_thread_num();
        auto& bin = tlBins[tid];
        #pragma omp for schedule(static)
        for (int el = 0; el < nTets; ++el) {
            const int i0 = TL[el * verticesPerTet + 0] - firstN;
            const int i1 = TL[el * verticesPerTet + 1] - firstN;
            const int i2 = TL[el * verticesPerTet + 2] - firstN;
            const int i3 = TL[el * verticesPerTet + 3] - firstN;
            const glm::dvec3 p0(PL[i0*3+0], PL[i0*3+1], PL[i0*3+2]);
            const glm::dvec3 p1(PL[i1*3+0], PL[i1*3+1], PL[i1*3+2]);
            const glm::dvec3 p2(PL[i2*3+0], PL[i2*3+1], PL[i2*3+2]);
            const glm::dvec3 p3(PL[i3*3+0], PL[i3*3+1], PL[i3*3+2]);

            double q              = knuppShape(p0, p1, p2, p3);
            const DihedralRange dr = dihedralRangeDeg(p0, p1, p2, p3);
            double sJ             = scaledJacobianMin(p0, p1, p2, p3);
            const double rr       = radiusRatio(p0, p1, p2, p3);
            const double sk       = equiangularSkew(p0, p1, p2, p3);
            if (isTet10) {
                glm::dvec3 nodes[10];
                for (int k = 0; k < 10; ++k) {
                    const int ik = TL[el * verticesPerTet + k] - firstN;
                    nodes[k] = glm::dvec3(PL[ik*3+0], PL[ik*3+1], PL[ik*3+2]);
                }
                q  = tet10IsoparametricKnupp(nodes);
                sJ = tet10ScaledJacobianMin(nodes);
            }

            knuppArr[el]       = q;
            dihedralArr[el]    = dr.minDeg;
            dihedralMaxArr[el] = dr.maxDeg;
            sJacArr[el]        = sJ;
            radiusRatioArr[el] = rr;
            skewArr[el]        = sk;
            centroidArr[el]    = 0.25 * (p0 + p1 + p2 + p3);
            bin[knuppBinIndex(q)]  += 1u;      // [0..3]
            bin[4 + skewBinIndex(sk)] += 1u;   // [4..7]
        }
    }
#else
    (void)useMT;
    auto& bin = tlBins[0];
    for (int el = 0; el < nTets; ++el) {
        const int i0 = TL[el * verticesPerTet + 0] - firstN;
        const int i1 = TL[el * verticesPerTet + 1] - firstN;
        const int i2 = TL[el * verticesPerTet + 2] - firstN;
        const int i3 = TL[el * verticesPerTet + 3] - firstN;
        const glm::dvec3 p0(PL[i0*3+0], PL[i0*3+1], PL[i0*3+2]);
        const glm::dvec3 p1(PL[i1*3+0], PL[i1*3+1], PL[i1*3+2]);
        const glm::dvec3 p2(PL[i2*3+0], PL[i2*3+1], PL[i2*3+2]);
        const glm::dvec3 p3(PL[i3*3+0], PL[i3*3+1], PL[i3*3+2]);

        double q              = knuppShape(p0, p1, p2, p3);
        const DihedralRange dr = dihedralRangeDeg(p0, p1, p2, p3);
        double sJ             = scaledJacobianMin(p0, p1, p2, p3);
        const double rr       = radiusRatio(p0, p1, p2, p3);
        const double sk       = equiangularSkew(p0, p1, p2, p3);
        if (isTet10) {
            glm::dvec3 nodes[10];
            for (int k = 0; k < 10; ++k) {
                const int ik = TL[el * verticesPerTet + k] - firstN;
                nodes[k] = glm::dvec3(PL[ik*3+0], PL[ik*3+1], PL[ik*3+2]);
            }
            q  = tet10IsoparametricKnupp(nodes);
            sJ = tet10ScaledJacobianMin(nodes);
        }

        knuppArr[el]       = q;
        dihedralArr[el]    = dr.minDeg;
        dihedralMaxArr[el] = dr.maxDeg;
        sJacArr[el]        = sJ;
        radiusRatioArr[el] = rr;
        skewArr[el]        = sk;
        centroidArr[el]    = 0.25 * (p0 + p1 + p2 + p3);
        bin[knuppBinIndex(q)]  += 1u;      // [0..3]
        bin[4 + skewBinIndex(sk)] += 1u;   // [4..7]
    }
#endif

    // Serial merge of thread-local histogram bins (knupp [0..3], skew [4..7]).
    for (const auto& tb : tlBins) {
        rpt.knuppHist.bin_0_20   += tb[0];
        rpt.knuppHist.bin_20_50  += tb[1];
        rpt.knuppHist.bin_50_80  += tb[2];
        rpt.knuppHist.bin_80_100 += tb[3];
        rpt.skewHist.bin_0_25    += tb[4];
        rpt.skewHist.bin_25_50   += tb[5];
        rpt.skewHist.bin_50_80   += tb[6];
        rpt.skewHist.bin_80_100  += tb[7];
    }

    // Count inverted / sliver elements.  `sliverCount` tracks the user/TetGen
    // target threshold, while `severeSliverCount` tracks the hard-reject floor.
    // The verdict allows a small sliver tail by percentage because TetGen's
    // min-dihedral switch is a target, not a guarantee.
    const double sliverThr = static_cast<double>(p.tetMinDihedralDeg);
    const double hardSliverThr = rpt.hardMinDihedralFailDeg;
    int inv = 0, sliv = 0, severeSliv = 0;
    for (int el = 0; el < nTets; ++el) {
        if (sJacArr[el]    <= 0.0)        ++inv;
        if (dihedralArr[el] < sliverThr)  ++sliv;
        if (dihedralArr[el] < hardSliverThr) ++severeSliv;
    }
    rpt.invertedCount     = inv;
    rpt.sliverCount       = sliv;
    rpt.severeSliverCount = severeSliv;

    // Min / max / median for the printed summary lines.
    {
        double dMin =  1e300, dMax = -1e300;
        double sMin =  1e300, sMax = -1e300;
        for (int el = 0; el < nTets; ++el) {
            if (dihedralArr[el] < dMin) dMin = dihedralArr[el];
            if (dihedralArr[el] > dMax) dMax = dihedralArr[el];
            if (sJacArr[el]     < sMin) sMin = sJacArr[el];
            if (sJacArr[el]     > sMax) sMax = sJacArr[el];
        }
        rpt.dihedralMin_min      = dMin;
        rpt.dihedralMin_maxOfMin = dMax;
        rpt.scaledJac_min        = sMin;
        rpt.scaledJac_max        = sMax;
    }
    {
        std::vector<double> tmp = dihedralArr;
        rpt.dihedralMin_median = medianInPlace(tmp);
    }
    {
        std::vector<double> tmp = sJacArr;
        rpt.scaledJac_median = medianInPlace(tmp);
    }

    // Percentiles (p05/p50/p95/p99) for all four metrics.
    // computePercentiles takes by value and uses nth_element internally.
    rpt.knuppPct       = computePercentiles(knuppArr);
    rpt.dihedralMinPct = computePercentiles(dihedralArr);
    rpt.scaledJacPct   = computePercentiles(sJacArr);
    rpt.skewPct        = computePercentiles(skewArr);

    // Worst-N elements by ascending Knupp shape.
    const int wantN = std::max(0, p.worstNCount);
    const int takeN = std::min(wantN, nTets);
    if (takeN > 0) {
        std::vector<int> idx(nTets);
        for (int i = 0; i < nTets; ++i) idx[i] = i;
        std::partial_sort(idx.begin(), idx.begin() + takeN, idx.end(),
            [&](int a, int b) { return knuppArr[a] < knuppArr[b]; });
        rpt.worstN.reserve(takeN);
        for (int k = 0; k < takeN; ++k) {
            const int ei = idx[k];
            ElemQuality eq;
            eq.elemIdx        = ei;
            eq.knupp          = knuppArr[ei];
            eq.dihedralMinDeg = dihedralArr[ei];
            eq.dihedralMaxDeg = dihedralMaxArr[ei];
            eq.scaledJacobian = sJacArr[ei];
            eq.radiusRatio    = radiusRatioArr[ei];
            eq.equiangSkew    = skewArr[ei];
            eq.centroid       = centroidArr[ei];
            rpt.worstN.push_back(eq);
        }
    }

    const double severePct = (nTets > 0) ? (100.0 * static_cast<double>(severeSliv) / static_cast<double>(nTets)) : 0.0;
    const double targetPct = (nTets > 0) ? (100.0 * static_cast<double>(sliv) / static_cast<double>(nTets)) : 0.0;
    rpt.pass = (inv == 0) && (severePct <= rpt.acceptableSliverPercent) && (targetPct <= rpt.acceptableSliverPercent);
    rpt.warn = (inv == 0) && !rpt.pass;
    return rpt;
}

// Forward declaration -- defined below after computeReportTet10.
static void emitReportImpl(const QualityReport& r, const FEAParams& p, std::ostream& os);

// -----------------------------------------------------------------------------
//  Public: emitReport (ostream& overload)
// -----------------------------------------------------------------------------
void emitReport(const tetgenio& out, const FEAParams& p, std::ostream& os)
{
    const QualityReport r = computeReport(out, p);
    emitReportImpl(r, p, os);
}

void emitReport(const tetgenio& out, const FEAParams& p)
{
    emitReport(out, p, std::cout);
}

// -----------------------------------------------------------------------------
//  Public: computeReportTet10
// -----------------------------------------------------------------------------
QualityReport computeReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p)
{
    QualityReport rpt;
    rpt.isTet10             = true;
    rpt.radiusEdgeBound     = static_cast<double>(p.tetRadiusEdge);
    rpt.minDihedralBoundDeg = static_cast<double>(p.tetMinDihedralDeg);
    rpt.hardMinDihedralFailDeg = 5.0;

    if (tet10Connectivity.size() % 10 != 0 || positions.empty()) return rpt;

    const int nTets = static_cast<int>(tet10Connectivity.size() / 10);
    rpt.numElements = nTets;
    if (nTets <= 0) return rpt;

    std::vector<double>     knuppArr(nTets, 0.0);
    std::vector<double>     dihedralArr(nTets, 0.0);
    std::vector<double>     dihedralMaxArr(nTets, 0.0);
    std::vector<double>     sJacArr(nTets, 0.0);
    std::vector<double>     radiusRatioArr(nTets, 0.0);
    std::vector<double>     skewArr(nTets, 0.0);
    std::vector<glm::dvec3> centroidArr(nTets, glm::dvec3(0.0));

    const bool useMT = p.useMultithreading;
    int nThreads = 1;
#ifdef _OPENMP
    if (useMT) nThreads = omp_get_max_threads();
#endif
    if (nThreads < 1) nThreads = 1;

    std::vector<std::array<std::size_t, 8>> tlBins(
        nThreads, std::array<std::size_t, 8>{0u,0u,0u,0u,0u,0u,0u,0u});

    auto processElem = [&](int el, std::array<std::size_t,8>& bin) {
        glm::dvec3 nodes[10];
        glm::dvec3 cornerCentroid(0.0);
        for (int k = 0; k < 10; ++k) {
            const unsigned ni = tet10Connectivity[el * 10 + k];
            nodes[k] = glm::dvec3(positions[ni]);
            if (k < 4) cornerCentroid += nodes[k];
        }
        centroidArr[el] = cornerCentroid * 0.25;

        // Corner-only metrics use the 4 corner nodes.
        const glm::dvec3& c0 = nodes[0];
        const glm::dvec3& c1 = nodes[1];
        const glm::dvec3& c2 = nodes[2];
        const glm::dvec3& c3 = nodes[3];

        const double q        = tet10IsoparametricKnupp(nodes);
        const DihedralRange dr = dihedralRangeDeg(c0, c1, c2, c3);
        const double sJ       = tet10ScaledJacobianMin(nodes); // isoparametric
        const double rr       = radiusRatio(c0, c1, c2, c3);
        const double sk       = equiangularSkew(c0, c1, c2, c3);

        knuppArr[el]       = q;
        dihedralArr[el]    = dr.minDeg;
        dihedralMaxArr[el] = dr.maxDeg;
        sJacArr[el]        = sJ;
        radiusRatioArr[el] = rr;
        skewArr[el]        = sk;
        bin[knuppBinIndex(q)]     += 1u;
        bin[4 + skewBinIndex(sk)] += 1u;
    };

#ifdef _OPENMP
    #pragma omp parallel if (useMT) num_threads(nThreads)
    {
        const int tid = omp_get_thread_num();
        auto& bin = tlBins[tid];
        #pragma omp for schedule(static)
        for (int el = 0; el < nTets; ++el) processElem(el, bin);
    }
#else
    (void)useMT;
    for (int el = 0; el < nTets; ++el) processElem(el, tlBins[0]);
#endif

    for (const auto& tb : tlBins) {
        rpt.knuppHist.bin_0_20   += tb[0];
        rpt.knuppHist.bin_20_50  += tb[1];
        rpt.knuppHist.bin_50_80  += tb[2];
        rpt.knuppHist.bin_80_100 += tb[3];
        rpt.skewHist.bin_0_25    += tb[4];
        rpt.skewHist.bin_25_50   += tb[5];
        rpt.skewHist.bin_50_80   += tb[6];
        rpt.skewHist.bin_80_100  += tb[7];
    }

    const double sliverThr = static_cast<double>(p.tetMinDihedralDeg);
    const double hardSliverThr = rpt.hardMinDihedralFailDeg;
    int inv = 0, sliv = 0, severeSliv = 0;
    for (int el = 0; el < nTets; ++el) {
        if (sJacArr[el]    <= 0.0)        ++inv;
        if (dihedralArr[el] < sliverThr)  ++sliv;
        if (dihedralArr[el] < hardSliverThr) ++severeSliv;
    }
    rpt.invertedCount     = inv;
    rpt.sliverCount       = sliv;
    rpt.severeSliverCount = severeSliv;

    {
        double dMin = 1e300, dMax = -1e300, sMin = 1e300, sMax = -1e300;
        for (int el = 0; el < nTets; ++el) {
            if (dihedralArr[el] < dMin) dMin = dihedralArr[el];
            if (dihedralArr[el] > dMax) dMax = dihedralArr[el];
            if (sJacArr[el]     < sMin) sMin = sJacArr[el];
            if (sJacArr[el]     > sMax) sMax = sJacArr[el];
        }
        rpt.dihedralMin_min      = dMin;
        rpt.dihedralMin_maxOfMin = dMax;
        rpt.scaledJac_min        = sMin;
        rpt.scaledJac_max        = sMax;
    }
    { std::vector<double> tmp = dihedralArr; rpt.dihedralMin_median = medianInPlace(tmp); }
    { std::vector<double> tmp = sJacArr;     rpt.scaledJac_median   = medianInPlace(tmp); }

    rpt.knuppPct       = computePercentiles(knuppArr);
    rpt.dihedralMinPct = computePercentiles(dihedralArr);
    rpt.scaledJacPct   = computePercentiles(sJacArr);
    rpt.skewPct        = computePercentiles(skewArr);

    const int wantN = std::max(0, p.worstNCount);
    const int takeN = std::min(wantN, nTets);
    if (takeN > 0) {
        std::vector<int> idx(nTets);
        for (int i = 0; i < nTets; ++i) idx[i] = i;
        std::partial_sort(idx.begin(), idx.begin() + takeN, idx.end(),
            [&](int a, int b){ return knuppArr[a] < knuppArr[b]; });
        rpt.worstN.reserve(takeN);
        for (int k = 0; k < takeN; ++k) {
            const int ei = idx[k];
            ElemQuality eq;
            eq.elemIdx        = ei;
            eq.knupp          = knuppArr[ei];
            eq.dihedralMinDeg = dihedralArr[ei];
            eq.dihedralMaxDeg = dihedralMaxArr[ei];
            eq.scaledJacobian = sJacArr[ei];
            eq.radiusRatio    = radiusRatioArr[ei];
            eq.equiangSkew    = skewArr[ei];
            eq.centroid       = centroidArr[ei];
            rpt.worstN.push_back(eq);
        }
    }

    const double severePct = (nTets > 0) ? (100.0 * static_cast<double>(severeSliv) / static_cast<double>(nTets)) : 0.0;
    const double targetPct = (nTets > 0) ? (100.0 * static_cast<double>(sliv) / static_cast<double>(nTets)) : 0.0;
    rpt.pass = (inv == 0) && (severePct <= rpt.acceptableSliverPercent) && (targetPct <= rpt.acceptableSliverPercent);
    rpt.warn = (inv == 0) && !rpt.pass;
    return rpt;
}

// Shared emit helper -- prints a QualityReport to a stream.
// Used by both emitReport and emitReportTet10.
static void emitReportImpl(const QualityReport& r, const FEAParams& p, std::ostream& os)
{
    std::ios_base::fmtflags savedFlags = os.flags();
    std::streamsize savedPrec = os.precision();

    os << "[Mesh] Quality Report ("
       << (r.isTet10 ? "Tet10" : "Tet4")
       << ", radius-edge ratio bound: "
       << std::fixed << std::setprecision(2) << r.radiusEdgeBound
       << ", min-dihedral: "
       << std::fixed << std::setprecision(1) << r.minDihedralBoundDeg
       << " deg)\n";

    os << "  Knupp shape    : "
       << "[0.00, 0.20): " << r.knuppHist.bin_0_20   << "   "
       << "[0.20, 0.50): " << r.knuppHist.bin_20_50  << "   "
       << "[0.50, 0.80): " << r.knuppHist.bin_50_80  << "   "
       << "[0.80, 1.00]: " << r.knuppHist.bin_80_100 << "\n";
    os << "  min dihedral   : "
       << "min = "        << std::fixed << std::setprecision(1) << r.dihedralMin_min      << " deg   "
       << "median = "     << std::fixed << std::setprecision(1) << r.dihedralMin_median   << " deg   "
       << "max-of-min = " << std::fixed << std::setprecision(1) << r.dihedralMin_maxOfMin << " deg\n";
    os << "  scaled Jacobian: "
       << "min = "    << std::fixed << std::setprecision(2) << r.scaledJac_min    << "   "
       << "median = " << std::fixed << std::setprecision(2) << r.scaledJac_median << "   "
       << "max = "    << std::fixed << std::setprecision(2) << r.scaledJac_max    << "\n";
    os << "  equiangular skew: "
       << "[0.00, 0.25): " << r.skewHist.bin_0_25   << "   "
       << "[0.25, 0.50): " << r.skewHist.bin_25_50  << "   "
       << "[0.50, 0.80): " << r.skewHist.bin_50_80  << "   "
       << "[0.80, 1.00]: " << r.skewHist.bin_80_100 << "\n";
    os << "  percentiles\n";
    auto pctLine = [&](const char* label, const MetricPercentiles& pct, int prec) {
        os << "    " << label
           << " p05=" << std::fixed << std::setprecision(prec) << pct.p05
           << " p50=" << std::fixed << std::setprecision(prec) << pct.p50
           << " p95=" << std::fixed << std::setprecision(prec) << pct.p95
           << " p99=" << std::fixed << std::setprecision(prec) << pct.p99 << "\n";
    };
    pctLine("Knupp           ", r.knuppPct,       2);
    pctLine("min dihedral    ", r.dihedralMinPct,  1);
    pctLine("scaled Jacobian ", r.scaledJacPct,    2);
    pctLine("skew            ", r.skewPct,         2);

    os << "  Worst-" << p.worstNCount << " elements (by Knupp):\n";
    for (const ElemQuality& eq : r.worstN) {
        os << "    elem " << eq.elemIdx
           << ": q="           << std::fixed << std::setprecision(2) << eq.knupp
           << " dihedral_min=" << std::fixed << std::setprecision(1) << eq.dihedralMinDeg
           << " sJac="         << std::fixed << std::setprecision(2) << eq.scaledJacobian
           << " skew="         << std::fixed << std::setprecision(2) << eq.equiangSkew
           << " at ("          << std::fixed << std::setprecision(2) << eq.centroid.x
           << ", "             << std::fixed << std::setprecision(2) << eq.centroid.y
           << ", "             << std::fixed << std::setprecision(2) << eq.centroid.z
           << ")\n";
    }

    const double severePct = (r.numElements > 0) ? (100.0 * static_cast<double>(r.severeSliverCount) / static_cast<double>(r.numElements)) : 0.0;
    const double targetPct = (r.numElements > 0) ? (100.0 * static_cast<double>(r.sliverCount) / static_cast<double>(r.numElements)) : 0.0;
    if (r.invertedCount > 0) {
        os << "  Verdict: FAIL -- "
           << r.invertedCount << " inverted, "
           << r.severeSliverCount << " severe slivers < "
           << std::fixed << std::setprecision(1) << r.hardMinDihedralFailDeg
           << " deg [" << std::fixed << std::setprecision(3) << severePct
           << "%]; " << r.sliverCount << " below target "
           << std::fixed << std::setprecision(1) << r.minDihedralBoundDeg
           << " deg [" << std::fixed << std::setprecision(3) << targetPct
           << "%]\n";
    } else if (r.warn) {
        os << "  Verdict: WARN ("
           << r.invertedCount << " inverted, "
           << r.severeSliverCount << " severe slivers < "
           << std::fixed << std::setprecision(1) << r.hardMinDihedralFailDeg
           << " deg [" << std::fixed << std::setprecision(3) << severePct
           << "%]; " << r.sliverCount << " below target "
           << std::fixed << std::setprecision(1) << r.minDihedralBoundDeg
           << " deg [" << std::fixed << std::setprecision(3) << targetPct
           << "%], allowed <= " << std::fixed << std::setprecision(1) << r.acceptableSliverPercent
           << "% )\n";
    } else {
        os << "  Verdict: PASS ("
           << r.invertedCount << " inverted, "
           << r.severeSliverCount << " severe slivers < "
           << std::fixed << std::setprecision(1) << r.hardMinDihedralFailDeg
           << " deg [" << std::fixed << std::setprecision(3) << severePct
           << "%]; " << r.sliverCount << " below target "
           << std::fixed << std::setprecision(1) << r.minDihedralBoundDeg
           << " deg [" << std::fixed << std::setprecision(3) << targetPct
           << "%], allowed <= " << std::fixed << std::setprecision(1) << r.acceptableSliverPercent
           << "% )\n";
    }

    os.flags(savedFlags);
    os.precision(savedPrec);
    os.flush();
}

// -----------------------------------------------------------------------------
//  Public: emitReportTet10
// -----------------------------------------------------------------------------
void emitReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p,
    std::ostream&                 os)
{
    const QualityReport r = computeReportTet10(positions, tet10Connectivity, p);
    emitReportImpl(r, p, os);
}

void emitReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p)
{
    emitReportTet10(positions, tet10Connectivity, p, std::cout);
}

// =============================================================================
//  Public: computeFidelity
// =============================================================================
FidelityReport computeFidelity(const RefSurface& ref, const tetgenio& out,
                               const FEAParams& p)
{
    FidelityReport result;

    if (ref.positions.empty() || ref.indices.empty() || out.numberoftrifaces <= 0)
        return result;

    const int   nRefVerts = static_cast<int>(ref.positions.size());
    const int   nRefTris  = static_cast<int>(ref.indices.size() / 3);
    const int   nVolTris  = out.numberoftrifaces;
    const int   nVolVerts = out.numberofpoints;
    const int   firstN    = out.firstnumber;
    const REAL* volPts    = out.pointlist;
    const int*  volTris   = out.trifacelist;

    if (nRefTris <= 0 || nVolVerts <= 0) return result;

    // -------------------------------------------------------------------------
    // 1. Reference surface: convert to double precision.
    // -------------------------------------------------------------------------
    std::vector<glm::dvec3> refPos(nRefVerts);
    for (int i = 0; i < nRefVerts; ++i) refPos[i] = glm::dvec3(ref.positions[i]);

    std::vector<int> refIdx(ref.indices.begin(), ref.indices.end());

    // Pre-compute per-triangle face normals for the reference surface.
    // Used in the forward pass instead of interpolated vertex normals so
    // that flat-faced geometry (cubes, boxes) doesn't produce false failures:
    // corner vertices have averaged normals pointing diagonally, which would
    // give >50° deviation even for a perfectly meshed cube.  The face normal
    // of the nearest reference triangle is the correct plane normal for any
    // piecewise-linear surface.
    std::vector<glm::dvec3> refTriNrm(nRefTris, glm::dvec3(0.0, 0.0, 1.0));
    for (int t = 0; t < nRefTris; ++t) {
        const int i0 = refIdx[t*3+0], i1 = refIdx[t*3+1], i2 = refIdx[t*3+2];
        const glm::dvec3 fn = glm::cross(refPos[i1]-refPos[i0], refPos[i2]-refPos[i0]);
        const double len = glm::length(fn);
        if (len > 1e-30) refTriNrm[t] = fn / len;
    }

    // -------------------------------------------------------------------------
    // 2. Bbox diagonal from vol-mesh point list.
    // -------------------------------------------------------------------------
    {
        glm::dvec3 lo{ 1e300, 1e300, 1e300}, hi{-1e300,-1e300,-1e300};
        for (int i = 0; i < nVolVerts; ++i) {
            const glm::dvec3 pt(volPts[i*3+0], volPts[i*3+1], volPts[i*3+2]);
            lo = glm::min(lo, pt);
            hi = glm::max(hi, pt);
        }
        result.bboxDiag = glm::length(hi - lo);
        if (result.bboxDiag < 1e-12) result.bboxDiag = 1.0;
    }

    // -------------------------------------------------------------------------
    // 3. Build BVH over reference surface (no vertex normals stored — we
    //    look up refTriNrm[r.triIdx] directly in the forward pass).
    // -------------------------------------------------------------------------
    TriBVH refBVH;
    refBVH.build(refPos.data(), refIdx.data(), nRefTris);

    // -------------------------------------------------------------------------
    // 4. Forward pass: vol boundary → ref surface.
    //    4 quadrature points per face: centroid + 3 edge midpoints.
    //    Parallel over boundary faces (read-only BVH queries).
    // -------------------------------------------------------------------------
    const int nSamplesFwd = nVolTris * 4;
    std::vector<double>     fwdDist(nSamplesFwd, 0.0);
    std::vector<double>     fwdNDev(nSamplesFwd, 0.0);  // degrees
    std::vector<glm::dvec3> fwdPos (nSamplesFwd, glm::dvec3(0.0));

    constexpr double kRad2Deg = 57.29577951308232087679815481;

#ifdef _OPENMP
    #pragma omp parallel for if(p.useMultithreading) schedule(static)
#endif
    for (int t = 0; t < nVolTris; ++t) {
        const int i0 = volTris[t*3+0] - firstN;
        const int i1 = volTris[t*3+1] - firstN;
        const int i2 = volTris[t*3+2] - firstN;
        const glm::dvec3 va(volPts[i0*3+0], volPts[i0*3+1], volPts[i0*3+2]);
        const glm::dvec3 vb(volPts[i1*3+0], volPts[i1*3+1], volPts[i1*3+2]);
        const glm::dvec3 vc(volPts[i2*3+0], volPts[i2*3+1], volPts[i2*3+2]);

        const glm::dvec3 rawN   = glm::cross(vb - va, vc - va);
        const double     rawLen = glm::length(rawN);
        const glm::dvec3 faceN  = (rawLen > 1e-30) ? rawN / rawLen
                                                     : glm::dvec3(0.0, 0.0, 1.0);
        const glm::dvec3 samples[4] = {
            (va + vb + vc) / 3.0,
            (va + vb) * 0.5,
            (vb + vc) * 0.5,
            (vc + va) * 0.5
        };
        for (int s = 0; s < 4; ++s) {
            const int si = t * 4 + s;
            fwdPos[si] = samples[s];
            const auto r = refBVH.nearest(samples[s]);
            fwdDist[si] = std::sqrt(r.sqDist);
            // Compare against the face normal of the nearest reference triangle.
            // Vertex-normal interpolation fails at sharp edges/corners (averaged
            // normals point diagonally and cause false 45-60° deviations on cubes).
            // abs() handles the inward/outward convention difference between
            // TetGen's trifacelist and the input PLCs.
            const glm::dvec3& refN = (r.triIdx >= 0)
                ? refTriNrm[r.triIdx]
                : glm::dvec3(0.0, 0.0, 1.0);
            const double cosT = std::clamp(glm::dot(faceN, refN), -1.0, 1.0);
            fwdNDev[si] = std::acos(std::abs(cosT)) * kRad2Deg;
        }
    }

    // Serial reduction: max forward drift and its location.
    double     maxDriftFwd = 0.0;
    int        maxDriftTriId = -1;
    glm::dvec3 maxDriftPos(0.0);
    for (int i = 0; i < nSamplesFwd; ++i) {
        if (fwdDist[i] > maxDriftFwd) {
            maxDriftFwd  = fwdDist[i];
            maxDriftTriId = i / 4;
            maxDriftPos   = fwdPos[i];
        }
    }

    // -------------------------------------------------------------------------
    // 5. Reverse pass: ref surface → vol boundary (symmetric Hausdorff).
    // -------------------------------------------------------------------------
    std::vector<glm::dvec3> volPos(nVolVerts);
    for (int i = 0; i < nVolVerts; ++i)
        volPos[i] = glm::dvec3(volPts[i*3+0], volPts[i*3+1], volPts[i*3+2]);

    std::vector<int> volTriIdx(nVolTris * 3);
    for (int t = 0; t < nVolTris; ++t) {
        volTriIdx[t*3+0] = volTris[t*3+0] - firstN;
        volTriIdx[t*3+1] = volTris[t*3+1] - firstN;
        volTriIdx[t*3+2] = volTris[t*3+2] - firstN;
    }

    TriBVH volBVH;
    volBVH.build(volPos.data(), volTriIdx.data(), nVolTris);

    const int nSamplesRev = nRefTris * 4;
    std::vector<double> revDist(nSamplesRev, 0.0);

#ifdef _OPENMP
    #pragma omp parallel for if(p.useMultithreading) schedule(static)
#endif
    for (int t = 0; t < nRefTris; ++t) {
        const int i0 = refIdx[t*3+0], i1 = refIdx[t*3+1], i2 = refIdx[t*3+2];
        const glm::dvec3& ra = refPos[i0], &rb = refPos[i1], &rc = refPos[i2];
        const glm::dvec3 samples[4] = {
            (ra + rb + rc) / 3.0,
            (ra + rb) * 0.5,
            (rb + rc) * 0.5,
            (rc + ra) * 0.5
        };
        for (int s = 0; s < 4; ++s)
            revDist[t*4+s] = std::sqrt(volBVH.nearest(samples[s]).sqDist);
    }

    const double maxDriftRev = revDist.empty() ? 0.0
        : *std::max_element(revDist.begin(), revDist.end());

    // -------------------------------------------------------------------------
    // 6. Aggregate stats.
    // -------------------------------------------------------------------------
    result.hausdorffMax  = std::max(maxDriftFwd, maxDriftRev);
    result.maxDriftTriId = maxDriftTriId;
    result.maxDriftPos   = maxDriftPos;

    // p95 from combined forward + reverse sample distances.
    {
        std::vector<double> all;
        all.reserve(fwdDist.size() + revDist.size());
        all.insert(all.end(), fwdDist.begin(), fwdDist.end());
        all.insert(all.end(), revDist.begin(), revDist.end());
        std::sort(all.begin(), all.end());
        const std::size_t n = all.size();
        if (n > 0) {
            const auto pick = [&](double pct) {
                const std::size_t k = std::min(
                    n - 1,
                    static_cast<std::size_t>(pct * static_cast<double>(n-1) + 0.5));
                return all[k];
            };
            result.hausdorffP95 = pick(0.95);
        }
    }

    // Normal-deviation percentiles (forward direction only).
    {
        std::vector<double> nd(fwdNDev);
        std::sort(nd.begin(), nd.end());
        const std::size_t n = nd.size();
        if (n > 0) {
            const auto pick = [&](double pct) {
                const std::size_t k = std::min(
                    n - 1,
                    static_cast<std::size_t>(pct * static_cast<double>(n-1) + 0.5));
                return nd[k];
            };
            result.normalDev_p50 = pick(0.50);
            result.normalDev_p95 = pick(0.95);
            result.normalDev_p99 = pick(0.99);
            result.normalDev_max = nd.back();
        }
    }

    // Pass: Hausdorff < 1% of bbox diagonal AND normal p95 < 15 deg.
    result.pass = (result.hausdorffMax < 0.01 * result.bboxDiag)
               && (result.normalDev_p95 < 15.0);

    return result;
}

// =============================================================================
//  Public: emitFidelityReport
// =============================================================================
static void emitFidelityImpl(const FidelityReport& r, std::ostream& os)
{
    std::ios_base::fmtflags savedFlags = os.flags();
    std::streamsize         savedPrec  = os.precision();

    const double target = 0.01 * r.bboxDiag;
    const double ratio  = (target > 1e-30) ? r.hausdorffMax / target : 0.0;

    os << "[Fidelity vs input surface]\n";
    os << "  Hausdorff (symmetric) : "
       << std::scientific << std::setprecision(2) << r.hausdorffMax
       << " m  (target: < " << std::scientific << std::setprecision(2) << target
       << " m, ratio " << std::fixed << std::setprecision(2) << ratio << ")\n";
    os << "  normal deviation      : "
       << "p50=" << std::fixed << std::setprecision(1) << r.normalDev_p50 << " deg   "
       << "p95=" << std::fixed << std::setprecision(1) << r.normalDev_p95 << " deg   "
       << "p99=" << std::fixed << std::setprecision(1) << r.normalDev_p99 << " deg   "
       << "max=" << std::fixed << std::setprecision(1) << r.normalDev_max << " deg\n";
    if (r.maxDriftTriId >= 0) {
        os << "  Max-drift triangle    : id=" << r.maxDriftTriId
           << " at (" << std::fixed << std::setprecision(2) << r.maxDriftPos.x
           << ", "    << std::fixed << std::setprecision(2) << r.maxDriftPos.y
           << ", "    << std::fixed << std::setprecision(2) << r.maxDriftPos.z
           << ") drift=" << std::scientific << std::setprecision(2) << r.hausdorffMax << "\n";
    }
    os << "  Verdict: " << (r.pass ? "PASS" : "FAIL") << "\n";

    os.flags(savedFlags);
    os.precision(savedPrec);
    os.flush();
}

void emitFidelityReport(const RefSurface& ref, const tetgenio& out,
                        const FEAParams& p, std::ostream& os)
{
    emitFidelityImpl(computeFidelity(ref, out, p), os);
}

void emitFidelityReport(const RefSurface& ref, const tetgenio& out,
                        const FEAParams& p)
{
    emitFidelityReport(ref, out, p, std::cout);
}

// =============================================================================
//  BRep-exact fidelity overloads
//
//  Forward pass: for each vol-boundary sample point, call
//  brep.nearestPointOnShape() (exact NURBS) instead of the BVH.
//  Normal deviation: use brep.normalAtNearest() for exact surface normal.
//  Reverse pass: unchanged — samples ref-surface tris → vol-mesh BVH.
//
//  When HAS_OCCT is not defined BRepHandle::nearestPointOnShape() and
//  normalAtNearest() both return the query point unchanged, so distances
//  collapse to zero — meaningful only in a HAS_OCCT build.  We fall back
//  gracefully but log a warning.
// =============================================================================

FidelityReport computeFidelity(const RefSurface& ref, const BRepHandle& brep,
                               const tetgenio& out, const FEAParams& p)
{
    // If the BRep has no geometry, fall back to triangulated-reference path.
    if (brep.numFaces() == 0) {
        std::cout << "[Fidelity] BRepHandle has no faces; "
                     "falling back to triangulated reference.\n";
        return computeFidelity(ref, out, p);
    }

    // Run the standard computation to get the reverse pass (ref→vol BVH) and
    // all aggregation logic, then REPLACE the forward distances with OCC values.
    // Strategy: call computeFidelity(ref, out, p) for the full result, then
    // redo the forward pass with OCC and patch hausdorffMax / hausdorffP95 /
    // maxDrift* / normalDev_* accordingly.

    // --- Shared setup (mirrors computeFidelity internals) ---
    if (ref.positions.empty() || ref.indices.empty() || out.numberoftrifaces <= 0)
        return FidelityReport{};

    const int   nVolTris  = out.numberoftrifaces;
    const int   nVolVerts = out.numberofpoints;
    const int   firstN    = out.firstnumber;
    const REAL* volPts    = out.pointlist;
    const int*  volTris   = out.trifacelist;
    if (nVolVerts <= 0 || nVolTris <= 0) return FidelityReport{};

    // Bbox diagonal (same as triangulated path).
    double bboxDiag = 1.0;
    {
        glm::dvec3 lo{ 1e300, 1e300, 1e300}, hi{-1e300,-1e300,-1e300};
        for (int i = 0; i < nVolVerts; ++i) {
            const glm::dvec3 pt(volPts[i*3+0], volPts[i*3+1], volPts[i*3+2]);
            lo = glm::min(lo, pt); hi = glm::max(hi, pt);
        }
        bboxDiag = glm::length(hi - lo);
        if (bboxDiag < 1e-12) bboxDiag = 1.0;
    }

    // --- OCC forward pass ---
    constexpr double kRad2Deg = 57.29577951308232087679815481;

    const int nSamplesFwd = nVolTris * 4;
    std::vector<double>     fwdDist(nSamplesFwd, 0.0);
    std::vector<double>     fwdNDev(nSamplesFwd, 0.0);
    std::vector<glm::dvec3> fwdPos (nSamplesFwd, glm::dvec3(0.0));

#ifdef _OPENMP
    #pragma omp parallel for if(p.useMultithreading) schedule(dynamic, 16)
#endif
    for (int t = 0; t < nVolTris; ++t) {
        const int i0 = volTris[t*3+0] - firstN;
        const int i1 = volTris[t*3+1] - firstN;
        const int i2 = volTris[t*3+2] - firstN;
        const glm::dvec3 va(volPts[i0*3+0], volPts[i0*3+1], volPts[i0*3+2]);
        const glm::dvec3 vb(volPts[i1*3+0], volPts[i1*3+1], volPts[i1*3+2]);
        const glm::dvec3 vc(volPts[i2*3+0], volPts[i2*3+1], volPts[i2*3+2]);

        const glm::dvec3 rawN  = glm::cross(vb - va, vc - va);
        const double     rLen  = glm::length(rawN);
        const glm::dvec3 faceN = (rLen > 1e-30) ? rawN / rLen
                                                  : glm::dvec3(0.0, 0.0, 1.0);
        const glm::dvec3 samples[4] = {
            (va + vb + vc) / 3.0,
            (va + vb) * 0.5,
            (vb + vc) * 0.5,
            (vc + va) * 0.5
        };
        for (int s = 0; s < 4; ++s) {
            const int si = t * 4 + s;
            fwdPos[si] = samples[s];
            // Exact OCC nearest-point.
            const glm::dvec3 nearest = brep.nearestPointOnShape(samples[s]);
            fwdDist[si] = glm::length(samples[s] - nearest);
            // Exact surface normal at nearest point.
            const glm::dvec3 refN = brep.normalAtNearest(samples[s]);
            const double cosT = std::clamp(glm::dot(faceN, refN), -1.0, 1.0);
            fwdNDev[si] = std::acos(std::abs(cosT)) * kRad2Deg;
        }
    }

    // --- Reverse pass: reuse triangulation BVH (same as existing path) ---
    // Obtain via the existing computeFidelity, which re-runs everything but
    // we only need the reverse distances.  To avoid full duplication we just
    // call computeFidelity(ref, out, p) for the reverse stats and patch.
    FidelityReport r = computeFidelity(ref, out, p);

    // --- Replace forward stats with OCC values ---
    double maxDriftFwd = 0.0;
    int    maxDriftTriId = -1;
    glm::dvec3 maxDriftPos(0.0);
    for (int i = 0; i < nSamplesFwd; ++i) {
        if (fwdDist[i] > maxDriftFwd) {
            maxDriftFwd   = fwdDist[i];
            maxDriftTriId = i / 4;
            maxDriftPos   = fwdPos[i];
        }
    }

    // Rebuild hausdorffMax using OCC forward + existing reverse.
    // r.hausdorffMax currently equals max(triangulated_fwd, rev).
    // We need max(occ_fwd, rev).  Extract reverse max from r's p95/combined
    // is tricky; simpler: just rerun the reverse internally.
    {
        const int nRefTris = static_cast<int>(ref.indices.size() / 3);
        std::vector<glm::dvec3> refPos(ref.positions.size());
        for (size_t i = 0; i < ref.positions.size(); ++i)
            refPos[i] = glm::dvec3(ref.positions[i]);
        std::vector<int> refIdx(ref.indices.begin(), ref.indices.end());

        // Build vol BVH for reverse pass.
        std::vector<glm::dvec3> volPos(nVolVerts);
        for (int i = 0; i < nVolVerts; ++i)
            volPos[i] = glm::dvec3(volPts[i*3+0], volPts[i*3+1], volPts[i*3+2]);
        std::vector<int> volTriIdx(nVolTris * 3);
        for (int t = 0; t < nVolTris; ++t) {
            volTriIdx[t*3+0] = volTris[t*3+0] - firstN;
            volTriIdx[t*3+1] = volTris[t*3+1] - firstN;
            volTriIdx[t*3+2] = volTris[t*3+2] - firstN;
        }

        TriBVH volBVH;
        volBVH.build(volPos.data(), volTriIdx.data(), nVolTris);

        const int nSamplesRev = nRefTris * 4;
        std::vector<double> revDist(nSamplesRev, 0.0);
#ifdef _OPENMP
        #pragma omp parallel for if(p.useMultithreading) schedule(static)
#endif
        for (int t = 0; t < nRefTris; ++t) {
            const int i0 = refIdx[t*3+0], i1 = refIdx[t*3+1], i2 = refIdx[t*3+2];
            const glm::dvec3& ra = refPos[i0], &rb = refPos[i1], &rc = refPos[i2];
            const glm::dvec3 samples[4] = {
                (ra+rb+rc)/3.0, (ra+rb)*0.5, (rb+rc)*0.5, (rc+ra)*0.5 };
            for (int s = 0; s < 4; ++s)
                revDist[t*4+s] = std::sqrt(volBVH.nearest(samples[s]).sqDist);
        }

        const double maxDriftRev = revDist.empty() ? 0.0
            : *std::max_element(revDist.begin(), revDist.end());

        r.hausdorffMax  = std::max(maxDriftFwd, maxDriftRev);
        r.maxDriftTriId = maxDriftTriId;
        r.maxDriftPos   = maxDriftPos;

        // Rebuild combined p95 with OCC forward + reverse.
        std::vector<double> all;
        all.reserve(fwdDist.size() + revDist.size());
        all.insert(all.end(), fwdDist.begin(), fwdDist.end());
        all.insert(all.end(), revDist.begin(), revDist.end());
        std::sort(all.begin(), all.end());
        if (!all.empty()) {
            const std::size_t n = all.size();
            r.hausdorffP95 = all[std::min(n-1,
                static_cast<std::size_t>(0.95*static_cast<double>(n-1)+0.5))];
        }
    }

    // Rebuild normal-deviation percentiles from OCC fwdNDev.
    {
        std::vector<double> nd(fwdNDev);
        std::sort(nd.begin(), nd.end());
        const std::size_t n = nd.size();
        if (n > 0) {
            const auto pick = [&](double pct) {
                return nd[std::min(n-1,
                    static_cast<std::size_t>(pct*static_cast<double>(n-1)+0.5))];
            };
            r.normalDev_p50 = pick(0.50);
            r.normalDev_p95 = pick(0.95);
            r.normalDev_p99 = pick(0.99);
            r.normalDev_max = nd.back();
        }
    }

    r.bboxDiag      = bboxDiag;
    r.pass          = (r.hausdorffMax < 0.01 * bboxDiag) && (r.normalDev_p95 < 15.0);
    r.usedExactBRep = true;
    return r;
}

void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                        const tetgenio& out, const FEAParams& p, std::ostream& os)
{
    FidelityReport r = computeFidelity(ref, brep, out, p);
    // Inject the "NURBS reference" label into the emit.
    std::ios_base::fmtflags savedFlags = os.flags();
    std::streamsize         savedPrec  = os.precision();

    const double target = 0.01 * r.bboxDiag;
    const double ratio  = (target > 1e-30) ? r.hausdorffMax / target : 0.0;

    os << "[Fidelity vs input surface] [NURBS B-rep exact]\n";
    os << "  Hausdorff (symmetric) : "
       << std::scientific << std::setprecision(2) << r.hausdorffMax
       << " m  (target: < " << std::scientific << std::setprecision(2) << target
       << " m, ratio " << std::fixed << std::setprecision(2) << ratio << ")\n";
    os << "  normal deviation      : "
       << "p50=" << std::fixed << std::setprecision(1) << r.normalDev_p50 << " deg   "
       << "p95=" << std::fixed << std::setprecision(1) << r.normalDev_p95 << " deg   "
       << "p99=" << std::fixed << std::setprecision(1) << r.normalDev_p99 << " deg   "
       << "max=" << std::fixed << std::setprecision(1) << r.normalDev_max << " deg\n";
    if (r.maxDriftTriId >= 0) {
        os << "  Max-drift triangle    : id=" << r.maxDriftTriId
           << " at (" << std::fixed << std::setprecision(2) << r.maxDriftPos.x
           << ", "    << std::fixed << std::setprecision(2) << r.maxDriftPos.y
           << ", "    << std::fixed << std::setprecision(2) << r.maxDriftPos.z
           << ") drift=" << std::scientific << std::setprecision(2) << r.hausdorffMax << "\n";
    }
    os << "  Verdict: " << (r.pass ? "PASS" : "FAIL") << "\n";

    os.flags(savedFlags);
    os.precision(savedPrec);
    os.flush();
}

void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                        const tetgenio& out, const FEAParams& p)
{
    emitFidelityReport(ref, brep, out, p, std::cout);
}

} // namespace MeshQuality
