#pragma once
// =============================================================================
//  LayerSlicer.h  --  SLICE data model + slice-plane contour
//  extraction. Produces, for every layer plane along the build axis, a clean
//  closed-polygon cross-section (outer loops CCW, holes CW). No 3-D meshing yet.
//
//  Decoupling: this header pulls in only FEAData.h (Vertex/FEAParams) + glm so
//  it never drags FEAModel.h or OCC headers into callers. The B-rep path is a
//  forward-declared seam (sliceBRepPlane, defined in StepSlicer.cpp).
// =============================================================================
#include <vector>
#include <glm/glm.hpp>
#include "FEAData.h"

class BRepHandle;   // forward decl only (B-rep seam; no OCC headers here)

namespace LayerSlicer {

// A single closed 2-D loop of a cross-section, in the build-plane (u,v) coords.
// Outer boundaries are CCW (isHole=false); interior holes are CW (isHole=true).
struct Polygon2D {
    std::vector<glm::vec2> pts;
    bool isHole = false;
};

// All loops of one slice plane = one "section".
using Section = std::vector<Polygon2D>;

// Output of slicing: one sample plane per FE slab (sampled at the slab centre so
// the contour is a clean interior cross-section, never a coplanar cap face).
struct SliceResult {
    int                  buildAxis = 2;   // 0=X 1=Y 2=Z
    std::vector<float>   planeCoords;      // sample heights (slab centres), ascending
    std::vector<Section> sections;         // sections[i] = loops at planeCoords[i]
};

// Layer-grouping result (the "achievability key"): cap physical layers at maxSlabs
// FE slabs by grouping k physical layers per slab.
struct SliceGrouping {
    int   nPhysical = 0;             // ceil(physicalExtentMM / layerThicknessMM)
    int   layersPerSlab = 1;         // grouping factor k
    int   nSlabs = 0;                // ceil(nPhysical / k)
    float physicalLayerThickness = 0.0f; // model-space layer thickness (for slab boundaries)
    float slabThickness = 0.0f;          // k * physicalLayerThickness (model space)
    // physically-readable thicknesses in MILLIMETRES.
    float physThickMM = 0.0f;            // = FEAParams::layerThickness (mm)
    float slabThickMM = 0.0f;            // = k * layerThickness (mm)
    std::vector<float> slabBoundaries;   // model-space, ascending, nSlabs+1 (top may be thinner)
};

// Per-plane diagnostic stats (for the report + console). Areas carry BOTH the
// model-space value (for back-compat / scale-invariant ratios) and the physical
// mm / mm^2 value (the physically-readable numbers).
struct PlaneStats {
    float  z = 0.0f;            // model-space plane height
    float  zMM = 0.0f;          // physical plane height (mm, from model centre)
    int    loops = 0;
    int    holes = 0;
    int    discardedChains = 0;
    double outerArea = 0.0;     // model-space area^2
    double holeArea  = 0.0;
    double netArea   = 0.0;
    double outerAreaMM2 = 0.0;  // physical area (mm^2)
    double holeAreaMM2  = 0.0;
    double netAreaMM2   = 0.0;
};

// Map FEAParams.buildAxisSel (0/1/2) to a valid axis index.
int axisFromParams(const FEAParams& p);

// Compute layer grouping from the part extent along the build axis.
//   modelToMM converts a model-space length to physical millimetres; grouping is
//   done in mm (nPhysical = ceil(physicalExtentMM / layerThickness_mm)).
SliceGrouping computeGrouping(float axisMin, float axisMax,
                              const FEAParams& p, float modelToMM);

// MAIN ENTRY. Slices the surface triangle mesh (always available) at every slab
// centre. If `brep` is non-null and the B-rep section path succeeds for a plane,
// that path is used for that plane; otherwise the triangle path is used.
//   surfVerts/surfIdx : the model's surfaceVertices/surfaceIndices (model space).
//   minB/maxB         : surface AABB (model space) — defines extent + L_diag.
//   modelToMM         : model-space length -> physical mm (FEAModel::modelToMM).
// Fills grpOut + statsOut (one PlaneStats per sampled plane).
SliceResult computeSlices(const std::vector<Vertex>& surfVerts,
                          const std::vector<unsigned int>& surfIdx,
                          const glm::vec3& minB, const glm::vec3& maxB,
                          const FEAParams& p, float modelToMM,
                          const BRepHandle* brep,
                          SliceGrouping& grpOut,
                          std::vector<PlaneStats>& statsOut);

// Convert one section's loops to a flat list of 3-D segment endpoints (consecutive
// pairs = one GL line) at the given plane height, for the GL_LINES preview. Used
// by BOTH the UI SLICE PREVIEW button and the headless harness (same path).
std::vector<glm::vec3> sectionToSegments(const Section& section, int axis,
                                         float planeCoord);

// Signed area of a 2-D loop (shoelace). >0 CCW, <0 CW.
double signedArea(const std::vector<glm::vec2>& loop);

// Classify raw closed loops into a Section with the shared convention
// (even-odd nesting depth: even -> outer CCW, odd -> hole CW). Extracted from
// the slice-plane cleanup so the toolpath-section builder emits
// the exact same contract without duplicating the nesting logic.
Section classifySection(std::vector<std::vector<glm::vec2>> loops);

// B-rep section seam (defined in StepSlicer.cpp). Returns false when no B-rep
// path is available (the analytic OCC body is not yet implemented), so callers fall back to
// the triangle path. `brep` is only dereferenced inside the OCC-enabled body.
bool sliceBRepPlane(const BRepHandle& brep, int axis, float coord, double Ldiag,
                    Section& out);

} // namespace LayerSlicer
