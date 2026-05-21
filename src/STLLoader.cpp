#include "STLLoader.h"
#include <filesystem>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Shared vertex welder used by both ASCII and binary paths.
// ---------------------------------------------------------------------------
namespace {

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

struct Welder {
    static constexpr float kTol = 1e-6f;

    std::unordered_map<IKey, unsigned, IKeyHash> map;
    LoadedGeometry& out;

    explicit Welder(LoadedGeometry& g) : out(g) {}

    unsigned add(float x, float y, float z) {
        auto snap = [](float v) -> int64_t {
            return static_cast<int64_t>(std::round(v / kTol));
        };
        IKey key = { snap(x), snap(y), snap(z) };
        auto it = map.find(key);
        if (it != map.end()) return it->second;
        const unsigned idx = static_cast<unsigned>(out.positions.size());
        map[key] = idx;
        out.positions.push_back(glm::vec3(x, y, z));
        return idx;
    }

    void tri(float v0[3], float v1[3], float v2[3]) {
        const unsigned i0 = add(v0[0], v0[1], v0[2]);
        const unsigned i1 = add(v1[0], v1[1], v1[2]);
        const unsigned i2 = add(v2[0], v2[1], v2[2]);
        if (i0 == i1 || i1 == i2 || i0 == i2) return;
        out.indices.push_back(i0);
        out.indices.push_back(i1);
        out.indices.push_back(i2);
    }
};

// ---------------------------------------------------------------------------
// Binary STL: 80-byte header + uint32 count + N * 50-byte records.
// ---------------------------------------------------------------------------
bool loadBinarySTL(std::istream& file, std::streamsize fileSize, LoadedGeometry& out)
{
    char header[80];
    file.read(header, 80);

    uint32_t N = 0;
    file.read(reinterpret_cast<char*>(&N), sizeof(N));

    if (fileSize < static_cast<std::streamsize>(84 + (std::streamsize)N * 50)) {
        std::cout << "STLLoader: binary triangle count inconsistent with file size."
                  << std::endl;
        return false;
    }

    Welder w(out);
    w.map.reserve(N * 2);
    out.indices.reserve(N * 3);

    for (uint32_t i = 0; i < N; ++i) {
        float n[3], v0[3], v1[3], v2[3];
        uint16_t attr;
        file.read(reinterpret_cast<char*>(n),  12);
        file.read(reinterpret_cast<char*>(v0), 12);
        file.read(reinterpret_cast<char*>(v1), 12);
        file.read(reinterpret_cast<char*>(v2), 12);
        file.read(reinterpret_cast<char*>(&attr), 2);
        w.tri(v0, v1, v2);
    }
    return !out.positions.empty();
}

// ---------------------------------------------------------------------------
// ASCII STL: tokenise by whitespace and collect every "vertex x y z" triplet.
// Groups of 3 consecutive vertices form one triangle — correct for well-formed
// ASCII STL where each "outer loop" block has exactly 3 vertex lines.
// ---------------------------------------------------------------------------
bool loadAsciiSTL(std::istream& file, LoadedGeometry& out)
{
    Welder w(out);
    std::string token;
    float verts[3][3];
    int vCount = 0;

    while (file >> token) {
        if (token != "vertex") continue;
        if (!(file >> verts[vCount][0] >> verts[vCount][1] >> verts[vCount][2]))
            break;
        ++vCount;
        if (vCount == 3) {
            w.tri(verts[0], verts[1], verts[2]);
            vCount = 0;
        }
    }
    return !out.positions.empty();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------
bool STLLoader::load(const std::string& path, LoadedGeometry& out) {
    out.clear();
    out.sourceLabel = fs::path(path).filename().string();
    out.objectCount = 1;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cout << "STLLoader: cannot open '" << path << "'" << std::endl;
        return false;
    }

    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Format detection:
    // If first 5 bytes spell "solid" AND size != 84 + N*50, treat as ASCII.
    // Otherwise assume binary (binary STL files can also start with "solid").
    bool isAscii = false;
    if (fileSize >= 5) {
        char hdr5[5] = {};
        file.read(hdr5, 5);

        if (hdr5[0]=='s' && hdr5[1]=='o' && hdr5[2]=='l'
                         && hdr5[3]=='i' && hdr5[4]=='d') {
            if (fileSize < 84) {
                isAscii = true;
            } else {
                file.seekg(80, std::ios::beg);
                uint32_t N = 0;
                file.read(reinterpret_cast<char*>(&N), 4);
                const std::streamsize expected = 84LL + (std::streamsize)N * 50LL;
                isAscii = (fileSize != expected);
            }
        }
        file.seekg(0, std::ios::beg);
    }

    const bool ok = isAscii ? loadAsciiSTL(file, out)
                             : loadBinarySTL(file, fileSize, out);
    if (!ok) {
        std::cout << "STLLoader: failed to parse '" << out.sourceLabel << "'" << std::endl;
        return false;
    }

    std::cout << "STLLoader: '" << out.sourceLabel << "' -> "
              << out.positions.size() << " verts, "
              << (out.indices.size() / 3) << " tris"
              << (isAscii ? " [ASCII]" : " [binary]") << std::endl;
    return true;
}
