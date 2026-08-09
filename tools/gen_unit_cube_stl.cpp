// =============================================================================
//  tools/gen_unit_cube_stl.cpp -- fixture generator.
//
//  Produces assets/test_fixtures/unit_cube.stl, a binary STL of a 1 m^3
//  axis-aligned cube centred at the origin, tessellated identically to
//  FEAModel::generateCube() at its default `subdivisions = 5` (i.e. 5 x 5
//  quads per face, two triangles per quad, 6 faces -> 300 triangles).
//
//  Why a standalone tool: FEAModel::generateCube() lives on the FEAModel
//  class whose constructor talks to OpenGL.  This generator replicates the
//  identical nested-loop tessellation in `generate_face()` so we can produce
//  the fixture without an OpenGL context.  Should the in-app tessellation
//  ever change, regenerate the fixture by rebuilding and re-running this
//  tool from the repository root:
//
//      cmake --build build --config Release --target gen_unit_cube_stl
//      build\Release\gen_unit_cube_stl.exe
//
//  The tool refuses to overwrite an existing fixture unless --force is
//  passed.
// =============================================================================
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct V3 {
    float x, y, z;
};

static V3 v3_add(V3 a, V3 b)         { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 v3_scl(V3 a, float s)      { return {a.x * s,   a.y * s,   a.z * s}; }
static V3 v3_mul(V3 a, V3 s)         { return {a.x*s.x,   a.y*s.y,   a.z*s.z}; }
static V3 v3_sub(V3 a, V3 b)         { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 v3_cross(V3 a, V3 b)       {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
static V3 v3_norm(V3 a) {
    const float L = std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
    if (L < 1e-20f) return {0.0f, 0.0f, 0.0f};
    return {a.x / L, a.y / L, a.z / L};
}

// Verbatim port of FEAModel::generate_face -- same vertex order, same
// triangle order, same winding.
static void emit_face(std::vector<V3>& verts,
                      std::vector<std::uint32_t>& idx,
                      V3 normal, V3 u, V3 v,
                      int sub, V3 size)
{
    const std::uint32_t start_idx = static_cast<std::uint32_t>(verts.size());
    for (int i = 0; i <= sub; ++i) {
        for (int j = 0; j <= sub; ++j) {
            const float u_coord = (static_cast<float>(j) / sub) - 0.5f;
            const float v_coord = (static_cast<float>(i) / sub) - 0.5f;
            const V3 nh = v3_scl(normal, 0.5f);
            const V3 uu = v3_scl(u, u_coord);
            const V3 vv = v3_scl(v, v_coord);
            const V3 p  = v3_mul(v3_add(v3_add(nh, uu), vv), size);
            verts.push_back(p);
        }
    }
    for (int i = 0; i < sub; ++i) {
        for (int j = 0; j < sub; ++j) {
            const std::uint32_t tl = start_idx + i * (sub + 1) + j;
            const std::uint32_t tr = tl + 1;
            const std::uint32_t bl = start_idx + (i + 1) * (sub + 1) + j;
            const std::uint32_t br = bl + 1;
            idx.push_back(tl); idx.push_back(bl); idx.push_back(tr);
            idx.push_back(tr); idx.push_back(bl); idx.push_back(br);
        }
    }
}

int main(int argc, char** argv)
{
    bool force = false;
    fs::path outDir = fs::path("assets") / "test_fixtures";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--force") force = true;
        else if (arg == "--out" && i + 1 < argc) outDir = argv[++i];
        else {
            std::cerr << "Unknown argument: " << arg << "\n"
                      << "Usage: gen_unit_cube_stl [--force] [--out <dir>]\n";
            return 2;
        }
    }
    const fs::path outFile = outDir / "unit_cube.stl";

    if (!force && fs::exists(outFile)) {
        std::cout << "Fixture already exists: " << outFile.string()
                  << " (use --force to regenerate)\n";
        return 0;
    }

    // --- Replicate FEAModel::generateCube() at sizeX=sizeY=sizeZ=1, sub=5 ---
    constexpr int kSub  = 5;
    const V3      kSize = {1.0f, 1.0f, 1.0f};

    std::vector<V3>            verts;
    std::vector<std::uint32_t> idx;
    verts.reserve(6 * (kSub + 1) * (kSub + 1));
    idx.reserve(6 * 6 * kSub * kSub);

    // Same 6 calls as FEAModel::generateCube().
    emit_face(verts, idx, { 0,  0,  1}, { 1,  0,  0}, { 0,  1,  0}, kSub, kSize);
    emit_face(verts, idx, { 0,  0, -1}, {-1,  0,  0}, { 0,  1,  0}, kSub, kSize);
    emit_face(verts, idx, { 1,  0,  0}, { 0,  0, -1}, { 0,  1,  0}, kSub, kSize);
    emit_face(verts, idx, {-1,  0,  0}, { 0,  0,  1}, { 0,  1,  0}, kSub, kSize);
    emit_face(verts, idx, { 0,  1,  0}, { 1,  0,  0}, { 0,  0, -1}, kSub, kSize);
    emit_face(verts, idx, { 0, -1,  0}, { 1,  0,  0}, { 0,  0,  1}, kSub, kSize);

    // --- Write binary STL ---
    fs::create_directories(outDir);
    std::ofstream os(outFile, std::ios::binary | std::ios::trunc);
    if (!os) {
        std::cerr << "Failed to open " << outFile.string() << " for writing\n";
        return 1;
    }

    char header[80] = {};
    std::snprintf(header, sizeof(header),
                  "FEAPreProcessor TODO_01 unit_cube fixture (1m, sub=%d)",
                  kSub);
    os.write(header, 80);

    const std::uint32_t nTri = static_cast<std::uint32_t>(idx.size() / 3);
    os.write(reinterpret_cast<const char*>(&nTri), 4);

    for (std::size_t k = 0; k + 2 < idx.size(); k += 3) {
        const V3 a = verts[idx[k + 0]];
        const V3 b = verts[idx[k + 1]];
        const V3 c = verts[idx[k + 2]];
        const V3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
        os.write(reinterpret_cast<const char*>(&n), 12);
        os.write(reinterpret_cast<const char*>(&a), 12);
        os.write(reinterpret_cast<const char*>(&b), 12);
        os.write(reinterpret_cast<const char*>(&c), 12);
        const std::uint16_t attr = 0;
        os.write(reinterpret_cast<const char*>(&attr), 2);
    }

    os.close();
    if (!os) {
        std::cerr << "I/O error while closing " << outFile.string() << "\n";
        return 1;
    }

    std::cout << "Wrote " << nTri << " triangles ("
              << verts.size() << " duplicated verts) to "
              << outFile.string() << "\n";
    return 0;
}
