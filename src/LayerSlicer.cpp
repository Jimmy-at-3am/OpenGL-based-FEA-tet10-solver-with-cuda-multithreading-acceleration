// =============================================================================
//  LayerSlicer.cpp  --  new_TODO_04: SLICE data model + slice-plane contour
//  extraction (triangle-mesh path + layer grouping + Clipper2 cleanup).
//
//  Pipeline per slab-centre plane:
//    1. Intersect every surface triangle with the plane -> 2-D segments.
//    2. Weld segment endpoints on a hash grid (tol = 1e-5 * L_diag).
//    3. Walk the edge graph to stitch closed loops; drop open chains.
//    4. Clipper2 Union(EvenOdd) + SimplifyPaths (robustness backstop; optional).
//    5. Even-odd containment -> classify outer (CCW) vs hole (CW).
//
//  The B-rep path (sliceBRepPlane) lives in StepSlicer.cpp and is preferred when
//  a BRepHandle is present; it returns false until new_TODO_04B lands the OCC
//  body, so callers transparently fall back to the triangle path here.
// =============================================================================
#include "LayerSlicer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_map>

#ifdef HAS_CLIPPER2
#include "clipper2/clipper.h"
#endif

namespace LayerSlicer {

// ---------------------------------------------------------------------------
// Build-axis <-> 2-D projection helpers. The dropped axis is the build axis;
// the remaining two form the (u,v) cross-section plane. from2D is the exact
// inverse of to2D at a given plane height.
// ---------------------------------------------------------------------------
// The (u,v) frame is chosen RIGHT-HANDED for every axis so that u x v = +axis
// normal: X->(y,z) [y x z=+x], Y->(z,x) [z x x=+y], Z->(x,y) [x x y=+z]. A naive
// Y->(x,z) is left-handed (x x z = -y), which would silently flip 'outer CCW' to
// physically CW about +Y and mirror the section preview -- inconsistent with the
// other two axes and a trap for the new_TODO_05 extrusion (which derives the
// wall-offset side from the loop winding). Keeping all three right-handed makes
// the documented "outer CCW" contract hold uniformly.
static inline glm::vec2 to2D(const glm::vec3& p, int axis) {
    if (axis == 0) return glm::vec2(p.y, p.z);
    if (axis == 1) return glm::vec2(p.z, p.x);
    return glm::vec2(p.x, p.y);
}
static inline float axisCoord(const glm::vec3& p, int axis) {
    return (axis == 0) ? p.x : (axis == 1) ? p.y : p.z;
}
static inline glm::vec3 from2D(const glm::vec2& q, int axis, float coord) {
    if (axis == 0) return glm::vec3(coord, q.x, q.y);
    if (axis == 1) return glm::vec3(q.y, coord, q.x);
    return glm::vec3(q.x, q.y, coord);
}

int axisFromParams(const FEAParams& p) {
    int a = p.buildAxisSel;
    return (a == 0 || a == 1 || a == 2) ? a : 2;
}

double signedArea(const std::vector<glm::vec2>& loop) {
    const size_t n = loop.size();
    if (n < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2& p = loop[i];
        const glm::vec2& q = loop[(i + 1) % n];
        a += (double)p.x * (double)q.y - (double)q.x * (double)p.y;
    }
    return 0.5 * a;
}

// Even-odd point-in-polygon (ray cast). Boundary cases are irrelevant here
// because we test loop centroids, which sit strictly inside/outside for the
// convex cross-sections this TODO validates.
static bool pointInLoop(const glm::vec2& pt, const std::vector<glm::vec2>& loop) {
    bool inside = false;
    const size_t n = loop.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const glm::vec2& a = loop[i];
        const glm::vec2& b = loop[j];
        if (((a.y > pt.y) != (b.y > pt.y)) &&
            (pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

// A representative test point that lies ON the loop (midpoint of its first edge),
// used for even-odd nesting classification. Unlike the loop centroid this is
// guaranteed to share the loop's inside/outside status relative to every other
// non-intersecting loop in the section: a boundary point of an outer loop can
// never lie inside a hole nested within it (the hole is in the loop's interior,
// disjoint from its boundary). A centroid carries no such guarantee -- for a
// concentric hole the outer square's centroid lands INSIDE the hole, which made
// the classifier count the hole as containing the outer loop and misclassify the
// outer loop as a hole (outerArea -> 0, NEGATIVE net area). The first-edge
// midpoint classifies concentric, off-centre, disjoint, and island-in-hole
// sections alike.
static glm::vec2 loopProbePoint(const std::vector<glm::vec2>& loop) {
    if (loop.size() < 2) return loop.empty() ? glm::vec2(0.0f) : loop[0];
    return 0.5f * (loop[0] + loop[1]);
}

// ---------------------------------------------------------------------------
// Triangle-plane intersection. Classifies each vertex by signed distance along
// the build axis (above = s > 0); a triangle straddling the plane emits one
// segment between its two crossing points. The s>0 tie-break is a deterministic
// equivalent of the spec's "nudge plane off a coincident vertex" (it never
// touches the geometry and is reproducible across runs).
// ---------------------------------------------------------------------------
static void slicePlaneTriangles(const std::vector<Vertex>& verts,
                                const std::vector<unsigned int>& idx,
                                int axis, float coord,
                                std::vector<std::pair<glm::vec2, glm::vec2>>& segs) {
    const size_t nVerts = verts.size();
    const size_t nTri = idx.size() / 3;
    for (size_t t = 0; t < nTri; ++t) {
        const unsigned int i0 = idx[t * 3 + 0], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
        // Bounds guard: a corrupt/soup index buffer (e.g. a malformed 3MF/STEP
        // tessellation) could index past the vertex array -> OOB read / UB.
        // Skip such triangles so the headless slicer never crashes on bad input.
        if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) continue;
        const glm::vec3& p0 = verts[i0].position;
        const glm::vec3& p1 = verts[i1].position;
        const glm::vec3& p2 = verts[i2].position;
        const glm::vec3 P[3] = { p0, p1, p2 };
        float s[3] = { axisCoord(p0, axis) - coord,
                       axisCoord(p1, axis) - coord,
                       axisCoord(p2, axis) - coord };

        // NOTE: the skip test is `above in {0,3}`, NOT a strict above&&below
        // straddle. This is deliberate: when the plane coincides with a horizontal
        // mesh edge ring (a prism wall split exactly at coord), the real contour is
        // carried by the ON-PLANE edge of the s=[0,0,+] triangle, while its lower
        // neighbour s=[0,0,-] is skipped -- so the ring edge is emitted exactly
        // once. A strict straddle would drop it and the section would vanish at
        // that plane. The only residual artefact is a pure tangent ridge (both
        // neighbours above) emitting a doubled on-plane edge, which stitchLoops
        // discards as a benign 2-cycle (a discardedChains++ but no loop corruption).
        int above = (s[0] > 0.0f) + (s[1] > 0.0f) + (s[2] > 0.0f);
        if (above == 0 || above == 3) continue; // no strict crossing (or coplanar)

        glm::vec2 hits[2];
        int nh = 0;
        for (int e = 0; e < 3 && nh < 2; ++e) {
            int a = e, b = (e + 1) % 3;
            bool aAbove = s[a] > 0.0f, bAbove = s[b] > 0.0f;
            if (aAbove == bAbove) continue;          // edge does not cross
            float denom = s[a] - s[b];
            float tt = (denom != 0.0f) ? s[a] / denom : 0.0f;
            glm::vec3 X = P[a] + tt * (P[b] - P[a]);
            hits[nh++] = to2D(X, axis);
        }
        if (nh == 2) {
            // Drop zero-length segments (vertex grazing the plane).
            glm::vec2 d = hits[1] - hits[0];
            if (d.x * d.x + d.y * d.y > 0.0f) segs.emplace_back(hits[0], hits[1]);
        }
    }
}

// ---------------------------------------------------------------------------
// Weld endpoints onto a hash grid and stitch segments into closed loops.
// discardedChains counts open chains (an STL crack / non-manifold edge).
// ---------------------------------------------------------------------------
static void stitchLoops(const std::vector<std::pair<glm::vec2, glm::vec2>>& segs,
                        float tol,
                        std::vector<std::vector<glm::vec2>>& loops,
                        int& discardedChains) {
    discardedChains = 0;
    if (segs.empty()) return;

    const float invTol = 1.0f / std::max(tol, 1e-12f);
    // Pack an integer cell (ix,iy) into a 64-bit key. Neighbour keys MUST be built
    // by re-packing the OFFSET integer cell (cellKeyI(bix+dx, biy+dy)), never by
    // arithmetic on an already-packed key: adding dy to the packed value lets a
    // low-word borrow/carry bleed into the ix field exactly at the iy = 0 / iy = -1
    // seam (the v = 0 centreline of origin-centred geometry). That silently probed
    // the wrong diagonal neighbour cell and could fail to weld two coincident
    // contour endpoints -> an open chain dropped by stitchLoops -> a lost loop.
    auto cellKeyI = [](int64_t ix, int64_t iy) -> int64_t {
        // Unsigned shift/xor: a signed left-shift of a negative ix (half of any
        // origin-centred model has negative cell indices) is UB in C++17; the
        // uint64_t cast makes the bit-packing well-defined on every toolchain.
        return (int64_t)(((uint64_t)ix << 32) ^ ((uint64_t)iy & 0xffffffffULL));
    };

    std::vector<glm::vec2> pts;                 // canonical welded points
    std::unordered_map<int64_t, std::vector<int>> grid; // cell -> point indices
    auto weld = [&](const glm::vec2& p) -> int {
        const int64_t bix = (int64_t)std::llround(p.x * invTol);
        const int64_t biy = (int64_t)std::llround(p.y * invTol);
        // Search the 3x3 neighbourhood so points straddling a cell border still
        // merge (robust weld, TODO_03/04 convention). Each neighbour key is
        // re-packed from the integer cell so the iy-seam carry bug cannot occur.
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int64_t k = cellKeyI(bix + dx, biy + dy);
                auto it = grid.find(k);
                if (it == grid.end()) continue;
                for (int pi : it->second) {
                    glm::vec2 d = pts[pi] - p;
                    if (d.x * d.x + d.y * d.y <= tol * tol) return pi;
                }
            }
        int id = (int)pts.size();
        pts.push_back(p);
        grid[cellKeyI(bix, biy)].push_back(id);
        return id;
    };

    // Build the edge list on welded indices + per-vertex adjacency.
    struct Edge { int a, b; bool used = false; };
    std::vector<Edge> edges;
    edges.reserve(segs.size());
    std::unordered_map<int, std::vector<int>> adj; // vertex -> edge indices
    for (const auto& s : segs) {
        int a = weld(s.first), b = weld(s.second);
        if (a == b) continue;
        int ei = (int)edges.size();
        edges.push_back({ a, b, false });
        adj[a].push_back(ei);
        adj[b].push_back(ei);
    }

    auto otherEnd = [&](const Edge& e, int v) { return e.a == v ? e.b : e.a; };
    auto nextUnused = [&](int v) -> int {
        auto it = adj.find(v);
        if (it == adj.end()) return -1;
        for (int ei : it->second) if (!edges[ei].used) return ei;
        return -1;
    };

    for (size_t startEi = 0; startEi < edges.size(); ++startEi) {
        if (edges[startEi].used) continue;
        std::vector<glm::vec2> loop;
        Edge& e0 = edges[startEi];
        e0.used = true;
        int start = e0.a, cur = e0.b;
        loop.push_back(pts[start]);
        loop.push_back(pts[cur]);
        bool closed = false;
        while (true) {
            if (cur == start) { closed = true; break; }
            int ei = nextUnused(cur);
            if (ei < 0) break;            // open chain -> dead end
            edges[ei].used = true;
            cur = otherEnd(edges[ei], cur);
            if (cur == start) { closed = true; break; }
            loop.push_back(pts[cur]);
        }
        if (closed && loop.size() >= 3) {
            // The walk's last pushed point may duplicate `start`; trim it.
            if (loop.size() >= 2) {
                glm::vec2 d = loop.back() - loop.front();
                if (d.x * d.x + d.y * d.y <= tol * tol) loop.pop_back();
            }
            if (loop.size() >= 3) loops.push_back(std::move(loop));
            else ++discardedChains;
        } else {
            ++discardedChains;
        }
    }
}

// ---------------------------------------------------------------------------
// Section cleanup + orientation. Optional Clipper2 Union(EvenOdd)+Simplify pass
// (robustness for dirty STL), then deterministic even-odd containment so BOTH
// the triangle and B-rep paths emit the same convention: outer CCW, holes CW.
// ---------------------------------------------------------------------------
static Section cleanupSection(std::vector<std::vector<glm::vec2>> loops,
                              float wallWidthAbs, double Ldiag) {
#ifdef HAS_CLIPPER2
    if (!loops.empty() && Ldiag > 1e-9) {
        using namespace Clipper2Lib;
        const double scale = 1.0e6 / Ldiag;       // model units -> int64 grid
        Paths64 subject;
        subject.reserve(loops.size());
        for (const auto& lp : loops) {
            Path64 path;
            path.reserve(lp.size());
            for (const auto& q : lp)
                path.push_back(Point64((int64_t)std::llround((double)q.x * scale),
                                       (int64_t)std::llround((double)q.y * scale)));
            if (path.size() >= 3) subject.push_back(std::move(path));
        }
        if (!subject.empty()) {
            Paths64 unioned = Union(subject, FillRule::EvenOdd);
            double eps = std::max(1.0, 0.25 * (double)wallWidthAbs * scale);
            Paths64 simplified = SimplifyPaths(unioned, eps);
            const Paths64& src = simplified.empty() ? unioned : simplified;
            loops.clear();
            for (const auto& path : src) {
                std::vector<glm::vec2> lp;
                lp.reserve(path.size());
                for (const auto& pt : path)
                    lp.emplace_back((float)((double)pt.x / scale),
                                    (float)((double)pt.y / scale));
                if (lp.size() >= 3) loops.push_back(std::move(lp));
            }
        }
    }
#else
    (void)wallWidthAbs; (void)Ldiag;
#endif

    // Even-odd containment classification (path-independent). depth = number of
    // OTHER loops that contain this loop's centroid; even -> outer, odd -> hole.
    Section sec;
    sec.reserve(loops.size());
    // Representative point ON each loop (see loopProbePoint) -- NOT the centroid,
    // which misclassifies concentric holes into negative net area.
    std::vector<glm::vec2> probes(loops.size());
    for (size_t i = 0; i < loops.size(); ++i) probes[i] = loopProbePoint(loops[i]);

    // Pass 1: classify every loop by even-odd nesting depth while all loops are
    // still intact. This MUST complete before any loop is moved out below --
    // moving loops[i] empties it, and a later loop's containment test against an
    // emptied neighbour would wrongly read depth 0 and miss the hole.
    std::vector<bool> isHole(loops.size(), false);
    for (size_t i = 0; i < loops.size(); ++i) {
        int depth = 0;
        for (size_t j = 0; j < loops.size(); ++j) {
            if (i == j) continue;
            if (pointInLoop(probes[i], loops[j])) ++depth;
        }
        isHole[i] = (depth % 2) == 1;
    }

    // Pass 2: build polygons with the shared orientation convention. Now that
    // classification is frozen, moving each loop out is safe.
    for (size_t i = 0; i < loops.size(); ++i) {
        Polygon2D poly;
        poly.pts = std::move(loops[i]);
        poly.isHole = isHole[i];
        double a = signedArea(poly.pts);
        // Outer loops CCW (area > 0); holes CW (area < 0).
        bool wantCCW = !poly.isHole;
        if ((wantCCW && a < 0.0) || (!wantCCW && a > 0.0))
            std::reverse(poly.pts.begin(), poly.pts.end());
        sec.push_back(std::move(poly));
    }
    return sec;
}

SliceGrouping computeGrouping(float axisMin, float axisMax,
                              const FEAParams& p, float modelToMM) {
    SliceGrouping g;
    const float m2mm = std::max(1e-9f, modelToMM);
    const float extentModel = std::max(0.0f, axisMax - axisMin);
    const float extentMM    = extentModel * m2mm;                 // physical build-axis extent
    const float layerMM     = std::max(1e-6f, p.layerThickness);  // physical FDM layer height (mm)
    const float physThick   = std::max(1e-9f, layerMM / m2mm);    // model-space layer thickness
    // Group in PHYSICAL mm: nPhysical = number of real layer heights along the
    // build axis. ceil with a small tolerance so an exact integer ratio (e.g.
    // 4.0/0.2) does not spuriously round up from float error.
    int nPhysical = (int)std::ceil((double)extentMM / (double)layerMM - 1e-4);
    if (nPhysical < 1) nPhysical = 1;
    int maxSlabs = std::max(1, p.maxSlabs);
    int k = 1;
    if (nPhysical > maxSlabs) k = (nPhysical + maxSlabs - 1) / maxSlabs;
    int nSlabs = (nPhysical + k - 1) / k;

    g.nPhysical = nPhysical;
    g.layersPerSlab = k;
    g.nSlabs = nSlabs;
    g.physicalLayerThickness = physThick;          // model space (for slab boundaries)
    g.slabThickness = (float)k * physThick;        // model space
    g.physThickMM = layerMM;                       // physically-readable (mm)
    g.slabThickMM = (float)k * layerMM;

    g.slabBoundaries.clear();
    g.slabBoundaries.reserve(nSlabs + 1);
    g.slabBoundaries.push_back(axisMin);
    for (int i = 1; i < nSlabs; ++i)
        g.slabBoundaries.push_back(axisMin + (float)i * g.slabThickness);
    g.slabBoundaries.push_back(axisMax); // top slab may be thinner
    return g;
}

SliceResult computeSlices(const std::vector<Vertex>& surfVerts,
                          const std::vector<unsigned int>& surfIdx,
                          const glm::vec3& minB, const glm::vec3& maxB,
                          const FEAParams& p, float modelToMM,
                          const BRepHandle* brep,
                          SliceGrouping& grpOut,
                          std::vector<PlaneStats>& statsOut) {
    SliceResult out;
    statsOut.clear();
    const int axis = axisFromParams(p);
    out.buildAxis = axis;

    // Recompute the surface AABB so the slicer is correct even if called after a
    // solve (when the model's draw bounds may reflect the deformed/vol mesh).
    glm::vec3 mn = minB, mx = maxB;
    if (!surfVerts.empty()) {
        mn = mx = surfVerts[0].position;
        for (const auto& v : surfVerts) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
    }
    const double Ldiag = (double)glm::length(mx - mn);
    const float axisMin = axisCoord(mn, axis);
    const float axisMax = axisCoord(mx, axis);

    grpOut = computeGrouping(axisMin, axisMax, p, modelToMM);
    out.buildAxis = axis;

    const float m2mm = std::max(1e-9f, modelToMM);
    const double mm2 = (double)m2mm * (double)m2mm;   // model area -> mm^2
    const float weldTol = std::max(1e-9f, 1e-5f * (float)Ldiag);
    // wall width is authored in mm; convert to model space for the Clipper cleanup.
    const float wallWidthAbs = std::max(1e-6f, p.wallWidth / m2mm);

    if (grpOut.layersPerSlab > 1)
        std::cout << "[SLICE] grouping " << grpOut.layersPerSlab
                  << " physical layers per FE slab" << std::endl;
    std::cout << "[SLICE] nPhysical=" << grpOut.nPhysical
              << " k=" << grpOut.layersPerSlab
              << " slabs=" << grpOut.nSlabs
              << " layerHeight=" << grpOut.physThickMM << "mm"
              << " axis=" << axis << std::endl;

    const int nSlabs = grpOut.nSlabs;
    out.planeCoords.reserve(nSlabs);
    out.sections.reserve(nSlabs);

    for (int i = 0; i < nSlabs; ++i) {
        float lo = grpOut.slabBoundaries[i];
        float hi = grpOut.slabBoundaries[i + 1];
        float coord = 0.5f * (lo + hi);          // sample at the slab centre
        out.planeCoords.push_back(coord);

        Section sec;
        bool fromBRep = false;
        if (brep != nullptr) {
            Section bsec;
            if (sliceBRepPlane(*brep, axis, coord, Ldiag, bsec)) {
                sec = std::move(bsec);
                fromBRep = true;
            }
        }
        int discarded = 0;
        if (!fromBRep) {
            std::vector<std::pair<glm::vec2, glm::vec2>> segs;
            slicePlaneTriangles(surfVerts, surfIdx, axis, coord, segs);
            std::vector<std::vector<glm::vec2>> loops;
            stitchLoops(segs, weldTol, loops, discarded);
            sec = cleanupSection(std::move(loops), wallWidthAbs, Ldiag);
        }

        // Per-plane stats (model-space + physically-readable mm / mm^2).
        PlaneStats ps;
        ps.z   = coord;
        ps.zMM = coord * m2mm;
        ps.discardedChains = discarded;
        for (const auto& poly : sec) {
            double a = signedArea(poly.pts);
            if (poly.isHole) { ps.holes++; ps.holeArea += std::fabs(a); }
            else             { ps.outerArea += std::fabs(a); }
            ps.loops++;
        }
        ps.netArea      = ps.outerArea - ps.holeArea;
        ps.outerAreaMM2 = ps.outerArea * mm2;
        ps.holeAreaMM2  = ps.holeArea  * mm2;
        ps.netAreaMM2   = ps.netArea   * mm2;
        statsOut.push_back(ps);
        out.sections.push_back(std::move(sec));
    }
    return out;
}

std::vector<glm::vec3> sectionToSegments(const Section& section, int axis,
                                         float planeCoord) {
    std::vector<glm::vec3> segs;
    for (const auto& poly : section) {
        const size_t n = poly.pts.size();
        if (n < 2) continue;
        for (size_t i = 0; i < n; ++i) {
            const glm::vec2& a = poly.pts[i];
            const glm::vec2& b = poly.pts[(i + 1) % n];
            segs.push_back(from2D(a, axis, planeCoord));
            segs.push_back(from2D(b, axis, planeCoord));
        }
    }
    return segs;
}

} // namespace LayerSlicer
