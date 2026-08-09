#include "FEAModel.h"
#include "BRepHandle.h"     // must be included here so unique_ptr<BRepHandle> destructor
#include "StepLoader.h"     // can see the complete BRepHandle type
#include "ToolpathModel.h"        // complete type for unique_ptr
#include "GcodeToolpathLoader.h"  // .gcode.3mf -> ToolpathModel
#include <meshoptimizer.h>
#include <glad/glad.h>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <map>
#include <array>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <tuple>
#include <algorithm>
#include <limits>
#include "GeometryUtils.h"
#include "tetgen.h"
#include "MeshQuality.h"
#include "Globals.h"
#include "GeometryLoaderDispatch.h"

namespace fs = std::filesystem;

FEAModel::FEAModel() {
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
    generateCube();
}

// Destructor defined here so unique_ptr<BRepHandle> can call ~BRepHandle()
// with the full type visible (pImpl pattern).
FEAModel::~FEAModel() = default;

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
    // Async compute: a worker thread is mutating solver/mesh data and owns no
    // GL context. Skip the entire rebuild (including the CPU-side vertex/index
    // copy the render thread reads) — the main thread re-runs buildBuffers()
    // once the job finishes.
    if (deferGLUpload) return;

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
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);

    // rebuild + upload the separate dead-element buffers so the
    // fracture pattern can be drawn (ghosted / mode-coloured) in a second pass.
    rebuildDeadElementBuffers();
    if (!deadVertices.empty() && !deadIndices.empty()) {
        if (deadVAO == 0) { glGenVertexArrays(1, &deadVAO); glGenBuffers(1, &deadVBO); glGenBuffers(1, &deadEBO); }
        glBindVertexArray(deadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, deadVBO);
        glBufferData(GL_ARRAY_BUFFER, deadVertices.size() * sizeof(Vertex), &deadVertices[0], GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, deadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, deadIndices.size() * sizeof(unsigned int), &deadIndices[0], GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);
        glBindVertexArray(VAO);
    }
    needsUpdate = false;
}

