#pragma once
// =============================================================================
// BRepHandle_impl.h  --  INTERNAL HEADER (not in include/).
//
// Defines BRepHandle::Impl with OCC types.  Include ONLY from:
//   src/BRepHandle.cpp
//   src/StepLoader.cpp
//
// All other translation units include only include/BRepHandle.h and pay zero
// OCC compile-time overhead.
// =============================================================================

#ifdef HAS_OCCT

#include "BRepHandle.h"

// --- OpenCASCADE headers (MSVC: no pragma warning suppression needed for 7.8) ---
#include <TopoDS_Shape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <Precision.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopLoc_Location.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <Poly_Triangulation.hxx>
#include <Interface_Static.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>

// ---------------------------------------------------------------------------
// BRepHandle::Impl
// ---------------------------------------------------------------------------
struct BRepHandle::Impl {
    TopoDS_Shape               shape;
    TopTools_IndexedMapOfShape faceMap;
    TopTools_IndexedMapOfShape edgeMap;
    TopTools_IndexedMapOfShape vertexMap;

    // Per surface-triangle: index into faceMap (1-based), populated during
    // tessellation harvest in StepLoader::loadWithBRep.
    // triangleFaceId[i] == faceMap index of the face that owns triangle i.
    std::vector<int>  triangleFaceId;

    int         numSolids_ = 0;
    std::string fileUnits_ = "MM";

    // Convenience: create a TopoDS_Vertex from a glm::dvec3.
    TopoDS_Vertex makeVertex(glm::dvec3 p) const {
        TopoDS_Vertex v;
        BRep_Builder b;
        b.MakeVertex(v, gp_Pnt(p.x, p.y, p.z), Precision::Confusion());
        return v;
    }
};

#endif // HAS_OCCT
