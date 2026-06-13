// =============================================================================
//  ScenarioRunner.cpp  --  new_TODO_02: headless scenario runner implementation.
//
//  Same-path rule: each stage below calls the EXACT FEAModel / FEASolver method
//  the interactive UI button calls (see src/main.cpp). The shared call sites are
//  annotated with "[same-path: <UI button>]".
// =============================================================================
#include "ScenarioRunner.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Globals.h"
#include "ShaderSources.h"
#include "BuiltInShader.h"
#include "FEAModel.h"
#include "FEASolver.h"
#include "MeshQuality.h"
#include "tetgen.h"
#include "../ext/json/json.hpp"

#include <miniz.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;
using minijson::Value;

// showWireframe is owned by main.cpp; reuse it so the screenshot draw path is
// identical to the interactive one.
extern bool showWireframe;

namespace {

// ---------------------------------------------------------------------------
// Local material loader (plain .mat key=value; identical format to the UI's
// loader in main.cpp). Pure file I/O, not a pipeline fork.
// ---------------------------------------------------------------------------
struct MaterialProps {
    std::string name           = "Steel";
    double E                    = 2.0e11;
    double nu                   = 0.3;
    double density              = 7850.0;
    double fractureStress       = 2.5e8;
    double E_z                  = 0.0;
    double nu_pz                = 0.0;
    double G_pz                 = 0.0;
    double fractureStress_intralayer = 0.0;
    double fractureStress_interlayer = 0.0;
    double fractureShear_interlayer  = 0.0;
};

bool loadMaterialFile(const std::string& path, MaterialProps& m) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        auto p = s.find_last_not_of(" \t\r\n");
        if (p != std::string::npos) s.erase(p + 1); else s.clear();
    };
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        trim(k); trim(v);
        try {
            if      (k == "name")    m.name = v;
            else if (k == "E")       m.E = std::stod(v);
            else if (k == "nu")      m.nu = std::stod(v);
            else if (k == "density") m.density = std::stod(v);
            else if (k == "fractureStress")            m.fractureStress = std::stod(v);
            else if (k == "E_z")                       m.E_z = std::stod(v);
            else if (k == "nu_pz")                     m.nu_pz = std::stod(v);
            else if (k == "G_pz")                      m.G_pz = std::stod(v);
            else if (k == "fractureStress_intralayer") m.fractureStress_intralayer = std::stod(v);
            else if (k == "fractureStress_interlayer") m.fractureStress_interlayer = std::stod(v);
            else if (k == "fractureShear_interlayer")  m.fractureShear_interlayer  = std::stod(v);
        } catch (...) {}
    }
    return true;
}

