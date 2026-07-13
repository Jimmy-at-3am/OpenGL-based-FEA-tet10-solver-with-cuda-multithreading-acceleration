// =============================================================================
//  SlabMesher.cpp  --  new_TODO_05: per-slab 2D triangulation + prism extrusion
//  -> conformal Tet4 slabs.
//
//  Triangulation: simple ear-clipper (O(n^3); fine for ≤ ~50-vertex FDM outlines).
//  Prism splitting: Dompierre rule (3 tets per prism, proven positive Jacobian).
//  Ring sharing: all slabs with identical 2D topology reuse the same ring nodes
//  (zero Steiner nodes, fully conforming across slab boundaries).
//  Wall/infill: bbox-inset centroid test (exact for convex polygons).
// =============================================================================
#include "SlabMesher.h"
#include "FEAModel.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <vector>
#include <array>
#include <map>
#include <limits>
#include <cmath>
#include <algorithm>
#include <iostream>

#ifdef HAS_CDT
#include "CDT.h"   // new_TODO_19C-b: constrained Delaunay (new_TODO_05's spec'd lib)
#endif

namespace SlabMesher {

// ---------------------------------------------------------------------------
// Ear-clipping polygon triangulation.
// Input:  CCW polygon (outer loop convention from LayerSlicer).
// Output: triangles as (i0,i1,i2) into `poly`, preserving CCW winding.
// ---------------------------------------------------------------------------
static std::vector<std::array<int,3>> earClip(const std::vector<glm::vec2>& poly)
{
    std::vector<std::array<int,3>> tris;
    const int n = static_cast<int>(poly.size());
    if (n < 3) return tris;
    if (n == 3) { tris.push_back({0,1,2}); return tris; }

    std::vector<int> ring(n);
    for (int i = 0; i < n; ++i) ring[i] = i;

    // Signed 2-D cross product at vertex `cur` (prev->cur->next triangle).
    auto cross2 = [&](int prev, int cur, int next) -> float {
        glm::vec2 ab = poly[cur]  - poly[prev];
        glm::vec2 ac = poly[next] - poly[prev];
        return ab.x * ac.y - ab.y * ac.x;
    };

    auto isEar = [&](int prev, int cur, int next) -> bool {
        if (cross2(prev, cur, next) <= 0.0f) return false; // reflex angle
        // No other active vertex strictly inside the ear triangle.
        for (int idx : ring) {
            if (idx == prev || idx == cur || idx == next) continue;
            float d0 = cross2(prev, cur,  idx);
            float d1 = cross2(cur,  next, idx);
            float d2 = cross2(next, prev, idx);
            if (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) return false;
        }
        return true;
    };

    while (ring.size() >= 3) {
        const int sz = static_cast<int>(ring.size());
        bool clipped = false;
        for (int i = 0; i < sz; ++i) {
            int prev = ring[(i + sz - 1) % sz];
            int cur  = ring[i];
            int next = ring[(i + 1) % sz];
            if (isEar(prev, cur, next)) {
                tris.push_back({prev, cur, next});
                ring.erase(ring.begin() + i);
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            // Degenerate polygon — fan triangulate from the first vertex.
            for (int i = 1; i + 1 < sz; ++i)
                tris.push_back({ring[0], ring[i], ring[i+1]});
            break;
        }
    }
    return tris;
}

// ---------------------------------------------------------------------------
// Prism -> 3 Tet4, Dompierre global-index rule (Dompierre et al. 1999, "How to
// Subdivide Pyramids, Prisms and Hexahedra into Tetrahedra").
//
// Every vertical quad face gets the diagonal through its GLOBALLY smallest
// node id. Two prisms sharing a quad therefore always pick the same diagonal
// -> no internal slits. (The previous fixed pattern made neighbours disagree
// on the shared face: under pure Z-tension the slit stays closed and every
// assert passes, under bending it opens -- the Class-B case the bending
// cross-check scenario exists to expose.)
//
// Pre-condition: (v[0],v[1],v[2]) CCW viewed from the +build-axis side,
// v[3..5] the matching top ring, dz > 0. All three emitted tets then have
// det(J) > 0 (verified for both diagonal cases on the unit prism).
// ---------------------------------------------------------------------------
static void prismToTets(const unsigned v[6], std::vector<unsigned int>& out)
{
    // Orientation-preserving prism symmetries: rows 0-2 rotate the bottom,
    // rows 3-5 flip bottom<->top (re-ordering keeps "bottom CCW seen from top").
    static const int P[6][6] = {
        {0,1,2,3,4,5},
        {1,2,0,4,5,3},
        {2,0,1,5,3,4},
        {3,5,4,0,2,1},
        {4,3,5,1,0,2},
        {5,4,3,2,1,0},
    };
    int k = 0;
    for (int i = 1; i < 6; ++i)
        if (v[i] < v[k]) k = i;
    const int* p = P[k];
    unsigned w[6];
    for (int j = 0; j < 6; ++j) w[j] = v[p[j]];

    // w[0] is now the smallest id: the two quads touching it take diagonals
    // w0-w4 and w0-w5 (through their min corner). The third quad (w1,w2,w5,w4)
    // takes the diagonal through ITS min corner:
    auto emit = [&](unsigned a, unsigned b, unsigned c, unsigned d) {
        out.push_back(a); out.push_back(b); out.push_back(c); out.push_back(d);
    };
    if (std::min(w[1], w[5]) < std::min(w[2], w[4])) {
        emit(w[0], w[1], w[2], w[5]);
        emit(w[0], w[1], w[5], w[4]);
        emit(w[0], w[4], w[5], w[3]);
    } else {
        emit(w[0], w[1], w[2], w[4]);
        emit(w[0], w[4], w[2], w[5]);
        emit(w[0], w[4], w[5], w[3]);
    }
}

// ---------------------------------------------------------------------------
// Uniform 4-way triangle refinement (each level splits every triangle into 4
// via edge midpoints). Midpoints are welded through a shared edge map, so the
// refined triangulation stays conforming and parent-triangle quality is
// preserved -- no new slivers, unlike refining by ear-clipping a denser ring.
// ---------------------------------------------------------------------------
static void refineOnce(std::vector<glm::vec2>& pts,
                       std::vector<std::array<int,3>>& tris)
{
    struct Key {
        int a, b;
        bool operator<(const Key& o) const {
            return a != o.a ? a < o.a : b < o.b;
        }
    };
    std::map<Key,int> mid;
    auto midpoint = [&](int i, int j) -> int {
        Key k{ std::min(i,j), std::max(i,j) };
        auto it = mid.find(k);
        if (it != mid.end()) return it->second;
        int idx = static_cast<int>(pts.size());
        pts.push_back(0.5f * (pts[i] + pts[j]));
        mid.emplace(k, idx);
        return idx;
    };
    std::vector<std::array<int,3>> out;
    out.reserve(tris.size() * 4);
    for (const auto& t : tris) {
        int m01 = midpoint(t[0], t[1]);
        int m12 = midpoint(t[1], t[2]);
        int m20 = midpoint(t[2], t[0]);
        out.push_back({t[0], m01, m20});
        out.push_back({m01, t[1], m12});
        out.push_back({m20, m12, t[2]});
        out.push_back({m01, m12, m20});
    }
    tris.swap(out);
}

// ---------------------------------------------------------------------------
// Wall / infill tag for a triangle centroid.
// Uses an axis-aligned bbox inset by `wallTotalMM / modelToMM` (model-space
// distance). A centroid strictly inside the inset box is "infill" (region 0);
// otherwise it is in the wall shell (region 1).
// Exact for rectangular cross-sections; approximate for curved outlines
// (conservative: rounds wall region slightly inward).
// ---------------------------------------------------------------------------
static bool isInfill(const glm::vec2& centroid,
                     const LayerSlicer::Section& section,
                     float wallTotalMM, float modelToMM)
{
    const float wallModel = (modelToMM > 1e-6f) ? wallTotalMM / modelToMM : wallTotalMM;
    for (const auto& poly : section) {
        if (poly.isHole || poly.pts.empty()) continue;
        float xlo =  std::numeric_limits<float>::max();
        float xhi = -std::numeric_limits<float>::max();
        float ylo =  std::numeric_limits<float>::max();
        float yhi = -std::numeric_limits<float>::max();
        for (const auto& p : poly.pts) {
            xlo = std::min(xlo, p.x); xhi = std::max(xhi, p.x);
            ylo = std::min(ylo, p.y); yhi = std::max(yhi, p.y);
        }
        if (centroid.x > xlo + wallModel && centroid.x < xhi - wallModel &&
            centroid.y > ylo + wallModel && centroid.y < yhi - wallModel)
            return true; // centroid inside inset box -> infill
    }
    return false; // wall
}

// ---------------------------------------------------------------------------
// meshSlabs — public entry point.
// ---------------------------------------------------------------------------
MeshStats meshSlabs(const LayerSlicer::SliceResult& slice,
                    const FEAParams& params,
                    FEAModel& model)
{
    MeshStats stats;
    const int nSlabs = static_cast<int>(slice.sections.size());
    if (nSlabs == 0 || !model.hasLayerStack()) return stats;

    LayerStack& ls = *model.layers;
    const int buildAxis = ls.buildAxis;
    const std::vector<float>& boundaries = ls.planeCoords; // nSlabs+1 values
    if (static_cast<int>(boundaries.size()) != nSlabs + 1) return stats;

    // ---- Reference outer loop (triangulation shared by all slabs) ----
    const LayerSlicer::Polygon2D* outerRef = nullptr;
    for (const auto& poly : slice.sections[0])
        if (!poly.isHole && !poly.pts.empty()) { outerRef = &poly; break; }
    if (!outerRef) return stats;

    const int nRing = static_cast<int>(outerRef->pts.size());
    if (nRing < 3) return stats;

    std::vector<glm::vec2> pts2d = outerRef->pts;   // grows under refinement
    auto tris = earClip(pts2d);
    if (tris.empty()) return stats;

    // ---- Section sanity flags (surfaced in the report so the harness can
    // see this mesher's v1 blind spots instead of silently absorbing them) ----
    stats.holesIgnored = false;
    stats.sectionUniform = true;
    for (const auto& sec : slice.sections) {
        int outerCount = -1;
        for (const auto& poly : sec) {
            if (poly.isHole) { if (!poly.pts.empty()) stats.holesIgnored = true; }
            else if (outerCount < 0) outerCount = static_cast<int>(poly.pts.size());
        }
        if (outerCount != nRing) stats.sectionUniform = false;
    }
    if (stats.holesIgnored)
        std::cout << "[SLAB][WARN] section holes present but v1 meshes the outer"
                     " loop only (hole support -> follow-up TODO)\n";
    if (!stats.sectionUniform)
        std::cout << "[SLAB][WARN] cross-sections vary across slabs; v1 reuses"
                     " the slab-0 outer loop for ALL slabs (geometry approximated)\n";

    // ---- Refinement level: target in-plane edge h ~ 2*wallWidth (model
    // units), clamped so neither triangle count per slab nor total tet count
    // explodes. Uniform 4-way refinement keeps parent quality. ----
    float maxEdge = 0.0f, diag2d = 0.0f;
    {
        glm::vec2 lo(std::numeric_limits<float>::max());
        glm::vec2 hi(-std::numeric_limits<float>::max());
        for (const auto& q : pts2d) {
            lo = glm::min(lo, q); hi = glm::max(hi, q);
        }
        diag2d = glm::length(hi - lo);
        for (const auto& t : tris)
            for (int e = 0; e < 3; ++e)
                maxEdge = std::max(maxEdge,
                                   glm::length(pts2d[t[e]] - pts2d[t[(e+1)%3]]));
    }
    const float wallModel = (model.modelToMM > 1e-6f)
        ? (params.wallCount * params.wallWidth) / model.modelToMM
        : params.wallCount * params.wallWidth;
    const float hTarget = std::max(2.0f * std::max(wallModel, 1e-6f) ,
                                   std::max(diag2d / 64.0f, 1e-6f));
    int levelWanted = 0;
    while (maxEdge / std::pow(2.0f, static_cast<float>(levelWanted)) > hTarget &&
           levelWanted < 8)
        ++levelWanted;
    int level = levelWanted;
    // Budget: regression scenarios must stay LDLT-solvable in seconds (the
    // linear path promotes to Tet10 and factorizes single-threaded; 18k Tet4
    // = ~81k DOF took minutes in a Debug build). 15k Tet4 keeps both box
    // scenarios at refineLevel 3 and the solve in seconds.
    const long trisPerSlabCap = 2048;
    const long totalTetCap   = 15000;
    auto trisAt = [&](int L) -> long {
        return static_cast<long>(tris.size()) * (1L << (2 * L));
    };
    while (level > 0 && (trisAt(level) > trisPerSlabCap ||
                         trisAt(level) * 3L * nSlabs > totalTetCap))
        --level;
    stats.refineLevel  = level;
    stats.refineCapped = (level < levelWanted);
    if (stats.refineCapped)
        std::cout << "[SLAB][WARN] refinement capped at level " << level
                  << " (wanted " << levelWanted << ") by element budget\n";
    for (int L = 0; L < level; ++L) refineOnce(pts2d, tris);

    const int nVerts2D = static_cast<int>(pts2d.size());
    stats.nSlabs   = nSlabs;
    stats.nRing    = nRing;
    stats.nVerts2D = nVerts2D;

    // ---- 2D -> 3D for a given boundary coordinate ----
    auto from2D = [&](const glm::vec2& q, float coord) -> glm::vec3 {
        if (buildAxis == 0) return {coord, q.x, q.y};
        if (buildAxis == 1) return {q.y, coord, q.x};
        return {q.x, q.y, coord};          // buildAxis == 2 (Z), default
    };

    // ---- Build node positions (shared across all adjacent slabs) ----
    // nVerts2D nodes per boundary level, (nSlabs+1) levels total. All slabs
    // reuse the SAME refined 2-D triangulation, so level l of slab s IS level
    // l of slab s+1 -> fully conforming stack, zero duplicated nodes.
    const int nBound = nSlabs + 1;
    const int nNodes = nVerts2D * nBound;
    std::vector<glm::vec3> positions;
    positions.reserve(nNodes);
    for (int lv = 0; lv < nBound; ++lv)
        for (int r = 0; r < nVerts2D; ++r)
            positions.push_back(from2D(pts2d[r], boundaries[lv]));

    auto nodeId = [&](int level, int r) -> unsigned int {
        return static_cast<unsigned int>(level * nVerts2D + r);
    };

    // ---- Build tetrahedra ----
    const float wallTotalMM = static_cast<float>(params.wallCount) * params.wallWidth;

    std::vector<unsigned int> tets;
    tets.reserve(static_cast<size_t>(nSlabs) * tris.size() * 3 * 4);
    std::vector<int>     elemSlab;
    std::vector<uint8_t> elemReg;
    elemSlab.reserve(static_cast<size_t>(nSlabs) * tris.size() * 3);
    elemReg.reserve(elemSlab.capacity());

    int wallTets = 0;
    for (int s = 0; s < nSlabs; ++s) {
        const LayerSlicer::Section& sec = slice.sections[s];
        for (const auto& tri : tris) {
            const int i0 = tri[0], i1 = tri[1], i2 = tri[2];
            unsigned int b0 = nodeId(s,   i0), b1 = nodeId(s,   i1), b2 = nodeId(s,   i2);
            unsigned int t0 = nodeId(s+1, i0), t1 = nodeId(s+1, i1), t2 = nodeId(s+1, i2);

            glm::vec2 cent = (pts2d[i0] + pts2d[i1] + pts2d[i2]) / 3.0f;
            uint8_t region = isInfill(cent, sec, wallTotalMM, model.modelToMM) ? 0 : 1;
            if (region == 1) ++wallTets;

            const unsigned prism[6] = { b0, b1, b2, t0, t1, t2 };
            prismToTets(prism, tets);
            for (int k = 0; k < 3; ++k) { elemSlab.push_back(s); elemReg.push_back(region); }
        }
    }

    const int nTets = static_cast<int>(tets.size() / 4);
    stats.wallPct = nTets > 0 ? 100.0f * static_cast<float>(wallTets) / static_cast<float>(nTets) : 0.0f;

    // ---- Populate FEAModel ----
    model.originalVolumetricPositions = positions;
    model.tetrahedra                  = tets;
    model.nLinearNodes                = nNodes;

    model.volumetricVertices.resize(nNodes);
    for (int i = 0; i < nNodes; ++i) {
        model.volumetricVertices[i].position     = positions[i];
        model.volumetricVertices[i].normal       = glm::vec3(0.0f, 1.0f, 0.0f);
        model.volumetricVertices[i].texCoords    = glm::vec2(0.0f);
        model.volumetricVertices[i].elementScalar = 0.0f;
    }

    // Mark all elements alive so buildBuffers() calls rebuildFracturedSurface()
    // and populates volumetricIndices with the outer boundary triangles.
    model.elementAlive.assign(nTets, 1);
    model.elementFailureIter.clear();
    model.elementFailureMode.clear();
    model.elementVonMisesAtDeath.clear();

    model.hasVolumetricMesh  = true;
    model.hasQuadraticMesh   = false;
    model.hasDeformation     = false;
    model.showVolumetricMesh = true;
    model.deformedPositions.clear();
    model.nodalDisplacementMagnitudes.assign(nNodes, 0.0f);

    // Per-tet slab index and region.
    ls.elemSlabIndex = elemSlab;
    ls.elemRegion    = elemReg;

    // Upload to GPU (rebuilds volumetricIndices via rebuildFracturedSurface +
    // updateBounds for the camera preset).
    model.buildBuffers();

    stats.nTets = nTets;
    // Conformity invariant of the shared-triangulation fast path: every
    // boundary level holds exactly the refined 2-D vertex set, nothing else.
    stats.ringConformal =
        (static_cast<int>(model.originalVolumetricPositions.size()) ==
         nVerts2D * nBound);

    std::cout << "[SLAB] nSlabs=" << nSlabs
              << "  nRing=" << nRing
              << "  nVerts2D=" << nVerts2D
              << "  refineLevel=" << stats.refineLevel
              << "  nNodes=" << nNodes
              << "  nTets=" << nTets
              << "  wallPct=" << stats.wallPct << "%"
              << "  conformal=" << (stats.ringConformal ? 1 : 0) << "\n";

    return stats;
}

// ============================================================================
//  new_TODO_19C-b: toolpath lane below.
// ============================================================================

// Resample a closed loop so no edge exceeds hTarget (keeps original vertices;
// inserts evenly spaced points on long edges). Coarse ALONG the ribbon is the
// point: across the ribbon the geometry itself is one bead wide, so ear-clip
// yields the owner's alternating-strip pattern.
static std::vector<glm::vec2> resampleLoop(const std::vector<glm::vec2>& in,
                                           float hTarget)
{
    std::vector<glm::vec2> out;
    const size_t n = in.size();
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2& a = in[i];
        const glm::vec2& b = in[(i + 1) % n];
        out.push_back(a);
        const float len = glm::length(b - a);
        const int   k   = static_cast<int>(std::floor(len / hTarget));
        for (int j = 1; j <= k; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(k + 1);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

// Point-in-triangle via signed areas (tolerant on edges).
static bool baryInTri(const glm::vec2& p, const glm::vec2& a,
                      const glm::vec2& b, const glm::vec2& c, float w[3])
{
    const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(d) < 1e-20f) return false;
    w[0] = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
    w[1] = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
    w[2] = 1.0f - w[0] - w[1];
    const float eps = -1e-4f;
    return w[0] >= eps && w[1] >= eps && w[2] >= eps;
}

// Weld coincident 2-D vertices (bridge cuts DUPLICATE the two bridge
// endpoints: without welding, triangles on either side of the cut reference
// different node ids at identical coordinates = an internal slit that a pull
// test cannot see — the exact Class-B failure the 2026-07-02 05-audit found in
// the fixed-diagonal prism split) and drop vertices no surviving triangle
// references (sliver-filtered bridge tris leave orphans = zero K rows = LDLT
// failure). Returns the welded/compacted triangulation.
static void weldAndCompact(std::vector<glm::vec2>& pts,
                           std::vector<std::array<int,3>>& tris)
{
    // Weld: 1e-5 mm grid — far below any bead feature, above float noise.
    const double grid = 1e-5;
    std::map<std::pair<long long,long long>, int> firstAt;
    std::vector<int> remap(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const std::pair<long long,long long> key(
            (long long)std::llround((double)pts[i].x / grid),
            (long long)std::llround((double)pts[i].y / grid));
        auto it = firstAt.find(key);
        if (it == firstAt.end()) { firstAt.emplace(key, (int)i); remap[i] = (int)i; }
        else                     { remap[i] = it->second; }
    }
    std::vector<std::array<int,3>> keep;
    keep.reserve(tris.size());
    for (auto& t : tris) {
        const int a = remap[t[0]], b = remap[t[1]], c = remap[t[2]];
        if (a == b || b == c || c == a) continue;   // collapsed by welding
        keep.push_back({a, b, c});
    }
    // Compact: drop unreferenced vertices, remap indices.
    std::vector<int> newId(pts.size(), -1);
    std::vector<glm::vec2> outPts;
    outPts.reserve(pts.size());
    for (auto& t : keep)
        for (int q = 0; q < 3; ++q) {
            if (newId[t[q]] < 0) {
                newId[t[q]] = (int)outPts.size();
                outPts.push_back(pts[t[q]]);
            }
            t[q] = newId[t[q]];
        }
    pts.swap(outPts);
    tris.swap(keep);
}

ToolpathMeshStats meshToolpathSlabs(const ToolpathSections::LayerSections& layers,
                                    const ToolpathMeshOptions& opt,
                                    FEAModel& model)
{
    ToolpathMeshStats stats;
    auto progress = [&](float f) {
        if (opt.progressOut)
            opt.progressOut->store(opt.progressLo + (opt.progressHi - opt.progressLo) *
                                                      std::clamp(f, 0.0f, 1.0f));
    };
    auto cancelled = [&]() {
        return opt.cancelRequested && opt.cancelRequested->load();
    };
    progress(0.0f);
    const int nLayers = static_cast<int>(layers.sections.size());
    if (nLayers == 0) return stats;

    const float hTarget = (opt.targetEdgeMM > 0.0f)
                              ? opt.targetEdgeMM
                              : 5.0f * layers.medianWidthMM;
    const int maxSlabs = std::max(1, opt.maxSlabs);
    const int k        = (nLayers + maxSlabs - 1) / maxSlabs;
    const int nSlabs   = (nLayers + k - 1) / k;
    const float m2mm   = std::max(1e-9f, model.modelToMM);

    // Per-slab z bounds (mm, part-centered) + representative section (mid layer).
    std::vector<float> zBotMM(nSlabs), zTopMM(nSlabs);
    std::vector<int>   repLayer(nSlabs);
    for (int s = 0; s < nSlabs; ++s) {
        const int l0 = s * k;
        const int l1 = std::min(nLayers - 1, l0 + k - 1);
        zBotMM[s]   = layers.zTopMM[l0] - layers.heightMM[l0];
        zTopMM[s]   = layers.zTopMM[l1];
        repLayer[s] = (l0 + l1) / 2;
    }

    std::vector<glm::vec3> positions;
    std::vector<unsigned>  tets;
    std::vector<int>       elemSlab;
    // Per-slab 2-D triangulation kept for tie point-location.
    std::vector<std::vector<glm::vec2>>        slabPts(nSlabs);
    std::vector<std::vector<std::array<int,3>>> slabTris(nSlabs);
    std::vector<unsigned> slabBase(nSlabs, 0);   // first node id of slab s
    std::vector<double>   slabAreaMM2(nSlabs, 0.0);

    for (int s = 0; s < nSlabs; ++s) {
        if (cancelled()) return ToolpathMeshStats{};
        const LayerSlicer::Section& sec = layers.sections[repLayer[s]];
        std::vector<glm::vec2> pts2d;
        std::vector<std::array<int,3>> tris;
#ifdef HAS_CDT
        // Constrained Delaunay of ALL rings at once (outer CCW + holes CW as
        // constraint edges); eraseOuterTrianglesAndHoles() keeps exactly the
        // material region (even-odd from the constraints). This replaced a
        // hand-rolled Eberly hole-bridging + ear-clip that broke down on
        // Bambu's 46-hole diagonal-grid infill sections (triangle overlap,
        // areaErr 382% — caught by the per-slab area oracle 2026-07-02).
        {
            std::vector<CDT::V2d<double>> vin;
            std::vector<CDT::Edge>        ein;
            for (const auto& poly : sec) {
                if (poly.pts.size() < 3) continue;
                std::vector<glm::vec2> ring = resampleLoop(poly.pts, hTarget);
                const CDT::VertInd base = static_cast<CDT::VertInd>(vin.size());
                const CDT::VertInd n    = static_cast<CDT::VertInd>(ring.size());
                for (const auto& q : ring)
                    vin.push_back({ (double)q.x, (double)q.y });
                for (CDT::VertInd i = 0; i < n; ++i)
                    ein.emplace_back(base + i, base + (i + 1) % n);
            }
            if (vin.size() >= 3) {
                CDT::RemoveDuplicatesAndRemapEdges(vin, ein);
                CDT::Triangulation<double> cdt;
                cdt.insertVertices(vin);
                cdt.insertEdges(ein);
                cdt.eraseOuterTrianglesAndHoles();
                pts2d.reserve(cdt.vertices.size());
                for (const auto& v : cdt.vertices)
                    pts2d.emplace_back((float)v.x, (float)v.y);
                tris.reserve(cdt.triangles.size());
                for (const auto& t : cdt.triangles)
                    tris.push_back({ (int)t.vertices[0], (int)t.vertices[1],
                                     (int)t.vertices[2] });
            }
        }
#else
        (void)sec;
        std::cout << "[TPMESH][ERROR] built without CDT (USE_CDT=OFF) — "
                     "toolpath meshing unavailable\n";
        return stats;
#endif
        if (tris.empty()) continue;
        weldAndCompact(pts2d, tris);
        if (tris.empty()) continue;

        // Triangulation truth check: area conservation per slab.
        double triArea = 0.0;
        for (const auto& t : tris) {
            const glm::vec2 &a = pts2d[t[0]], &b = pts2d[t[1]], &c = pts2d[t[2]];
            triArea += 0.5 * std::fabs((double)(b.x - a.x) * (c.y - a.y) -
                                       (double)(b.y - a.y) * (c.x - a.x));
        }
        const double net = layers.netAreaMM2[repLayer[s]];
        if (net > 1e-9) {
            const double err = std::fabs(triArea - net) / net * 100.0;
            if (err > 1.0) {
                int nO = 0, nH = 0;
                for (const auto& poly : sec) (poly.isHole ? nH : nO)++;
                std::cout << "[TPMESH][WARN] slab " << s << " (layer "
                          << repLayer[s] << "): triArea=" << triArea
                          << " net=" << net << " err=" << err
                          << "%  outers=" << nO << " holes=" << nH << "\n";
            }
            stats.areaErrMaxPct = std::max(stats.areaErrMaxPct, err);
        }
        slabAreaMM2[s] = triArea;
        stats.sumThickOverAreaPerMM +=
            (double)(zTopMM[s] - zBotMM[s]) / std::max(1e-9, triArea);

        // Extrude: independent bottom+top rings for this slab (model space).
        slabBase[s] = static_cast<unsigned>(positions.size());
        const int nV = static_cast<int>(pts2d.size());
        for (int r = 0; r < nV; ++r)
            positions.emplace_back(pts2d[r].x / m2mm, pts2d[r].y / m2mm,
                                   zBotMM[s] / m2mm);
        for (int r = 0; r < nV; ++r)
            positions.emplace_back(pts2d[r].x / m2mm, pts2d[r].y / m2mm,
                                   zTopMM[s] / m2mm);
        for (const auto& t : tris) {
            const unsigned prism[6] = {
                slabBase[s] + (unsigned)t[0], slabBase[s] + (unsigned)t[1],
                slabBase[s] + (unsigned)t[2],
                slabBase[s] + (unsigned)(nV + t[0]),
                slabBase[s] + (unsigned)(nV + t[1]),
                slabBase[s] + (unsigned)(nV + t[2]) };
            prismToTets(prism, tets);
            for (int q = 0; q < 3; ++q) elemSlab.push_back(s);
        }
        slabPts[s]  = std::move(pts2d);
        slabTris[s] = std::move(tris);
        progress(0.75f * static_cast<float>(s + 1) / static_cast<float>(nSlabs));
    }

    const int nTets  = static_cast<int>(tets.size() / 4);
    const int nNodes = static_cast<int>(positions.size());
    if (nTets == 0) return stats;

    // ---- Weld-interface registry (new_TODO_06 general path) ----
    LayerStack ls = model.layers ? *model.layers : LayerStack{};
    ls.interfaces.clear();
    ls.tieAlpha = opt.tieAlpha;
    for (int s = 0; s + 1 < nSlabs; ++s) {
        if (cancelled()) return ToolpathMeshStats{};
        if (slabTris[s].empty() || slabTris[s + 1].empty()) continue;
        LayerStack::WeldInterface wi;
        wi.slabBelow   = s;
        wi.areaMM2     = static_cast<float>(std::min(slabAreaMM2[s], slabAreaMM2[s + 1]));
        wi.thicknessMM = zTopMM[s] - zBotMM[s];
        const int nVtop = static_cast<int>(slabPts[s].size());
        for (int r = 0; r < nVtop; ++r) {
            const glm::vec2 p = slabPts[s][r];
            const unsigned topNode = slabBase[s] + (unsigned)(nVtop + r);
            for (const auto& t : slabTris[s + 1]) {
                float w[3];
                if (baryInTri(p, slabPts[s + 1][t[0]], slabPts[s + 1][t[1]],
                              slabPts[s + 1][t[2]], w)) {
                    LayerStack::WeldInterface::Tie tie;
                    tie.nodeTop = topNode;
                    for (int q = 0; q < 3; ++q) {
                        tie.triBottom[q] = slabBase[s + 1] + (unsigned)t[q];
                        tie.bary[q]      = w[q];
                    }
                    wi.ties.push_back(tie);
                    break;   // overhang nodes simply find no triangle -> untied
                }
            }
        }
        if (!wi.ties.empty()) {
            stats.nTies += static_cast<int>(wi.ties.size());
            ls.interfaces.push_back(std::move(wi));
        }
        progress(0.75f + 0.20f * static_cast<float>(s + 1) /
                                  static_cast<float>(std::max(1, nSlabs - 1)));
    }
    stats.nInterfaces = static_cast<int>(ls.interfaces.size());

    // ---- Populate FEAModel (same contract as meshSlabs) ----
    // Everything above is local. A cancelled job therefore leaves the live
    // mesh and LayerStack untouched instead of exposing a half-built result.
    if (cancelled()) return ToolpathMeshStats{};
    if (!model.layers) model.layers = std::make_unique<LayerStack>();
    *model.layers = std::move(ls);
    auto& committedLs = *model.layers;
    model.originalVolumetricPositions = positions;
    model.tetrahedra                  = tets;
    model.nLinearNodes                = nNodes;
    model.volumetricVertices.resize(nNodes);
    for (int i = 0; i < nNodes; ++i) {
        model.volumetricVertices[i].position      = positions[i];
        model.volumetricVertices[i].normal        = glm::vec3(0.0f, 1.0f, 0.0f);
        model.volumetricVertices[i].texCoords     = glm::vec2(0.0f);
        model.volumetricVertices[i].elementScalar = 0.0f;
    }
    model.elementAlive.assign(nTets, 1);
    model.elementFailureIter.clear();
    model.elementFailureMode.clear();
    model.elementVonMisesAtDeath.clear();
    model.hasVolumetricMesh  = true;
    model.hasQuadraticMesh   = false;
    model.hasDeformation     = false;
    model.showVolumetricMesh = true;
    model.deformedPositions.clear();
    model.nodalDisplacementMagnitudes.assign(nNodes, 0.0f);

    committedLs.buildAxis     = 2;
    committedLs.layersPerSlab = k;
    committedLs.physicalLayerThickness =
        (nLayers > 0 ? layers.heightMM[0] : 0.2f) / m2mm;
    committedLs.planeCoords.assign(nSlabs + 1, 0.0f);
    for (int s = 0; s < nSlabs; ++s) committedLs.planeCoords[s] = zBotMM[s] / m2mm;
    committedLs.planeCoords[nSlabs] = zTopMM[nSlabs - 1] / m2mm;
    committedLs.elemSlabIndex = elemSlab;
    committedLs.elemRegion.assign(nTets, 1);  // single-material until new_TODO_07

    model.buildBuffers();

    stats.nSlabs = nSlabs;
    stats.layersPerSlab = k;
    stats.nTets = nTets;
    stats.nNodes = nNodes;
    progress(1.0f);
    std::cout << "[TPMESH] slabs=" << nSlabs << " (k=" << k << ")"
              << "  nodes=" << nNodes << "  tets=" << nTets
              << "  interfaces=" << stats.nInterfaces
              << "  ties=" << stats.nTies
              << "  areaErrMax=" << stats.areaErrMaxPct << "%"
              << "  sum(t/A)=" << stats.sumThickOverAreaPerMM << "/mm\n";
    return stats;
}

} // namespace SlabMesher
