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
#include "GeometryLoaderDispatch.h"

namespace fs = std::filesystem;

FEAModel::FEAModel() {
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
    generateCube();
}

// Rebuild the volumetric surface from alive Tet4 elements for fracture visualization.
// For each face of a tet, count how many alive tets share it.  Faces shared by
// exactly 1 alive tet are boundary faces of the surviving body.
static void rebuildFracturedSurface(const std::vector<unsigned int>& tetrahedra,
                                     const std::vector<uint8_t>&      alive,
                                     std::vector<unsigned int>&        outIndices)
{
    using Face3 = std::tuple<unsigned int, unsigned int, unsigned int>;
    auto makeFace = [](unsigned int a, unsigned int b, unsigned int c) -> Face3 {
        unsigned int v[3] = {a, b, c};
        std::sort(v, v + 3);
        return {v[0], v[1], v[2]};
    };
    std::unordered_map<unsigned int,
        std::unordered_map<unsigned int,
            std::unordered_map<unsigned int, int>>> faceCount;

    // Raw face orderings for a tet with nodes n0,n1,n2,n3 (4 faces).
    // We use sorted keys for the count map, but store the original winding too.
    struct FaceWinding { int n[3]; };
    static const FaceWinding kFaces[4] = {{{1,2,3}},{{0,3,2}},{{0,1,3}},{{0,2,1}}};

    // Per-face: map sorted-key -> (alive_count, original_winding as first seen)
    using Key = std::tuple<unsigned int,unsigned int,unsigned int>;
    struct FaceInfo { int aliveCount = 0; unsigned int w[3] = {}; };
    std::unordered_map<unsigned int, std::unordered_map<unsigned int,
        std::unordered_map<unsigned int, FaceInfo>>> fmap;

    const int nElems = static_cast<int>(tetrahedra.size() / 4);
    for (int el = 0; el < nElems; ++el) {
        if (!alive.empty() && !alive[el]) continue;
        unsigned int n[4];
        for (int i = 0; i < 4; ++i) n[i] = tetrahedra[el * 4 + i];
        for (int f = 0; f < 4; ++f) {
            unsigned int a = n[kFaces[f].n[0]];
            unsigned int b = n[kFaces[f].n[1]];
            unsigned int c = n[kFaces[f].n[2]];
            unsigned int s[3] = {a, b, c};
            std::sort(s, s + 3);
            FaceInfo& fi = fmap[s[0]][s[1]][s[2]];
            fi.aliveCount++;
            if (fi.aliveCount == 1) { fi.w[0] = a; fi.w[1] = b; fi.w[2] = c; }
        }
    }
    outIndices.clear();
    for (auto& [k0, m1] : fmap)
        for (auto& [k1, m2] : m1)
            for (auto& [k2, fi] : m2)
                if (fi.aliveCount == 1) {
                    outIndices.push_back(fi.w[0]);
                    outIndices.push_back(fi.w[1]);
                    outIndices.push_back(fi.w[2]);
                }
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

    // When fracture state is active, regenerate the volumetric surface from
    // surviving elements only so deleted elements visually disappear.
    if (showVolumetricMesh && hasVolumetricMesh && !elementAlive.empty()
        && !tetrahedra.empty()) {
        rebuildFracturedSurface(tetrahedra, elementAlive, volumetricIndices);
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

// =============================================================================
// FEAModel::loadSTL
// =============================================================================
// Thin wrapper: delegates to STLLoader via GeometryLoaderDispatch, then runs
// the shared processRawGeometry() pipeline.
// The full binary-STL parsing logic now lives in src/STLLoader.cpp.
bool FEAModel::loadSTL(const std::string& filepath) {
    LoadedGeometry geo;
    if (!GeometryLoaderDispatch::load(filepath, geo)) return false;
    return processRawGeometry(geo, "STL");
}

// =============================================================================
// FEAModel::load3MF
// =============================================================================
bool FEAModel::load3MF(const std::string& filepath) {
    LoadedGeometry geo;
    if (!GeometryLoaderDispatch::load(filepath, geo)) return false;
    return processRawGeometry(geo, "3MF");
}

// =============================================================================
// FEAModel::loadFile
// =============================================================================
// Dispatch by extension — call this when you don't know the format ahead of time.
bool FEAModel::loadFile(const std::string& filepath) {
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".3mf") return load3MF(filepath);
    return loadSTL(filepath); // default — covers .stl
}

// =============================================================================
// FEAModel::processRawGeometry
// =============================================================================
// Shared post-processing pipeline executed after ANY geometry loader succeeds.
// Inputs:  geo.positions (welded vertex positions), geo.indices (triangle soup)
// Outputs: surfaceVertices, surfaceIndices, GPU buffers uploaded.
//
// Steps
// -----
//  1. Optional surface decimation (meshoptimizer) when enablePolarRemoval==true.
//  2. Degenerate triangle removal.
//  3. Vertex compaction (remove orphaned verts after decimation).
//  4. Per-vertex normal recomputation (OpenMP-parallel).
//  5. AABB centering + uniform scale to 3-unit diagonal.
//  6. Reset all volumetric / FEA state.
//  7. Upload to GPU via buildBuffers().
bool FEAModel::processRawGeometry(LoadedGeometry& geo, const std::string& formatTag) {
    std::vector<glm::vec3> weldedPos  = geo.positions;
    std::vector<unsigned int>& indices = geo.indices;

    if (weldedPos.empty() || indices.empty()) {
        std::cout << "processRawGeometry: empty geometry from loader." << std::endl;
        return false;
    }

    // -----------------------------------------------------------------------
    // 1. Optional decimation (meshoptimizer)
    // -----------------------------------------------------------------------
    if (params.enablePolarRemoval && !weldedPos.empty()) {
        std::vector<float> posFlat;
        posFlat.reserve(weldedPos.size() * 3);
        for (const auto& p : weldedPos) {
            posFlat.push_back(p.x);
            posFlat.push_back(p.y);
            posFlat.push_back(p.z);
        }

        size_t target_index_count = std::max<size_t>(3u, (indices.size() * 7) / 10);
        float  target_error       = 5e-2f;
        float  result_error       = 0.0f;

        std::vector<unsigned int> new_indices(indices.size());
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

        // 2. Remove degenerate triangles
        {
            std::vector<unsigned int> clean;
            clean.reserve(new_indices.size());
            for (size_t t = 0; t < new_indices.size(); t += 3) {
                unsigned int i0 = new_indices[t + 0];
                unsigned int i1 = new_indices[t + 1];
                unsigned int i2 = new_indices[t + 2];
                if (i0 == i1 || i1 == i2 || i0 == i2) continue;
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

        std::cout << "Surface Decimation: "
                  << (indices.size() / 3) << " tris -> "
                  << (new_indices.size() / 3) << " tris"
                  << " (error=" << result_error * 100.0f << "%)" << std::endl;
        indices = std::move(new_indices);

        // 3. Vertex compaction
        {
            std::vector<unsigned int> remap(weldedPos.size(), ~0u);
            unsigned int nextIdx = 0;
            for (auto idx : indices) {
                if (idx < remap.size() && remap[idx] == ~0u)
                    remap[idx] = nextIdx++;
            }
            std::vector<glm::vec3> compactPos(nextIdx);
            for (size_t i = 0; i < weldedPos.size(); ++i)
                if (remap[i] != ~0u) compactPos[remap[i]] = weldedPos[i];
            for (auto& idx : indices) idx = remap[idx];
            weldedPos = std::move(compactPos);
            std::cout << "Vertex compaction: -> " << weldedPos.size() << " verts." << std::endl;
        }
    }

    // -----------------------------------------------------------------------
    // 4. Per-vertex normal recomputation (OpenMP-parallel)
    // -----------------------------------------------------------------------
    std::vector<glm::vec3> weldedNrm;
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
                glm::vec3 fN = glm::cross(weldedPos[i1] - weldedPos[i0],
                                          weldedPos[i2] - weldedPos[i0]);
                localNrm[i0] += fN; localNrm[i1] += fN; localNrm[i2] += fN;
            }
            #pragma omp critical
            for (int v = 0; v < nVerts; ++v)
                weldedNrm[v] += localNrm[v];
        }
    }

    // -----------------------------------------------------------------------
    // 5. Build Vertex array
    // -----------------------------------------------------------------------
    vertices.clear();
    vertices.reserve(weldedPos.size());
    for (size_t i = 0; i < weldedPos.size(); ++i) {
        glm::vec3 nrm = weldedNrm[i];
        float len = glm::length(nrm);
        if (len > 1e-8f) nrm /= len;
        vertices.push_back({ weldedPos[i], nrm, glm::vec2(0.0f) });
    }

    std::cout << "Loaded: " << vertices.size() << " verts, "
              << (indices.size() / 3) << " tris  [" << formatTag << "]" << std::endl;

    // -----------------------------------------------------------------------
    // 5. AABB centering + uniform scale to fit in a 3-unit diagonal
    // -----------------------------------------------------------------------
    if (!vertices.empty()) {
        glm::vec3 minAABB = vertices[0].position;
        glm::vec3 maxAABB = vertices[0].position;
        for (const auto& v : vertices) {
            minAABB = glm::min(minAABB, v.position);
            maxAABB = glm::max(maxAABB, v.position);
        }
        glm::vec3 center  = (minAABB + maxAABB) * 0.5f;
        glm::vec3 sizeVec = maxAABB - minAABB;

        bboxVolume = sizeVec.x * sizeVec.y * sizeVec.z;
        if (bboxVolume < 0.0001f) bboxVolume = 1.0f;

        float maxDim = std::max(sizeVec.x, std::max(sizeVec.y, sizeVec.z));
        if (maxDim < 0.001f) maxDim = 1.0f;
        float scale = 3.0f / maxDim;

        for (auto& v : vertices) v.position = (v.position - center) * scale;
        bboxVolume *= (scale * scale * scale);
    }

    // -----------------------------------------------------------------------
    // 6. Store surface mesh, reset all volumetric / FEA state
    // -----------------------------------------------------------------------
    surfaceVertices = vertices;
    surfaceIndices  = indices;
    volumetricVertices.clear();
    volumetricIndices.clear();
    tetrahedra.clear();
    originalVolumetricPositions.clear();
    deformedPositions.clear();
    appliedForces.clear();
    nodalDisplacementMagnitudes.clear();
    nodalForceMagnitudes.clear();
    hasDeformation        = false;
    hasVolumetricMesh     = false;
    showVolumetricMesh    = false;
    showAppliedForceField = false;
    displacementMin  = 0.0f;  displacementMax  = 0.0f;
    appliedForceMin  = 0.0f;  appliedForceMax  = 0.0f;
    totalAppliedForce    = 0.0f;
    appliedForcePerNode  = 0.0f;

    // Metadata for UI
    loadedFileName          = geo.sourceLabel;
    lastLoadedFormat        = formatTag;
    lastLoadedObjectCount   = geo.objectCount;

    // -----------------------------------------------------------------------
    // 7. Upload to GPU
    // -----------------------------------------------------------------------
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
