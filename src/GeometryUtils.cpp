#include "GeometryUtils.h"
#include <numeric>
#include <execution>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <cmath>
#include <omp.h>

namespace {
constexpr float kGeomEps = 1e-6f;

// Returns the point on triangle (a,b,c) closest to p, via Voronoi region
// decomposition with barycentric coordinates.
//
// Computes dot products d1..d6 of edge vectors ab, ac against vectors from
// each vertex to p. The barycentric weights (u, v, w = 1-u-v) minimising
// ||p − (a + u·ab + v·ac)||² satisfy KKT conditions for the 7 Voronoi
// regions (vertex a/b/c, edge ab/ac/bc, interior triangle). Returns the
// unclamped projection when p projects into the triangle interior.
glm::vec3 closestPointOnTriangle(
    const glm::vec3& p,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return a + ab * v + ac * w;
}

// Möller–Trumbore ray-triangle intersection test.
//
// Solves origin + t·dir = a + u·(b−a) + v·(c−a) via Cramer's rule:
//   e1 = b−a,  e2 = c−a,  p = dir×e2,  q = (origin−a)×e1
//   det = e1·p    (= 0 when ray is parallel to triangle plane)
//   u   = (origin−a)·p / det       must be in [0,1]
//   v   = dir·q / det              must be in [0,1], u+v ≤ 1
//   t   = e2·q / det               must be > ε (forward hit only)
// Returns false if parallel (|det| ≤ ε) or (u,v,t) fall outside the valid range.
bool rayTriangleIntersection(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    float& outT)
{
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(dir, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) <= kGeomEps) return false;
    const float invDet = 1.0f / det;
    const glm::vec3 tVec = origin - a;
    const float u = glm::dot(tVec, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(tVec, e1);
    const float v = glm::dot(dir, q) * invDet;
    if (v < 0.0f || (u + v) > 1.0f) return false;
    const float t = glm::dot(e2, q) * invDet;
    if (t <= kGeomEps) return false;
    outT = t;
    return true;
}

// Returns the closest point on a triangle mesh to query point p:
//   argmin_{q ∈ mesh} ||p − q||  =  argmin_f closestPointOnTriangle(p, a_f, b_f, c_f)
// Brute-force O(n_triangles) — suitable for small meshes or infrequent queries.
glm::vec3 closestPointOnMesh(
    const glm::vec3& p,
    const std::vector<glm::vec3>& srcPos,
    const std::vector<unsigned int>& srcIdx)
{
    float bestDist2 = std::numeric_limits<float>::max();
    glm::vec3 bestPoint = p;
    for (size_t i = 0; i + 2 < srcIdx.size(); i += 3) {
        const glm::vec3& a = srcPos[srcIdx[i]];
        const glm::vec3& b = srcPos[srcIdx[i + 1]];
        const glm::vec3& c = srcPos[srcIdx[i + 2]];
        const glm::vec3 candidate = closestPointOnTriangle(p, a, b, c);
        const float dist2 = glm::dot(candidate - p, candidate - p);
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestPoint = candidate;
        }
    }
    return bestPoint;
}

// Casts ray (center, dir) against all mesh triangles and returns the
// intersection point with the smallest positive parameter t (first forward hit):
//   outPoint = center + t_min · dir,   t_min = min{ t > ε : ray hits triangle f }
// Returns false if no triangle is intersected in the forward direction.
bool projectAlongRay(
    const glm::vec3& center,
    const glm::vec3& dir,
    const std::vector<glm::vec3>& srcPos,
    const std::vector<unsigned int>& srcIdx,
    glm::vec3& outPoint)
{
    float bestT = std::numeric_limits<float>::max();
    bool hit = false;
    for (size_t i = 0; i + 2 < srcIdx.size(); i += 3) {
        const glm::vec3& a = srcPos[srcIdx[i]];
        const glm::vec3& b = srcPos[srcIdx[i + 1]];
        const glm::vec3& c = srcPos[srcIdx[i + 2]];
        float t = 0.0f;
        if (!rayTriangleIntersection(center, dir, a, b, c, t)) continue;
        if (t < bestT) {
            bestT = t;
            outPoint = center + dir * t;
            hit = true;
        }
    }
    return hit;
}

