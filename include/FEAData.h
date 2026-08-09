#pragma once
#include <glm/glm.hpp>

// texCoords.x = per-vertex displacement magnitude (scalarMode 1)
// texCoords.y = per-vertex applied-force magnitude (scalarMode 2)
// elementScalar = per-ELEMENT value (flat, no interpolation) used by the
//   fracture views: scalarMode 3 = failure mode, 4 = failure iteration,
//   5 = von Mises at death, and reserved 6 = slab index.
//   Kept as a dedicated channel so it never clobbers the force map.
struct Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 texCoords; float elementScalar = 0.0f; };

struct FEAParams {
    float sizeX = 5.0f;
    float sizeY = 1.0f;
    float sizeZ = 1.0f;
    float subdivisions = 5.0f;

    // --- NEW: TETGEN CONTROLS ---
    float tetQuality = 1.4f;      // Lower is better (1.1 to 2.0). Controls triangle shape.
    float maxVolPercent = 0.1f;   // Limits maximum tetrahedron size relative to part size
    bool enablePolarRemoval = false; // Disable by default to preserve complex CAD topologies

    // --- TetGen quality switches & quality-report knobs ---
    // Radius-edge ratio bound passed to TetGen as `pq<this>/...`. Lower = stricter
    // (TetGen lower bound is 1.0; 1.2 is the tight default).
    float tetRadiusEdge      = 1.2f;
    // Minimum dihedral angle bound (degrees) used for two purposes:
    //   1. Passed to TetGen as `.../<this>` -- a TARGET for its smoother, NOT a guarantee.
    //   2. Reported by MeshQuality::emitReport (slivers = elements below this target).
    // 10 deg matches ANSYS Fluent's default sliver-removal threshold (industry standard).
    // Hard-reject is <5 deg (NAFEMS/COMSOL). 18 deg is too strict for raw TetGen output.
    float tetMinDihedralDeg  = 10.0f;
    // TetGen mesh-optimisation level (`O<level>`): 0 disables, 7 = flips + Laplace
    // smoother. See TetGen manual §"-O" and plan Appendix §2 Phase A.4.
    int   tetOptimizeLevel   = 7;
    // Robustness tolerance forwarded as `T<this>` (TetGen's "tolerance for
    // recognising coplanar/colinear/zero-volume primitives").
    double tetRobustnessTol  = 1.0e-10;

    // --- Quality-report controls (consumed by MeshQuality::emitReport) ---
    int   worstNCount        = 10;     // How many worst elements to dump.
    bool  useMultithreading  = true;   // Per-element parallel quality computation.

    // --- STEP / BRep tessellation controls ---
    // Chord error as a fraction of the bounding-box diagonal.
    // lin_def = sizingChordError * L_diag  (absolute linear deflection for OCC mesher).
    // 1e-3 → 0.1% chord error (COMSOL "Fine" equivalent on a 1 m part: 1 mm deflection).
    float sizingChordError   = 1e-3f;

    // --- layered-FDM slice controls ---
    // These describe the print/slice intent; consumed by LayerSlicer
    // and the downstream extrusion/region pipeline.
    bool  enableLayerSlicing = false;
    // STL is unitless; this multiplies STL coords to millimetres
    // (1.0 = treat STL as mm, the slicer/printer convention). 3MF/STEP carry their
    // own declared unit and ignore this. Used for non-mm STL imports.
    float stlUnitToMM        = 1.0f;
    float layerThickness     = 0.2f;   // MILLIMETRES (physical FDM layer height)
    int   buildAxisSel       = 2;      // 0=X 1=Y 2=Z
    int   infillPattern      = 0;      // 0=rect 1=grid 2=tri 3=gyroid 4=honeycomb
    float infillDensity      = 0.20f;
    int   wallCount          = 2;
    float wallWidth          = 0.4f;
    int   maxSlabs           = 40;     // layer-grouping cap (see LayerSlicer grouping)
};
