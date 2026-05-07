#pragma once
#include <vector>
#include <glm/glm.hpp>

// Detects whether the point cloud pos lies on a sphere within a relative tolerance.
//   c = mean(p_i)                        (centroid estimate)
//   r = mean(||p_i − c||)               (average radius)
//   sphere iff ∀p_i: |  ||p_i − c|| − r | / r ≤ tolFraction
// Returns true and writes outCenter = c, outRadius = r on success.
// Requires ≥ 50 input points to avoid false positives on coarse meshes.
bool detectSphere(
    const std::vector<glm::vec3>& pos,
    glm::vec3& outCenter, float& outRadius,
    float tolFraction = 0.04f);

// Replaces an approximately-spherical mesh with a subdivided icosphere whose
// vertices are ray-projected onto srcMesh then relaxed with tangential
// Laplacian smoothing.
//
// Steps:
//   1. Compute area-weighted centroid c and mean radius r of the src mesh.
//   2. Choose subdivision level s* ≈ argmin_s |20·4^s − n_src_triangles|.
//   3. Generate icosphere at (c, r) and ray-project each vertex along its
//      radial direction r_hat onto src, via Möller–Trumbore intersection.
//   4. Run 6 iterations of tangential Laplacian smoothing (step size 0.45):
//        Δ = mean(neighbors) − p;   Δ -= (Δ·n̂)n̂   (remove normal component)
//        p' = closestPointOnMesh(p + Δ · 0.45, src)
//   5. Compact degenerate triangles; verify outward winding.
// Returns false if projection coverage < 80% or output is degenerate.
bool remeshProjectedIcosphere(
    const std::vector<glm::vec3>& srcPos,
    const std::vector<unsigned int>& srcIdx,
    std::vector<glm::vec3>& outPos,
    std::vector<unsigned int>& outIdx);

// Generates a geodesic sphere by icosahedron subdivision.
//
// Starts from the 12 vertices and 20 faces of a regular icosahedron
// (vertices at ±1, ±φ, 0 permutations, φ = (1+√5)/2 ≈ 1.618, normalised to
// unit sphere). Applies `subdivs` rounds of edge-midpoint refinement:
//   each triangle → 4 children by inserting normalised midpoints on all 3 edges.
//   Triangle count after s subdivisions: 20 · 4^s.
// All vertices are then scaled and translated to (center, radius).
// Outward-facing winding is verified and corrected if needed.
void generateIcosphere(
    glm::vec3 center, float radius, int subdivs,
    std::vector<glm::vec3>& outPos,
    std::vector<unsigned int>& outIdx);
