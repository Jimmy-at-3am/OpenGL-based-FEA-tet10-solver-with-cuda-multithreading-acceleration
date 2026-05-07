#include "FEAModel.h"
#include <meshoptimizer.h>
#include <glad/glad.h>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <tuple>
#include <algorithm>
#include <limits>
#include "GeometryUtils.h"
#include "tetgen.h"
#include "Globals.h"

namespace fs = std::filesystem;

FEAModel::FEAModel() {
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
    generateCube();
}

void FEAModel::buildBuffers() {
    // Apply FEA deformation if enabled
    if (showVolumetricMesh && hasVolumetricMesh && hasDeformation && !deformedPositions.empty()) {
        size_t positionCount = std::min(volumetricVertices.size(), std::min(deformedPositions.size(), originalVolumetricPositions.size()));
        for (size_t i = 0; i < positionCount; ++i) {
            if (showDeformedMesh)
                volumetricVertices[i].position = deformedPositions[i];
            else
                volumetricVertices[i].position = originalVolumetricPositions[i];
        }
    }

    updateScalarFieldData();

    const std::vector<Vertex>& activeVertices = (showVolumetricMesh && hasVolumetricMesh) ? volumetricVertices : surfaceVertices;
    const std::vector<unsigned int>& activeIndices = (showVolumetricMesh && hasVolumetricMesh) ? volumetricIndices : surfaceIndices;
    
    // Update the drawing reference for later
    vertices = activeVertices;
    indices = activeIndices;
    updateBounds();

    if (vertices.empty() || indices.empty()) return;
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
    needsUpdate = false;
}

void FEAModel::generateCube() {
    vertices.clear(); indices.clear();
    int sub = static_cast<int>(std::round(params.subdivisions));
    generate_face(glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), sub);
    generate_face(glm::vec3(0, 0, -1), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), sub);
    generate_face(glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), sub);
    generate_face(glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), sub);
    generate_face(glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), sub);
    generate_face(glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), sub);
    
    surfaceVertices = vertices;
    surfaceIndices = indices;
    volumetricVertices.clear();
    volumetricIndices.clear();
    tetrahedra.clear();
    originalVolumetricPositions.clear();
    deformedPositions.clear();
    appliedForces.clear();
    nodalDisplacementMagnitudes.clear();
    nodalForceMagnitudes.clear();
    hasDeformation = false;
    hasVolumetricMesh = false;
    showVolumetricMesh = false;
    showAppliedForceField = false;
    displacementMin = 0.0f;
    displacementMax = 0.0f;
    appliedForceMin = 0.0f;
    appliedForceMax = 0.0f;
    totalAppliedForce = 0.0f;
    appliedForcePerNode = 0.0f;
    loadedFileName = "";

    buildBuffers();
}

