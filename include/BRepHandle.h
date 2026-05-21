#pragma once
// =============================================================================
// BRepHandle.h  --  OCC-free public interface for a retained B-rep shape.
//
// When HAS_OCCT=1: full pImpl class; implementation split across
//   src/BRepHandle.cpp (methods) and src/BRepHandle_impl.h (Impl struct).
//   FEAModel.h forward-declares BRepHandle; FEAModel::~FEAModel() must be
//   defined in FEAModel.cpp (where this header is #included) so that
//   unique_ptr<BRepHandle> can call the complete destructor.
//
// When HAS_OCCT=0: lightweight stub class; all methods inline; no .cpp needed.
//   mesh_diag and any other target that lacks HAS_OCCT link nothing extra.
// =============================================================================

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>

#ifdef HAS_OCCT

// ---------------------------------------------------------------------------
// Full pImpl class — used by FEAPreProcessor (HAS_OCCT=1 build).
// ---------------------------------------------------------------------------
class BRepHandle {
public:
    struct Impl;   // defined in src/BRepHandle_impl.h (OCC-aware, internal only)

    BRepHandle();
    ~BRepHandle();
    BRepHandle(BRepHandle&&) noexcept;
    BRepHandle& operator=(BRepHandle&&) noexcept;
    BRepHandle(const BRepHandle&)            = delete;
    BRepHandle& operator=(const BRepHandle&) = delete;

    // Shape statistics.
    int         numSolids()   const;
    int         numFaces()    const;
    int         numEdges()    const;
    int         numVertices() const;
    std::string fileUnits()   const; // "MM", "M", "INCH", …

    // Nearest point on the analytic NURBS/B-rep surface to query point.
    // Returns the query point itself when the BRep is empty.
    glm::dvec3 nearestPointOnShape(glm::dvec3 query) const;

    // Surface unit normal at the point on the B-rep nearest to query.
    // Returns (0,1,0) when the nearest locus is an edge/vertex or BRep is empty.
    glm::dvec3 normalAtNearest(glm::dvec3 query) const;

    // Per-face principal curvatures at the nearest point. Returns {0,0} if unavailable.
    std::pair<double,double> principalCurvatures(glm::dvec3 query) const;

    // Factory: called by StepLoader only (Impl is complete there via BRepHandle_impl.h).
    static std::unique_ptr<BRepHandle> create(Impl impl);

private:
    std::unique_ptr<Impl> impl_;
};

#else // !HAS_OCCT

// ---------------------------------------------------------------------------
// Stub class — all methods inline; compiles with zero OCC dependency.
// Targets that lack HAS_OCCT (mesh_diag, etc.) use this path automatically.
// ---------------------------------------------------------------------------
class BRepHandle {
public:
    int         numSolids()   const { return 0; }
    int         numFaces()    const { return 0; }
    int         numEdges()    const { return 0; }
    int         numVertices() const { return 0; }
    std::string fileUnits()   const { return "UNKNOWN"; }

    glm::dvec3 nearestPointOnShape(glm::dvec3 q)  const { return q; }
    glm::dvec3 normalAtNearest(glm::dvec3 /*q*/)  const { return glm::dvec3(0.0, 1.0, 0.0); }
    std::pair<double,double> principalCurvatures(glm::dvec3 /*q*/) const { return {0.0, 0.0}; }
};

#endif // HAS_OCCT
