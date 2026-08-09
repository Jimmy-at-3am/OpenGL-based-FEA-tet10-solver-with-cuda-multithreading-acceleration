#include "StepLoader.h"
#include "BRepHandle.h"

#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <tuple>
#include <cmath>
#include <cctype>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Shared vertex welder (same pattern as STLLoader.cpp)
// ---------------------------------------------------------------------------
namespace {

using IKey = std::tuple<int64_t, int64_t, int64_t>;

struct IKeyHash {
    size_t operator()(const IKey& k) const {
        size_t h = 0;
        auto mix = [&](int64_t v) {
            h ^= std::hash<int64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(std::get<0>(k)); mix(std::get<1>(k)); mix(std::get<2>(k));
        return h;
    }
};

struct Welder {
    static constexpr float kTol = 1e-6f;
    std::unordered_map<IKey, unsigned, IKeyHash> map;
    LoadedGeometry& out;

    explicit Welder(LoadedGeometry& g) : out(g) {}

    unsigned add(float x, float y, float z) {
        auto snap = [](float v) -> int64_t {
            return static_cast<int64_t>(std::round(v / kTol));
        };
        IKey key = { snap(x), snap(y), snap(z) };
        auto it = map.find(key);
        if (it != map.end()) return it->second;
        const unsigned idx = static_cast<unsigned>(out.positions.size());
        map[key] = idx;
        out.positions.push_back(glm::vec3(x, y, z));
        return idx;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------

#ifdef HAS_OCCT

#include "BRepHandle_impl.h"

// OCC STEP / shape headers
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <ShapeFix_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopLoc_Location.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <Interface_Static.hxx>
#include <XSControl_WorkSession.hxx>

// Count top-level solids in a compound shape.
static int countSolids(const TopoDS_Shape& shape) {
    int n = 0;
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) ++n;
    return std::max(n, 1); // treat a single shell/face as 1 solid
}

// Heal common B-rep issues (open edges, bad orientation).
static void healShape(TopoDS_Shape& shape) {
    Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(shape);
    fixer->SetPrecision(Precision::Confusion());
    fixer->Perform();
    shape = fixer->Shape();
}

// Harvest tessellated triangles from a tessellated shape into LoadedGeometry.
// Also populates triangleFaceId (one entry per output triangle, 1-based face index).
static void harvestTriangles(const TopoDS_Shape& shape,
                             const TopTools_IndexedMapOfShape& faceMap,
                             LoadedGeometry& out,
                             std::vector<int>& triangleFaceId)
{
    Welder w(out);
    // Reserve conservatively: faces × 2 triangles × 3 indices
    w.map.reserve(static_cast<size_t>(faceMap.Extent()) * 8);

    for (TopExp_Explorer fexp(shape, TopAbs_FACE); fexp.More(); fexp.Next()) {
        const TopoDS_Face face = TopoDS::Face(fexp.Current());
        const int faceId = faceMap.FindIndex(face); // 1-based

        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull() || tri->NbTriangles() == 0) {
            std::cout << "[STEP] Warning: face " << faceId
                      << " has no triangulation (degenerate?). Skipping.\n";
            continue;
        }

        const bool reversed = (face.Orientation() == TopAbs_REVERSED);
        const gp_Trsf trsf  = loc.IsIdentity() ? gp_Trsf() : loc.IsIdentity() ?
                                                  gp_Trsf() : loc.Transformation();
        // NOTE: loc.IsIdentity() guard avoids applying an identity transform
        // (it's a no-op but wastes computation). We always apply loc below.

        // Build a local position array for this face's nodes.
        // OCC nodes are 1-indexed; we convert to 0-indexed for Welder.
        const int nNodes = tri->NbNodes();
        std::vector<glm::vec3> pts(nNodes);
        for (int n = 1; n <= nNodes; ++n) {
            gp_Pnt p = tri->Node(n).Transformed(loc.Transformation());
            pts[n-1] = glm::vec3((float)p.X(), (float)p.Y(), (float)p.Z());
        }

        // Build a local → welded-global index map.
        std::vector<unsigned> localToGlobal(nNodes);
        for (int n = 0; n < nNodes; ++n)
            localToGlobal[n] = w.add(pts[n].x, pts[n].y, pts[n].z);

        // Harvest triangles.
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int n1, n2, n3;
            tri->Triangle(t).Get(n1, n2, n3);
            // OCC is 1-indexed.
            unsigned i0 = localToGlobal[n1 - 1];
            unsigned i1 = localToGlobal[n2 - 1];
            unsigned i2 = localToGlobal[n3 - 1];
            if (i0 == i1 || i1 == i2 || i0 == i2) continue; // degenerate
            // REVERSED faces: flip winding so outward normals are consistent.
            if (reversed) std::swap(i1, i2);
            out.indices.push_back(i0);
            out.indices.push_back(i1);
            out.indices.push_back(i2);
            triangleFaceId.push_back(faceId);
        }
    }
}

// Core implementation shared by load() and loadWithBRep().
static bool loadSTEP_impl(const std::string& path,
                           LoadedGeometry& out,
                           BRepHandle::Impl& impl,
                           const FEAParams& params)
{
    out.clear();
    out.sourceLabel  = fs::path(path).filename().string();
    out.objectCount  = 1;

    // --- Read ---
    STEPControl_Reader reader;
    IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    if (status != IFSelect_RetDone) {
        std::cout << "[STEP] Cannot read '" << path << "' (status=" << status << ")\n";
        return false;
    }
    reader.TransferRoots();
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        std::cout << "[STEP] No geometry found in '" << path << "'\n";
        return false;
    }