void FEAModel::generate_face(glm::vec3 normal, glm::vec3 u, glm::vec3 v, int sub) {
    int start_idx = vertices.size();
    glm::vec3 scale(params.sizeX, params.sizeY, params.sizeZ);
    for (int i = 0; i <= sub; ++i) {
        for (int j = 0; j <= sub; ++j) {
            float u_coord = ((float)j / sub) - 0.5f; float v_coord = ((float)i / sub) - 0.5f;
            glm::vec3 pos = (normal * 0.5f + u * u_coord + v * v_coord) * scale;
            vertices.push_back({ pos, normal, glm::vec2(0.0f) });
        }
    }
    for (int i = 0; i < sub; ++i) {
        for (int j = 0; j < sub; ++j) {
            int tl = start_idx + i * (sub + 1) + j;
            int tr = tl + 1;
            int bl = start_idx + (i + 1) * (sub + 1) + j;
            int br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }
}

bool FEAModel::loadSTL(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) return false;

    std::streamsize size = file.tellg(); file.seekg(0, std::ios::beg);
    if (size < 84) return false;

    char header[80]; file.read(header, 80);
    uint32_t numTriangles; file.read(reinterpret_cast<char*>(&numTriangles), sizeof(numTriangles));

    if (size < 84 + numTriangles * 50) {
        std::cout << "Skipped ASCII STL or corrupted file: " << filepath << std::endl;
        return false;
    }

    struct RawTri { glm::vec3 n, v0, v1, v2; };
    std::vector<RawTri> rawTris;
    rawTris.reserve(numTriangles);
    for (uint32_t i = 0; i < numTriangles; ++i) {
        RawTri t;
        float normal[3], v0[3], v1[3], v2[3]; uint16_t attr;
        file.read(reinterpret_cast<char*>(normal), 12);
        file.read(reinterpret_cast<char*>(v0), 12);
        file.read(reinterpret_cast<char*>(v1), 12);
        file.read(reinterpret_cast<char*>(v2), 12);
        file.read(reinterpret_cast<char*>(&attr), 2);
        t.n  = glm::vec3(normal[0], normal[1], normal[2]);
        t.v0 = glm::vec3(v0[0], v0[1], v0[2]);
        t.v1 = glm::vec3(v1[0], v1[1], v1[2]);
        t.v2 = glm::vec3(v2[0], v2[1], v2[2]);
        rawTris.push_back(t);
    }

    const float weldTol = 1e-6f;
    auto snapVal = [&](float v) -> int64_t {
        return static_cast<int64_t>(std::round(v / weldTol));
    };
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
    std::unordered_map<IKey, unsigned int, IKeyHash> vertexMap;
    std::vector<glm::vec3> weldedPos;
    std::vector<glm::vec3> weldedNrm;

    auto getOrAdd = [&](const glm::vec3& pos, const glm::vec3& faceN) -> unsigned int {
        IKey key = { snapVal(pos.x), snapVal(pos.y), snapVal(pos.z) };
        auto it = vertexMap.find(key);
        if (it != vertexMap.end()) {
            weldedNrm[it->second] += faceN; // accumulate normal for smooth shading
            return it->second;
        }
        unsigned int idx = static_cast<unsigned int>(weldedPos.size());
        vertexMap[key] = idx;
        weldedPos.push_back(pos);
        weldedNrm.push_back(faceN);
        return idx;
    };

    vertices.clear(); indices.clear();
    for (const auto& t : rawTris) {
        unsigned int i0 = getOrAdd(t.v0, t.n);
        unsigned int i1 = getOrAdd(t.v1, t.n);
        unsigned int i2 = getOrAdd(t.v2, t.n);
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;
        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
    }

    if (params.enablePolarRemoval && !weldedPos.empty()) {
        // ------------------------------------------------------------------
        // Feature-preserving surface decimation via meshoptimizer.
        // meshopt_simplify reduces triangle count while locking sharp borders.
        // ------------------------------------------------------------------

        // Build a flat float array — guaranteed compatible with meshoptimizer's
        // float3 requirement regardless of GLM packing/alignment details.
        std::vector<float> posFlat;
        posFlat.reserve(weldedPos.size() * 3);
        for (const auto& p : weldedPos) {
            posFlat.push_back(p.x);
            posFlat.push_back(p.y);
            posFlat.push_back(p.z);
        }

        // Target 70% of original count — less aggressive to preserve topology
        // on complex multi-component geometry (holes, mixed shapes).
        size_t target_index_count = std::max<size_t>(3u, (indices.size() * 7) / 10);
        float  target_error       = 5e-2f;  // 5% shape deformation tolerance
        float  result_error       = 0.0f;

        std::vector<unsigned int> new_indices(indices.size()); // worst-case

        size_t new_index_count = meshopt_simplify(
            new_indices.data(),
            indices.data(),
            indices.size(),
            posFlat.data(),
            weldedPos.size(),
            sizeof(float) * 3,
            target_index_count,
            target_error,
            meshopt_SimplifyLockBorder,
            &result_error
        );

        new_indices.resize(new_index_count);

        // ------------------------------------------------------------------
        // Remove degenerate triangles (zero-area / duplicate indices).
        // These can cause TetGen to crash on complex geometry.
        // ------------------------------------------------------------------
        {
            std::vector<unsigned int> clean;
            clean.reserve(new_indices.size());
            for (size_t t = 0; t < new_indices.size(); t += 3) {
                unsigned int i0 = new_indices[t + 0];
                unsigned int i1 = new_indices[t + 1];
                unsigned int i2 = new_indices[t + 2];
                // Skip degenerate: duplicate vertex indices
                if (i0 == i1 || i1 == i2 || i0 == i2) continue;
                // Skip degenerate: zero-area triangles
                if (i0 < weldedPos.size() && i1 < weldedPos.size() && i2 < weldedPos.size()) {
                    glm::vec3 cross = glm::cross(weldedPos[i1] - weldedPos[i0],
                                                  weldedPos[i2] - weldedPos[i0]);
                    if (glm::length(cross) < 1e-12f) continue;
                }
                clean.push_back(i0);
                clean.push_back(i1);
                clean.push_back(i2);
            }
            new_indices = std::move(clean);
        }

        std::cout << "Surface Decimation (meshoptimizer): "
                  << (indices.size() / 3) << " tris -> "
                  << (new_indices.size() / 3) << " tris"
                  << " (error=" << result_error * 100.0f << "%)" << std::endl;

        indices = std::move(new_indices);

        // ------------------------------------------------------------------
        // Compact the vertex buffer: remove orphaned (unreferenced) vertices.
        // After decimation many vertices are no longer used by any triangle.
        // Passing them to TetGen creates floating interior points that crash
        // the Delaunay tetrahedralization on complex multi-component meshes.
        // ------------------------------------------------------------------
        {
            // Build remap: old index -> new index.  Unreferenced verts get ~0u.
            std::vector<unsigned int> remap(weldedPos.size(), ~0u);
            unsigned int nextIdx = 0;
            for (auto idx : indices) {
                if (idx < remap.size() && remap[idx] == ~0u)
                    remap[idx] = nextIdx++;
            }

            // Build compacted vertex array
            std::vector<glm::vec3> compactPos(nextIdx);
            for (size_t i = 0; i < weldedPos.size(); ++i) {
                if (remap[i] != ~0u)
                    compactPos[remap[i]] = weldedPos[i];
            }

            // Remap all indices
            for (auto& idx : indices)
                idx = remap[idx];

            weldedPos = std::move(compactPos);

            std::cout << "Vertex compaction: " << remap.size()
                      << " verts -> " << weldedPos.size()
                      << " referenced verts." << std::endl;
        }
    }

    // ------------------------------------------------------------------
    // Recompute per-vertex normals after potential decimation + compaction.
    // Use per-thread accumulation buffers to avoid races under OpenMP.
    // ------------------------------------------------------------------
    {
        const int nVerts = static_cast<int>(weldedPos.size());
        const int nFaces = static_cast<int>(indices.size() / 3);
        weldedNrm.assign(nVerts, glm::vec3(0.0f));

        #pragma omp parallel
        {
            std::vector<glm::vec3> localNrm(nVerts, glm::vec3(0.0f));

            #pragma omp for schedule(static)
            for (int f = 0; f < nFaces; ++f) {
                unsigned int i0 = indices[f * 3 + 0];
                unsigned int i1 = indices[f * 3 + 1];
                unsigned int i2 = indices[f * 3 + 2];
                if (i0 >= (unsigned)nVerts || i1 >= (unsigned)nVerts || i2 >= (unsigned)nVerts)
                    continue;
                glm::vec3 e0 = weldedPos[i1] - weldedPos[i0];
                glm::vec3 e1 = weldedPos[i2] - weldedPos[i0];
                glm::vec3 fN = glm::cross(e0, e1);
                localNrm[i0] += fN;
                localNrm[i1] += fN;
                localNrm[i2] += fN;
            }

            #pragma omp critical
            for (int v = 0; v < nVerts; ++v)
                weldedNrm[v] += localNrm[v];
        }
    }

    vertices.reserve(weldedPos.size());
    for (size_t i = 0; i < weldedPos.size(); ++i) {
        glm::vec3 nrm = weldedNrm[i];
        float len = glm::length(nrm);
        if (len > 1e-8f) nrm /= len;
        vertices.push_back({ weldedPos[i], nrm, glm::vec2(0.0f) });
    }
    std::cout << "Welded: " << rawTris.size() << " tris -> "
              << vertices.size() << " unique verts, "
              << (indices.size() / 3) << " tris kept." << std::endl;

    if (!vertices.empty()) {
        glm::vec3 minAABB = vertices[0].position;
        glm::vec3 maxAABB = vertices[0].position;
        for(const auto& v : vertices) {
            minAABB = glm::min(minAABB, v.position);
            maxAABB = glm::max(maxAABB, v.position);
        }
        glm::vec3 center = (minAABB + maxAABB) * 0.5f;
        glm::vec3 sizeVec = maxAABB - minAABB;

        bboxVolume = sizeVec.x * sizeVec.y * sizeVec.z;
        if(bboxVolume < 0.0001f) bboxVolume = 1.0f;

        float maxDim = std::max(sizeVec.x, std::max(sizeVec.y, sizeVec.z));
        if(maxDim < 0.001f) maxDim = 1.0f;
        float scale = 3.0f / maxDim;

        for(auto& v : vertices) { v.position = (v.position - center) * scale; }
        bboxVolume *= (scale * scale * scale);
    }

    surfaceVertices = vertices;
    surfaceIndices = indices;
    volumetricVertices.clear();
    volumetricIndices.clear();
    tetrahedra.clear();
    originalVolumetricPositions.clear();
    deformedPositions.clear();
    appliedForces.clear();
    nodalDisplacementMagnitudes.clear();
    nodalForceMagnitudes.clear();
    hasDeformation = false;
    hasVolumetricMesh = false;
    showVolumetricMesh = false;
    showAppliedForceField = false;
    displacementMin = 0.0f;
    displacementMax = 0.0f;
    appliedForceMin = 0.0f;
    appliedForceMax = 0.0f;
    totalAppliedForce = 0.0f;
    appliedForcePerNode = 0.0f;
    loadedFileName = fs::path(filepath).filename().string();

    buildBuffers();
    return true;
}

