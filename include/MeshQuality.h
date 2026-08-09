// =============================================================================
//  MeshQuality.h  --  per-element tetrahedral quality
//                     metrics + console quality-report printer.
//
//  Provides:
//    * MeshQuality::initExactPredicates() -- idempotent wrapper around
//      Shewchuk's exactinit() (numerical care-point #1).
//    * Per-element shape measures (all double, all guarded against degenerate
//      denominators -- care-point #2):
//        Knupp/Pebay shape, min/max dihedral (deg), scaled Jacobian,
//        radius ratio (3r/R), equiangular skew (volume-based, [0,1]).
//    * Tet10 isoparametric variants of scaled Jacobian and Knupp shape,
//      evaluated at the 4 Gauss points of the standard quadratic rule.
//    * Aggregate QualityReport (histograms + summary stats + percentiles +
//      worst-N) for both Tet4 (tetgenio path) and Tet10 (connectivity path).
//    * emitReport / emitReportTet10 print the verbatim console block.
//
//  This header keeps the tetgen.h dependency to translation units that
//  actually need to inspect tetgenio internals: we forward-declare
//  `class tetgenio` here and include <tetgen.h> only inside MeshQuality.cpp
//  (and in callers that already pull it in, e.g. FEAModel.cpp).
// =============================================================================
#pragma once

#include "FEAData.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <iosfwd>
#include <vector>

// Forward declarations — keep OCC and TetGen headers out of every consumer.
class tetgenio;
class BRepHandle;

namespace MeshQuality {

// -----------------------------------------------------------------------------
// Exact-predicates bootstrap.
//
// Shewchuk's orient3d/insphere routines require exactinit() to have run
// before they are called.  TetGen itself calls exactinit() inside
// tetrahedralize(), so once the user has produced a mesh exactinit() is
// already live; however we also want this library to be safely callable
// before any tetrahedralize() invocation (e.g. from mesh_diag's standalone
// path or from unit tests).  initExactPredicates() is therefore idempotent:
// it only forwards to exactinit() on the first call.
//
//   maxCoord -- a conservative upper bound on the absolute value of any
//               vertex coordinate the predicates will see.  Pass the
//               longest bounding-box side of the input mesh, or 1.0 if
//               the geometry is unknown / has already been normalised.
// -----------------------------------------------------------------------------
void initExactPredicates(double maxCoord = 1.0);

// -----------------------------------------------------------------------------
// Per-element quality record. Populated by computeReport() for every tet
// and later used to assemble the "Worst-N elements" block.
// -----------------------------------------------------------------------------
struct ElemQuality {
    int        elemIdx        = -1;   // index into tetgenio::tetrahedronlist / 4
    double     knupp          =  0.0; // Knupp/Pebay shape, 0..1 (1 == regular)
    double     dihedralMinDeg =  0.0; // smallest of the 6 dihedral angles, deg
    double     dihedralMaxDeg =  0.0; // largest of the 6 dihedral angles, deg
    double     scaledJacobian =  0.0; // signed scaled Jacobian, <=0 == inverted
    double     radiusRatio    =  0.0; // 3r/R, 0..1 (1 == regular)
    double     equiangSkew    =  0.0; // volume-based equiangular skew, 0..1
    glm::dvec3 centroid       {0.0, 0.0, 0.0};
};

// Bin layout for the Knupp histogram printed in the report.  Bins are
// half-open on the left, closed on the right for the top bucket so that the
// nominal regular-tet value of exactly 1.0 still lands in [0.80, 1.00].
struct KnuppHistogram {
    std::size_t bin_0_20   = 0;   // [0.00, 0.20)
    std::size_t bin_20_50  = 0;   // [0.20, 0.50)
    std::size_t bin_50_80  = 0;   // [0.50, 0.80)
    std::size_t bin_80_100 = 0;   // [0.80, 1.00]
};

// Bin layout for the equiangular-skew histogram.  Closed on the right
// of the top bucket so that the pathological value 1.0 still lands there.
struct SkewHistogram {
    std::size_t bin_0_25   = 0;   // [0.00, 0.25)
    std::size_t bin_25_50  = 0;   // [0.25, 0.50)
    std::size_t bin_50_80  = 0;   // [0.50, 0.80)
    std::size_t bin_80_100 = 0;   // [0.80, 1.00]
};

// Per-metric percentile summary (p05/p50/p95/p99) across all elements.
struct MetricPercentiles {
    double p05 = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

// -----------------------------------------------------------------------------
// Aggregate quality summary for one mesh.
// -----------------------------------------------------------------------------
struct QualityReport {
    int             numElements          = 0;
    int             invertedCount        = 0;   // scaledJacobian <= 0
    int             sliverCount          = 0;   // dihedralMinDeg < tetMinDihedralDeg
    int             severeSliverCount    = 0;   // dihedralMinDeg < hardMinDihedralFailDeg

    bool            isTet10              = false; // true when produced by Tet10 path

    double          radiusEdgeBound      = 0.0; // echoed in header (from FEAParams)
    double          minDihedralBoundDeg  = 0.0; // echoed in header (from FEAParams)
    double          hardMinDihedralFailDeg = 5.0;
    double          acceptableSliverPercent = 1.0;

    KnuppHistogram  knuppHist;
    SkewHistogram   skewHist;