// Area-weighted centroid of a triangle mesh:
//   c = Σ_f A_f · c_f / Σ_f A_f,   A_f = (1/2)||e1×e2||,   c_f = (a+b+c)/3
// Falls back to the arithmetic mean of all vertices if total area is degenerate.
glm::vec3 computeSurfaceCenter(
    const std::vector<glm::vec3>& srcPos,
    const std::vector<unsigned int>& srcIdx)
{
    glm::vec3 center(0.0f);
    float totalArea = 0.0f;
    for (size_t i = 0; i + 2 < srcIdx.size(); i += 3) {
        const glm::vec3& a = srcPos[srcIdx[i]];
        const glm::vec3& b = srcPos[srcIdx[i + 1]];
        const glm::vec3& c = srcPos[srcIdx[i + 2]];
        const float area = 0.5f * glm::length(glm::cross(b - a, c - a));
        if (area <= kGeomEps) continue;
        center += ((a + b + c) / 3.0f) * area;
        totalArea += area;
    }
    if (totalArea > kGeomEps) return center / totalArea;
    return std::reduce(std::execution::par, srcPos.begin(), srcPos.end(), glm::vec3(0.0f)) / (float)srcPos.size();
}

// Selects subdivision level s* ∈ {0..6} minimising |20·4^s − originalTriangleCount|.
// The baseline icosahedron has 20 triangles; each subdivision multiplies by 4.
int chooseIcosphereSubdivision(int originalTriangleCount)
{
    int bestSubdivs = 0;
    int triCount = 20;
    int bestDiff = std::abs(originalTriangleCount - triCount);
    for (int subdivs = 1; subdivs <= 6; ++subdivs) {
        triCount *= 4;
        const int diff = std::abs(originalTriangleCount - triCount);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestSubdivs = subdivs;
        }
    }
    return bestSubdivs;
}

// Builds the 1-ring vertex adjacency list from an indexed triangle mesh.
// For each triangle (a,b,c): adds b,c to neighbors[a]; a,c to neighbors[b];
// a,b to neighbors[c]. Each list is then sorted and deduplicated, giving
// the unique set of edge-adjacent vertices (the combinatorial 1-ring) for each vertex.
void buildNeighbors(
    size_t vertexCount,
    const std::vector<unsigned int>& idx,
    std::vector<std::vector<unsigned int>>& neighbors)
{
    neighbors.assign(vertexCount, {});
    // Serial edge insertion (map writes are not thread-safe without partitioning)
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        unsigned int a0 = idx[i], a1 = idx[i+1], a2 = idx[i+2];
        if (a0 < vertexCount && a1 < vertexCount && a0 != a1) {
            neighbors[a0].push_back(a1);
            neighbors[a1].push_back(a0);
        }
        if (a1 < vertexCount && a2 < vertexCount && a1 != a2) {
            neighbors[a1].push_back(a2);
            neighbors[a2].push_back(a1);
        }
        if (a2 < vertexCount && a0 < vertexCount && a2 != a0) {
            neighbors[a2].push_back(a0);
            neighbors[a0].push_back(a2);
        }
    }
    // Dedup each ring in parallel -- each ring[i] is independent
    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < (int)vertexCount; ++i) {
        auto& ring = neighbors[i];
        std::sort(ring.begin(), ring.end());
        ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
    }
}