void FEAModel::draw(BuiltInShader& shader, glm::vec3 viewPos) {
    if (indices.empty()) return;

    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setVec3("viewPos", viewPos);
    int scalarMode = getActiveScalarMode();
    shader.setInt("scalarMode", scalarMode);
    shader.setFloat("scalarMin", getActiveScalarMin());
    shader.setFloat("scalarMax", getActiveScalarMax());
    glBindVertexArray(VAO);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    shader.setVec3("objectColor", 0.75f, 0.75f, 0.75f);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);

    if (showWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        shader.setInt("scalarMode", 0);
        shader.setVec3("objectColor", 0.1f, 0.1f, 0.1f);
        glLineWidth(1.5f);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);
}

void FEAModel::updateScalarFieldData() {
    for (auto& vertex : surfaceVertices) vertex.texCoords = glm::vec2(0.0f);
    for (auto& vertex : volumetricVertices) vertex.texCoords = glm::vec2(0.0f);

    displacementMin = 0.0f;
    displacementMax = 0.0f;
    appliedForceMin = 0.0f;
    appliedForceMax = 0.0f;

    size_t displacementCount = std::min(volumetricVertices.size(), nodalDisplacementMagnitudes.size());
    if (displacementCount > 0) {
        displacementMin = nodalDisplacementMagnitudes[0];
        displacementMax = nodalDisplacementMagnitudes[0];
        for (size_t i = 0; i < displacementCount; ++i) {
            float magnitude = nodalDisplacementMagnitudes[i];
            volumetricVertices[i].texCoords.x = magnitude;
            displacementMin = std::min(displacementMin, magnitude);
            displacementMax = std::max(displacementMax, magnitude);
        }
    }

    size_t forceCount = std::min(volumetricVertices.size(), nodalForceMagnitudes.size());
    if (forceCount > 0) {
        appliedForceMin = nodalForceMagnitudes[0];
        appliedForceMax = nodalForceMagnitudes[0];
        for (size_t i = 0; i < forceCount; ++i) {
            float magnitude = nodalForceMagnitudes[i];
            volumetricVertices[i].texCoords.y = magnitude;
            appliedForceMin = std::min(appliedForceMin, magnitude);
            appliedForceMax = std::max(appliedForceMax, magnitude);
        }
    }
}