// Thrown for "could not run" conditions (-> exit 2).
struct RunError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Reject any key not in the allowed set, naming the offending key (protects the
// agent from silent typos; new_TODO_02 unknown-key rule).
void checkKeys(const Value& o, const std::vector<std::string>& allowed,
               const std::string& ctx) {
    if (!o.isObject()) return;
    for (const auto& kv : o.obj) {
        bool ok = false;
        for (const auto& a : allowed) if (a == kv.first) { ok = true; break; }
        if (!ok) throw RunError("unknown key '" + kv.first + "' in " + ctx);
    }
}

const Value* req(const Value& o, const std::string& key, const std::string& ctx) {
    const Value* v = o.find(key);
    if (!v) throw RunError("missing required key '" + key + "' in " + ctx);
    return v;
}

double num(const Value& v, const std::string& ctx) {
    if (!v.isNumber()) throw RunError("expected number for " + ctx);
    return v.num;
}

// ---------------------------------------------------------------------------
// Screenshot statistics + PNG capture.
// ---------------------------------------------------------------------------
struct ShotStats {
    int    w = 0, h = 0;
    double meanRGB[3] = {0, 0, 0};
    double stdRGB[3]  = {0, 0, 0};
    long   uniqueColors = 0;
    bool   written = false;
};

// Deterministic camera presets from the model AABB (no mouse state).
void cameraFor(const std::string& preset, const glm::vec3& mn, const glm::vec3& mx,
               glm::vec3& eye, glm::vec3& center, glm::vec3& up) {
    center = 0.5f * (mn + mx);
    float radius = 0.5f * glm::length(mx - mn);
    if (radius < 1e-4f) radius = 1.0f;
    float dist = radius * 3.0f;
    glm::vec3 dir;
    up = glm::vec3(0, 1, 0);
    if      (preset == "front") dir = glm::vec3(0, 0, 1);
    else if (preset == "top")   { dir = glm::vec3(0, 1, 0); up = glm::vec3(0, 0, -1); }
    else if (preset == "right") dir = glm::vec3(1, 0, 0);
    else                        dir = glm::normalize(glm::vec3(1.0f, 0.8f, 1.0f)); // iso
    eye = center + dir * dist;
}

ShotStats captureScreenshot(GLFWwindow* window, FEAModel& model,
                            BuiltInShader& shader, const std::string& preset,
                            const std::string& outPath) {
    ShotStats st;
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
    st.w = w; st.h = h;

    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.9f, 0.92f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec3 eye, center, up;
    cameraFor(preset, model.currentMinBounds, model.currentMaxBounds, eye, center, up);
    // Near/far scale with the model so huge deformations stay inside the
    // frustum (a fixed far plane silently clips them and yields a blank frame).
    float radius = 0.5f * glm::length(model.currentMaxBounds - model.currentMinBounds);
    if (!(radius > 1e-4f)) radius = 1.0f;
    float far  = radius * 100.0f;
    float near = std::max(far * 1e-4f, radius * 1e-3f);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                                            (float)w / (float)h, near, far);
    glm::mat4 view = glm::lookAt(eye, center, up);

    shader.use();
    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    model.draw(shader, eye); // [same-path: render loop model.draw]

    glFinish();

    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    // Stats over the RGB buffer.
    double sum[3] = {0, 0, 0}, sumsq[3] = {0, 0, 0};
    std::unordered_set<uint32_t> uniq;
    uniq.reserve(4096);
    const size_t nPix = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < nPix; ++i) {
        unsigned char r = px[i * 3 + 0], g = px[i * 3 + 1], b = px[i * 3 + 2];
        sum[0] += r; sum[1] += g; sum[2] += b;
        sumsq[0] += double(r) * r; sumsq[1] += double(g) * g; sumsq[2] += double(b) * b;
        uniq.insert((uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b));
    }
    for (int c = 0; c < 3; ++c) {
        st.meanRGB[c] = sum[c] / double(nPix);
        double var = sumsq[c] / double(nPix) - st.meanRGB[c] * st.meanRGB[c];
        st.stdRGB[c] = var > 0.0 ? std::sqrt(var) : 0.0;
    }
    st.uniqueColors = static_cast<long>(uniq.size());

    // Encode PNG. GL rows are bottom-up; miniz flips when flip=MZ_TRUE.
    size_t pngLen = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(
        px.data(), w, h, 3, &pngLen, 6 /*level*/, MZ_TRUE /*flip*/);
    if (png) {
        std::ofstream of(outPath, std::ios::binary);
        if (of.is_open()) {
            of.write(reinterpret_cast<const char*>(png), static_cast<std::streamsize>(pngLen));
            st.written = of.good();
        }
        mz_free(png);
    }
    return st;
}