    // Per-tet min-dihedral statistics across the mesh:
    //   dihedralMin_min      -- worst (smallest) per-tet min-dihedral
    //   dihedralMin_median   -- median per-tet min-dihedral
    //   dihedralMin_maxOfMin -- best (largest) per-tet min-dihedral
    double          dihedralMin_min      = 0.0;
    double          dihedralMin_median   = 0.0;
    double          dihedralMin_maxOfMin = 0.0;

    // Per-tet scaled-Jacobian statistics across the mesh.
    double          scaledJac_min        = 0.0;
    double          scaledJac_median     = 0.0;
    double          scaledJac_max        = 0.0;

    // Per-metric percentile summaries (p05 / p50 / p95 / p99).
    MetricPercentiles knuppPct;
    MetricPercentiles dihedralMinPct;
    MetricPercentiles scaledJacPct;
    MetricPercentiles skewPct;

    // Worst-N elements by ascending Knupp shape (size <= FEAParams::worstNCount).
    std::vector<ElemQuality> worstN;

    bool            pass                 = true;
    bool            warn                 = false;
};

// -----------------------------------------------------------------------------
// Geometric fidelity (Hausdorff + normal deviation).
// -----------------------------------------------------------------------------

// Snapshot of the input surface taken before TetGen meshing.  Stored in
// FEAModel::refSurfaceForFidelity and passed to computeFidelity() after
// tetrahedralization so the reference is the pre-optimisation input.
struct RefSurface {
    std::vector<glm::vec3>    positions; // vertex positions (post-scale)
    std::vector<unsigned int> indices;   // flat triangle list (3 per triangle)
};

// Results from computeFidelity().
struct FidelityReport {
    double     hausdorffMax  = 0.0;  // symmetric Hausdorff (both directions)
    double     hausdorffP95  = 0.0;  // 95th-pct drift distance (combined)
    int        maxDriftTriId = -1;   // boundary-face index of worst-drift sample
    glm::dvec3 maxDriftPos  {0.0, 0.0, 0.0}; // position of that sample point
    double     normalDev_p50 = 0.0;  // normal-deviation percentiles (degrees)
    double     normalDev_p95 = 0.0;
    double     normalDev_p99 = 0.0;
    double     normalDev_max = 0.0;
    double     bboxDiag      = 1.0;  // vol-mesh bbox diagonal (for pass ratio)
    bool       pass          = false; // Hausdorff < 1% bboxDiag AND p95 < 15 deg
    // true when forward pass used OCC exact nearest-point (NURBS reference).
    bool       usedExactBRep = false;
};

// Compute Hausdorff + normal deviation between the vol-mesh boundary and the
// reference input surface.  Uses a BVH (median-split, 4-sample quadrature per
// face) for both forward (vol→ref) and reverse (ref→vol) passes.
FidelityReport computeFidelity(const RefSurface& ref, const tetgenio& out,
                               const FEAParams& p);

// Compute and print the fidelity block.
void emitFidelityReport(const RefSurface& ref, const tetgenio& out,
                        const FEAParams& p, std::ostream& os);
void emitFidelityReport(const RefSurface& ref, const tetgenio& out,
                        const FEAParams& p);

// BRep-exact overloads.
// Forward pass uses OCC BRepExtrema_DistShapeShape (exact NURBS nearest-point)
// instead of BVH-on-triangulation.  Reverse pass is unchanged (ref → vol BVH).
// When HAS_OCCT is not defined these fall back to the triangulation path.
FidelityReport computeFidelity(const RefSurface& ref, const BRepHandle& brep,
                               const tetgenio& out, const FEAParams& p);
void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                        const tetgenio& out, const FEAParams& p, std::ostream& os);
void emitFidelityReport(const RefSurface& ref, const BRepHandle& brep,
                        const tetgenio& out, const FEAParams& p);

// -----------------------------------------------------------------------------
// Public API -- Tet4 path (tetgenio, produced by TetGen / computeReport).
// -----------------------------------------------------------------------------
// Build a QualityReport from a populated tetgenio (post-tetrahedralize).
// Respects p.useMultithreading for the per-element loop and p.worstNCount
// for the worst-N collection; PASS allows a small reported sliver tail but
// FAIL remains reserved for inverted elements.
QualityReport computeReport(const tetgenio& out, const FEAParams& p);

// Build the report and print the verbatim console block.
// Two overloads: explicit stream and std::cout default.
void emitReport(const tetgenio& out, const FEAParams& p, std::ostream& os);
void emitReport(const tetgenio& out, const FEAParams& p);

// -----------------------------------------------------------------------------
// Public API -- Tet10 path (positions + flat 10-per-element connectivity).
// -----------------------------------------------------------------------------
// positions         : one entry per node (corner + mid-edge, glm::vec3).
// tet10Connectivity : flat array, 10 indices per element (0-based).
// These functions evaluate isoparametric quality at the 4 standard Gauss
// points.  The Tet10 entry point is shipped as a forward-looking API and
// does NOT gate acceptance (the cube smoke test uses
// the Tet4 tetgenio path).
QualityReport computeReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p);

void emitReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p,
    std::ostream&                 os);

void emitReportTet10(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tet10Connectivity,
    const FEAParams&              p);

} // namespace MeshQuality