void FEAModel::updateBounds() {
    if (vertices.empty()) return;

    currentMinBounds = vertices[0].position;
    currentMaxBounds = vertices[0].position;
    for (const auto& vertex : vertices) {
        currentMinBounds = glm::min(currentMinBounds, vertex.position);
        currentMaxBounds = glm::max(currentMaxBounds, vertex.position);
    }
}

int FEAModel::getActiveScalarMode() const {
    if (showVolumetricMesh && hasVolumetricMesh) {
        if (showAppliedForceField && !nodalForceMagnitudes.empty() && appliedForceMax > 0.0f) return 2;
        if (hasDeformation && !nodalDisplacementMagnitudes.empty()) return 1;
    }
    return 0;
}

float FEAModel::getActiveScalarMin() const {
    if (getActiveScalarMode() == 2) return appliedForceMin;
    if (getActiveScalarMode() == 1) return displacementMin;
    return 0.0f;
}

float FEAModel::getActiveScalarMax() const {
    if (getActiveScalarMode() == 2) return appliedForceMax;
    if (getActiveScalarMode() == 1) return displacementMax;
    return 1.0f;
}

bool FEAModel::generateVolumetricMesh() {
    if (surfaceVertices.empty() || surfaceIndices.empty()) {
        std::cout << "No surface mesh to tetrahedralize!" << std::endl;
        return false;
    }

    std::cout << "Starting TetGen Meshing..." << std::endl;

    tetgenio in, out;
    in.firstnumber = 0;

    in.numberofpoints = surfaceVertices.size();
    in.pointlist = new REAL[in.numberofpoints * 3];
    for (size_t i = 0; i < surfaceVertices.size(); ++i) {
        in.pointlist[i * 3 + 0] = surfaceVertices[i].position.x;
        in.pointlist[i * 3 + 1] = surfaceVertices[i].position.y;
        in.pointlist[i * 3 + 2] = surfaceVertices[i].position.z;
    }

    in.numberoffacets = surfaceIndices.size() / 3;
    in.facetlist = new tetgenio::facet[in.numberoffacets];
    in.facetmarkerlist = new int[in.numberoffacets];

    for (size_t i = 0; i < in.numberoffacets; ++i) {
        tetgenio::facet* f = &in.facetlist[i];
        f->polygonlist = new tetgenio::polygon[1];
        f->numberofpolygons = 1;
        f->holelist = NULL;
        f->numberofholes = 0;

        tetgenio::polygon* p = &f->polygonlist[0];
        p->numberofvertices = 3;
        p->vertexlist = new int[3];
        p->vertexlist[0] = surfaceIndices[i * 3 + 0];
        p->vertexlist[1] = surfaceIndices[i * 3 + 1];
        p->vertexlist[2] = surfaceIndices[i * 3 + 2];

        in.facetmarkerlist[i] = 1; 
    }

    float absoluteMaxVol = bboxVolume * (params.maxVolPercent / 100.0f);
    char switches[128];
    snprintf(switches, sizeof(switches), "pq%f/15.0a%f", params.tetQuality, absoluteMaxVol);

    std::cout << "Running TetGen with command: " << switches << std::endl;

    {
        float minA = FLT_MAX, maxA = 0.0f;
        for (size_t ti = 0; ti < surfaceIndices.size(); ti += 3) {
            glm::vec3 va = surfaceVertices[surfaceIndices[ti    ]].position;
            glm::vec3 vb = surfaceVertices[surfaceIndices[ti + 1]].position;
            glm::vec3 vc = surfaceVertices[surfaceIndices[ti + 2]].position;
            float area = 0.5f * glm::length(glm::cross(vb - va, vc - va));
            if (area > 0.0f) { minA = std::min(minA, area); maxA = std::max(maxA, area); }
        }
        if (maxA > 0.0f && minA > 0.0f) {
            float ratio = maxA / minA;
            std::cout << "  Triangle area range: min=" << minA << " max=" << maxA
                      << " ratio=" << ratio << std::endl;
            if (ratio > 1e5f)
                std::cout << "  WARNING: extreme size variation (ratio " << ratio
                          << "). Tiny features may cause TetGen instability." << std::endl;
        }
    }

    try {
        tetgenbehavior b;
        b.parse_commandline(switches);
        tetrahedralize(&b, &in, &out);
    }
    catch (int e) {
        std::cout << "TetGen failed! Integer error code: " << e << std::endl;
        return false;
    }
    catch (std::exception& e) {
        std::cout << "TetGen failed! " << e.what() << std::endl;
        std::cout << "  Tip: check for extremely small holes/edges in the STL." << std::endl;
        return false;
    }
    catch (...) {
        std::cout << "TetGen threw an unknown exception. Mesh aborted." << std::endl;
        return false;
    }

    std::cout << "Meshing Complete! Generated " << out.numberoftetrahedra
              << " tetrahedrons, " << out.numberoftrifaces
              << " tri-faces (" << out.numberoftrifaces << " boundary)." << std::endl;

    volumetricVertices.clear();
    volumetricIndices.clear();
    tetrahedra.clear();
    originalVolumetricPositions.clear();
    deformedPositions.clear();
    appliedForces.clear();
    nodalDisplacementMagnitudes.clear();
    nodalForceMagnitudes.clear();
    hasDeformation = false;
    showAppliedForceField = false;
    displacementMin = 0.0f;
    displacementMax = 0.0f;
    appliedForceMin = 0.0f;
    appliedForceMax = 0.0f;
    totalAppliedForce = 0.0f;
    appliedForcePerNode = 0.0f;

    for (int i = 0; i < out.numberofpoints; ++i) {
        glm::vec3 pos(
            (float)out.pointlist[i * 3 + 0],
            (float)out.pointlist[i * 3 + 1],
            (float)out.pointlist[i * 3 + 2]
        );
        volumetricVertices.push_back({ pos, glm::vec3(0.0f), glm::vec2(0.0f) });
        originalVolumetricPositions.push_back(pos);
    }

    for (int i = 0; i < out.numberoftrifaces; ++i) {
        int ia = out.trifacelist[i * 3 + 0];
        int ib = out.trifacelist[i * 3 + 1];
        int ic = out.trifacelist[i * 3 + 2];
        glm::vec3 va = volumetricVertices[ia].position;
        glm::vec3 vb = volumetricVertices[ib].position;
        glm::vec3 vc = volumetricVertices[ic].position;
        glm::vec3 faceN = glm::cross(vb - va, vc - va);  
        volumetricVertices[ia].normal += faceN;
        volumetricVertices[ib].normal += faceN;
        volumetricVertices[ic].normal += faceN;
    }
    for (auto& v : volumetricVertices) {
        float len = glm::length(v.normal);
        if (len > 1e-8f) v.normal /= len;
        else v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    for (int i = 0; i < out.numberoftrifaces; ++i) {
        volumetricIndices.push_back(out.trifacelist[i * 3 + 0]);
        volumetricIndices.push_back(out.trifacelist[i * 3 + 1]);
        volumetricIndices.push_back(out.trifacelist[i * 3 + 2]);
    }

    for (int i = 0; i < out.numberoftetrahedra; ++i) {
        tetrahedra.push_back(out.tetrahedronlist[i * 4 + 0]);
        tetrahedra.push_back(out.tetrahedronlist[i * 4 + 1]);
        tetrahedra.push_back(out.tetrahedronlist[i * 4 + 2]);
        tetrahedra.push_back(out.tetrahedronlist[i * 4 + 3]);
    }

    hasVolumetricMesh = true;
    showVolumetricMesh = true;
    hasQuadraticMesh = false;
    tetrahedraQuadratic.clear();
    edgeToMidNode.clear();
    nLinearNodes = static_cast<int>(originalVolumetricPositions.size());

    buildBuffers();
    return true;
}

// =============================================================================
// generateMidEdgeNodes
// =============================================================================
// Promotes the Tet4 mesh to a Tet10 mesh by inserting one mid-edge node per
// unique edge, converting each linear element into a quadratic isoparametric
// element.
//
// For each unique undirected edge (a, b) — stored with canonical key
// (min(a,b), max(a,b)) to ensure each edge is visited exactly once — the
// mid-edge node position is:
//   x_m = (1/2)(x_a + x_b)
// (straight-sided midpoint; no surface projection). The key ensures that
// shared edges between adjacent tetrahedra map to the same global node index.
//
// DOF count growth:
//   Before: N_DOF = 3 · |V_linear|
//   After:  N_DOF = 3 · (|V_linear| + |E_unique|)
// where |E_unique| ≤ 6 · nElems (shared edges are counted once).
//
// The resulting 10-entry per-element connectivity layout is:
//   [c0, c1, c2, c3, m(0,1), m(1,2), m(0,2), m(0,3), m(1,3), m(2,3)]
// with local index 4..9 following TetGen -o2 / ABAQUS Tet10 numbering:
//   edge(c0,c1) -> midnode 4      edge(c1,c2) -> midnode 5
//   edge(c0,c2) -> midnode 6      edge(c0,c3) -> midnode 7
//   edge(c1,c3) -> midnode 8      edge(c2,c3) -> midnode 9
// =============================================================================
void FEAModel::generateMidEdgeNodes()
{
    if (tetrahedra.empty()) return;

    const int nElems = static_cast<int>(tetrahedra.size() / 4);
    nLinearNodes = static_cast<int>(originalVolumetricPositions.size());

    // 6 edges per tet, encoded as pairs of local node indices (i < j).
    static constexpr int edgeLocal[6][2] = {
        {0, 1}, {1, 2}, {0, 2}, {0, 3}, {1, 3}, {2, 3}
    };

    edgeToMidNode.clear();
    tetrahedraQuadratic.clear();
    tetrahedraQuadratic.reserve(static_cast<size_t>(nElems) * 10);

    for (int el = 0; el < nElems; ++el) {
        unsigned int c[4];
        for (int k = 0; k < 4; ++k)
            c[k] = tetrahedra[static_cast<size_t>(el) * 4 + k];

        // push corners
        for (int k = 0; k < 4; ++k)
            tetrahedraQuadratic.push_back(c[k]);

        // push mid-edge nodes in canonical order
        for (int e = 0; e < 6; ++e) {
            unsigned int a = c[edgeLocal[e][0]];
            unsigned int b = c[edgeLocal[e][1]];
            auto key = std::make_pair(std::min(a, b), std::max(a, b));

            auto it = edgeToMidNode.find(key);
            if (it != edgeToMidNode.end()) {
                tetrahedraQuadratic.push_back(it->second);
            } else {
                // Create new mid-edge node
                const glm::vec3& pa = originalVolumetricPositions[a];
                const glm::vec3& pb = originalVolumetricPositions[b];
                glm::vec3 mid = 0.5f * (pa + pb);

                unsigned int newIdx = static_cast<unsigned int>(
                    originalVolumetricPositions.size());
                originalVolumetricPositions.push_back(mid);
                volumetricVertices.push_back({ mid, glm::vec3(0,1,0), glm::vec2(0) });

                edgeToMidNode[key] = newIdx;
                tetrahedraQuadratic.push_back(newIdx);
            }
        }
    }

    hasQuadraticMesh = true;
    std::cout << "Tet10: generated " << edgeToMidNode.size()
              << " mid-edge nodes (" << originalVolumetricPositions.size()
              << " total nodes, " << nElems << " quadratic elements)." << std::endl;
}
