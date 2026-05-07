#pragma once
#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>
#include "FEAData.h"
#include "BuiltInShader.h"

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
    int  nLinearNodes = 0;  // number of original Tet4 nodes (before mid-edge insertion)
    // Maps canonical edge (min,max) -> mid-edge node index.
    std::map<std::pair<unsigned int,unsigned int>, unsigned int> edgeToMidNode;
    std::vector<glm::vec3> originalVolumetricPositions; // NEW: stores initial positions before deformation
    std::vector<glm::vec3> deformedPositions; // NEW: stores displaced positions

    FEAModel();

    void buildBuffers();
    void generateCube();
    void generate_face(glm::vec3 normal, glm::vec3 u, glm::vec3 v, int sub);
    bool loadSTL(const std::string& filepath);
    void draw(BuiltInShader& shader, glm::vec3 viewPos);
    bool generateVolumetricMesh();
    // Generates mid-edge nodes from the existing Tet4 mesh, populating
    // tetrahedraQuadratic (10*nElems), extending originalVolumetricPositions
    // and volumetricVertices, and setting hasQuadraticMesh = true.
    void generateMidEdgeNodes();
    void updateScalarFieldData();
    void updateBounds();
    int getActiveScalarMode() const;
    float getActiveScalarMin() const;
    float getActiveScalarMax() const;
};