// build the dead-element render geometry. Each dead element emits
// its 4 triangular faces with DUPLICATED vertices, so every triangle carries a
// single flat per-element value (selected by fractureViewMode) in elementScalar
// — adjacent dead elements never blend colours. Positions are read from the
// already-deformed volumetricVertices so the dead shells line up with the body.
void FEAModel::rebuildDeadElementBuffers() {
    deadVertices.clear();
    deadIndices.clear();
    deadScalarMin = 0.0f;
    deadScalarMax = 1.0f;

    if (elementFailureMode.empty() || tetrahedra.empty() || volumetricVertices.empty())
        return;

    const int nElems = static_cast<int>(tetrahedra.size() / 4);
    // Per-element value selector for the current view mode.
    auto selValue = [this](int el) -> float {
        switch (fractureViewMode) {
            case 4: { int it = (el < (int)elementFailureIter.size()) ? elementFailureIter[el] : -1;
                      return it < 0 ? 0.0f : static_cast<float>(it); }
            case 5: return (el < (int)elementVonMisesAtDeath.size()) ? elementVonMisesAtDeath[el] : 0.0f;
            case 3:
            default: return (el < (int)elementFailureMode.size()) ? static_cast<float>(elementFailureMode[el]) : 0.0f;
        }
    };

    // Colour range for the heatmap modes (4 = crack order, 5 = stress).
    float vmax = 0.0f, itmax = 0.0f;
    for (int el = 0; el < nElems; ++el) {
        if (el < (int)elementAlive.size() && elementAlive[el]) continue; // only dead
        if (el < (int)elementVonMisesAtDeath.size()) vmax = std::max(vmax, elementVonMisesAtDeath[el]);
        if (el < (int)elementFailureIter.size() && elementFailureIter[el] >= 0)
            itmax = std::max(itmax, static_cast<float>(elementFailureIter[el]));
    }
    if (fractureViewMode == 4) { deadScalarMin = 0.0f; deadScalarMax = std::max(itmax, 1.0f); }
    else if (fractureViewMode == 5) { deadScalarMin = 0.0f; deadScalarMax = std::max(vmax, 1.0f); }

    // 4 faces per tet (corner-node winding); same table used by the surface rebuild.
    static const int kFaces[4][3] = {{1,2,3},{0,3,2},{0,1,3},{0,2,1}};
    for (int el = 0; el < nElems; ++el) {
        if (el < (int)elementAlive.size() && elementAlive[el]) continue; // skip alive
        float ev = selValue(el);
        unsigned int n[4];
        for (int i = 0; i < 4; ++i) n[i] = tetrahedra[el * 4 + i];
        for (int f = 0; f < 4; ++f) {
            glm::vec3 p0 = volumetricVertices[n[kFaces[f][0]]].position;
            glm::vec3 p1 = volumetricVertices[n[kFaces[f][1]]].position;
            glm::vec3 p2 = volumetricVertices[n[kFaces[f][2]]].position;
            glm::vec3 nrm = glm::cross(p1 - p0, p2 - p0);
            float len = glm::length(nrm);
            nrm = (len > 1e-12f) ? nrm / len : glm::vec3(0.0f, 0.0f, 1.0f);
            unsigned int base = static_cast<unsigned int>(deadVertices.size());
            Vertex v0{p0, nrm, glm::vec2(0.0f), ev};
            Vertex v1{p1, nrm, glm::vec2(0.0f), ev};
            Vertex v2{p2, nrm, glm::vec2(0.0f), ev};
            deadVertices.push_back(v0);
            deadVertices.push_back(v1);
            deadVertices.push_back(v2);
            deadIndices.push_back(base + 0);
            deadIndices.push_back(base + 1);
            deadIndices.push_back(base + 2);
        }
    }
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
    bboxVolume = params.sizeX * params.sizeY * params.sizeZ;
    if (bboxVolume < 0.0001f) bboxVolume = 1.0f;
    importScale = 1.0f; // preset cube is authored in model units (no rescale)
    // preset cube is authored directly in millimetres, centred at
    // the origin and spanning [-size/2, +size/2]; model space IS physical mm here.
    modelToMM     = 1.0f;
    physicalMaxMM = 0.5f * glm::vec3(params.sizeX, params.sizeY, params.sizeZ);
    physicalMinMM = -physicalMaxMM;
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

    // drop slice/layer state so a prior section overlay does not
    // linger after a cube-dimension change or a switch back to CUBE mode.
    layers.reset();
    showSlicePreview     = false;
    sliceLineVertexCount = 0;

    // A stale gcode toolpath must not hijack the cube draw path.
    toolpath.reset();
    showToolpathPreview = false;
    tpLineVertexCount   = 0;
    tpRanges.clear();
    sectionEnabled = false;
    sectionZModel  = 0.0f;

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
// FEAModel::loadSTEP
// =============================================================================
// Loads a .step/.stp file, retains the analytic B-rep in `brep`, and routes
// the tessellated triangle mesh through the shared processRawGeometry pipeline.
bool FEAModel::loadSTEP(const std::string& filepath) {
    LoadedGeometry geo;
    std::unique_ptr<BRepHandle> newBrep;
    StepLoader loader;
    if (!loader.loadWithBRep(filepath, geo, newBrep, params)) return false;
    if (!processRawGeometry(geo, "STEP")) return false;
    brep = std::move(newBrep); // set AFTER processRawGeometry (which clears brep)
    return true;
}

// =============================================================================
// FEAModel::loadGcode3mf
// =============================================================================
// A Bambu sliced export: the toolpath is the AUTHORITATIVE geometry (real
// per-layer heights + infill as printed). No triangle mesh exists in a form we
// mesh from, so synthesize a 12-triangle shell of the part bbox — enough for
// processRawGeometry to establish units/model space and for the renderer and
// camera to work until the toolpath slab mesh replaces it.
bool FEAModel::loadGcode3mf(const std::string& filepath, bool partOnly) {
    auto tp = std::make_unique<Toolpath::ToolpathModel>();
    std::string err;
    GcodeToolpathLoader::Options opt;
    opt.partOnly = partOnly;
    if (!GcodeToolpathLoader::load(filepath, opt, *tp, err)) {
        std::cout << "[GCODE] load failed: " << err << std::endl;
        return false;
    }
    const glm::vec3 lo = tp->bbMin, hi = tp->bbMax;
    if (!(hi.x > lo.x && hi.y > lo.y && hi.z > lo.z)) {
        std::cout << "[GCODE] degenerate part bbox — refusing load" << std::endl;
        return false;
    }
    LoadedGeometry geo;
    geo.positions = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    geo.indices = {                    // outward-facing winding, all 6 faces
        0, 2, 1,  0, 3, 2,             // -Z
        4, 5, 6,  4, 6, 7,             // +Z
        0, 1, 5,  0, 5, 4,             // -Y
        2, 3, 7,  2, 7, 6,             // +Y
        1, 2, 6,  1, 6, 5,             // +X
        3, 0, 4,  3, 4, 7};            // -X
    geo.sourceLabel  = std::filesystem::path(filepath).filename().string();
    geo.fileUnitToMM = 1.0f;           // Bambu gcode is always millimetres
    if (!processRawGeometry(geo, "GCODE")) return false;
    toolpath = std::move(tp);          // AFTER processRawGeometry (which resets it)
    // Slicer-frontend preview: show the real extrusion moves instead of the
    // placeholder bbox shell until GENERATE 3D MESH replaces the view.
    buildToolpathPreview();
    showToolpathPreview = (tpLineVertexCount > 0);
    return true;
}

// =============================================================================
// FEAModel::buildToolpathPreview / drawToolpathPreview
// =============================================================================
// Feature-coloured GL_LINES built from toolpath->segments (mm plate frame),
// mapped into the centered/rescaled model space via the same transform that
// processRawGeometry applied to the bbox shell:
//   model = (p_mm - partCenter_mm) / modelToMM.
// Colours approximate the Bambu Studio line-type palette so a user coming from
// the slicer immediately reads walls / infill / surfaces.
void FEAModel::buildToolpathPreview() {
    tpRanges.clear();
    tpLineVertexCount = 0;
    if (!toolpath || toolpath->segments.empty()) return;

    const glm::vec3 centerMM = 0.5f * (physicalMinMM + physicalMaxMM);
    const float     invMM    = 1.0f / std::max(1e-9f, modelToMM);

    auto featureColor = [](int f) -> glm::vec3 {
        switch (f) {
            case Toolpath::FT_OUTER_WALL:        return {0.95f, 0.25f, 0.10f};
            case Toolpath::FT_INNER_WALL:        return {0.98f, 0.70f, 0.15f};
            case Toolpath::FT_OVERHANG_WALL:     return {0.15f, 0.55f, 0.95f};
            case Toolpath::FT_SOLID_INFILL:      return {0.55f, 0.20f, 0.70f};
            case Toolpath::FT_SPARSE_INFILL:     return {0.65f, 0.80f, 0.25f};
            case Toolpath::FT_TOP_SURFACE:       return {0.85f, 0.10f, 0.30f};
            case Toolpath::FT_BOTTOM_SURFACE:    return {0.35f, 0.75f, 0.95f};
            case Toolpath::FT_BRIDGE:            return {0.30f, 0.60f, 0.90f};
            case Toolpath::FT_GAP_INFILL:        return {0.90f, 0.90f, 0.90f};
            case Toolpath::FT_SUPPORT:
            case Toolpath::FT_SUPPORT_INTERFACE:
            case Toolpath::FT_BRIM:              return {0.55f, 0.55f, 0.55f};
            default:                             return {0.70f, 0.70f, 0.70f};
        }
    };

    std::vector<Vertex> verts;
    verts.reserve(toolpath->segments.size() * 2);
    for (int f = 0; f < Toolpath::FT_COUNT; ++f) {
        if (f == Toolpath::FT_CUSTOM) continue;   // prime/wipe: never part geometry
        const int first = static_cast<int>(verts.size());
        for (const Toolpath::Segment& s : toolpath->segments) {
            if (s.feature != f) continue;
            const glm::vec3 a = (s.p0 - centerMM) * invMM;
            const glm::vec3 b = (s.p1 - centerMM) * invMM;
            verts.push_back({ a, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f), 0.0f });
            verts.push_back({ b, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f), 0.0f });
        }
        const int count = static_cast<int>(verts.size()) - first;
        if (count > 0) tpRanges.push_back({ first, count, featureColor(f) });
    }

    tpLineVertexCount = static_cast<int>(verts.size());
    if (verts.empty()) return;
    if (tpVAO == 0) { glGenVertexArrays(1, &tpVAO); glGenBuffers(1, &tpVBO); }
    glBindVertexArray(tpVAO);
    glBindBuffer(GL_ARRAY_BUFFER, tpVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), &verts[0], GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    std::cout << "[GCODE] toolpath preview: " << (tpLineVertexCount / 2)
              << " segments in " << tpRanges.size() << " feature groups" << std::endl;
}

