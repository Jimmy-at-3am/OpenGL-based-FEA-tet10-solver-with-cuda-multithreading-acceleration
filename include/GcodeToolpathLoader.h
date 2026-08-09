#pragma once
// =============================================================================
//  GcodeToolpathLoader.h  --  open a Bambu .gcode.3mf (a zip),
//  extract Metadata/plate_1.gcode (or the first *.gcode entry) and parse it
//  into a Toolpath::ToolpathModel.
//
//  NOT an IGeometryLoader: it produces toolpath DATA, never a triangle mesh
//  (architecture rule: toolpaths drive material/section building, they are not
//  geometry to tessellate). FEAModel::loadGcode3mf() wraps this and
//  synthesizes the minimal render shell.
//
//  Port of code/fea_slicer/gcode_viewer/gcode_parser.cpp (proven on the
//  193-layer bearing-pod fixture); zip access uses the same miniz build
//  ThreeMFLoader already links.
// =============================================================================
#include "ToolpathModel.h"
#include <string>

namespace GcodeToolpathLoader {

struct Options {
    // Drop print aids (support / support interface / brim / custom) from the
    // segment list and feature counts. The object bbox excludes them always.
    bool partOnly = false;
};

// Returns false with a message in `err` on any failure (missing/corrupt zip,
// no .gcode entry, zero extrusion segments).
bool load(const std::string& path, const Options& opt,
          Toolpath::ToolpathModel& out, std::string& err);

} // namespace GcodeToolpathLoader