// ---------------------------------------------------------------------------
// Mesh-quality stats: reuse MeshQuality::computeReport (TODO_02 library) by
// reconstructing a tetgenio from the model's Tet4 mesh. Same library the UI
// console report uses.
// ---------------------------------------------------------------------------
Value meshStats(FEAModel& model) {
    Value m = Value::makeObject();
    const int nNodes = model.nLinearNodes > 0
                       ? model.nLinearNodes
                       : static_cast<int>(model.originalVolumetricPositions.size());
    const int nTets = static_cast<int>(model.tetrahedra.size() / 4);
    m.set("nNodes", Value(nNodes));
    m.set("nTets", Value(nTets));
    if (nTets <= 0 || nNodes <= 0) {
        m.set("minJacobian", Value::makeNull());
        return m;
    }

    tetgenio tio;
    tio.firstnumber = 0;
    tio.numberofpoints = nNodes;
    tio.pointlist = new REAL[static_cast<size_t>(nNodes) * 3];
    for (int i = 0; i < nNodes; ++i) {
        const glm::vec3& p = model.originalVolumetricPositions[i];
        tio.pointlist[i * 3 + 0] = p.x;
        tio.pointlist[i * 3 + 1] = p.y;
        tio.pointlist[i * 3 + 2] = p.z;
    }
    tio.numberofcorners = 4;
    tio.numberoftetrahedra = nTets;
    tio.tetrahedronlist = new int[static_cast<size_t>(nTets) * 4];
    for (int i = 0; i < nTets * 4; ++i)
        tio.tetrahedronlist[i] = static_cast<int>(model.tetrahedra[i]);

    MeshQuality::QualityReport q = MeshQuality::computeReport(tio, model.params);

    m.set("minJacobian", Value(q.scaledJac_min));
    m.set("scaledJacMedian", Value(q.scaledJac_median));
    m.set("scaledJacMax", Value(q.scaledJac_max));
    m.set("invertedCount", Value(q.invertedCount));
    m.set("sliverCount", Value(q.sliverCount));
    Value pct = Value::makeObject();
    pct.set("q05", Value(q.scaledJacPct.p05));
    pct.set("q50", Value(q.scaledJacPct.p50));
    pct.set("q95", Value(q.scaledJacPct.p95));
    m.set("scaledJacPct", pct);
    Value kp = Value::makeObject();
    kp.set("q05", Value(q.knuppPct.p05));
    kp.set("q50", Value(q.knuppPct.p50));
    kp.set("q95", Value(q.knuppPct.p95));
    m.set("knuppPct", kp);
    // tetgenio frees pointlist / tetrahedronlist in its destructor.
    return m;
}

// ---------------------------------------------------------------------------
// Dotted-path resolution into the report (objects only; e.g.
// "probes.tip.magnitude").
// ---------------------------------------------------------------------------
const Value* resolvePath(const Value& root, const std::string& path) {
    const Value* cur = &root;
    size_t start = 0;
    while (start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos
                                              ? std::string::npos : dot - start);
        if (!cur->isObject()) return nullptr;
        cur = cur->find(key);
        if (!cur) return nullptr;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

bool evalAssert(double actual, const std::string& op, double expect, double rel) {
    if (op == ">")  return actual >  expect;
    if (op == "<")  return actual <  expect;
    if (op == ">=") return actual >= expect;
    if (op == "<=") return actual <= expect;
    if (op == "==") return actual == expect;
    if (op == "~=") {
        double tol = std::fabs(rel) * std::max(1.0, std::fabs(expect));
        return std::fabs(actual - expect) <= tol;
    }
    throw RunError("unknown assert op '" + op + "'");
}

std::string isoDate() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
}

Value buildBlock() {
    Value b = Value::makeObject();
    b.set("date", Value(isoDate()));
#if defined(_MSC_VER)
    b.set("compiler", Value(std::string("MSVC ") + std::to_string(_MSC_VER)));
#else
    b.set("compiler", Value(std::string("unknown")));
#endif
#if defined(HAS_CUDA)
    b.set("cuda", Value(true));
#else
    b.set("cuda", Value(false));
#endif
    return b;
}

// ===========================================================================
// Runner: parses one scenario and orchestrates geometry -> mesh -> solve ->
// visualise, accumulating the report.
// ===========================================================================
class Runner {
public:
    Runner(GLFWwindow* window, const std::string& shotsDir)
        : m_window(window), m_shotsDir(shotsDir) {}

