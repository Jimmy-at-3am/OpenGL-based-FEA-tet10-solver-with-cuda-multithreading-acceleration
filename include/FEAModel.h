#pragma once
#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>
#include "FEAData.h"
#include "BuiltInShader.h"
#include "IGeometryLoader.h"

struct ForceArrow {
    glm::vec3 start;
    glm::vec3 end;
};

class FEAModel {
public:
    unsigned int VAO, VBO, EBO;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    FEAParams params;
    bool needsUpdate = true;
    float bboxVolume = 1000.0f; // NEW: Stores overall part volume

    std::string loadedFileName = "";
    std::string lastLoadedFormat = "";  // "STL" | "3MF" | ""
    int         lastLoadedObjectCount = 0; // number of objects found in last load
    bool hasVolumetricMesh = false;
    bool showVolumetricMesh = false;
    
    // NEW: FEA state tracking
    bool hasDeformation = false;
    bool showDeformedMesh = true;
    bool showAppliedForceField = false;
    std::vector<ForceArrow> appliedForces;
    std::vector<float> nodalDisplacementMagnitudes;
    std::vector<float> nodalForceMagnitudes;
    float displacementMin = 0.0f;
    float displacementMax = 0.0f;
    float appliedForceMin = 0.0f;
    float appliedForceMax = 0.0f;
    float totalAppliedForce = 0.0f;
    float appliedForcePerNode = 0.0f;
    glm::vec3 currentMinBounds = glm::vec3(-0.5f);
    glm::vec3 currentMaxBounds = glm::vec3(0.5f);
    
    std::vector<Vertex> surfaceVertices;
    std::vector<unsigned int> surfaceIndices;
    std::vector<Vertex> volumetricVertices;
    std::vector<unsigned int> volumetricIndices;
    std::vector<unsigned int> tetrahedra; // NEW: stores the 4 node indices of each tetrahedron
    std::vector<unsigned int> tetrahedraQuadratic; // 10 node indices per tet (Tet10)
    bool hasQuadraticMesh = false;

    // Brittle-fracture state (set by FEASolver::solveBrittleFracture).
    // Length = nElems.  1 = active, 0 = removed.
    std::vector<uint8_t> elementAlive;
    // Fracture iteration at which each element died; -1 means still alive.
    std::vector<int>     elementFailureIter;
    // Failure mode that killed each element (0=alive, 1=interlayer-tension,
    // 2=interlayer-shear, 3=intralayer).  Only meaningful when useFdmAnisotropy
    // is true; otherwise all killed elements carry mode 0.
    std::vector<uint8_t> elementFailureMode;
    int  nLinearNodes = 0;  // number of original Tet4 nodes (before mid-edge insertion)
    // Maps canonical edge (min,max) -> mid-edge node index.
    std::map<std::pair<unsigned int,unsigned int>, unsigned int> edgeToMidNode;
    std::vector<glm::vec3> originalVolumetricPositions; // NEW: stores initial positions before deformation
    std::vector<glm::vec3> deformedPositions; // NEW: stores displaced positions

    FEAModel();

    void buildBuffers();
    void generateCube();
    void generate_face(glm::vec3 normal, glm::vec3 u, glm::vec3 v, int sub);
    // --- Format-specific load entry points ---
    // Both route through processRawGeometry() for identical post-processing.
    bool loadSTL(const std::string& filepath);
    bool load3MF(const std::string& filepath);

    // Load any supported format (dispatches by extension).
    bool loadFile(const std::string& filepath);
    void draw(BuiltInShader& shader, glm::vec3 viewPos);
    bool generateVolumetricMesh();
    // Generates mid-edge nodes from the existing Tet4 mesh, populating
    // tetrahedraQuadratic (10*nElems), extending originalVolumetricPositions
    // and volumetricVertices, and setting hasQuadraticMesh = true.
    void generateMidEdgeNodes();

private:
    // Shared post-processing after any geometry loader:
    // welding (done by loaders), optional decimation, normal recomputation,
    // bbox centering/scaling, and surface buffer upload.
    // On success fills surfaceVertices/surfaceIndices, resets volumetric state,
    // sets loadedFileName / lastLoadedFormat / lastLoadedObjectCount, and
    // calls buildBuffers().
    bool processRawGeometry(LoadedGeometry& geo, const std::string& formatTag);

public:
    void updateScalarFieldData();
    void updateBounds();
    int getActiveScalarMode() const;
    float getActiveScalarMin() const;
    float getActiveScalarMax() const;
};
