// BRepHandle.cpp is only compiled when HAS_OCCT=1.
// When HAS_OCCT=0, BRepHandle.h provides inline stubs — nothing to define here.
#ifdef HAS_OCCT

#include "BRepHandle.h"
#include "BRepHandle_impl.h"

#include <iostream>

// =============================================================================
// BRepHandle — lifecycle
// =============================================================================

BRepHandle::BRepHandle()  = default;
BRepHandle::~BRepHandle() = default;
BRepHandle::BRepHandle(BRepHandle&&) noexcept            = default;
BRepHandle& BRepHandle::operator=(BRepHandle&&) noexcept = default;

// =============================================================================
// BRepHandle — factory
// =============================================================================

std::unique_ptr<BRepHandle> BRepHandle::create(BRepHandle::Impl impl)
{
    auto h      = std::make_unique<BRepHandle>();
    h->impl_    = std::make_unique<BRepHandle::Impl>(std::move(impl));
    return h;
}

// =============================================================================
// BRepHandle — shape statistics
// =============================================================================

int BRepHandle::numSolids() const {
#ifdef HAS_OCCT
    return impl_ ? impl_->numSolids_ : 0;
#else
    return 0;
#endif
}

int BRepHandle::numFaces() const {
#ifdef HAS_OCCT
    return impl_ ? impl_->faceMap.Extent() : 0;
#else
    return 0;
#endif
}

int BRepHandle::numEdges() const {
#ifdef HAS_OCCT
    return impl_ ? impl_->edgeMap.Extent() : 0;
#else
    return 0;
#endif
}

int BRepHandle::numVertices() const {
#ifdef HAS_OCCT
    return impl_ ? impl_->vertexMap.Extent() : 0;
#else
    return 0;
#endif
}

std::string BRepHandle::fileUnits() const {
#ifdef HAS_OCCT
    return impl_ ? impl_->fileUnits_ : "UNKNOWN";
#else
    return "UNKNOWN";
#endif
}

// =============================================================================
// BRepHandle — exact geometry queries
// =============================================================================

glm::dvec3 BRepHandle::nearestPointOnShape(glm::dvec3 query) const
{
#ifdef HAS_OCCT
    if (!impl_ || impl_->shape.IsNull()) return query;
    try {
        const TopoDS_Vertex vx = impl_->makeVertex(query);
        BRepExtrema_DistShapeShape dss(vx, impl_->shape);
        if (!dss.IsDone() || dss.NbSolution() < 1) return query;
        const gp_Pnt& res = dss.PointOnShape2(1);
        return glm::dvec3(res.X(), res.Y(), res.Z());
    } catch (...) {
        return query;
    }
#else
    return query;
#endif
}

glm::dvec3 BRepHandle::normalAtNearest(glm::dvec3 query) const
{
#ifdef HAS_OCCT
    if (!impl_ || impl_->shape.IsNull()) return glm::dvec3(0.0, 1.0, 0.0);
    try {
        const TopoDS_Vertex vx = impl_->makeVertex(query);
        BRepExtrema_DistShapeShape dss(vx, impl_->shape);
        if (!dss.IsDone() || dss.NbSolution() < 1)
            return glm::dvec3(0.0, 1.0, 0.0);

        // Prefer an InFace solution — gives exact UV → analytic normal.
        for (int i = 1; i <= dss.NbSolution(); ++i) {
            if (dss.SupportTypeShape2(i) == BRepExtrema_IsInFace) {
                const TopoDS_Face face = TopoDS::Face(dss.SupportOnShape2(i));
                Standard_Real u = 0.0, v = 0.0;
                dss.ParOnFaceS2(i, u, v);
                BRepLProp_SLProps props(BRepAdaptor_Surface(face), u, v, 1, 1e-6);
                if (props.IsNormalDefined()) {
                    const gp_Dir& n = props.Normal();
                    const double sign = (face.Orientation() == TopAbs_REVERSED) ? -1.0 : 1.0;
                    return glm::dvec3(sign * n.X(), sign * n.Y(), sign * n.Z());
                }
            }
        }

        // Fallback for edge/vertex support (degenerate pole edges, seam edges,
        // cylinder caps, etc.).  BRepExtrema classified the nearest point as
        // lying on an edge or vertex rather than a face interior.  We recover
        // the analytic normal by projecting the nearest 3-D point onto the
        // parametric surface of each face and taking the face whose projection
        // is closest.  This handles UV-parameterisation singularities (sphere
        // poles, cone apex) where InFace solutions are never generated.
        const gp_Pnt nearPt = dss.PointOnShape2(1);
        double     bestD2 = 1e300;
        glm::dvec3 bestN(0.0, 1.0, 0.0);
        for (int fi = 1; fi <= impl_->faceMap.Extent(); ++fi) {
            const TopoDS_Face       face  = TopoDS::Face(impl_->faceMap(fi));
            const BRepAdaptor_Surface adapt(face);
            GeomAPI_ProjectPointOnSurf proj(
                nearPt,
                adapt.Surface().Surface(),
                adapt.FirstUParameter(), adapt.LastUParameter(),
                adapt.FirstVParameter(), adapt.LastVParameter(),
                1e-7);
            if (!proj.IsDone() || proj.NbPoints() == 0) continue;
            const double d2 = proj.LowerDistance() * proj.LowerDistance();
            if (d2 < bestD2) {
                Standard_Real u, v;
                proj.LowerDistanceParameters(u, v);
                BRepLProp_SLProps props(adapt, u, v, 1, 1e-6);
                if (props.IsNormalDefined()) {
                    const gp_Dir& n = props.Normal();
                    const double sign = (face.Orientation() == TopAbs_REVERSED) ? -1.0 : 1.0;
                    bestD2 = d2;
                    bestN  = glm::dvec3(sign * n.X(), sign * n.Y(), sign * n.Z());
                }
            }
        }
        return bestN;
    } catch (...) {}
    return glm::dvec3(0.0, 1.0, 0.0);
#else
    (void)query;
    return glm::dvec3(0.0, 1.0, 0.0);
#endif
}

std::pair<double,double> BRepHandle::principalCurvatures(glm::dvec3 query) const
{
#ifdef HAS_OCCT
    if (!impl_ || impl_->shape.IsNull()) return {0.0, 0.0};
    try {
        const TopoDS_Vertex vx = impl_->makeVertex(query);
        BRepExtrema_DistShapeShape dss(vx, impl_->shape);
        if (!dss.IsDone() || dss.NbSolution() < 1) return {0.0, 0.0};

        for (int i = 1; i <= dss.NbSolution(); ++i) {
            if (dss.SupportTypeShape2(i) == BRepExtrema_IsInFace) {
                const TopoDS_Face face = TopoDS::Face(dss.SupportOnShape2(i));
                Standard_Real u = 0.0, v = 0.0;
                dss.ParOnFaceS2(i, u, v);
                BRepLProp_SLProps props(BRepAdaptor_Surface(face), u, v, 2, 1e-6);
                if (props.IsCurvatureDefined()) {
                    return {props.MinCurvature(), props.MaxCurvature()};
                }
            }
        }
    } catch (...) {}
    return {0.0, 0.0};
#else
    (void)query;
    return {0.0, 0.0};
#endif
}

#endif // HAS_OCCT