    // Returns the assert-derived exit code (0 pass, 1 fail). Throws RunError on
    // could-not-run conditions.
    int run(const Value& sc, Value& report) {
        report.set("build", buildBlock());

        // ---- schema version -------------------------------------------
        const Value* vv = req(sc, "v", "scenario");
        if (!vv->isNumber() || static_cast<int>(vv->num) != 1)
            throw RunError("unsupported scenario schema version (need v=1)");

        checkKeys(sc, {"v", "geometry", "material", "fdm", "mesh", "loads",
                       "constraints", "solve", "probes", "screenshots",
                       "asserts"}, "scenario");

        // ---- echo the resolved scenario -------------------------------
        report.set("paramsEcho", sc);

        // ---- geometry -------------------------------------------------
        const Value& geom = *req(sc, "geometry", "scenario");
        checkKeys(geom, {"preset", "size", "stl", "step"}, "geometry");
        applyGeometry(geom);

        // ---- material -------------------------------------------------
        MaterialProps mat;
        if (const Value* mv = sc.find("material")) {
            if (!mv->isString()) throw RunError("material must be a path string");
            if (!loadMaterialFile(mv->str, mat))
                throw RunError("cannot open material file '" + mv->str + "'");
        }

        // ---- mesh -----------------------------------------------------
        if (const Value* mev = sc.find("mesh")) {
            checkKeys(*mev, {"maxVolume", "quality"}, "mesh");
            if (const Value* q = mev->find("quality"))
                m_model.params.tetRadiusEdge = static_cast<float>(num(*q, "mesh.quality"));
            if (const Value* mv = mev->find("maxVolume")) {
                double absV = num(*mv, "mesh.maxVolume");
                double bbox = std::max(1e-9, double(m_model.bboxVolume));
                m_model.params.maxVolPercent = static_cast<float>(100.0 * absV / bbox);
            }
        }
        if (!m_model.generateVolumetricMesh()) // [same-path: GENERATE 3D MESH]
            throw RunError("volumetric meshing failed (see console)");

        // mesh stats captured on the clean Tet4 mesh, before any Tet10 promotion.
        report.set("mesh", meshStats(m_model));

        // ---- solve ----------------------------------------------------
        const Value& solve = *req(sc, "solve", "scenario");
        checkKeys(solve, {"kind", "maxIter", "gpu", "fdmAnisotropy", "buildAxis"},
                  "solve");
        runSolve(sc, solve, mat, report);

        // ---- probes ---------------------------------------------------
        report.set("probes", evalProbes(sc));

        // ---- screenshots ----------------------------------------------
        report.set("screenshots", captureShots(sc));

        // ---- asserts --------------------------------------------------
        return evalAsserts(sc, report);
    }

private:
    GLFWwindow* m_window;
    std::string m_shotsDir;
    FEAModel    m_model;            // ctor needs a live GL context (provided by caller)
    BuiltInShader m_shader{modelVS, modelFS};

    void applyGeometry(const Value& geom) {
        if (const Value* preset = geom.find("preset")) {
            if (preset->asString() != "box")
                throw RunError("unknown geometry.preset '" + preset->asString() + "'");
            if (const Value* sz = geom.find("size")) {
                if (!sz->isArray() || sz->arr.size() != 3)
                    throw RunError("geometry.size must be [x,y,z]");
                m_model.params.sizeX = static_cast<float>(sz->arr[0].asNumber(1));
                m_model.params.sizeY = static_cast<float>(sz->arr[1].asNumber(1));
                m_model.params.sizeZ = static_cast<float>(sz->arr[2].asNumber(1));
            }
            m_model.generateCube(); // [same-path: CUBE MODE + slider regen]
        } else if (const Value* stl = geom.find("stl")) {
            if (!m_model.loadFile(stl->asString())) // [same-path: IMPORT FILE]
                throw RunError("cannot load STL '" + stl->asString() + "'");
        } else if (const Value* step = geom.find("step")) {
            if (!m_model.loadFile(step->asString()))
                throw RunError("cannot load STEP '" + step->asString() + "'");
        } else {
            throw RunError("geometry needs one of: preset, stl, step");
        }
    }