void FEAModel::drawToolpathPreview(BuiltInShader& shader) {
    if (tpVAO == 0 || tpLineVertexCount <= 0) return;
    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setInt("scalarMode", 0);
    shader.setFloat("fragAlpha", 1.0f);
    shader.setInt("sectionOn", sectionEnabled ? 1 : 0);
    shader.setFloat("sectionZ", sectionZModel);
    glLineWidth(1.5f);
    glBindVertexArray(tpVAO);
    for (const TpDrawRange& r : tpRanges) {
        shader.setVec3("objectColor", r.color.x, r.color.y, r.color.z);
        glDrawArrays(GL_LINES, r.first, r.count);
    }
    glBindVertexArray(0);
    glLineWidth(1.0f);
    shader.setInt("sectionOn", 0);
}

// Translucent grey XY plane at the section cut height, spanning the model's
// XY bounds (slightly enlarged). Drawn after the model with depth writes off
// so it never occludes later overlays.
void FEAModel::drawSectionPlane(BuiltInShader& shader) {
    if (!sectionEnabled) return;
    const glm::vec3 mn = currentMinBounds, mx = currentMaxBounds;
    const float padX = 0.08f * (mx.x - mn.x) + 1e-3f;
    const float padY = 0.08f * (mx.y - mn.y) + 1e-3f;
    const float z = sectionZModel;
    const Vertex quad[6] = {
        {{mn.x - padX, mn.y - padY, z}, {0,0,1}, {0,0}, 0},
        {{mx.x + padX, mn.y - padY, z}, {0,0,1}, {0,0}, 0},
        {{mx.x + padX, mx.y + padY, z}, {0,0,1}, {0,0}, 0},
        {{mn.x - padX, mn.y - padY, z}, {0,0,1}, {0,0}, 0},
        {{mx.x + padX, mx.y + padY, z}, {0,0,1}, {0,0}, 0},
        {{mn.x - padX, mx.y + padY, z}, {0,0,1}, {0,0}, 0},
    };
    if (secVAO == 0) {
        glGenVertexArrays(1, &secVAO); glGenBuffers(1, &secVBO);
        glBindVertexArray(secVAO);
        glBindBuffer(GL_ARRAY_BUFFER, secVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);
    }
    glBindVertexArray(secVAO);
    glBindBuffer(GL_ARRAY_BUFFER, secVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);

    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setInt("scalarMode", 0);
    shader.setInt("sectionOn", 0);
    shader.setVec3("objectColor", 0.45f, 0.47f, 0.50f);
    shader.setFloat("fragAlpha", 0.40f);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    shader.setFloat("fragAlpha", 1.0f);
    glBindVertexArray(0);
}