// Computes area-weighted vertex normals:
//   n_v = normalize( Σ_{f∋v} n_f ),   n_f = (p_1−p_0)×(p_2−p_0)  (unnormalised, |n_f| = 2A_f)
// Accumulation uses thread-private buffers that are reduced after the parallel
// phase, avoiding atomic operations on the hot path. Result is normalised per vertex.
void computeVertexNormals(
    const std::vector<glm::vec3>& pos,
    const std::vector<unsigned int>& idx,
    std::vector<glm::vec3>& normals)
{
    const int nVerts = (int)pos.size();
    const int nFaces = (int)(idx.size() / 3);
    normals.assign(pos.size(), glm::vec3(0.0f));

    // Each thread accumulates into its own buffer, then we reduce.
    // This avoids any atomic/critical on the hot path.
    const int nThreads = omp_get_max_threads();
    std::vector<std::vector<glm::vec3>> threadNormals(
        nThreads, std::vector<glm::vec3>(nVerts, glm::vec3(0.0f)));

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto& localNrm = threadNormals[tid];
        #pragma omp for schedule(static)
        for (int i = 0; i < nFaces; ++i) {
            const unsigned int i0 = idx[i * 3];
            const unsigned int i1 = idx[i * 3 + 1];
            const unsigned int i2 = idx[i * 3 + 2];
            const glm::vec3 faceNormal = glm::cross(pos[i1] - pos[i0], pos[i2] - pos[i0]);
            localNrm[i0] += faceNormal;
            localNrm[i1] += faceNormal;
            localNrm[i2] += faceNormal;
        }
    }

    // Reduce thread-local buffers
    for (int t = 0; t < nThreads; ++t)
        for (int v = 0; v < nVerts; ++v)
            normals[v] += threadNormals[t][v];

    #pragma omp parallel for schedule(static)
    for (int v = 0; v < nVerts; ++v) {
        const float len = glm::length(normals[v]);
        if (len > kGeomEps) normals[v] /= len;
    }
}

// Removes degenerate triangles (||e1×e2|| ≤ ε) and reindexes the surviving
// faces into a compact vertex array. Each first-seen original index is assigned
// a new sequential index via a remap table; unreferenced vertices are dropped.
void compactMesh(
    std::vector<glm::vec3>& pos,
    std::vector<unsigned int>& idx)
{
    std::vector<unsigned int> remap(pos.size(), std::numeric_limits<unsigned int>::max());
    std::vector<glm::vec3> compactPos;
    std::vector<unsigned int> compactIdx;
    compactPos.reserve(pos.size());
    compactIdx.reserve(idx.size());
    auto remapIndex = [&](unsigned int oldIndex) -> unsigned int {
        unsigned int& mapped = remap[oldIndex];
        if (mapped == std::numeric_limits<unsigned int>::max()) {
            mapped = static_cast<unsigned int>(compactPos.size());
            compactPos.push_back(pos[oldIndex]);
        }
        return mapped;
    };
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const unsigned int i0 = idx[i];
        const unsigned int i1 = idx[i + 1];
        const unsigned int i2 = idx[i + 2];
        const float area2 = glm::length(glm::cross(pos[i1] - pos[i0], pos[i2] - pos[i0]));
        if (area2 <= kGeomEps) continue;
        compactIdx.push_back(remapIndex(i0));
        compactIdx.push_back(remapIndex(i1));
        compactIdx.push_back(remapIndex(i2));
    }
    pos.swap(compactPos);
    idx.swap(compactIdx);
}
}

bool detectSphere(
    const std::vector<glm::vec3>& pos,
    glm::vec3& outCenter, float& outRadius,
    float tolFraction)
{
    if ((int)pos.size() < 50) return false;
    
    // 1. Threaded centroid calculation using std::reduce
    glm::vec3 c = std::reduce(std::execution::par, pos.begin(), pos.end(), glm::vec3(0.0f)) / (float)pos.size();

    // 2. Threaded average radius calculation using std::transform_reduce
    float avgR = std::transform_reduce(std::execution::par, pos.begin(), pos.end(), 0.0f, std::plus<float>{}, 
        [&](const glm::vec3& p) { return glm::length(p - c); }) / (float)pos.size();

    if (avgR < 1e-6f) return false;

    // 3. Threaded tolerance check to verify if all vertices lie on the sphere
    bool isSphere = std::all_of(std::execution::par, pos.begin(), pos.end(), 
        [&](const glm::vec3& p) { return std::abs(glm::length(p - c) - avgR) / avgR <= tolFraction; });

    if (!isSphere) return false;

    outCenter = c; outRadius = avgR;
    return true;
}