    FEASolver::LoadType mapLoad(const std::string& name) {
        if (name == "topFaceDown" || name == "surfaceCompY") return FEASolver::LoadType::SurfaceCompressionY;
        if (name == "pointZ")        return FEASolver::LoadType::PointForceZ;
        if (name == "cantileverZ")   return FEASolver::LoadType::CantileverBendingZ;
        if (name == "tensionX")      return FEASolver::LoadType::TensionX;
        if (name == "tensionY")      return FEASolver::LoadType::TensionY;
        if (name == "tensionZ")      return FEASolver::LoadType::TensionZ;
        throw RunError("unknown load preset '" + name + "'");
    }

    void runSolve(const Value& sc, const Value& solve, const MaterialProps& mat,
                  Value& report) {
        FEASolver solver;
        solver.youngsModulus = mat.E;
        solver.poissonRatio  = mat.nu;
        solver.fractureStress = mat.fractureStress;

        // load
        double mag = 1.0e6;
        std::string loadName = "cantileverZ";
        if (const Value* loads = sc.find("loads")) {
            if (!loads->isArray() || loads->arr.empty())
                throw RunError("loads must be a non-empty array");
            const Value& l0 = loads->arr[0];
            checkKeys(l0, {"type", "name", "mag", "dir", "pos"}, "loads[0]");
            if (const Value* nm = l0.find("name")) loadName = nm->asString();
            if (const Value* mg = l0.find("mag"))  mag = num(*mg, "loads[0].mag");
        }
        solver.loadType = mapLoad(loadName);
        solver.forceMagnitude = mag;

        if (const Value* cons = sc.find("constraints")) {
            if (!cons->isArray()) throw RunError("constraints must be an array");
            for (const auto& c : cons->arr)
                checkKeys(c, {"preset"}, "constraints[]");
        }

        int buildAxis = 1;
        if (const Value* ba = solve.find("buildAxis")) buildAxis = static_cast<int>(num(*ba, "solve.buildAxis"));
        solver.buildAxis = buildAxis;

        bool gpu = false;
        if (const Value* g = solve.find("gpu")) gpu = g->asBool();
        solver.useGPU = gpu;

        bool fdm = false;
        if (const Value* f = solve.find("fdmAnisotropy")) fdm = f->asBool();
        if (fdm && mat.E_z > 0.0) {
            solver.useFdmAnisotropy = true;
            solver.E_z = mat.E_z; solver.nu_pz = mat.nu_pz; solver.G_pz = mat.G_pz;
            solver.fractureStress_intralayer = mat.fractureStress_intralayer;
            solver.fractureStress_interlayer = mat.fractureStress_interlayer;
            solver.fractureShear_interlayer  = mat.fractureShear_interlayer;
        }

        std::string kind = "linear";
        if (const Value* k = solve.find("kind")) kind = k->asString();
        int maxIter = 50;
        if (const Value* mi = solve.find("maxIter")) maxIter = static_cast<int>(num(*mi, "solve.maxIter"));

        auto t0 = std::chrono::steady_clock::now();
        bool ok = false;
        if (kind == "linear") {
            solver.useQuadraticElements = true;          // [same-path: LINEAR STATIC FEA]
            ok = solver.solveLinearStatic(m_model, 10.0f);
        } else if (kind == "nonlinear") {
            solver.useQuadraticElements = true;          // [same-path: NONLINEAR FEA (NR)]
            NRParams nrp;
            ok = solver.solveNonlinearStatic(m_model, 10.0f, nrp);
        } else if (kind == "fracture") {
            m_model.elementAlive.clear();
            m_model.elementFailureIter.clear();
            m_model.elementFailureMode.clear();
            solver.useQuadraticElements = m_model.hasQuadraticMesh;
            ok = solver.solveBrittleFracture(m_model, 10.0f, maxIter); // [same-path: BRITTLE FRACTURE]
        } else {
            throw RunError("unknown solve.kind '" + kind + "'");
        }
        auto t1 = std::chrono::steady_clock::now();
        double wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        Value sblk = Value::makeObject();
        sblk.set("kind", Value(kind));
        sblk.set("ok", Value(ok));
        sblk.set("wallMs", Value(wallMs));
        sblk.set("backend", Value(std::string(gpu ? "gpu(requested)" : "cpu")));
        sblk.set("fdmAnisotropy", Value(solver.useFdmAnisotropy));
        report.set("solver", sblk);

        if (!ok) throw RunError("solver '" + kind + "' returned failure");

        if (kind == "fracture") report.set("fracture", fractureStats());
    }

