#pragma once
#include <glm/glm.hpp>

struct Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 texCoords; };

struct FEAParams {
    float sizeX = 5.0f;
    float sizeY = 1.0f;
    float sizeZ = 1.0f;
    float subdivisions = 5.0f;

    // --- NEW: TETGEN CONTROLS ---
    float tetQuality = 1.4f;      // Lower is better (1.1 to 2.0). Controls triangle shape.
    float maxVolPercent = 0.1f;   // Limits maximum tetrahedron size relative to part size
    bool enablePolarRemoval = false; // Disable by default to preserve complex CAD topologies
};