    // --- Detect cascade unit (after TransferRoots) ---
    // keep the unit as a mm conversion factor (was previously logged
    // then discarded). FEAModel records the real physical size from it before the
    // 3-unit render normalisation.
    float stepUnitToMM = 1.0f;
    {
        const char* cu = Interface_Static::CVal("xstep.cascade.unit");
        impl.fileUnits_ = (cu && *cu) ? cu : "MM";
        std::string u = impl.fileUnits_;
        for (auto& c : u) c = (char)std::toupper((unsigned char)c);
        if      (u == "MM" || u == "MILLIMETER" || u == "MILLIMETRE") stepUnitToMM = 1.0f;
        else if (u == "CM" || u == "CENTIMETER" || u == "CENTIMETRE") stepUnitToMM = 10.0f;
        else if (u == "M"  || u == "METER"      || u == "METRE")      stepUnitToMM = 1000.0f;
        else if (u == "IN" || u == "INCH")                            stepUnitToMM = 25.4f;
        else if (u == "FT" || u == "FOOT")                            stepUnitToMM = 304.8f;
        else if (u == "UM" || u == "MICRON" || u == "MICROMETER")     stepUnitToMM = 0.001f;
        else stepUnitToMM = 1.0f; // unknown -> assume mm
        std::cout << "[STEP] cascade unit: " << impl.fileUnits_
                  << " (= " << stepUnitToMM << " mm/unit; real size preserved)\n";
    }

    // --- Count solids ---
    impl.numSolids_ = countSolids(shape);
    if (impl.numSolids_ > 1)
        std::cout << "[STEP] " << impl.numSolids_
                  << " solids found; meshed as a single assembly\n";

    // --- Heal ---
    healShape(shape);

    // --- Populate face/edge/vertex maps (AFTER healing — face IDs may shift) ---
    TopExp::MapShapes(shape, TopAbs_FACE,   impl.faceMap);
    TopExp::MapShapes(shape, TopAbs_EDGE,   impl.edgeMap);
    TopExp::MapShapes(shape, TopAbs_VERTEX, impl.vertexMap);

    // --- Compute bbox for lin_def ---
    Bnd_Box bbox;
    BRepBndLib::Add(shape, bbox);
    double xMin, yMin, zMin, xMax, yMax, zMax;
    bbox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    const double dx   = xMax - xMin, dy = yMax - yMin, dz = zMax - zMin;
    const double Ldiag = std::sqrt(dx*dx + dy*dy + dz*dz);
    const double linDef = std::max(static_cast<double>(params.sizingChordError) * Ldiag,
                                   1e-6);
    constexpr double angDef = 0.3; // ~17° — COMSOL default

    // --- Tessellate ---
    // IMeshTools_Parameters gives access to MinSize, which prevents the UV
    // parameterisation singularity at sphere/cone poles from producing
    // degenerate sliver triangles (all meridians converge to a single point
    // at the pole, creating fans of near-zero-area triangles).
    // MinSize = linDef means no triangle edge shorter than the chord error
    // budget is ever created.  This collapses the polar fan into a single
    // triangle cap instead of dozens of slivers.
    IMeshTools_Parameters meshParams;
    meshParams.Deflection  = linDef;
    meshParams.Angle       = angDef;
    meshParams.InParallel  = Standard_True;
    meshParams.MinSize     = linDef;   // polar sliver suppression
    meshParams.Relative    = Standard_False;
    BRepMesh_IncrementalMesh mesher(shape, meshParams);
    // BRepMesh_IncrementalMesh performs tessellation in its constructor.

    // --- Harvest ---
    std::vector<int> triangleFaceId;
    harvestTriangles(shape, impl.faceMap, out, triangleFaceId);
    out.fileUnitToMM = stepUnitToMM; // real-size preservation

    if (out.positions.empty() || out.indices.empty()) {
        std::cout << "[STEP] Tessellation produced no triangles for '"
                  << out.sourceLabel << "'\n";
        return false;
    }

    impl.shape          = shape;
    impl.triangleFaceId = std::move(triangleFaceId);

    const int nTris = static_cast<int>(out.indices.size() / 3);
    std::cout << "[STEP] read OK, " << impl.numSolids_ << " solid(s), "
              << impl.faceMap.Extent()   << " faces, "
              << impl.edgeMap.Extent()   << " edges, "
              << impl.vertexMap.Extent() << " vertices\n";
    std::cout << "[STEP] tessellated -> " << nTris << " triangles"
              << " (lin_def=" << linDef
              << ", ang_def=" << angDef << " rad, parallel=true)\n";
    return true;
}

#endif // HAS_OCCT

// =============================================================================
// StepLoader::load  (IGeometryLoader interface)
// =============================================================================
bool StepLoader::load(const std::string& path, LoadedGeometry& out)
{
#ifdef HAS_OCCT
    BRepHandle::Impl impl;
    FEAParams defaults;
    return loadSTEP_impl(path, out, impl, defaults);
#else
    (void)path; (void)out;
    std::cout << "[STEP] StepLoader: OCCT support not compiled in "
                 "(rebuild with USE_OCCT=ON)\n";
    return false;
#endif
}

// =============================================================================
// StepLoader::loadWithBRep  (extended API)
// =============================================================================
bool StepLoader::loadWithBRep(const std::string& path,
                               LoadedGeometry& out,
                               std::unique_ptr<BRepHandle>& brep_out,
                               const FEAParams& params)
{
#ifdef HAS_OCCT
    BRepHandle::Impl impl;
    if (!loadSTEP_impl(path, out, impl, params)) return false;
    brep_out = BRepHandle::create(std::move(impl));
    return brep_out != nullptr;
#else
    (void)path; (void)out; (void)brep_out; (void)params;
    std::cout << "[STEP] StepLoader: OCCT support not compiled in "
                 "(rebuild with USE_OCCT=ON)\n";
    return false;
#endif
}
