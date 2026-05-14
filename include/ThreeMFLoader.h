#pragma once
// =============================================================================
// ThreeMFLoader.h
// =============================================================================
// Loads .3mf files into LoadedGeometry.
//
// SUPPORTED FORMATS
// -----------------
//
// 1. Standard 3MF (PrusaSlicer, Fusion 360, OnShape, …)
//    3D/3dmodel.model contains inline <mesh> data inside each <object>.
//    This is the path the original loader always took.
//
// 2. Bambu Studio project .3mf
//    3D/3dmodel.model is a MANIFEST — each <object> contains <components>
//    that reference separate per-object files via p:path:
//
//      <object id="1" type="model">
//        <components>
//          <component p:path="/3D/Objects/object_1.model"
//                     objectid="1"
//                     transform="m00 m10 m20 m01 m11 m21 m02 m12 m22 m03 m13 m23"/>
//        </components>
//      </object>
//
//    The 12-float transform is column-major 3×4 (each column listed top-to-bottom,
//    then the translation column):
//      [ m00 m01 m02 m03 ]
//      [ m10 m11 m12 m13 ]
//      [ m20 m21 m22 m23 ]
//
//    The loader follows each p:path entry, extracts the object file from the
//    same ZIP, parses its mesh, and applies the component transform before
//    appending vertices into the merged output.
//
// NOTE: Bambu Studio SLICED exports (.gcode.3mf) do NOT contain geometry —
//       they strip the 3D model and store only the sliced G-code.  Use the
//       Bambu Studio project .3mf export instead.
//
// IMPLEMENTATION NOTES
// --------------------
// • ZIP decompression: miniz (single-header, pulled via FetchContent).
// • XML parsing: hand-written scanner — no external XML library.
// • Multiple objects are merged into one position/index array with correct
//   per-object vertex offsets.
// =============================================================================

#include "IGeometryLoader.h"
#include <string>
#include <vector>

// Column-major 3×4 affine transform (identity by default).
// Storage: m[0..2]=col0, m[3..5]=col1, m[6..8]=col2, m[9..11]=translation.
struct Mat34 {
    float m[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
    bool  isIdentity = true;
};

// A <build><item> entry from 3D/3dmodel.model.
struct BuildItem {
    int   objectId = -1;
    Mat34 xform;
};

// A <components><component> entry — references a mesh in a separate ZIP file.
struct ComponentRef {
    int         objectId = -1;
    std::string pPath;       // e.g. "/3D/Objects/object_1.model"
    Mat34       xform;
};

class ThreeMFLoader : public IGeometryLoader {
public:
    bool load(const std::string& path, LoadedGeometry& out) override;

private:
    // Parse decompressed XML from a .model file.
    //
    // objectIdFilter: if >= 0, only <object id="objectIdFilter"> contributes
    //                 mesh data.  Pass -1 to collect all objects.
    //
    // Fills outBuildItems with every <build><item>, and outComponentRefs with
    // every <components><component> that carries a p:path attribute.
    bool parseModelXml(const std::string& xml,
                       LoadedGeometry&             out,
                       int                         objectIdFilter,
                       std::vector<BuildItem>&     outBuildItems,
                       std::vector<ComponentRef>&  outComponentRefs);
};
