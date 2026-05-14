#include "STLLoader.h"
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

bool STLLoader::load(const std::string& path, LoadedGeometry& out) {
    out.clear();
    out.sourceLabel = fs::path(path).filename().string();
    out.objectCount = 1;

    // ------------------------------------------------------------------
    // Open the file in binary mode and validate the minimum size for a
    // binary STL (80-byte header + 4-byte triangle count = 84 bytes).
    // ------------------------------------------------------------------
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cout << "STLLoader: cannot open '" << path << "'" << std::endl;
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < 84) {
        std::cout << "STLLoader: file too small to be a binary STL." << std::endl;
        return false;
    }

    // Skip 80-byte header
    char header[80];
    file.read(header, 80);

    uint32_t numTriangles = 0;
    file.read(reinterpret_cast<char*>(&numTriangles), sizeof(numTriangles));

    // Each binary STL triangle is exactly 50 bytes:
    //   12 bytes normal + 3*12 bytes vertices + 2 bytes attribute count
    if (fileSize < static_cast<std::streamsize>(84 + numTriangles * 50)) {
        std::cout << "STLLoader: file size does not match triangle count; "
                     "possibly ASCII STL (not supported) or corrupted." << std::endl;
        return false;
    }

    // ------------------------------------------------------------------
    // Read all triangles into a raw array first, then weld.
    // ------------------------------------------------------------------
    struct RawTri { float n[3], v0[3], v1[3], v2[3]; };
    std::vector<RawTri> rawTris(numTriangles);

    for (uint32_t i = 0; i < numTriangles; ++i) {
        file.read(reinterpret_cast<char*>(rawTris[i].n),  12);
        file.read(reinterpret_cast<char*>(rawTris[i].v0), 12);
        file.read(reinterpret_cast<char*>(rawTris[i].v1), 12);
        file.read(reinterpret_cast<char*>(rawTris[i].v2), 12);
        uint16_t attr;
        file.read(reinterpret_cast<char*>(&attr), 2);
    }

    // ------------------------------------------------------------------
    // Vertex welding via integer-snapped hash map.
    // Tolerance: 1e-6 (same as original FEAModel::loadSTL).
    // ------------------------------------------------------------------
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
    vertexMap.reserve(numTriangles * 2);

    auto getOrAdd = [&](const float v[3]) -> unsigned int {
        IKey key = { snapVal(v[0]), snapVal(v[1]), snapVal(v[2]) };
        auto it = vertexMap.find(key);
        if (it != vertexMap.end()) return it->second;
        unsigned int idx = static_cast<unsigned int>(out.positions.size());
        vertexMap[key] = idx;
        out.positions.push_back(glm::vec3(v[0], v[1], v[2]));
        return idx;
    };

    out.indices.reserve(numTriangles * 3);

    for (const auto& t : rawTris) {
        unsigned int i0 = getOrAdd(t.v0);
        unsigned int i1 = getOrAdd(t.v1);
        unsigned int i2 = getOrAdd(t.v2);
        // Skip degenerate triangles (duplicate vertices)
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;
        out.indices.push_back(i0);
        out.indices.push_back(i1);
        out.indices.push_back(i2);
    }

    std::cout << "STLLoader: '" << out.sourceLabel << "' -> "
              << out.positions.size() << " verts, "
              << (out.indices.size() / 3) << " tris." << std::endl;
    return true;
}