bool remeshProjectedIcosphere(
    const std::vector<glm::vec3>& srcPos,
    const std::vector<unsigned int>& srcIdx,
    std::vector<glm::vec3>& outPos,
    std::vector<unsigned int>& outIdx)
{
    if (srcPos.size() < 20 || srcIdx.size() < 60) return false;

    const glm::vec3 center = computeSurfaceCenter(srcPos, srcIdx);
    const float avgRadius = std::transform_reduce(
        std::execution::par,
        srcPos.begin(), srcPos.end(),
        0.0f,
        std::plus<float>{},
        [&](const glm::vec3& p) { return glm::length(p - center); }) / (float)srcPos.size();
    if (avgRadius <= kGeomEps) return false;

    const int originalTriangleCount = (int)(srcIdx.size() / 3);
    const int subdivs = chooseIcosphereSubdivision(originalTriangleCount);
    generateIcosphere(center, avgRadius, subdivs, outPos, outIdx);

    int rayHitCount = 0;
    for (auto& p : outPos) {
        glm::vec3 dir = p - center;
        const float len = glm::length(dir);
        if (len <= kGeomEps) continue;
        dir /= len;
        glm::vec3 projectedPoint(0.0f);
        if (projectAlongRay(center, dir, srcPos, srcIdx, projectedPoint)) {
            p = projectedPoint;
            ++rayHitCount;
        }
    }

    if (rayHitCount * 5 < (int)outPos.size() * 4) {
        outPos.clear();
        outIdx.clear();
        return false;
    }

    std::vector<std::vector<unsigned int>> neighbors;
    buildNeighbors(outPos.size(), outIdx, neighbors);

    for (int iter = 0; iter < 6; ++iter) {
        std::vector<glm::vec3> normals;
        computeVertexNormals(outPos, outIdx, normals);
        std::vector<glm::vec3> nextPos = outPos;
        const int nVerts = (int)outPos.size();
        // Each vertex is independent: reads outPos/normals (immutable this iter),
        // writes nextPos[i] (no sharing) -> embarrassingly parallel.
        #pragma omp parallel for schedule(dynamic, 32)
        for (int i = 0; i < nVerts; ++i) {
            if (neighbors[i].empty()) continue;
            glm::vec3 avgNeighbor(0.0f);
            for (unsigned int nb : neighbors[i]) avgNeighbor += outPos[nb];
            avgNeighbor /= (float)neighbors[i].size();
            glm::vec3 delta = avgNeighbor - outPos[i];
            const glm::vec3& n = normals[i];
            const float nLen = glm::length(n);
            if (nLen > kGeomEps) delta -= n * glm::dot(delta, n);
            const glm::vec3 candidate = outPos[i] + delta * 0.45f;
            nextPos[i] = closestPointOnMesh(candidate, srcPos, srcIdx);
        }
        outPos.swap(nextPos);
    }

    compactMesh(outPos, outIdx);
    if (outPos.empty() || outIdx.empty()) return false;

    {
        const glm::vec3 fn = glm::cross(outPos[outIdx[1]] - outPos[outIdx[0]],
                                        outPos[outIdx[2]] - outPos[outIdx[0]]);
        const glm::vec3 fc = (outPos[outIdx[0]] + outPos[outIdx[1]] + outPos[outIdx[2]]) / 3.0f;
        if (glm::dot(fn, fc - center) < 0.0f) {
            for (size_t i = 0; i + 2 < outIdx.size(); i += 3) {
                std::swap(outIdx[i + 1], outIdx[i + 2]);
            }
        }
    }

    return true;
}