// =============================================================================
// FEAModel::loadFile
// =============================================================================
// Dispatch by extension — call this when you don't know the format ahead of time.
bool FEAModel::loadFile(const std::string& filepath) {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    // Sliced gcode exports end in ".gcode.3mf" — they must NOT fall through to
    // the design-3MF XML loader.
    if (lower.size() > 10 && lower.compare(lower.size() - 10, 10, ".gcode.3mf") == 0)
        return loadGcode3mf(filepath);
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".3mf")  return load3MF(filepath);
    if (ext == ".step" || ext == ".stp") return loadSTEP(filepath);
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

        // preserve the REAL physical size (mm) BEFORE the render
        // rescale. STL is unitless -> apply the FEAParams override; 3MF/STEP carry
        // their declared unit in geo.fileUnitToMM (default 1.0 = mm).
        float unitToMM = geo.fileUnitToMM;
        if (formatTag == "STL") unitToMM *= std::max(1e-9f, params.stlUnitToMM);
        physicalMinMM = minAABB * unitToMM;
        physicalMaxMM = maxAABB * unitToMM;

        for (auto& v : vertices) v.position = (v.position - center) * scale;
        bboxVolume *= (scale * scale * scale);
        importScale = scale; // print-unit -> model-unit factor for slicer
        // model-space length * modelToMM = physical millimetres.
        modelToMM = unitToMM / scale;
        glm::vec3 szMM = physicalMaxMM - physicalMinMM;
        std::cout << "[UNITS] real size = " << szMM.x << " x " << szMM.y << " x "
                  << szMM.z << " mm (unit " << unitToMM << " mm/file-unit, modelToMM "
                  << modelToMM << ")" << std::endl;
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

    // Clear stale fidelity reference; rebuilt on next generateVolumetricMesh().
    refSurfaceForFidelity = {};

    // Clear stale B-rep; loadSTEP() re-assigns it after this returns.
    brep.reset();

    // Clear stale toolpath; loadGcode3mf() re-assigns it after this returns
    // — an STL loaded after a gcode must not keep stale beads.
    toolpath.reset();
    showToolpathPreview = false;
    tpLineVertexCount   = 0;
    tpRanges.clear();

    // Sectional view resets with every new geometry (the cut height belongs
    // to the previous part).
    sectionEnabled = false;
    sectionZModel  = 0.0f;

    // drop any slice/layer state from the previously loaded model so
    // a stale section overlay or LayerStack never carries across a reload.
    layers.reset();
    showSlicePreview     = false;
    sliceLineVertexCount = 0;

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
    // Slicer-frontend lane: while a gcode toolpath is loaded and the volume
    // mesh is not being shown, render the real extrusion moves instead of the
    // placeholder bbox shell.
    if (hasToolpath() && showToolpathPreview && !(showVolumetricMesh && hasVolumetricMesh)) {
        shader.use();
        shader.setVec3("viewPos", viewPos);
        drawToolpathPreview(shader);
        return;
    }

    if (indices.empty()) return;

    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setVec3("viewPos", viewPos);
    shader.setFloat("fragAlpha", 1.0f);   // opaque unless ghost pass overrides
    shader.setInt("sectionOn", sectionEnabled ? 1 : 0);
    shader.setFloat("sectionZ", sectionZModel);
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

    // second pass — draw the DEAD elements so the fracture pattern
    // stays visible. HIDDEN skips it; GHOST draws translucent grey; COLORED draws
    // them by the current fractureViewMode (mode / crack order / stress).
    if (!elementFailureMode.empty() && fractureDeadView != DEAD_HIDDEN
        && deadVAO != 0 && !deadIndices.empty()) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glBindVertexArray(deadVAO);
        if (fractureDeadView == DEAD_GHOST) {
            shader.setInt("scalarMode", 0);
            shader.setVec3("objectColor", 0.55f, 0.55f, 0.55f);
            shader.setFloat("fragAlpha", 0.30f);
        } else { // DEAD_COLORED
            // DEFORM(1) has no per-element source -> show failure mode instead.
            int deadMode = (fractureViewMode == 1) ? 3 : fractureViewMode;
            shader.setInt("scalarMode", deadMode);
            shader.setFloat("scalarMin", deadScalarMin);
            shader.setFloat("scalarMax", deadScalarMax);
            shader.setFloat("fragAlpha", 1.0f);
        }
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(deadIndices.size()), GL_UNSIGNED_INT, 0);
        shader.setFloat("fragAlpha", 1.0f);
    }

    shader.setInt("sectionOn", 0);   // overlays drawn after the model are never clipped
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
        for (size_t i = 0; i < displacementCount; ++i)
            volumetricVertices[i].texCoords.x = nodalDisplacementMagnitudes[i];

        // after fracture, detached / near-singular nodes pick up
        // enormous displacements that blow up the raw max and collapse the contour
        // to flat blue. When fracture state is active, base the colour range on the
        // 1st/99th PERCENTILE over only the nodes that still belong to >=1 alive
        // element. Otherwise keep the simple raw min/max.
        const bool fractureActive = !elementAlive.empty() && !tetrahedra.empty();
        if (fractureActive) {
            std::vector<uint8_t> nodeAlive(displacementCount, 0);
            const bool useQuad = hasQuadraticMesh && !tetrahedraQuadratic.empty();
            const int nodesPerEl = useQuad ? 10 : 4;
            const std::vector<unsigned int>& conn = useQuad ? tetrahedraQuadratic : tetrahedra;
            const int nElems = static_cast<int>(conn.size() / nodesPerEl);
            for (int el = 0; el < nElems; ++el) {
                if (el < static_cast<int>(elementAlive.size()) && !elementAlive[el]) continue;
                for (int k = 0; k < nodesPerEl; ++k) {
                    unsigned int nid = conn[static_cast<size_t>(el) * nodesPerEl + k];
                    if (nid < nodeAlive.size()) nodeAlive[nid] = 1;
                }
            }
            std::vector<float> mags;
            mags.reserve(displacementCount);
            for (size_t i = 0; i < displacementCount; ++i)
                if (nodeAlive[i]) mags.push_back(nodalDisplacementMagnitudes[i]);

            if (mags.empty()) {            // nothing alive -> fall back to raw range
                displacementMin = nodalDisplacementMagnitudes[0];
                displacementMax = nodalDisplacementMagnitudes[0];
                for (size_t i = 0; i < displacementCount; ++i) {
                    displacementMin = std::min(displacementMin, nodalDisplacementMagnitudes[i]);
                    displacementMax = std::max(displacementMax, nodalDisplacementMagnitudes[i]);
                }
            } else {
                auto pctl = [&mags](double p) {
                    size_t n = mags.size();
                    size_t k = static_cast<size_t>(p * (n - 1) + 0.5);
                    if (k >= n) k = n - 1;
                    std::nth_element(mags.begin(), mags.begin() + k, mags.end());
                    return mags[k];
                };
                displacementMin = pctl(0.01);
                displacementMax = pctl(0.99);
            }
        } else {
            displacementMin = nodalDisplacementMagnitudes[0];
            displacementMax = nodalDisplacementMagnitudes[0];
            for (size_t i = 0; i < displacementCount; ++i) {
                float magnitude = nodalDisplacementMagnitudes[i];
                displacementMin = std::min(displacementMin, magnitude);
                displacementMax = std::max(displacementMax, magnitude);
            }
        }
        // Percentile guard: avoid a zero-width range (shader already clamps, but
        // keep the legend honest).
        if (displacementMax <= displacementMin)
            displacementMax = displacementMin + 1e-6f;
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
        // with fracture results present, the ALIVE body is coloured
        // by displacement only in the DEFORM view; for MODE / CRACK ORDER / STRESS
        // it goes grey so the colour-coded dead elements (second pass) read clearly.
        if (!elementFailureMode.empty()) {
            if (fractureViewMode == 1 && hasDeformation && !nodalDisplacementMagnitudes.empty())
                return 1;
            return 0;
        }
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
    auto progress = [&](float f) {
        if (computeProgressOut) computeProgressOut->store(std::clamp(f, 0.0f, 1.0f));
    };
    auto cancelled = [&]() {
        return computeCancelRequested && computeCancelRequested->load();
    };
    progress(0.02f);
    if (surfaceVertices.empty() || surfaceIndices.empty()) {
        std::cout << "No surface mesh to tetrahedralize!" << std::endl;
        return false;
    }

    std::cout << "Starting TetGen Meshing..." << std::endl;

    // snapshot the input surface BEFORE TetGen so fidelity check
    // compares the original boundary, not any post-optimisation state.
    refSurfaceForFidelity.positions.clear();
    refSurfaceForFidelity.positions.reserve(surfaceVertices.size());
    for (const auto& sv : surfaceVertices)
        refSurfaceForFidelity.positions.push_back(sv.position);
    refSurfaceForFidelity.indices.assign(surfaceIndices.begin(), surfaceIndices.end());

    // Weld coincident surface vertices before handing the PLC to TetGen.
    // Procedurally generated geometry (e.g. the cube preset) emits one vertex
    // per face-corner, so adjacent faces duplicate their shared edge/corner
    // vertices. TetGen treats those duplicates as overlapping boundary segments
    // and aborts with "self-intersections". Loaded geometry is already welded
    // in processRawGeometry(); this gives the generated path the same guarantee.
    // We weld a LOCAL copy only — surfaceVertices/surfaceIndices still drive the
    // render buffers untouched.
    std::vector<glm::vec3> weldedPos;
    std::vector<int>       remap(surfaceVertices.size(), -1);
    {
        // Quantise positions onto a fine grid keyed in a hash map. The grid is
        // sized off the bounding box so the tolerance scales with the model.
        glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
        for (const auto& sv : surfaceVertices) {
            lo = glm::min(lo, sv.position);
            hi = glm::max(hi, sv.position);
        }
        float diag = glm::length(hi - lo);
        if (!(diag > 0.0f)) diag = 1.0f;
        const double inv = 1.0 / (double(diag) * 1e-6); // ~1e-6 of diagonal
        // Exact key on the quantised integer triple: no false merges.
        std::map<std::array<long long, 3>, int> grid;
        for (size_t i = 0; i < surfaceVertices.size(); ++i) {
            const glm::vec3& p = surfaceVertices[i].position;
            std::array<long long, 3> k = {
                llround(double(p.x) * inv),
                llround(double(p.y) * inv),
                llround(double(p.z) * inv)
            };
            auto it = grid.find(k);
            if (it != grid.end()) {
                remap[i] = it->second;
            } else {
                int idx = static_cast<int>(weldedPos.size());
                weldedPos.push_back(p);
                grid.emplace(k, idx);
                remap[i] = idx;
            }
        }
    }

    // Remap triangles, dropping any that collapse to a degenerate after welding.
    std::vector<std::array<int, 3>> weldedTris;
    weldedTris.reserve(surfaceIndices.size() / 3);
    for (size_t i = 0; i + 2 < surfaceIndices.size(); i += 3) {
        int a = remap[surfaceIndices[i + 0]];
        int b = remap[surfaceIndices[i + 1]];
        int c = remap[surfaceIndices[i + 2]];
        if (a == b || b == c || a == c) continue;
        weldedTris.push_back({a, b, c});
    }

    tetgenio in, out;
    in.firstnumber = 0;

    in.numberofpoints = static_cast<int>(weldedPos.size());
    in.pointlist = new REAL[in.numberofpoints * 3];
    for (size_t i = 0; i < weldedPos.size(); ++i) {
        in.pointlist[i * 3 + 0] = weldedPos[i].x;
        in.pointlist[i * 3 + 1] = weldedPos[i].y;
        in.pointlist[i * 3 + 2] = weldedPos[i].z;
    }

    in.numberoffacets = static_cast<int>(weldedTris.size());
    in.facetlist = new tetgenio::facet[in.numberoffacets];
    in.facetmarkerlist = new int[in.numberoffacets];

    for (size_t i = 0; i < static_cast<size_t>(in.numberoffacets); ++i) {
        tetgenio::facet* f = &in.facetlist[i];
        f->polygonlist = new tetgenio::polygon[1];
        f->numberofpolygons = 1;
        f->holelist = NULL;
        f->numberofholes = 0;

        tetgenio::polygon* p = &f->polygonlist[0];
        p->numberofvertices = 3;
        p->vertexlist = new int[3];
        p->vertexlist[0] = weldedTris[i][0];
        p->vertexlist[1] = weldedTris[i][1];
        p->vertexlist[2] = weldedTris[i][2];

        in.facetmarkerlist[i] = 1;
    }

    float absoluteMaxVol = bboxVolume * (params.maxVolPercent / 100.0f);
    // tighter TetGen switches.
    //   pq<radius-edge>/<min-dihedral>  -- quality bounds (Plan A.4 / care-point #?).
    //   a<maxVol>                       -- per-tet volume cap.
    //   A                               -- assign region attributes (no-op without regions).
    //   O<level>                        -- mesh optimisation level (7 = flips + Laplace).
    //   T<tol>                          -- Shewchuk robustness tolerance.
    char switches[256];
    snprintf(switches, sizeof(switches),
             "pq%g/%ga%gAO%dT%g",
             static_cast<double>(params.tetRadiusEdge),
             static_cast<double>(params.tetMinDihedralDeg),
             static_cast<double>(absoluteMaxVol),
             params.tetOptimizeLevel,
             params.tetRobustnessTol);

    std::cout << "Running TetGen with command: " << switches << std::endl;
    progress(0.18f);
    if (cancelled()) return false;

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

    // TetGen has no cooperative cancellation API. If the user pressed X while
    // it was inside tetrahedralize(), discard the completed local output here;
    // the previously displayed model remains intact.
    progress(0.82f);
    if (cancelled()) return false;

    std::cout << "Meshing Complete! Generated " << out.numberoftetrahedra
              << " tetrahedrons, " << out.numberoftrifaces
              << " tri-faces (" << out.numberoftrifaces << " boundary)." << std::endl;

    MeshQuality::emitReport(out, params);
    // if a B-rep is retained, use exact OCC nearest-point for the
    // forward Hausdorff pass (dramatically tighter result on smooth geometry).
    if (hasBRep())
        MeshQuality::emitFidelityReport(refSurfaceForFidelity, *brep, out, params);
    else
        MeshQuality::emitFidelityReport(refSurfaceForFidelity, out, params);
    progress(0.90f);
    if (cancelled()) return false;

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
    progress(1.0f);
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

