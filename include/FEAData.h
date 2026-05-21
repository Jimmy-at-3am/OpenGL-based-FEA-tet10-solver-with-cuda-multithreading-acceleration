#pragma once
#include <glm/glm.hpp>

struct Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 texCoords; };

struct FEAParams {
    float sizeX = 5.0f;
    float sizeY = 1.0f;
    float sizeZ = 1.0f;
    float subdivisions = 5.0f;

    // --- NEW: TETGEN CONTROLS ---
    float tetQuality = 1.4f;      // Lower is better (1.1 to 2.0). Controls triangle shape.
    float maxVolPercent = 0.1f;   // Limits maximum tetrahedron size relative to part size
    bool enablePolarRemoval = false; // Disable by default to preserve complex CAD topologies

    // --- TODO_01: TetGen quality switches & quality-report knobs ---
    // Radius-edge ratio bound passed to TetGen as `pq<this>/...`. Lower = stricter
    // (TetGen lower bound is 1.0; 1.2 is the tight default agreed for TODO_01).
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

    // --- TODO_04: STEP / BRep tessellation controls ---
    // Chord error as a fraction of the bounding-box diagonal.
    // lin_def = sizingChordError * L_diag  (absolute linear deflection for OCC mesher).
    // 1e-3 → 0.1% chord error (COMSOL "Fine" equivalent on a 1 m part: 1 mm deflection).
    float sizingChordError   = 1e-3f;
};