void generateIcosphere(
    glm::vec3 center, float radius, int subdivs,
    std::vector<glm::vec3>& outPos,
    std::vector<unsigned int>& outIdx)
{
    // 12 vertices of a regular icosahedron, normalised to the unit sphere
    outPos = {
        glm::normalize(glm::vec3(-1.f, 1.618f,  0.f)),
        glm::normalize(glm::vec3( 1.f, 1.618f,  0.f)),
        glm::normalize(glm::vec3(-1.f,-1.618f,  0.f)),
        glm::normalize(glm::vec3( 1.f,-1.618f,  0.f)),
        glm::normalize(glm::vec3( 0.f,-1.f, 1.618f)),
        glm::normalize(glm::vec3( 0.f, 1.f, 1.618f)),
        glm::normalize(glm::vec3( 0.f,-1.f,-1.618f)),
        glm::normalize(glm::vec3( 0.f, 1.f,-1.618f)),
        glm::normalize(glm::vec3( 1.618f, 0.f,-1.f)),
        glm::normalize(glm::vec3( 1.618f, 0.f, 1.f)),
        glm::normalize(glm::vec3(-1.618f, 0.f,-1.f)),
        glm::normalize(glm::vec3(-1.618f, 0.f, 1.f))
    };
    // 20 faces (outward-facing CCW winding)
    outIdx = {
        0,11,5,  0,5,1,   0,1,7,   0,7,10,  0,10,11,
        1,5,9,   5,11,4,  11,10,2, 10,7,6,  7,1,8,
        3,9,4,   3,4,2,   3,2,6,   3,6,8,   3,8,9,
        4,9,5,   2,4,11,  6,2,10,  8,6,7,   9,8,1
    };
    // Repeated edge-midpoint subdivision
    for (int d = 0; d < subdivs; ++d) {
        std::unordered_map<uint64_t, unsigned int> midCache;
        auto getMid = [&](unsigned int a, unsigned int b) -> unsigned int {
            uint64_t key = ((uint64_t)std::min(a, b) << 32) | (uint64_t)std::max(a, b);
            auto it = midCache.find(key);
            if (it != midCache.end()) return it->second;
            unsigned int idx = (unsigned int)outPos.size();
            outPos.push_back(glm::normalize(outPos[a] + outPos[b]));
            midCache[key] = idx;
            return idx;
        };
        std::vector<unsigned int> newIdx;
        newIdx.reserve(outIdx.size() * 4);
        for (size_t i = 0; i < outIdx.size(); i += 3) {
            unsigned int v0 = outIdx[i], v1 = outIdx[i+1], v2 = outIdx[i+2];
            unsigned int m01 = getMid(v0, v1);
            unsigned int m12 = getMid(v1, v2);
            unsigned int m20 = getMid(v2, v0);
            newIdx.insert(newIdx.end(), { v0,m01,m20,  m01,v1,m12,  m20,m12,v2,  m01,m12,m20 });
        }
        outIdx = std::move(newIdx);
    }
    // Verify outward-facing winding and flip if needed
    {
        glm::vec3 fn = glm::cross(outPos[outIdx[1]] - outPos[outIdx[0]],
                                  outPos[outIdx[2]] - outPos[outIdx[0]]);
        glm::vec3 fc = (outPos[outIdx[0]] + outPos[outIdx[1]] + outPos[outIdx[2]]) / 3.0f;
        if (glm::dot(fn, fc) < 0.0f)
            for (size_t i = 0; i < outIdx.size(); i += 3)
                std::swap(outIdx[i+1], outIdx[i+2]);
    }
    // Scale and translate to match the detected sphere
    for (auto& p : outPos) p = center + p * radius;
}