    Value fractureStats() {
        Value f = Value::makeObject();
        long total = 0, interT = 0, interS = 0, intra = 0;
        int firstIter = -1;
        const auto& alive = m_model.elementAlive;
        const auto& mode  = m_model.elementFailureMode;
        const auto& iter  = m_model.elementFailureIter;
        for (size_t e = 0; e < alive.size(); ++e) {
            if (alive[e]) continue;
            ++total;
            if (e < mode.size()) {
                if      (mode[e] == 1) ++interT;
                else if (mode[e] == 2) ++interS;
                else if (mode[e] == 3) ++intra;
            }
            if (e < iter.size() && iter[e] >= 0)
                firstIter = (firstIter < 0) ? iter[e] : std::min(firstIter, iter[e]);
        }
        f.set("totalFailed", Value(static_cast<long long>(total)));
        Value bm = Value::makeObject();
        bm.set("interlayerTension", Value(static_cast<long long>(interT)));
        bm.set("interlayerShear",   Value(static_cast<long long>(interS)));
        bm.set("intralayer",        Value(static_cast<long long>(intra)));
        f.set("byMode", bm);
        f.set("firstFailureIter", Value(firstIter));
        return f;
    }

    Value evalProbes(const Value& sc) {
        Value out = Value::makeObject();
        const Value* probes = sc.find("probes");
        if (!probes || !probes->isArray()) return out;

        const auto& orig = m_model.originalVolumetricPositions;
        const auto& def  = m_model.deformedPositions;
        const bool haveDef = (def.size() == orig.size()) && !def.empty();

        for (const auto& p : probes->arr) {
            checkKeys(p, {"id", "pos", "quantity"}, "probes[]");
            std::string id = p.find("id") ? p.find("id")->asString() : "probe";
            const Value* pos = p.find("pos");
            if (!pos || !pos->isArray() || pos->arr.size() != 3)
                throw RunError("probe '" + id + "' needs pos [x,y,z]");
            glm::vec3 target(float(pos->arr[0].asNumber()),
                             float(pos->arr[1].asNumber()),
                             float(pos->arr[2].asNumber()));
            // nearest-node snap
            int best = -1; float bestD2 = 1e30f;
            for (size_t i = 0; i < orig.size(); ++i) {
                float d2 = glm::dot(orig[i] - target, orig[i] - target);
                if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
            }
            Value pr = Value::makeObject();
            if (best < 0) { pr.set("error", Value(std::string("no nodes"))); out.set(id, pr); continue; }
            glm::vec3 disp(0.0f);
            if (haveDef) disp = def[best] - orig[best];
            Value vec = Value::makeArray();
            vec.push(Value(double(disp.x)));
            vec.push(Value(double(disp.y)));
            vec.push(Value(double(disp.z)));
            pr.set("vector", vec);
            pr.set("magnitude", Value(double(glm::length(disp))));
            pr.set("snapDist", Value(double(std::sqrt(bestD2))));
            pr.set("node", Value(best));
            out.set(id, pr);
        }
        return out;
    }