// =============================================================================
// layered-FDM slice data model + 2-D section preview.
// These methods take only glm/std types so LayerSlicer.h (and OCC) never leak
// into FEAModel.h. The slicer free functions live in src/LayerSlicer.cpp.
// =============================================================================

// Populate the FE-facing LayerStack. `slabBoundaryCoords` is ascending and has
// nSlabs+1 entries (slab boundaries along the build axis). Per-tet fields
// (elemSlabIndex / elemRegion) are filled later by the slab mesher.
void FEAModel::setLayerStack(int buildAxis, float physicalLayerThickness,
                             int layersPerSlab,
                             const std::vector<float>& slabBoundaryCoords) {
    if (!layers) layers = std::make_unique<LayerStack>();
    layers->buildAxis             = buildAxis;
    layers->physicalLayerThickness = physicalLayerThickness;
    layers->layersPerSlab         = layersPerSlab;
    layers->planeCoords           = slabBoundaryCoords;
    layers->elemSlabIndex.clear();
    layers->elemRegion.clear();
    std::cout << "[SLICE] LayerStack: axis=" << buildAxis
              << " physThick=" << physicalLayerThickness
              << " k=" << layersPerSlab
              << " nSlabs=" << layers->nSlabs() << std::endl;
}

// Rebuild the preview line buffer from already-projected 3-D segment endpoints
// (consecutive pairs = one GL line). A dedicated VAO/VBO keeps the model buffers
// untouched. Empty input clears the preview.
void FEAModel::buildSlicePreview(const std::vector<glm::vec3>& segmentEndpoints) {
    sliceLineVertexCount = static_cast<int>(segmentEndpoints.size());
    if (segmentEndpoints.empty()) return;

    // Fixed normal (build-plane facing) so the shader's lighting term is finite
    // when these lines render with scalarMode 0 (normalize(0) would be NaN).
    std::vector<Vertex> verts;
    verts.reserve(segmentEndpoints.size());
    for (const glm::vec3& p : segmentEndpoints)
        verts.push_back({ p, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f), 0.0f });

    if (sliceVAO == 0) { glGenVertexArrays(1, &sliceVAO); glGenBuffers(1, &sliceVBO); }
    glBindVertexArray(sliceVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sliceVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), &verts[0], GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

