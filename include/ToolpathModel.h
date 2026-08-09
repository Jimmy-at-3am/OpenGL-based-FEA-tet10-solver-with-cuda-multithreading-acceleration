#pragma once
// =============================================================================
//  ToolpathModel.h  --  data contract for a parsed Bambu
//  .gcode.3mf toolpath (the AUTHORITATIVE geometry source for FDM simulation:
//  STL carries no units and no per-layer height / infill edits; only the
//  sliced file records what the printer actually deposits).
//
//  Ported from the proven standalone prototype
//  `code/fea_slicer/gcode_viewer/gcode.h` -- FeatureType enum values are kept
//  IDENTICAL to the prototype so its README color table and docs stay valid.
//
//  Coordinates are millimetres in PLATE frame (Bambu gcode is always mm, and
//  the build axis is always +Z = layer normal). This header stays glm+stdlib
//  only: no miniz, no GL, no OCC.
// =============================================================================
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Toolpath {

// Feature/line types emitted by Bambu Studio as "; FEATURE: <name>" comments.
// Values MUST match fea_slicer/gcode_viewer/gcode.h.
enum FeatureType : int {
    FT_OTHER = 0,
    FT_OUTER_WALL,
    FT_INNER_WALL,
    FT_OVERHANG_WALL,
    FT_SOLID_INFILL,
    FT_SPARSE_INFILL,
    FT_TOP_SURFACE,
    FT_BOTTOM_SURFACE,
    FT_BRIDGE,
    FT_GAP_INFILL,
    FT_SUPPORT,
    FT_SUPPORT_INTERFACE,
    FT_BRIM,
    FT_FLOATING_SHELL,
    FT_CUSTOM,        // prime/flush/wipe lines -- never part of the object
    FT_COUNT
};

// One extruding move (already arc-tessellated). p0/p1 = nozzle positions (mm).
struct Segment {
    glm::vec3 p0, p1;
    float width  = 0.42f;  // extrusion line width (mm)
    float height = 0.20f;  // layer height (mm)
    int   feature = FT_OTHER;
    int   layer   = 0;     // 0-based layer index
};

struct ToolpathModel {
    std::vector<Segment> segments;
    int       layerCount = 0;
    // Object bbox (mm, plate frame). Deviation from the prototype, on purpose:
    // print AIDS (support / support interface / brim) are excluded here in
    // addition to FT_CUSTOM, so bbMin/bbMax describe the PART the FEA will
    // simulate. rawMin/rawMax still cover every extrusion.
    glm::vec3 bbMin{0.0f}, bbMax{0.0f};
    glm::vec3 rawMin{0.0f}, rawMax{0.0f};
    long long arcSegments = 0;             // sub-segments produced by G2/G3
    int       featureCounts[FT_COUNT] = {0}; // segments per feature (post-filter)
};

// True for features that are printing aids, not part material.
bool        isPrintAid(int feature);
int         featureFromString(const std::string& s);
const char* featureName(int feature);

} // namespace Toolpath