    Value captureShots(const Value& sc) {
        Value out = Value::makeObject();
        const Value* shots = sc.find("screenshots");
        if (!shots || !shots->isArray()) return out;

        // Ensure shots dir exists.
        if (!m_shotsDir.empty()) {
            std::error_code ec; fs::create_directories(m_shotsDir, ec);
        }
        showWireframe = true;

        for (const auto& s : shots->arr) {
            checkKeys(s, {"name", "camera", "scalarMode", "deformScale"}, "screenshots[]");
            std::string name = s.find("name") ? s.find("name")->asString() : "shot";
            std::string cam  = s.find("camera") ? s.find("camera")->asString() : "iso";
            int scalarMode = 1;
            if (const Value* sm = s.find("scalarMode")) scalarMode = static_cast<int>(sm->asNumber());
            double deformScale = 1.0;
            if (const Value* ds = s.find("deformScale")) deformScale = ds->asNumber();

            // Drive the existing scalar/deformation toggles, then rebuild buffers
            // exactly as the UI does after toggling SHOWING/FORCE MAP.
            m_model.showVolumetricMesh    = m_model.hasVolumetricMesh;
            m_model.showDeformedMesh      = (deformScale != 0.0);
            m_model.showAppliedForceField = (scalarMode == 2);
            m_model.buildBuffers();

            std::string file = (fs::path(m_shotsDir) / (name + ".png")).string();
            ShotStats st;
            bool glOk = (m_window != nullptr);
            if (glOk) st = captureScreenshot(m_window, m_model, m_shader, cam, file);

            Value sv = Value::makeObject();
            sv.set("file", Value(file));
            sv.set("camera", Value(cam));
            sv.set("w", Value(st.w));
            sv.set("h", Value(st.h));
            Value mean = Value::makeArray();
            for (int c = 0; c < 3; ++c) mean.push(Value(st.meanRGB[c]));
            Value sd = Value::makeArray();
            for (int c = 0; c < 3; ++c) sd.push(Value(st.stdRGB[c]));
            sv.set("meanRGB", mean);
            sv.set("stdRGB", sd);
            // Convenience scalar: max channel std (assertable single number).
            double stdMax = std::max(st.stdRGB[0], std::max(st.stdRGB[1], st.stdRGB[2]));
            sv.set("stdRGBmax", Value(stdMax));
            sv.set("uniqueColors", Value(static_cast<long long>(st.uniqueColors)));
            sv.set("written", Value(st.written));
            out.set(name, sv);
        }
        return out;
    }

    int evalAsserts(const Value& sc, Value& report) {
        Value results = Value::makeArray();
        bool allPass = true;
        const Value* asserts = sc.find("asserts");
        if (asserts && asserts->isArray()) {
            for (const auto& a : asserts->arr) {
                checkKeys(a, {"path", "op", "value", "rel"}, "asserts[]");
                std::string path = a.find("path") ? a.find("path")->asString() : "";
                std::string op   = a.find("op")   ? a.find("op")->asString()   : "==";
                double expect    = a.find("value") ? a.find("value")->asNumber() : 0.0;
                double rel       = a.find("rel")   ? a.find("rel")->asNumber()   : 1e-6;

                Value r = Value::makeObject();
                r.set("path", Value(path));
                r.set("op", Value(op));
                r.set("expect", Value(expect));
                const Value* actualV = resolvePath(report, path);
                // Bools assert as 0/1 so flags like solver.ok are comparable.
                const bool numeric = actualV && (actualV->isNumber() || actualV->isBool());
                if (!numeric) {
                    r.set("actual", Value::makeNull());
                    r.set("pass", Value(false));
                    r.set("note", Value(std::string("path not found or non-numeric")));
                    allPass = false;
                } else {
                    double actual = actualV->isBool() ? (actualV->b ? 1.0 : 0.0)
                                                      : actualV->num;
                    bool pass = evalAssert(actual, op, expect, rel);
                    r.set("actual", Value(actual));
                    r.set("pass", Value(pass));
                    if (!pass) allPass = false;
                }
                results.push(r);
            }
        }
        report.set("asserts", results);
        report.set("pass", Value(allPass));
        return allPass ? 0 : 1;
    }
};

// ---------------------------------------------------------------------------
// File IO helpers.
// ---------------------------------------------------------------------------
bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

void writeReport(const std::string& outPath, const Value& report) {
    if (outPath.empty()) return;
    std::ofstream of(outPath, std::ios::binary);
    if (of.is_open()) of << report.dump(2) << "\n";
}

