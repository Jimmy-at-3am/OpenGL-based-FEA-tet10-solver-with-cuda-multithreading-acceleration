#pragma once
// =============================================================================
//  ToolpathSections.h  --  new_TODO_19B: bead segments -> per-layer "cookie"
//  polygons (the owner's two algorithms, 2026-07-02):
//    1. rectangle-ize: every extruding move becomes an oriented rectangle
//       (the oval/stadium bead cross-section squared off, endpoints extended
//       by width/2 so bead length coverage is preserved);
//    2. weld: Clipper2 Union (NonZero) + morphological CLOSE (inflate by g,
//       deflate by g) seals corner gaps and kissing beads that must not
//       matter to simulation, then simplify + drop dust loops.
//  Output is one LayerSlicer::Section per printed layer (outer CCW, holes CW
//  via LayerSlicer::classifySection) with the sparse-infill pattern preserved
//  as real geometry. Coordinates: millimetres, PART-CENTERED (the toolpath
//  part-bbox centre subtracted) in the (x,y) build plane — build axis is
//  always +Z in gcode.
//
//  HARD-requires Clipper2 (HAS_CLIPPER2): build() fails with a message
//  otherwise — exact polygon booleans are load-bearing here, no fallback.
// =============================================================================
#include "LayerSlicer.h"
#include "ToolpathModel.h"
#include <string>
#include <vector>

namespace ToolpathSections {

struct Options {
    float closeGapMM = -1.0f;  // gap-weld radius; <0 -> auto (0.5 x median bead width)
    float simplifyMM = 0.05f;  // vertex simplification tolerance
    float minAreaMM2 = -1.0f;  // dust filter; <0 -> auto ((width/2)^2)
    bool  includeAids = false; // keep support/brim beads (default: part only)
};

struct LayerSections {
    std::vector<LayerSlicer::Section> sections; // one per printed layer, ascending
    std::vector<float>  zTopMM;     // per layer: TOP plane, part-centered mm
    std::vector<float>  heightMM;   // per layer: layer height (mm)
    std::vector<double> netAreaMM2; // per layer: outer - holes (mm^2)
    int   totalLoops = 0;
    int   totalHoles = 0;
    float medianWidthMM  = 0.42f;   // median bead width (drives auto knobs)
    float closeGapUsedMM = 0.0f;    // resolved weld radius (report echo)
};

bool build(const Toolpath::ToolpathModel& tp, const Options& opt,
           LayerSections& out, std::string& err);

} // namespace ToolpathSections
