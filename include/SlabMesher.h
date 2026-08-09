#pragma once
// =============================================================================
//  SlabMesher.h  --  per-slab 2D ear-clip triangulation + prism
//  extrusion -> conformal tetrahedral slabs.
//
//  The sole public entry is meshSlabs().  It reads the SliceResult produced by
//  LayerSlicer::computeSlices(), triangulates each slab cross-section with a
//  simple ear-clipper (adequate for convex/nearly-convex FDM outlines), extrudes
//  the triangles to prisms along the build axis, and splits each prism into 3
//  Tet4 elements using the Dompierre rule (all positive Jacobians when the base
//  triangle is CCW and dz > 0).
//
//  Adjacent slabs with identical 2D topology share ring nodes (conforming mesh,
//  zero Steiner points).  Non-identical topology produces independent rings;
//  TODO: conforming stitching across topology changes is not yet implemented.
// =============================================================================
#include "LayerSlicer.h"
#include "ToolpathSections.h"   // toolpath-lane input
#include "FEAData.h"
#include <atomic>

class FEAModel;

namespace SlabMesher {

struct MeshStats {
    int   nSlabs   = 0;    // slabs actually meshed
    int   nRing    = 0;    // outer-loop vertex count BEFORE refinement
    int   nVerts2D = 0;    // 2-D vertices per boundary level AFTER refinement
    int   nTets    = 0;    // Tet4 elements emitted
    int   refineLevel  = 0;     // uniform 4-way refinement levels applied
    bool  refineCapped = false; // true if the element budget clamped the level
    float wallPct = 0.0f;  // percentage of Tet4 elements tagged as wall (region==1)
    // Blind-spot flags surfaced into the report (oracle principle: a bug the
    // report cannot see is a bug the harness will never catch):
    bool  ringConformal  = false; // nNodes == nVerts2D*(nSlabs+1) exactly
    bool  holesIgnored   = false; // sections had holes; v1 meshed outer loop only
    bool  sectionUniform = false; // all sections match slab-0's outer loop count
};

// Main entry.  Requires model.hasLayerStack() (set by ScenarioRunner::runSlice).
// On success sets model.hasVolumetricMesh = true and populates:
//   model.tetrahedra, model.originalVolumetricPositions, model.volumetricVertices,
//   model.elementAlive (all-1 so buildBuffers reconstructs the surface),
//   model.layers->elemSlabIndex, model.layers->elemRegion.
// Calls model.buildBuffers() before returning.
MeshStats meshSlabs(const LayerSlicer::SliceResult& slice,
                    const FEAParams& params,
                    FEAModel& model);

// =============================================================================
// toolpath lane. Meshes the per-layer cookie sections
// (ToolpathSections output) into Tet4 slabs the way the printer builds them:
// every slab gets its OWN 2-D triangulation (sections differ per layer, so no
// shared rings), holes are bridged into the outer loop (Eberly keyholes), the
// boundary is resampled coarsely ALONG ribbons (the owner's strip pattern: one
// element across a bead, alternating apex — accuracy comes from Tet10, not
// density), and consecutive slabs are coupled by barycentric weld TIES stored
// in LayerStack::interfaces (the weld-interface registry).
// =============================================================================
struct ToolpathMeshOptions {
    // Layer-grouping cap (k = ceil(nLayers/maxSlabs)). 128 keeps a typical
    // 200-250-layer print to two physical layers per FE slab while still
    // bounding cost; prints with <=128 layers retain every Z contour. Scenario JSONs
    // that explicitly specify mesh.maxSlabs remain unaffected.
    int   maxSlabs     = 128;
    float targetEdgeMM = -1.0f; // boundary resample step; <0 -> 5 x median width
    float tieAlpha     = 100.0f;// interlayer penalty stiffness factor
    std::atomic<float>* progressOut = nullptr;     // optional, 0..1
    std::atomic<bool>*  cancelRequested = nullptr; // checked per slab/interface
    float progressLo = 0.0f, progressHi = 1.0f;
};

struct ToolpathMeshStats {
    int    nSlabs = 0, layersPerSlab = 1;
    int    nTets = 0, nNodes = 0;
    int    nInterfaces = 0, nTies = 0;
    // max over slabs of |sum(tri areas) - section net area| / net area:
    // the triangulation truth check (any bridging/clipping bug moves it).
    double areaErrMaxPct = 0.0;
    // sum over slabs of (thickness / section area) in 1/mm: the series-stack
    // axial-compliance factor for the analytic pull oracle.
    double sumThickOverAreaPerMM = 0.0;
};

ToolpathMeshStats meshToolpathSlabs(const ToolpathSections::LayerSections& layers,
                                    const ToolpathMeshOptions& opt,
                                    FEAModel& model);

} // namespace SlabMesher