// Create a hidden GL context. Returns nullptr on failure.
GLFWwindow* makeHiddenContext() {
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);          // hidden offscreen window
    GLFWwindow* win = glfwCreateWindow(800, 600, "PolyFEA headless", nullptr, nullptr);
    if (!win) { glfwTerminate(); return nullptr; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(win); glfwTerminate(); return nullptr;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    return win;
}

} // namespace

namespace ScenarioRunner {

int runScenario(const std::string& scenarioPath, const std::string& outPath,
                const std::string& shotsDir) {
    Value report = Value::makeObject();

    // Parse first so a malformed scenario is a clean exit-2 with a message.
    std::string src;
    if (!readFile(scenarioPath, src)) {
        std::cerr << "[harness] cannot open scenario '" << scenarioPath << "'\n";
        report.set("error", Value(std::string("cannot open scenario file")));
        report.set("pass", Value(false));
        writeReport(outPath, report);
        return 2;
    }
    Value sc;
    try {
        sc = minijson::parse(src);
    } catch (const std::exception& e) {
        std::cerr << "[harness] " << e.what() << "\n";
        report.set("error", Value(std::string(e.what())));
        report.set("pass", Value(false));
        writeReport(outPath, report);
        return 2;
    }

    GLFWwindow* win = makeHiddenContext();
    if (!win) {
        std::cerr << "[harness] GL init failed; cannot run (FEAModel requires a GL context)\n";
        report.set("error", Value(std::string("GL init failure")));
        report.set("pass", Value(false));
        writeReport(outPath, report);
        return 2;
    }

    int code = 2;
    try {
        Runner runner(win, shotsDir);
        code = runner.run(sc, report);
    } catch (const RunError& e) {
        std::cerr << "[harness] could not run: " << e.what() << "\n";
        report.set("error", Value(std::string(e.what())));
        report.set("pass", Value(false));
        code = 2;
    } catch (const std::exception& e) {
        std::cerr << "[harness] exception: " << e.what() << "\n";
        report.set("error", Value(std::string(e.what())));
        report.set("pass", Value(false));
        code = 2;
    }

    writeReport(outPath, report);

    glfwDestroyWindow(win);
    glfwTerminate();
    return code;
}

int runRegress() {
    int worst = 0; // 0 < 1 < 2 severity
    std::cout << "=== PolyFEA regression (--regress all) ===\n";

    std::vector<std::string> scenarioFiles;
    if (fs::exists("scenarios")) {
        for (const auto& e : fs::directory_iterator("scenarios"))
            if (e.path().extension() == ".json")
                scenarioFiles.push_back(e.path().string());
        std::sort(scenarioFiles.begin(), scenarioFiles.end());
    }

    if (scenarioFiles.empty())
        std::cout << "  (no scenarios/*.json found)\n";

    fs::create_directories("regression/out", std::error_code{});
    for (const auto& sfile : scenarioFiles) {
        std::string stem = fs::path(sfile).stem().string();
        std::string out  = "regression/out/" + stem + ".report.json";
        std::string shots = "regression/out/shots_" + stem;
        int code = runScenario(sfile, out, shots);
        const char* tag = (code == 0) ? "PASS" : (code == 1 ? "FAIL" : "ERROR");
        std::cout << "  [" << tag << "] " << stem << "  (exit " << code << ")\n";
        worst = std::max(worst, code);
    }

    // Legacy regression/*.txt sentinels are recorded as informational lines;
    // they are pinned-output text fixtures, not re-run here (migration pending).
    if (fs::exists("regression")) {
        for (const auto& e : fs::directory_iterator("regression"))
            if (e.path().extension() == ".txt")
                std::cout << "  [NOTE] legacy sentinel " << e.path().filename().string()
                          << " (text fixture; not re-run)\n";
    }

    std::cout << "=== aggregate: "
              << (worst == 0 ? "PASS" : (worst == 1 ? "FAIL" : "ERROR"))
              << " (exit " << worst << ") ===\n";
    return worst;
}

} // namespace ScenarioRunner