// load-arrow overlay. Geometry from `appliedForces` (tail ->
// tip); each arrow = shaft line + 4 head lines. Same dedicated-VAO pattern as
// the slice preview so model buffers are untouched.
void FEAModel::buildForceArrowBuffers() {
    arrowSourceCount = appliedForces.size();
    std::vector<Vertex> verts;
    verts.reserve(appliedForces.size() * 10);
    for (const ForceArrow& fa : appliedForces) {
        const glm::vec3 tail = fa.start, tip = fa.end;
        const glm::vec3 d = tip - tail;
        const float len = glm::length(d);
        if (len < 1e-9f) continue;
        const glm::vec3 dir = d / len;
        glm::vec3 ref = std::abs(dir.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(dir, ref));
        const glm::vec3 v = glm::normalize(glm::cross(dir, u));
        const float hl = 0.28f * len, hw = 0.12f * len;
        auto push = [&](const glm::vec3& p) {
            verts.push_back({ p, glm::vec3(0, 0, 1), glm::vec2(0.0f), 0.0f });
        };
        push(tail); push(tip);
        push(tip); push(tip - dir * hl + u * hw);
        push(tip); push(tip - dir * hl - u * hw);
        push(tip); push(tip - dir * hl + v * hw);
        push(tip); push(tip - dir * hl - v * hw);
    }
    arrowLineVertexCount = static_cast<int>(verts.size());
    if (verts.empty()) return;
    if (arrowVAO == 0) { glGenVertexArrays(1, &arrowVAO); glGenBuffers(1, &arrowVBO); }
    glBindVertexArray(arrowVAO);
    glBindBuffer(GL_ARRAY_BUFFER, arrowVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), &verts[0], GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, elementScalar)); glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

void FEAModel::drawForceArrows(BuiltInShader& shader) {
    if (!showAppliedForceField) return;
    if (arrowSourceCount != appliedForces.size()) buildForceArrowBuffers();
    if (arrowVAO == 0 || arrowLineVertexCount <= 0) return;
    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setInt("scalarMode", 0);
    shader.setFloat("fragAlpha", 1.0f);
    shader.setVec3("objectColor", 1.0f, 0.15f, 0.9f);  // magenta: reads on any view
    glDisable(GL_DEPTH_TEST);
    glLineWidth(4.0f);
    glBindVertexArray(arrowVAO);
    glDrawArrays(GL_LINES, 0, arrowLineVertexCount);
    glBindVertexArray(0);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

// Draw the preview overlay. No-op unless showSlicePreview and a buffer exist.
// Reuses the model shader (projection/view already set by the caller); draws the
// section polygons as bright GL_LINES at their plane height.
void FEAModel::drawSlicePreview(BuiltInShader& shader) {
    if (!showSlicePreview || sliceVAO == 0 || sliceLineVertexCount <= 0) return;
    shader.use();
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setInt("scalarMode", 0);
    shader.setFloat("fragAlpha", 1.0f);
    shader.setVec3("objectColor", 1.0f, 0.45f, 0.05f); // bright orange contour
    glDisable(GL_DEPTH_TEST);   // overlay so the section reads over the body
    glLineWidth(2.5f);
    glBindVertexArray(sliceVAO);
    glDrawArrays(GL_LINES, 0, sliceLineVertexCount);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}
