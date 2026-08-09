# Load & Boundary-Condition Input System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a student click a part, drag a direction, type a magnitude, and get a physically correct FEA load or support — with validation, undo/redo, and a clean renderer-facing contract.

**Architecture:** A GL-free algorithmic core under `include/loads/` + `src/loads/` owns the load data model, picking, unit/coordinate conversion, validation and the solver adapter. The adapter emits an immutable SI `LoadSet` that is passed *as a parameter* into each solve. The existing `SimpleUI`, render loop, camera and `FEASolver` are extended, never forked.

**Tech Stack:** C++17, CMake + Ninja + MSVC, Eigen (SVD / dense solves), GLM (vectors/matrices), OpenGL 3.3 (placeholder gizmos only).

**Spec:** `docs/superpowers/specs/2026-08-09-load-input-system-design.md`

## Global Constraints

- **No new third-party dependency.** nlohmann/json is credited in the README but is *not* in the tree. Do not add it.
- **No persistence.** No file format, no sidecar, no scenario-schema change. Structures are serialisable in memory for round-trip tests only.
- **Existing regression must stay green and unmodified.** `scenarios/*.json` and `regression/*.txt` are not edited. Verify with `build\FEAPreProcessor.exe --regress all`.
- **Do not retrofit the existing `FEASolver::LoadType` presets.** Changing their lumping would move regression sentinels. Correct Tet10 lumping lives only in the new adapter.
- **One magnitude, authoritative, in SI.** No second mutable display-unit copy.
- **One geometry scale path:** `metersPerModelUnit(model) = model.modelToMM / 1000.0`, used by adapter *and* solver.
- **`Frame` (World|ObjectLocal) is separate from `DirMode`.** Reverse is an operation (negate), never a mode.
- **`ObjectLocal` is not exposed in the student UI** while no object transform exists.
- **Input priority chain:** UI → gizmo handle → placement picking → camera. Left-click is not globally reserved.
- **Build:** `.\build.bat build` (~3–5 min incremental). The exe is **locked while `--regress all` runs** — never build concurrently.
- Unit tests must not link OpenGL/GLFW.

---

### Task 1: Test target + unit conversion

**Files:**
- Create: `tests/test_util.h`, `tests/load_tests.cpp`
- Create: `include/loads/LoadUnits.h`, `src/loads/LoadUnits.cpp`
- Modify: `CMakeLists.txt` (append after the `mesh_diag` block, ~line 458)

**Interfaces:**
- Produces: `LoadUnits::UnitId`, `LoadUnits::toSI(double, UnitId)`, `LoadUnits::fromSI(double, UnitId)`, `LoadUnits::parseToSI(const std::string&, UnitId, double& out)`, `LoadUnits::suffix(UnitId)`, `LoadUnits::quantityOf(UnitId)`.

- [ ] **Step 1: Write the failing test**

`tests/test_util.h`:
```cpp
#pragma once
#include <cstdio>
#include <cmath>
extern int g_failures;
extern int g_checks;
#define CHECK(cond) do { ++g_checks; if(!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECK_NEAR(a,b,tol) do { ++g_checks; const double _a=(a), _b=(b); \
    if(!(std::fabs(_a-_b) <= (tol))) { ++g_failures; \
    std::printf("FAIL %s:%d  %s ~= %s  (%.9g vs %.9g)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b); } } while(0)
```

`tests/load_tests.cpp`:
```cpp
#include "test_util.h"
#include "loads/LoadUnits.h"
int g_failures = 0;
int g_checks   = 0;
using namespace LoadUnits;

static void test_units() {
    CHECK_NEAR(toSI(1.0, UnitId::Newton),        1.0,    1e-15);
    CHECK_NEAR(toSI(1.0, UnitId::Kilonewton),    1000.0, 1e-12);
    CHECK_NEAR(toSI(1000.0, UnitId::NewtonMm),   1.0,    1e-12); // 1000 N.mm = 1 N.m
    CHECK_NEAR(toSI(1.0, UnitId::NewtonM),       1.0,    1e-15);
    CHECK_NEAR(toSI(1.0, UnitId::Megapascal),    1.0e6,  1e-6);
    CHECK_NEAR(toSI(1.0, UnitId::GramPerCm3),    1000.0, 1e-9);
    CHECK_NEAR(toSI(1.0, UnitId::GravityG),      9.80665,1e-9);

    // round-trip every unit
    for (int i = 0; i < static_cast<int>(UnitId::COUNT); ++i) {
        const UnitId u = static_cast<UnitId>(i);
        CHECK_NEAR(fromSI(toSI(7.25, u), u), 7.25, 1e-9);
    }
    // torque units are distinct, never inferred
    CHECK(toSI(1.0, UnitId::NewtonMm) != toSI(1.0, UnitId::NewtonM));

    double v = -1.0;
    CHECK(parseToSI("2.5",  UnitId::Newton, v) && std::fabs(v - 2.5) < 1e-12);
    CHECK(!parseToSI("",     UnitId::Newton, v));
    CHECK(!parseToSI("abc",  UnitId::Newton, v));
    CHECK(!parseToSI("nan",  UnitId::Newton, v));
    CHECK(!parseToSI("inf",  UnitId::Newton, v));
    CHECK(!parseToSI("1e999",UnitId::Newton, v));
}

int main() {
    test_units();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadUnits.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadUnits.h`:
```cpp
#pragma once
#include <string>

namespace LoadUnits {

enum class Quantity { Force, Torque, Pressure, Length, Density, Acceleration };

enum class UnitId {
    Newton, Kilonewton, PoundForce,
    NewtonMm, NewtonM,
    Pascal, Kilopascal, Megapascal,
    Millimetre, Metre,
    KgPerM3, GramPerCm3,
    MetrePerSec2, GravityG,
    COUNT
};

// Multiplicative factor: si = display * factor(u).
double   factor(UnitId u);
Quantity quantityOf(UnitId u);
const char* suffix(UnitId u);

inline double toSI  (double display, UnitId u) { return display * factor(u); }
inline double fromSI(double si,      UnitId u) { return si     / factor(u); }

// Parses user text. Rejects empty, non-numeric, trailing garbage, NaN, +/-inf
// and overflow. Returns false and leaves `outSI` untouched on rejection.
bool parseToSI(const std::string& text, UnitId u, double& outSI);

} // namespace LoadUnits
```

`src/loads/LoadUnits.cpp`:
```cpp
#include "loads/LoadUnits.h"
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <cctype>

namespace LoadUnits {

double factor(UnitId u) {
    switch (u) {
        case UnitId::Newton:       return 1.0;
        case UnitId::Kilonewton:   return 1.0e3;
        case UnitId::PoundForce:   return 4.4482216152605;
        case UnitId::NewtonMm:     return 1.0e-3;   // N.mm -> N.m
        case UnitId::NewtonM:      return 1.0;
        case UnitId::Pascal:       return 1.0;
        case UnitId::Kilopascal:   return 1.0e3;
        case UnitId::Megapascal:   return 1.0e6;
        case UnitId::Millimetre:   return 1.0e-3;
        case UnitId::Metre:        return 1.0;
        case UnitId::KgPerM3:      return 1.0;
        case UnitId::GramPerCm3:   return 1.0e3;
        case UnitId::MetrePerSec2: return 1.0;
        case UnitId::GravityG:     return 9.80665;
        default:                   return 1.0;
    }
}

Quantity quantityOf(UnitId u) {
    switch (u) {
        case UnitId::Newton: case UnitId::Kilonewton: case UnitId::PoundForce:
            return Quantity::Force;
        case UnitId::NewtonMm: case UnitId::NewtonM:
            return Quantity::Torque;
        case UnitId::Pascal: case UnitId::Kilopascal: case UnitId::Megapascal:
            return Quantity::Pressure;
        case UnitId::Millimetre: case UnitId::Metre:
            return Quantity::Length;
        case UnitId::KgPerM3: case UnitId::GramPerCm3:
            return Quantity::Density;
        default:
            return Quantity::Acceleration;
    }
}

const char* suffix(UnitId u) {
    switch (u) {
        case UnitId::Newton:       return "N";
        case UnitId::Kilonewton:   return "kN";
        case UnitId::PoundForce:   return "lbf";
        case UnitId::NewtonMm:     return "N.mm";
        case UnitId::NewtonM:      return "N.m";
        case UnitId::Pascal:       return "Pa";
        case UnitId::Kilopascal:   return "kPa";
        case UnitId::Megapascal:   return "MPa";
        case UnitId::Millimetre:   return "mm";
        case UnitId::Metre:        return "m";
        case UnitId::KgPerM3:      return "kg/m3";
        case UnitId::GramPerCm3:   return "g/cm3";
        case UnitId::MetrePerSec2: return "m/s2";
        case UnitId::GravityG:     return "g";
        default:                   return "";
    }
}

bool parseToSI(const std::string& text, UnitId u, double& outSI) {
    // Reject empty / whitespace-only before strtod, which would accept "".
    size_t b = text.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = text.find_last_not_of(" \t\r\n");
    const std::string t = text.substr(b, e - b + 1);

    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return false;   // trailing garbage / "abc"
    if (errno == ERANGE)             return false;   // 1e999
    if (!std::isfinite(v))           return false;   // "nan", "inf"

    const double si = v * factor(u);
    if (!std::isfinite(si))          return false;
    outSI = si;
    return true;
}

} // namespace LoadUnits
```

`CMakeLists.txt` — append after the `mesh_diag` block (after line ~458):
```cmake
# --- Load-system unit tests (no GL/GLFW; pure algorithmic core) ---
add_executable(load_tests
    tests/load_tests.cpp
    src/loads/LoadUnits.cpp)
target_include_directories(load_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
    ${eigen_SOURCE_DIR}
    ${glm_SOURCE_DIR})
set_target_properties(load_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
```

> Every later task appends its new `src/loads/*.cpp` to this `add_executable(load_tests ...)` list.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `N checks, 0 failures`, exit 0

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests include/loads src/loads
git commit -m "Add load_tests target and centralized unit conversion layer"
```

---

### Task 2: Core types + load data model + in-memory serialisation

**Files:**
- Create: `include/loads/LoadTypes.h`, `include/loads/LoadModel.h`, `src/loads/LoadModel.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt` (`load_tests` sources)

**Interfaces:**
- Consumes: `LoadUnits::UnitId` (Task 1).
- Produces: `Loads::LoadId`, `Loads::LoadType`, `Loads::Frame`, `Loads::DirMode`, `Loads::SelectionKind`, `Loads::AnchorStrategy`, `Loads::Magnitude`, `Loads::FaceAnchor`, `Loads::SurfaceRef`, `Loads::TargetSelection`, `Loads::LoadDefinition`, `Loads::serialize(const std::vector<LoadDefinition>&) -> std::string`, `Loads::deserialize(const std::string&, std::vector<LoadDefinition>&) -> bool`.

- [ ] **Step 1: Write the failing test**

Add to `tests/load_tests.cpp` (and call from `main`):
```cpp
#include "loads/LoadModel.h"
using namespace Loads;

static void test_model_roundtrip() {
    std::vector<LoadDefinition> in;

    LoadDefinition f;
    f.id = 1; f.name = "Tip force"; f.type = LoadType::PointForce;
    f.anchorModel = glm::dvec3(1.0, 2.0, 3.0);
    f.dir = glm::dvec3(0.0, 0.0, -1.0);
    f.frame = Frame::World; f.dirMode = DirMode::AxisZ;
    f.magnitude.si = 250.0; f.magnitude.display = LoadUnits::UnitId::Newton;
    f.target.kind = SelectionKind::Point;
    f.target.strategy = AnchorStrategy::OriginalSurface;
    f.target.surfaceRefs.push_back({7, glm::dvec3(0.25, 0.25, 0.5)});
    f.target.distanceTol = 0.01; f.target.normalAngleTolDeg = 30.0;
    in.push_back(f);

    LoadDefinition t;
    t.id = 2; t.name = "Twist"; t.type = LoadType::Torque;
    t.dir = glm::dvec3(0.0, 1.0, 0.0);
    t.magnitude.si = 12.5; t.magnitude.display = LoadUnits::UnitId::NewtonMm;
    t.target.kind = SelectionKind::Faces;
    t.target.strategy = AnchorStrategy::AnchorCloud;
    t.target.anchors.push_back({glm::dvec3(1,0,0), glm::dvec3(1,0,0), 0.5});
    t.target.anchors.push_back({glm::dvec3(1,1,0), glm::dvec3(1,0,0), 0.25});
    t.target.adjacency.push_back({0u, 1u});
    in.push_back(t);

    const std::string blob = serialize(in);
    std::vector<LoadDefinition> out;
    CHECK(deserialize(blob, out));
    CHECK(out.size() == in.size());
    CHECK(out[0].id == 1u);
    CHECK(out[0].name == "Tip force");
    CHECK(out[0].type == LoadType::PointForce);
    CHECK_NEAR(out[0].magnitude.si, 250.0, 1e-12);
    CHECK(out[0].magnitude.display == LoadUnits::UnitId::Newton);
    CHECK_NEAR(out[0].dir.z, -1.0, 1e-12);
    CHECK(out[0].target.surfaceRefs.size() == 1);
    CHECK(out[0].target.surfaceRefs[0].tri == 7u);
    CHECK(out[1].target.anchors.size() == 2);
    CHECK_NEAR(out[1].target.anchors[1].area, 0.25, 1e-12);
    CHECK(out[1].target.adjacency.size() == 1);
    CHECK(out[1].magnitude.display == LoadUnits::UnitId::NewtonMm);

    // serialise(deserialise(x)) is a fixed point
    CHECK(serialize(out) == blob);
    // resolved cache is NOT authoritative and is not persisted
    CHECK(out[0].target.valid == false);
    CHECK(out[0].target.resolvedMeshVersion == 0u);

    std::vector<LoadDefinition> bad;
    CHECK(!deserialize("garbage", bad));
    CHECK(!deserialize("", bad));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadModel.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadTypes.h`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "loads/LoadUnits.h"

namespace Loads {

using LoadId = uint64_t;

enum class LoadType {
    PointForce, Pressure, DistributedForce, Torque, FixedSupport, Gravity
};

// Which basis a stored vector lives in. Independent of how the user picked it.
enum class Frame { World, ObjectLocal };

// How the user chose the direction. Sign lives in the vector, never here:
// "-X" is AxisX with a negated vector, and Reverse is an operation.
enum class DirMode { Free, AxisX, AxisY, AxisZ, SurfaceNormal, Components };

enum class SelectionKind  { Point, Faces, ConnectedRegion, WholeBody };
enum class AnchorStrategy { OriginalSurface, AnchorCloud };

// Beginner default spreads a "point" load over a small patch. SingleNode is the
// advanced escape hatch and carries a mesh-dependence warning.
enum class Distribution { AreaWeighted, SingleNode };

enum class Severity { Info, Warning, Error };

// Structured validation output. Defined here (not in LoadValidation.h) because
// LoadAdapter.h raises issues too and must not depend on the validation module.
struct ValidationIssue {
    Severity    severity = Severity::Info;
    std::string message;     // plain language, classroom-facing
    std::string detail;      // technical, for logs
    uint64_t    loadId = 0;  // 0 = model-wide
    std::string suggestion;  // corrective action
};

// One authoritative SI number. `display` is a presentation tag and never
// participates in computation, so the two can never drift.
struct Magnitude {
    double            si      = 0.0;
    LoadUnits::UnitId display = LoadUnits::UnitId::Newton;
};

const char* toString(LoadType t);

} // namespace Loads
```

`include/loads/LoadModel.h`:
```cpp
#pragma once
#include "loads/LoadTypes.h"

namespace Loads {

// Primary remesh anchor: index into the ORIGINAL surface triangulation plus a
// barycentric coordinate. Stable across a re-tet because TetGen re-runs on the
// same surface.
struct SurfaceRef {
    uint32_t   tri = 0;
    glm::dvec3 bary{0.0};
};

// Fallback anchor for meshes with no usable original surface (G-code toolpath).
struct FaceAnchor {
    glm::dvec3 centroid{0.0};
    glm::dvec3 normal{0.0, 0.0, 1.0};
    double     area = 0.0;
};

// Resolution product. Never authoritative — always rebuildable from anchors.
struct Resolved {
    std::vector<int>      nodes;
    std::vector<uint32_t> faces;      // boundary-face indices
    double                area = 0.0; // model-space units squared
    glm::dvec3            centroid{0.0};
    glm::dvec3            normal{0.0};
};

struct TargetSelection {
    SelectionKind   kind     = SelectionKind::Faces;
    AnchorStrategy  strategy = AnchorStrategy::OriginalSurface;

    std::vector<SurfaceRef> surfaceRefs;                       // primary
    std::vector<FaceAnchor> anchors;                           // fallback
    std::vector<std::pair<uint32_t,uint32_t>> adjacency;       // region regrow

    double distanceTol        = 0.0;   // absolute, model units
    double normalAngleTolDeg  = 30.0;

    Resolved cache;
    uint64_t resolvedMeshVersion = 0;
    bool     valid = false;
};

// Per-type extras, kept as one flat struct so serialisation stays trivial.
struct LoadParams {
    bool   pressureOutward = true;   // Pressure: sign of the normal traction
    bool   lockX = true, lockY = true, lockZ = true;  // FixedSupport
    double densitySI = 0.0;          // Gravity: kg/m^3, 0 = "not supplied"
    bool   densityKnown = false;
};

struct ValidationStatus {
    Severity    severity = Severity::Info;
    bool        ok       = true;
    std::string message;
};

struct LoadDefinition {
    LoadId           id = 0;
    std::string      name;
    LoadType         type = LoadType::PointForce;
    TargetSelection  target;
    glm::dvec3       anchorModel{0.0};   // application point / torque centre
    glm::dvec3       dir{0.0, 0.0, -1.0};// unit: force direction or torque axis
    Frame            frame   = Frame::World;
    DirMode          dirMode = DirMode::Free;
    Magnitude        magnitude;
    bool             enabled = true;
    bool             visible = true;
    Distribution     distribution = Distribution::AreaWeighted;
    LoadParams       params;
    ValidationStatus status;
};

// In-memory round-trip only. NOT a file format: no I/O, no versioned migration
// path, no UI. Exists so tests can prove the model carries all state.
std::string serialize(const std::vector<LoadDefinition>& loads);
bool        deserialize(const std::string& blob, std::vector<LoadDefinition>& out);

} // namespace Loads
```

`src/loads/LoadModel.cpp` — a compact whitespace-delimited text encoding. Strings are length-prefixed so names with spaces survive; doubles use `%.17g` so the round-trip is exact.
```cpp
#include "loads/LoadModel.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Loads {

const char* toString(LoadType t) {
    switch (t) {
        case LoadType::PointForce:       return "Force";
        case LoadType::Pressure:         return "Pressure";
        case LoadType::DistributedForce: return "Distributed force";
        case LoadType::Torque:           return "Torque";
        case LoadType::FixedSupport:     return "Fixed support";
        case LoadType::Gravity:          return "Gravity";
        default:                         return "Load";
    }
}

namespace {

constexpr const char* kMagic = "POLYFEA_LOADS";
constexpr int         kVer   = 1;

void putD(std::ostream& o, double v) {
    o << std::setprecision(17) << std::scientific << v << ' ';
}
void putS(std::ostream& o, const std::string& s) {
    o << s.size() << ' ';
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
    o << ' ';
}
bool getS(std::istream& i, std::string& s) {
    size_t n = 0;
    if (!(i >> n)) return false;
    if (n > (1u << 20)) return false;
    i.get();                       // single delimiting space
    s.assign(n, '\0');
    if (n && !i.read(&s[0], static_cast<std::streamsize>(n))) return false;
    return true;
}
void putV(std::ostream& o, const glm::dvec3& v) { putD(o,v.x); putD(o,v.y); putD(o,v.z); }
bool getV(std::istream& i, glm::dvec3& v) { return static_cast<bool>(i >> v.x >> v.y >> v.z); }

} // namespace

std::string serialize(const std::vector<LoadDefinition>& loads) {
    std::ostringstream o;
    o << kMagic << ' ' << kVer << ' ' << loads.size() << ' ';
    for (const auto& L : loads) {
        o << L.id << ' ';
        putS(o, L.name);
        o << static_cast<int>(L.type)    << ' '
          << static_cast<int>(L.frame)   << ' '
          << static_cast<int>(L.dirMode) << ' '
          << static_cast<int>(L.distribution) << ' '
          << (L.enabled ? 1 : 0) << ' ' << (L.visible ? 1 : 0) << ' ';
        putV(o, L.anchorModel);
        putV(o, L.dir);
        putD(o, L.magnitude.si);
        o << static_cast<int>(L.magnitude.display) << ' ';
        o << (L.params.pressureOutward ? 1 : 0) << ' '
          << (L.params.lockX ? 1 : 0) << ' ' << (L.params.lockY ? 1 : 0) << ' '
          << (L.params.lockZ ? 1 : 0) << ' ';
        putD(o, L.params.densitySI);
        o << (L.params.densityKnown ? 1 : 0) << ' ';

        const TargetSelection& t = L.target;
        o << static_cast<int>(t.kind) << ' ' << static_cast<int>(t.strategy) << ' ';
        putD(o, t.distanceTol);
        putD(o, t.normalAngleTolDeg);
        o << t.surfaceRefs.size() << ' ';
        for (const auto& r : t.surfaceRefs) { o << r.tri << ' '; putV(o, r.bary); }
        o << t.anchors.size() << ' ';
        for (const auto& a : t.anchors) { putV(o,a.centroid); putV(o,a.normal); putD(o,a.area); }
        o << t.adjacency.size() << ' ';
        for (const auto& e : t.adjacency) o << e.first << ' ' << e.second << ' ';
    }
    return o.str();
}

bool deserialize(const std::string& blob, std::vector<LoadDefinition>& out) {
    std::istringstream i(blob);
    std::string magic; int ver = 0; size_t n = 0;
    if (!(i >> magic >> ver >> n))       return false;
    if (magic != kMagic || ver != kVer)  return false;
    if (n > (1u << 20))                  return false;

    std::vector<LoadDefinition> tmp;
    tmp.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        LoadDefinition L;
        int type=0, frame=0, dm=0, dist=0, en=0, vis=0, unit=0;
        int po=0, lx=0, ly=0, lz=0, dk=0, kind=0, strat=0;
        if (!(i >> L.id))                return false;
        if (!getS(i, L.name))            return false;
        if (!(i >> type >> frame >> dm >> dist >> en >> vis)) return false;
        if (!getV(i, L.anchorModel))     return false;
        if (!getV(i, L.dir))             return false;
        if (!(i >> L.magnitude.si >> unit)) return false;
        if (!(i >> po >> lx >> ly >> lz)) return false;
        if (!(i >> L.params.densitySI >> dk)) return false;

        L.type    = static_cast<LoadType>(type);
        L.frame   = static_cast<Frame>(frame);
        L.dirMode = static_cast<DirMode>(dm);
        L.distribution = static_cast<Distribution>(dist);
        L.enabled = (en != 0); L.visible = (vis != 0);
        L.magnitude.display = static_cast<LoadUnits::UnitId>(unit);
        L.params.pressureOutward = (po != 0);
        L.params.lockX = (lx != 0); L.params.lockY = (ly != 0); L.params.lockZ = (lz != 0);
        L.params.densityKnown = (dk != 0);

        TargetSelection& t = L.target;
        if (!(i >> kind >> strat >> t.distanceTol >> t.normalAngleTolDeg)) return false;
        t.kind = static_cast<SelectionKind>(kind);
        t.strategy = static_cast<AnchorStrategy>(strat);

        size_t ns = 0;
        if (!(i >> ns) || ns > (1u << 22)) return false;
        t.surfaceRefs.resize(ns);
        for (auto& r : t.surfaceRefs)
            if (!(i >> r.tri) || !getV(i, r.bary)) return false;

        size_t na = 0;
        if (!(i >> na) || na > (1u << 22)) return false;
        t.anchors.resize(na);
        for (auto& a : t.anchors)
            if (!getV(i,a.centroid) || !getV(i,a.normal) || !(i >> a.area)) return false;

        size_t ne = 0;
        if (!(i >> ne) || ne > (1u << 22)) return false;
        t.adjacency.resize(ne);
        for (auto& e : t.adjacency)
            if (!(i >> e.first >> e.second)) return false;

        // Resolved cache is deliberately not persisted — it is rebuilt from
        // anchors against the current mesh version.
        tmp.push_back(std::move(L));
    }
    out = std::move(tmp);
    return true;
}

} // namespace Loads
```

Add `src/loads/LoadModel.cpp` to the `load_tests` source list in `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add load data model with authoritative SI magnitude and in-memory round-trip"
```

---

### Task 3: Coordinate conversion (`LoadCoords`)

**Files:**
- Create: `include/loads/LoadCoords.h`, `src/loads/LoadCoords.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::Ray`, `Loads::screenToRay`, `Loads::worldToObject`, `Loads::objectToWorld`, `Loads::dirObjectToWorld`, `Loads::dirWorldToObject`, `Loads::metersPerModelUnit(double modelToMM)`, `Loads::dragToDirection`, `Loads::axisVector`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadCoords.h"
#include <glm/gtc/matrix_transform.hpp>

static void test_coords() {
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f/9.0f, 0.1f, 1000.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0,0,5), glm::vec3(0), glm::vec3(0,1,0));

    // Centre pixel -> ray through the origin, pointing -Z.
    Ray r = screenToRay(800.0f, 450.0f, 1600, 900, view, proj);
    CHECK_NEAR(glm::length(r.dir), 1.0, 1e-6);
    CHECK_NEAR(r.dir.z, -1.0, 1e-3);
    CHECK_NEAR(r.origin.x, 0.0, 1e-3);
    CHECK_NEAR(r.origin.y, 0.0, 1e-3);

    // Geometry scale: 1 model unit = modelToMM mm = modelToMM/1000 m.
    CHECK_NEAR(metersPerModelUnit(50.0), 0.05, 1e-15);
    CHECK_NEAR(metersPerModelUnit(1.0),  0.001, 1e-15);

    // Synthetic object transform: 90 deg about +Z maps +X -> +Y.
    const glm::mat4 M = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                                    glm::vec3(0,0,1));
    const glm::dvec3 xObj(1,0,0);
    const glm::dvec3 xWorld = dirObjectToWorld(xObj, M);
    CHECK_NEAR(xWorld.x, 0.0, 1e-9);
    CHECK_NEAR(xWorld.y, 1.0, 1e-9);
    // round-trip
    const glm::dvec3 back = dirWorldToObject(xWorld, M);
    CHECK_NEAR(back.x, 1.0, 1e-9);
    CHECK_NEAR(back.y, 0.0, 1e-9);
    // identity is a no-op (the app's case today)
    const glm::dvec3 id = dirObjectToWorld(xObj, glm::mat4(1.0f));
    CHECK_NEAR(id.x, 1.0, 1e-15);

    // Points translate; directions do not.
    const glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(10,0,0));
    CHECK_NEAR(objectToWorld(glm::dvec3(0,0,0), T).x, 10.0, 1e-9);
    CHECK_NEAR(dirObjectToWorld(glm::dvec3(1,0,0), T).x, 1.0, 1e-9);

    CHECK_NEAR(axisVector(DirMode::AxisZ).z, 1.0, 1e-15);

    // Drag on a plane facing the camera returns a unit vector.
    glm::dvec3 out(0.0);
    const bool ok = dragToDirection(glm::dvec3(0,0,0), 800.0f, 450.0f, 900.0f, 450.0f,
                                    1600, 900, view, proj, out);
    CHECK(ok);
    CHECK_NEAR(glm::length(out), 1.0, 1e-9);
    CHECK(out.x > 0.5);                       // dragged right -> +X

    // Zero-length drag is rejected, leaving the caller's value untouched.
    glm::dvec3 keep(0.0, 0.0, -1.0);
    CHECK(!dragToDirection(glm::dvec3(0,0,0), 800.0f, 450.0f, 800.0f, 450.0f,
                           1600, 900, view, proj, keep));
    CHECK_NEAR(keep.z, -1.0, 1e-15);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadCoords.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadCoords.h`:
```cpp
#pragma once
#include "loads/LoadTypes.h"

namespace Loads {

struct Ray {
    glm::dvec3 origin{0.0};
    glm::dvec3 dir{0.0, 0.0, -1.0};   // normalised
};

// Metres per model-space unit. THE single geometry-scale conversion; both the
// load adapter and FEASolver must call this and nothing else.
inline double metersPerModelUnit(double modelToMM) { return modelToMM / 1000.0; }

Ray screenToRay(float px, float py, int screenW, int screenH,
                const glm::mat4& view, const glm::mat4& proj);

// `M` is the object->world matrix. The app passes identity today; tests pass
// synthetic matrices. Points are affine, directions ignore translation.
glm::dvec3 objectToWorld   (const glm::dvec3& p, const glm::mat4& M);
glm::dvec3 worldToObject   (const glm::dvec3& p, const glm::mat4& M);
glm::dvec3 dirObjectToWorld(const glm::dvec3& d, const glm::mat4& M);
glm::dvec3 dirWorldToObject(const glm::dvec3& d, const glm::mat4& M);

glm::dvec3 axisVector(DirMode m);   // AxisX/Y/Z -> unit basis; else (0,0,0)

// Projects a screen drag onto a plane through `anchor` and returns a unit
// direction. Returns false (leaving `outDir` untouched) when the drag is
// degenerate or the resulting vector has no length.
bool dragToDirection(const glm::dvec3& anchor,
                     float x0, float y0, float x1, float y1,
                     int screenW, int screenH,
                     const glm::mat4& view, const glm::mat4& proj,
                     glm::dvec3& outDir);

} // namespace Loads
```

`src/loads/LoadCoords.cpp`:
```cpp
#include "loads/LoadCoords.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>

namespace Loads {

Ray screenToRay(float px, float py, int screenW, int screenH,
                const glm::mat4& view, const glm::mat4& proj) {
    Ray r;
    if (screenW <= 0 || screenH <= 0) return r;

    // Pixel -> NDC. Y flips: pixels grow downward, NDC grows upward.
    const float ndcX =  (2.0f * px) / static_cast<float>(screenW) - 1.0f;
    const float ndcY = 1.0f - (2.0f * py) / static_cast<float>(screenH);

    const glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 pNear = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 pFar  = inv * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    if (std::fabs(pNear.w) < 1e-20f || std::fabs(pFar.w) < 1e-20f) return r;
    pNear /= pNear.w;
    pFar  /= pFar.w;

    const glm::dvec3 a(pNear), b(pFar);
    const glm::dvec3 d = b - a;
    const double len = glm::length(d);
    r.origin = a;
    r.dir    = (len > 1e-20) ? d / len : glm::dvec3(0.0, 0.0, -1.0);
    return r;
}

glm::dvec3 objectToWorld(const glm::dvec3& p, const glm::mat4& M) {
    const glm::vec4 w = M * glm::vec4(static_cast<glm::vec3>(p), 1.0f);
    return glm::dvec3(w);
}
glm::dvec3 worldToObject(const glm::dvec3& p, const glm::mat4& M) {
    const glm::vec4 o = glm::inverse(M) * glm::vec4(static_cast<glm::vec3>(p), 1.0f);
    return glm::dvec3(o);
}
glm::dvec3 dirObjectToWorld(const glm::dvec3& d, const glm::mat4& M) {
    // w = 0 drops translation. Non-uniform scale is not used by this app; if it
    // ever is, this must become the inverse-transpose.
    const glm::vec4 w = M * glm::vec4(static_cast<glm::vec3>(d), 0.0f);
    return glm::dvec3(w);
}
glm::dvec3 dirWorldToObject(const glm::dvec3& d, const glm::mat4& M) {
    const glm::vec4 o = glm::inverse(M) * glm::vec4(static_cast<glm::vec3>(d), 0.0f);
    return glm::dvec3(o);
}

glm::dvec3 axisVector(DirMode m) {
    switch (m) {
        case DirMode::AxisX: return glm::dvec3(1,0,0);
        case DirMode::AxisY: return glm::dvec3(0,1,0);
        case DirMode::AxisZ: return glm::dvec3(0,0,1);
        default:             return glm::dvec3(0,0,0);
    }
}

namespace {
// Intersect a ray with the plane (point p0, unit normal n). Returns false when
// the ray is near-parallel to the plane, which is exactly the unstable case the
// caller must fall back from.
bool rayPlane(const Ray& r, const glm::dvec3& p0, const glm::dvec3& n,
              glm::dvec3& hit) {
    const double denom = glm::dot(n, r.dir);
    if (std::fabs(denom) < 1e-6) return false;
    const double t = glm::dot(n, p0 - r.origin) / denom;
    if (!std::isfinite(t)) return false;
    hit = r.origin + r.dir * t;
    return true;
}
} // namespace

bool dragToDirection(const glm::dvec3& anchor,
                     float x0, float y0, float x1, float y1,
                     int screenW, int screenH,
                     const glm::mat4& view, const glm::mat4& proj,
                     glm::dvec3& outDir) {
    if (std::fabs(x1 - x0) < 1e-4f && std::fabs(y1 - y0) < 1e-4f) return false;

    // Camera basis from the view matrix rows.
    const glm::dvec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::dvec3 camUp   (view[0][1], view[1][1], view[2][1]);
    const glm::dvec3 camFwd  (-view[0][2], -view[1][2], -view[2][2]);

    // Prefer the plane facing the camera; if the ray grazes it (camera nearly
    // in-plane) fall back to the best-conditioned remaining camera-basis plane.
    const glm::dvec3 candidates[3] = { camFwd, camUp, camRight };

    const Ray r0 = screenToRay(x0, y0, screenW, screenH, view, proj);
    const Ray r1 = screenToRay(x1, y1, screenW, screenH, view, proj);

    for (const glm::dvec3& n : candidates) {
        glm::dvec3 h0, h1;
        if (!rayPlane(r0, anchor, n, h0)) continue;
        if (!rayPlane(r1, anchor, n, h1)) continue;
        const glm::dvec3 d = h1 - h0;
        const double len = glm::length(d);
        if (len < 1e-12) continue;
        outDir = d / len;
        return true;
    }
    return false;
}

} // namespace Loads
```

Add `src/loads/LoadCoords.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add LoadCoords: explicit-matrix coordinate transforms and stable drag-to-direction"
```

---

### Task 4: Boundary surface extraction + BVH

**Files:**
- Create: `include/loads/MeshSurface.h`, `src/loads/MeshSurface.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::BoundaryFace`, `Loads::SurfaceCache`, `SurfaceCache::build(positions, tetrahedra, tetrahedraQuadratic, hasQuadratic, edgeToMidNode)`, `SurfaceCache::raycast(Ray, hit) -> bool`, `SurfaceCache::faceArea(i)`, `SurfaceCache::faceNormal(i)`, `SurfaceCache::faceCentroid(i)`, `SurfaceCache::adjacency()`, `Loads::rayTriangle(...)`.

A `BoundaryFace` carries the 3 corner node indices *and* the 3 midside node indices (`-1` on a Tet4 mesh) so Task 7 can lump quadratic loads without re-deriving connectivity.

- [ ] **Step 1: Write the failing test**

Build a unit cube from 6 tets (the canonical Freudenthal split of a cube's 8 corners) and assert the boundary has 12 triangles totalling area 6.

```cpp
#include "loads/MeshSurface.h"

// 8 corners of the unit cube [0,1]^3, indexed  i = x + 2y + 4z.
static void unitCubeMesh(std::vector<glm::vec3>& P, std::vector<unsigned>& T) {
    P = { {0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1} };
    // Freudenthal 6-tet split along the 0-7 diagonal.
    const unsigned t[6][4] = {
        {0,1,3,7},{0,1,7,5},{0,5,7,4},{0,3,2,7},{0,2,6,7},{0,6,4,7} };
    T.clear();
    for (auto& e : t) for (int k = 0; k < 4; ++k) T.push_back(e[k]);
}

static void test_surface_cache() {
    std::vector<glm::vec3> P; std::vector<unsigned> T;
    unitCubeMesh(P, T);

    SurfaceCache sc;
    std::map<std::pair<unsigned,unsigned>,unsigned> noMid;
    sc.build(P, T, {}, false, noMid);

    CHECK(sc.faceCount() == 12);            // 6 quad sides x 2 triangles
    double total = 0.0;
    for (size_t i = 0; i < sc.faceCount(); ++i) total += sc.faceArea(i);
    CHECK_NEAR(total, 6.0, 1e-9);           // unit cube surface area

    // Tet4 mesh: no midside nodes recorded.
    CHECK(sc.face(0).mid[0] == -1);

    // Every boundary normal points outward: centroid + eps*n leaves the cube.
    for (size_t i = 0; i < sc.faceCount(); ++i) {
        const glm::dvec3 c = sc.faceCentroid(i);
        const glm::dvec3 p = c + sc.faceNormal(i) * 1e-3;
        const bool outside = p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1 ||
                             p.z < 0 || p.z > 1;
        CHECK(outside);
    }

    // Ray straight down the +Z face from above hits at z = 1.
    Ray r; r.origin = glm::dvec3(0.5, 0.5, 5.0); r.dir = glm::dvec3(0,0,-1);
    SurfaceHit h;
    CHECK(sc.raycast(r, h));
    CHECK_NEAR(h.point.z, 1.0, 1e-9);
    CHECK_NEAR(h.normal.z, 1.0, 1e-9);      // nearest hit is the TOP face
    CHECK(h.face < sc.faceCount());

    // A miss stays a miss.
    Ray m; m.origin = glm::dvec3(5.0, 5.0, 5.0); m.dir = glm::dvec3(0,0,-1);
    SurfaceHit hm;
    CHECK(!sc.raycast(m, hm));

    // BVH agrees with brute force on many random rays.
    int agree = 0, tested = 0;
    for (int i = 0; i < 200; ++i) {
        Ray q;
        const double a = i * 0.031, b = i * 0.017;
        q.origin = glm::dvec3(0.5 + 3.0*std::cos(a), 0.5 + 3.0*std::sin(a),
                              0.5 + 2.0*std::sin(b));
        q.dir = glm::normalize(glm::dvec3(0.5,0.5,0.5) - q.origin);
        SurfaceHit hb, hf;
        const bool okB = sc.raycast(q, hb);
        const bool okF = sc.raycastBruteForce(q, hf);
        ++tested;
        if (okB == okF && (!okB || std::fabs(hb.t - hf.t) < 1e-9)) ++agree;
    }
    CHECK(agree == tested);

    // Adjacency: every boundary triangle of a closed surface has 3 neighbours.
    const auto& adj = sc.adjacency();
    CHECK(adj.size() == sc.faceCount());
    for (const auto& nb : adj) CHECK(nb.size() == 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/MeshSurface.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/MeshSurface.h`:
```cpp
#pragma once
#include "loads/LoadCoords.h"
#include <map>
#include <utility>

namespace Loads {

// A boundary triangle of the volumetric mesh. `mid` holds the midside node of
// edges (0-1, 1-2, 2-0) on a Tet10 mesh, or -1 on Tet4. Geometry is
// straight-sided either way, so picking uses the corners and only load lumping
// needs the midsides.
struct BoundaryFace {
    int corner[3] = {-1,-1,-1};
    int mid[3]    = {-1,-1,-1};
};

struct SurfaceHit {
    size_t     face = 0;
    double     t    = 0.0;      // ray parameter
    glm::dvec3 point{0.0};
    glm::dvec3 normal{0.0};
    glm::dvec3 bary{0.0};       // barycentric within the corner triangle
    int        nearestNode = -1;
};

bool rayTriangle(const Ray& r, const glm::dvec3& a, const glm::dvec3& b,
                 const glm::dvec3& c, double& tOut, glm::dvec3& baryOut);

// Boundary extraction + median-split BVH, cached against a mesh version by the
// caller. Rebuild only when FEAModel::meshVersion changes.
class SurfaceCache {
public:
    void build(const std::vector<glm::vec3>& positions,
               const std::vector<unsigned>&  tetrahedra,
               const std::vector<unsigned>&  tetrahedraQuadratic,
               bool                          hasQuadratic,
               const std::map<std::pair<unsigned,unsigned>,unsigned>& edgeToMidNode);

    size_t              faceCount() const { return m_faces.size(); }
    const BoundaryFace& face(size_t i) const { return m_faces[i]; }
    const std::vector<BoundaryFace>& faces() const { return m_faces; }

    glm::dvec3 vertex(int node) const { return m_pos[static_cast<size_t>(node)]; }
    double     faceArea(size_t i) const     { return m_area[i]; }
    glm::dvec3 faceNormal(size_t i) const   { return m_normal[i]; }
    glm::dvec3 faceCentroid(size_t i) const { return m_centroid[i]; }

    // adjacency()[f] = faces sharing an edge with f.
    const std::vector<std::vector<uint32_t>>& adjacency() const { return m_adj; }

    bool raycast(const Ray& r, SurfaceHit& out) const;
    bool raycastBruteForce(const Ray& r, SurfaceHit& out) const;  // test oracle

private:
    struct Node { glm::dvec3 lo{0.0}, hi{0.0}; int left = -1, right = -1;
                  uint32_t first = 0, count = 0; };

    void buildBVH();
    int  buildNode(uint32_t first, uint32_t count, int depth);
    bool traverse(int node, const Ray& r, const glm::dvec3& invDir,
                  SurfaceHit& best, bool& found) const;
    bool hitFace(size_t f, const Ray& r, SurfaceHit& out) const;

    std::vector<glm::dvec3>      m_pos;
    std::vector<BoundaryFace>    m_faces;
    std::vector<double>          m_area;
    std::vector<glm::dvec3>      m_normal, m_centroid;
    std::vector<std::vector<uint32_t>> m_adj;
    std::vector<uint32_t>        m_index;   // BVH leaf ordering
    std::vector<Node>            m_bvh;
};

} // namespace Loads
```

`src/loads/MeshSurface.cpp` — key logic:

```cpp
#include "loads/MeshSurface.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Loads {

bool rayTriangle(const Ray& r, const glm::dvec3& a, const glm::dvec3& b,
                 const glm::dvec3& c, double& tOut, glm::dvec3& baryOut) {
    // Moller-Trumbore, double sided (the student may orbit behind a face).
    const glm::dvec3 e1 = b - a, e2 = c - a;
    const glm::dvec3 p  = glm::cross(r.dir, e2);
    const double det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-18) return false;
    const double inv = 1.0 / det;
    const glm::dvec3 tv = r.origin - a;
    const double u = glm::dot(tv, p) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return false;
    const glm::dvec3 q = glm::cross(tv, e1);
    const double v = glm::dot(r.dir, q) * inv;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) return false;
    const double t = glm::dot(e2, q) * inv;
    if (t < 1e-9) return false;
    tOut = t;
    baryOut = glm::dvec3(1.0 - u - v, u, v);
    return true;
}

void SurfaceCache::build(
    const std::vector<glm::vec3>& positions,
    const std::vector<unsigned>&  tetrahedra,
    const std::vector<unsigned>&  tetrahedraQuadratic,
    bool                          hasQuadratic,
    const std::map<std::pair<unsigned,unsigned>,unsigned>& edgeToMidNode)
{
    m_pos.assign(positions.begin(), positions.end());
    m_faces.clear(); m_area.clear(); m_normal.clear();
    m_centroid.clear(); m_adj.clear(); m_bvh.clear(); m_index.clear();

    const size_t nElems = tetrahedra.size() / 4;
    // Same technique as FEASolver::buildConsistentLoadVector: a face used by
    // exactly one tet is on the boundary. Keep the FIRST occurrence's winding
    // so the outward orientation can be fixed up against the opposite vertex.
    struct Rec { std::array<int,3> tri; int opposite; int count; };
    std::map<std::array<int,3>, Rec> faceMap;
    const int tetFaces[4][3] = {{1,2,3},{0,2,3},{0,1,3},{0,1,2}};
    const int oppVert[4]     = {0, 1, 2, 3};

    for (size_t el = 0; el < nElems; ++el) {
        int v[4];
        for (int k = 0; k < 4; ++k) v[k] = static_cast<int>(tetrahedra[el*4+k]);
        for (int f = 0; f < 4; ++f) {
            std::array<int,3> tri = { v[tetFaces[f][0]], v[tetFaces[f][1]],
                                      v[tetFaces[f][2]] };
            std::array<int,3> key = tri;
            std::sort(key.begin(), key.end());
            auto it = faceMap.find(key);
            if (it == faceMap.end()) faceMap.emplace(key, Rec{tri, v[oppVert[f]], 1});
            else                     ++it->second.count;
        }
    }

    auto midOf = [&](int a, int b) -> int {
        if (!hasQuadratic) return -1;
        const unsigned lo = static_cast<unsigned>(std::min(a,b));
        const unsigned hi = static_cast<unsigned>(std::max(a,b));
        auto it = edgeToMidNode.find({lo, hi});
        return (it == edgeToMidNode.end()) ? -1 : static_cast<int>(it->second);
    };

    for (const auto& kv : faceMap) {
        if (kv.second.count != 1) continue;             // interior face
        std::array<int,3> t = kv.second.tri;

        const glm::dvec3 a = m_pos[t[0]], b = m_pos[t[1]], c = m_pos[t[2]];
        glm::dvec3 n = glm::cross(b - a, c - a);
        const double len2 = glm::length(n);
        if (len2 < 1e-20) continue;                     // degenerate sliver
        // Orient outward: away from the tet's 4th vertex.
        const glm::dvec3 opp = m_pos[kv.second.opposite];
        if (glm::dot(n, a - opp) < 0.0) { std::swap(t[1], t[2]); n = -n; }

        BoundaryFace bf;
        bf.corner[0] = t[0]; bf.corner[1] = t[1]; bf.corner[2] = t[2];
        bf.mid[0] = midOf(t[0], t[1]);
        bf.mid[1] = midOf(t[1], t[2]);
        bf.mid[2] = midOf(t[2], t[0]);

        m_faces.push_back(bf);
        m_area.push_back(0.5 * len2);
        m_normal.push_back(glm::normalize(n));
        m_centroid.push_back((m_pos[t[0]] + m_pos[t[1]] + m_pos[t[2]]) / 3.0);
    }

    // Edge -> faces, then face -> face adjacency.
    std::map<std::pair<int,int>, std::vector<uint32_t>> edgeMap;
    for (uint32_t f = 0; f < m_faces.size(); ++f)
        for (int e = 0; e < 3; ++e) {
            int a = m_faces[f].corner[e], b = m_faces[f].corner[(e+1)%3];
            if (a > b) std::swap(a, b);
            edgeMap[{a,b}].push_back(f);
        }
    m_adj.assign(m_faces.size(), {});
    for (const auto& kv : edgeMap)
        for (uint32_t f : kv.second)
            for (uint32_t g : kv.second)
                if (f != g) m_adj[f].push_back(g);
    for (auto& v : m_adj) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }

    buildBVH();
}
```

Implement `buildBVH`/`buildNode` as a median split on the widest axis of the
centroid bounds, leaf size 8, recursion depth cap 48; `traverse` as a slab test
against `Node.lo/hi` using `invDir`, descending nearest-child-first and pruning
on `best.t`. `hitFace(f, r, out)` calls `rayTriangle` on the three corners and
fills `point`, `normal = m_normal[f]`, `bary`, and `nearestNode` (the corner with
the largest barycentric weight). `raycastBruteForce` loops every face and keeps
the smallest `t` — it exists purely as the test oracle above.

Add `src/loads/MeshSurface.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add boundary-face extraction with outward normals, adjacency and BVH raycast"
```

---

### Task 5: Shape functions, consistent loads and tributary weights

**Files:**
- Create: `include/loads/FaceIntegration.h`, `src/loads/FaceIntegration.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::BoundaryFace`, `Loads::SurfaceCache` (Task 4).
- Produces: `Loads::NodeWeight`, `Loads::consistentFaceWeights(face, area, isQuadratic, out)`, `Loads::tributaryFaceWeights(face, area, isQuadratic, out)`.

This task encodes the two corrections that matter most: quadratic consistent
lumping, and the fact that tributary weights are a *different* quantity that
must be strictly positive.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/FaceIntegration.h"

static void test_face_integration() {
    const double A = 2.0;

    BoundaryFace lin;  lin.corner[0]=0; lin.corner[1]=1; lin.corner[2]=2;
    BoundaryFace quad = lin;
    quad.mid[0]=3; quad.mid[1]=4; quad.mid[2]=5;

    std::vector<NodeWeight> w;

    // --- Tet4 (T3) consistent: A/3 at each corner ---
    consistentFaceWeights(lin, A, false, w);
    CHECK(w.size() == 3);
    double sum = 0.0; for (auto& e : w) { CHECK_NEAR(e.w, A/3.0, 1e-12); sum += e.w; }
    CHECK_NEAR(sum, A, 1e-12);

    // --- Tet10 (T6) consistent: 0 at corners, A/3 at midsides ---
    consistentFaceWeights(quad, A, true, w);
    CHECK(w.size() == 6);
    sum = 0.0;
    for (auto& e : w) {
        sum += e.w;
        const bool isCorner = (e.node <= 2);
        if (isCorner) CHECK_NEAR(e.w, 0.0,     1e-12);
        else          CHECK_NEAR(e.w, A/3.0,   1e-12);
    }
    CHECK_NEAR(sum, A, 1e-12);   // partition of unity: sum(int Ni dA) = A

    // --- Tributary weights are a DIFFERENT quantity: strictly positive ---
    tributaryFaceWeights(quad, A, true, w);
    CHECK(w.size() == 6);
    sum = 0.0;
    for (auto& e : w) {
        CHECK(e.w > 0.0);                       // never zero: no 1/w blowup
        sum += e.w;
        if (e.node <= 2) CHECK_NEAR(e.w, A/12.0, 1e-12);  // corner
        else             CHECK_NEAR(e.w, A/4.0,  1e-12);  // midside
    }
    CHECK_NEAR(sum, A, 1e-12);

    tributaryFaceWeights(lin, A, false, w);
    CHECK(w.size() == 3);
    sum = 0.0;
    for (auto& e : w) { CHECK(e.w > 0.0); CHECK_NEAR(e.w, A/3.0, 1e-12); sum += e.w; }
    CHECK_NEAR(sum, A, 1e-12);

    // A Tet10 face missing its midside nodes degrades to the linear rule rather
    // than emitting node -1.
    BoundaryFace broken = lin;
    consistentFaceWeights(broken, A, true, w);
    CHECK(w.size() == 3);
    for (auto& e : w) CHECK(e.node >= 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/FaceIntegration.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/FaceIntegration.h`:
```cpp
#pragma once
#include "loads/MeshSurface.h"

namespace Loads {

struct NodeWeight { int node = -1; double w = 0.0; };

// Consistent (Galerkin) weights: w_i = integral of N_i over the face.
// Converts a uniform distributed traction into nodal forces.
//   T3: A/3 per corner.
//   T6: 0 per corner, A/3 per midside  (because int L_i(2L_i - 1) dA = 0).
// Sums to A in both cases. Falls back to the T3 rule when a face claims to be
// quadratic but has no midside nodes recorded.
void consistentFaceWeights(const BoundaryFace& f, double area, bool isQuadratic,
                           std::vector<NodeWeight>& out);

// Tributary weights: STRICTLY POSITIVE area shares used to weight constrained
// optimisation (the torque solve). NOT interchangeable with the consistent
// weights — those are zero at T6 corners, which would divide by zero in a
// 1/w objective and pin corner forces to zero.
//   T3: A/3 per corner.
//   T6: split the face into 4 medial sub-triangles (each A/4) and give each
//       sub-triangle's area equally to its 3 vertices ->
//       A/12 per corner, A/4 per midside. Sums to A.
void tributaryFaceWeights(const BoundaryFace& f, double area, bool isQuadratic,
                          std::vector<NodeWeight>& out);

} // namespace Loads
```

`src/loads/FaceIntegration.cpp`:
```cpp
#include "loads/FaceIntegration.h"

namespace Loads {

namespace {
bool hasMidsides(const BoundaryFace& f) {
    return f.mid[0] >= 0 && f.mid[1] >= 0 && f.mid[2] >= 0;
}
} // namespace

void consistentFaceWeights(const BoundaryFace& f, double area, bool isQuadratic,
                           std::vector<NodeWeight>& out) {
    out.clear();
    if (isQuadratic && hasMidsides(f)) {
        // Area coordinates L1,L2,L3 with  int L_i dA = A/3,
        // int L_i^2 dA = A/6,  int L_i L_j dA = A/12.
        //   corner  N_i = L_i(2L_i - 1) -> 2(A/6) - A/3 = 0
        //   midside N_k = 4 L_i L_j     -> 4(A/12)      = A/3
        for (int i = 0; i < 3; ++i) out.push_back({f.corner[i], 0.0});
        for (int i = 0; i < 3; ++i) out.push_back({f.mid[i],    area / 3.0});
    } else {
        for (int i = 0; i < 3; ++i) out.push_back({f.corner[i], area / 3.0});
    }
}

void tributaryFaceWeights(const BoundaryFace& f, double area, bool isQuadratic,
                          std::vector<NodeWeight>& out) {
    out.clear();
    if (isQuadratic && hasMidsides(f)) {
        // Medial subdivision: 4 sub-triangles of area A/4, each contributing
        // A/12 to its 3 vertices. A corner sits in 1 sub-triangle (A/12); a
        // midside sits in 3 (3 * A/12 = A/4). Total 3(A/12) + 3(A/4) = A.
        for (int i = 0; i < 3; ++i) out.push_back({f.corner[i], area / 12.0});
        for (int i = 0; i < 3; ++i) out.push_back({f.mid[i],    area / 4.0});
    } else {
        for (int i = 0; i < 3; ++i) out.push_back({f.corner[i], area / 3.0});
    }
}

} // namespace Loads
```

Add `src/loads/FaceIntegration.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add Tet4/Tet10 consistent load lumping and strictly-positive tributary weights"
```

---

### Task 6: Selection operations (`MeshPick`)

**Files:**
- Create: `include/loads/MeshPick.h`, `src/loads/MeshPick.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::SurfaceCache`, `Loads::Ray` (Tasks 3–4).
- Produces: `Loads::FaceSet`, `Loads::pickFace`, `Loads::pickNode`, `Loads::toggleFace`, `Loads::growConnectedRegion`, `Loads::wholeBodyFaces`, `Loads::patchAroundPoint`, `Loads::faceSetArea`, `Loads::faceSetCentroid`, `Loads::faceSetNormal`, `Loads::faceSetNodes`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/MeshPick.h"

static void test_pick_and_select() {
    std::vector<glm::vec3> P; std::vector<unsigned> T;
    unitCubeMesh(P, T);                          // from Task 4
    SurfaceCache sc;
    std::map<std::pair<unsigned,unsigned>,unsigned> noMid;
    sc.build(P, T, {}, false, noMid);

    // Click the centre of the top face from directly above.
    Ray r; r.origin = glm::dvec3(0.5,0.5,5.0); r.dir = glm::dvec3(0,0,-1);
    SurfaceHit h;
    CHECK(pickFace(sc, r, h));
    CHECK_NEAR(h.normal.z, 1.0, 1e-9);

    // Connected region at a tight angle tolerance stays on the flat top face:
    // 2 coplanar triangles, area 1, normal +Z.
    FaceSet top = growConnectedRegion(sc, h.face, 15.0);
    CHECK(top.size() == 2);
    CHECK_NEAR(faceSetArea(sc, top), 1.0, 1e-9);
    CHECK_NEAR(faceSetNormal(sc, top).z, 1.0, 1e-9);
    CHECK_NEAR(faceSetCentroid(sc, top).z, 1.0, 1e-9);

    // A 90-degree tolerance leaks around the cube edges and takes everything.
    FaceSet all = growConnectedRegion(sc, h.face, 95.0);
    CHECK(all.size() == sc.faceCount());
    CHECK_NEAR(faceSetArea(sc, all), 6.0, 1e-9);

    // Whole body.
    CHECK(wholeBodyFaces(sc).size() == sc.faceCount());

    // Shift-click semantics: toggle adds, toggling again removes.
    FaceSet s;
    toggleFace(s, 3); CHECK(s.size() == 1);
    toggleFace(s, 7); CHECK(s.size() == 2);
    toggleFace(s, 3); CHECK(s.size() == 1);
    CHECK(s.count(7) == 1);

    // Node pick returns a real cube corner nearest the hit.
    Ray rc; rc.origin = glm::dvec3(0.02,0.02,5.0); rc.dir = glm::dvec3(0,0,-1);
    SurfaceHit hc;
    CHECK(pickFace(sc, rc, hc));
    const int n = pickNode(sc, hc);
    CHECK(n >= 0);
    const glm::dvec3 p = sc.vertex(n);
    CHECK_NEAR(p.x, 0.0, 1e-9); CHECK_NEAR(p.y, 0.0, 1e-9); CHECK_NEAR(p.z, 1.0, 1e-9);

    // Localised patch: radius 0 still yields the hit face (never empty), and a
    // generous radius on the top plane cannot exceed that face's own region.
    FaceSet tiny = patchAroundPoint(sc, h, 0.0, 15.0);
    CHECK(tiny.size() >= 1);
    FaceSet patch = patchAroundPoint(sc, h, 10.0, 15.0);
    CHECK(patch.size() == 2);

    // Node list of the top face set: the 4 corners at z = 1.
    const std::vector<int> nodes = faceSetNodes(sc, top);
    CHECK(nodes.size() == 4);
    for (int id : nodes) CHECK_NEAR(sc.vertex(id).z, 1.0, 1e-9);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/MeshPick.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/MeshPick.h`:
```cpp
#pragma once
#include "loads/MeshSurface.h"
#include <set>

namespace Loads {

// Ordered so selections serialise and compare deterministically.
using FaceSet = std::set<uint32_t>;

// Section view: when `enabled`, geometry below `zModel` is clipped in the
// shader, so it must not be pickable either — a student can only select what
// they can actually see. Mirrors FEAModel::sectionEnabled / sectionZModel.
struct SectionClip {
    bool   enabled = false;
    double zModel  = 0.0;
};

bool pickFace(const SurfaceCache& sc, const Ray& r, SurfaceHit& out,
              const SectionClip& clip = {});
int  pickNode(const SurfaceCache& sc, const SurfaceHit& hit);

void toggleFace(FaceSet& s, uint32_t face);   // shift-click add/remove

// Flood fill across shared edges while the neighbour's normal stays within
// `maxAngleDeg` of the SEED normal. Comparing against the seed (not the walking
// front) stops a gradual curve from wrapping the whole body.
FaceSet growConnectedRegion(const SurfaceCache& sc, uint32_t seed, double maxAngleDeg);

FaceSet wholeBodyFaces(const SurfaceCache& sc);

// Localised patch for the beginner "point" force: faces within `radius` of the
// hit point, angle-limited, always including the hit face itself.
FaceSet patchAroundPoint(const SurfaceCache& sc, const SurfaceHit& hit,
                         double radius, double maxAngleDeg);

double     faceSetArea    (const SurfaceCache& sc, const FaceSet& s);
glm::dvec3 faceSetCentroid(const SurfaceCache& sc, const FaceSet& s);
glm::dvec3 faceSetNormal  (const SurfaceCache& sc, const FaceSet& s); // area-weighted, unit
std::vector<int> faceSetNodes(const SurfaceCache& sc, const FaceSet& s); // corners + midsides

} // namespace Loads
```

`src/loads/MeshPick.cpp` — implementation notes with the non-obvious parts spelled out:
```cpp
#include "loads/MeshPick.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace Loads {

bool pickFace(const SurfaceCache& sc, const Ray& r, SurfaceHit& out,
              const SectionClip& clip) {
    if (!sc.raycast(r, out)) return false;
    // Reject a hit on geometry the section view has cut away, then retry past
    // it so the student can pick the newly exposed interior surface.
    if (clip.enabled && out.point.z < clip.zModel) {
        Ray next = r;
        next.origin = out.point + r.dir * 1e-6;
        return pickFace(sc, next, out, clip);
    }
    return true;
}

int pickNode(const SurfaceCache& sc, const SurfaceHit& hit) {
    return hit.nearestNode;   // corner with the largest barycentric weight
}

void toggleFace(FaceSet& s, uint32_t face) {
    auto it = s.find(face);
    if (it == s.end()) s.insert(face); else s.erase(it);
}

FaceSet growConnectedRegion(const SurfaceCache& sc, uint32_t seed, double maxAngleDeg) {
    FaceSet out;
    if (seed >= sc.faceCount()) return out;
    const glm::dvec3 nSeed = sc.faceNormal(seed);
    const double cosTol = std::cos(maxAngleDeg * 3.14159265358979323846 / 180.0);

    std::queue<uint32_t> q;
    q.push(seed); out.insert(seed);
    while (!q.empty()) {
        const uint32_t f = q.front(); q.pop();
        for (uint32_t g : sc.adjacency()[f]) {
            if (out.count(g)) continue;
            if (glm::dot(sc.faceNormal(g), nSeed) < cosTol) continue;
            out.insert(g);
            q.push(g);
        }
    }
    return out;
}

FaceSet wholeBodyFaces(const SurfaceCache& sc) {
    FaceSet s;
    for (uint32_t f = 0; f < sc.faceCount(); ++f) s.insert(f);
    return s;
}

FaceSet patchAroundPoint(const SurfaceCache& sc, const SurfaceHit& hit,
                         double radius, double maxAngleDeg) {
    FaceSet region = growConnectedRegion(sc, static_cast<uint32_t>(hit.face),
                                         maxAngleDeg);
    FaceSet out;
    for (uint32_t f : region)
        if (glm::length(sc.faceCentroid(f) - hit.point) <= radius) out.insert(f);
    out.insert(static_cast<uint32_t>(hit.face));   // never empty
    return out;
}

double faceSetArea(const SurfaceCache& sc, const FaceSet& s) {
    double a = 0.0;
    for (uint32_t f : s) a += sc.faceArea(f);
    return a;
}

glm::dvec3 faceSetCentroid(const SurfaceCache& sc, const FaceSet& s) {
    glm::dvec3 c(0.0); double a = 0.0;
    for (uint32_t f : s) { const double w = sc.faceArea(f);
                           c += sc.faceCentroid(f) * w; a += w; }
    return (a > 0.0) ? c / a : glm::dvec3(0.0);
}

glm::dvec3 faceSetNormal(const SurfaceCache& sc, const FaceSet& s) {
    glm::dvec3 n(0.0);
    for (uint32_t f : s) n += sc.faceNormal(f) * sc.faceArea(f);
    const double L = glm::length(n);
    return (L > 1e-15) ? n / L : glm::dvec3(0.0);
}

std::vector<int> faceSetNodes(const SurfaceCache& sc, const FaceSet& s) {
    std::vector<int> v;
    for (uint32_t f : s) {
        const BoundaryFace& bf = sc.face(f);
        for (int i = 0; i < 3; ++i) {
            v.push_back(bf.corner[i]);
            if (bf.mid[i] >= 0) v.push_back(bf.mid[i]);
        }
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

} // namespace Loads
```

Add `src/loads/MeshPick.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add mesh picking and face/region/body selection operations"
```

---

### Task 7: Selection resolution and remesh re-resolution

**Files:**
- Create: `include/loads/LoadResolve.h`, `src/loads/LoadResolve.cpp`
- Modify: `include/FEAModel.h` (add `meshVersion`), `src/FEAModel.cpp` (bump it), `src/SlabMesher.cpp` (bump it), `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::TargetSelection`, `Loads::SurfaceCache`, `Loads::FaceSet`.
- Produces: `Loads::ResolveContext`, `Loads::captureSelection`, `Loads::resolveSelection`, `Loads::ResolveResult`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadResolve.h"

static void test_resolve_and_remesh() {
    std::vector<glm::vec3> P; std::vector<unsigned> T;
    unitCubeMesh(P, T);
    SurfaceCache sc;
    std::map<std::pair<unsigned,unsigned>,unsigned> noMid;
    sc.build(P, T, {}, false, noMid);

    Ray r; r.origin = glm::dvec3(0.5,0.5,5.0); r.dir = glm::dvec3(0,0,-1);
    SurfaceHit h; CHECK(pickFace(sc, r, h));
    const FaceSet top = growConnectedRegion(sc, h.face, 15.0);

    ResolveContext ctx; ctx.surface = &sc; ctx.meshVersion = 1;

    // Capture with the anchor-cloud strategy (no original surface supplied).
    TargetSelection sel = captureSelection(ctx, top, SelectionKind::ConnectedRegion,
                                           AnchorStrategy::AnchorCloud);
    CHECK(sel.anchors.size() == 2);
    CHECK(sel.strategy == AnchorStrategy::AnchorCloud);
    CHECK(sel.valid);
    CHECK(sel.resolvedMeshVersion == 1);
    CHECK_NEAR(sel.cache.area, 1.0, 1e-9);
    CHECK(sel.cache.nodes.size() == 4);

    // Re-resolving against the SAME mesh is a no-op that still validates.
    ResolveResult rr = resolveSelection(ctx, sel);
    CHECK(rr.ok);
    CHECK(!rr.ambiguous);
    CHECK_NEAR(sel.cache.area, 1.0, 1e-9);

    // --- Remesh: same cube geometry, different triangulation/indices ---
    // Subdividing every tet changes node indices and boundary face ids, but the
    // top face is still a unit square at z = 1.
    std::vector<glm::vec3> P2; std::vector<unsigned> T2;
    refinedCubeMesh(P2, T2);                 // helper below
    SurfaceCache sc2; sc2.build(P2, T2, {}, false, noMid);
    CHECK(sc2.faceCount() > sc.faceCount());   // genuinely a different mesh

    ResolveContext ctx2; ctx2.surface = &sc2; ctx2.meshVersion = 2;
    ResolveResult rr2 = resolveSelection(ctx2, sel);
    CHECK(rr2.ok);
    CHECK(sel.valid);
    CHECK(sel.resolvedMeshVersion == 2);
    CHECK_NEAR(sel.cache.area, 1.0, 1e-6);     // same physical patch recovered
    CHECK_NEAR(sel.cache.normal.z, 1.0, 1e-9);

    // --- Anchors that match nothing invalidate rather than binding wrongly ---
    TargetSelection bogus = sel;
    bogus.anchors.clear();
    bogus.anchors.push_back({glm::dvec3(99.0, 99.0, 99.0), glm::dvec3(0,0,1), 1.0});
    bogus.distanceTol = 0.01;
    ResolveResult rr3 = resolveSelection(ctx2, bogus);
    CHECK(!rr3.ok);
    CHECK(!bogus.valid);
    CHECK(!rr3.message.empty());

    // --- Normal mismatch is rejected even when the position matches ---
    TargetSelection flipped = sel;
    for (auto& a : flipped.anchors) a.normal = glm::dvec3(0,0,-1);
    flipped.normalAngleTolDeg = 20.0;
    ResolveResult rr4 = resolveSelection(ctx2, flipped);
    CHECK(!rr4.ok);
}
```

Add the refined-cube helper next to `unitCubeMesh`:
```cpp
// 2x2x2 grid of cubes, each Freudenthal-split into 6 tets. Same [0,1]^3 domain,
// different node indices and a finer boundary triangulation.
static void refinedCubeMesh(std::vector<glm::vec3>& P, std::vector<unsigned>& T) {
    const int N = 2;
    auto idx = [&](int i,int j,int k){ return static_cast<unsigned>(
        i + (N+1)*(j + (N+1)*k)); };
    P.clear();
    for (int k = 0; k <= N; ++k)
      for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
          P.push_back(glm::vec3(i/float(N), j/float(N), k/float(N)));
    const int loc[6][4] = {{0,1,3,7},{0,1,7,5},{0,5,7,4},
                           {0,3,2,7},{0,2,6,7},{0,6,4,7}};
    T.clear();
    for (int k = 0; k < N; ++k)
      for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
          unsigned c[8];
          for (int b = 0; b < 8; ++b)
            c[b] = idx(i + (b & 1), j + ((b >> 1) & 1), k + ((b >> 2) & 1));
          for (auto& e : loc) for (int v = 0; v < 4; ++v) T.push_back(c[e[v]]);
        }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadResolve.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadResolve.h`:
```cpp
#pragma once
#include "loads/LoadModel.h"
#include "loads/MeshPick.h"

namespace Loads {

// Everything resolution needs. `originalSurfaceVerts/Indices` are the loaded
// surface (FEAModel::surfaceVertices/surfaceIndices) and enable the primary
// strategy; leave them null for meshes with no usable original surface
// (the G-code toolpath lane), which forces the anchor-cloud fallback.
struct ResolveContext {
    const SurfaceCache*           surface = nullptr;
    uint64_t                      meshVersion = 0;
    const std::vector<glm::vec3>* originalSurfaceVerts   = nullptr;
    const std::vector<unsigned>*  originalSurfaceIndices = nullptr;
    double                        modelDiagonal = 1.0;  // for default tolerances
};

struct ResolveResult {
    bool        ok        = false;
    bool        ambiguous = false;
    std::string message;
    std::string detail;
};

// Snapshot a live FaceSet into anchors. Chooses `strategy`, fills the resolved
// cache, and stamps `resolvedMeshVersion`.
TargetSelection captureSelection(const ResolveContext& ctx,
                                 const FaceSet& faces,
                                 SelectionKind kind,
                                 AnchorStrategy strategy);

// Rebuild `sel.cache` from `sel`'s anchors against the current mesh. A no-op
// returning ok when `sel.resolvedMeshVersion == ctx.meshVersion` and valid.
// On failure sets sel.valid = false and returns a repair suggestion rather than
// silently binding to different geometry.
ResolveResult resolveSelection(const ResolveContext& ctx, TargetSelection& sel);

} // namespace Loads
```

`src/loads/LoadResolve.cpp` — the matching core:

- `captureSelection` stores one `FaceAnchor{centroid, normal, area}` per selected
  face (never one anchor plus a radius — painted and disconnected selections need
  the full cloud), records adjacency pairs restricted to the set, defaults
  `distanceTol = 0.02 * ctx.modelDiagonal` and `normalAngleTolDeg = 30`, then
  calls `resolveSelection`.
- With `AnchorStrategy::OriginalSurface` it additionally records, for each anchor,
  the nearest original-surface triangle and the barycentric coordinate of the
  anchor centroid within it.
- `resolveSelection` matches each anchor to the best boundary face by
  `distance <= distanceTol` **and** `angle(normal) <= normalAngleTolDeg`, using a
  uniform spatial hash over face centroids so it stays linear.
- **Ambiguity checks**, any of which set `ok = false` (or `ambiguous = true` with
  a warning message):
  - an anchor's best match exceeds `distanceTol` → *"Part of this selection no longer matches the mesh."*
  - the runner-up match is within 10 % of the best distance → ambiguous.
  - resolved total area differs from `Σ anchor.area` by more than 25 % → area drift.
  - the selection was `ConnectedRegion` but the resolved faces form more than one
    connected component → *"This surface selection split apart when the mesh changed."*
- Every failure sets `sel.valid = false`, leaves the anchors untouched (so a repair
  workflow can retry), and writes a `suggestion`-grade `message`.
- On success it fills `cache.faces`, `cache.nodes` (via `faceSetNodes`, so Tet10
  midsides are included), `cache.area`, `cache.centroid`, `cache.normal`, sets
  `valid = true` and `resolvedMeshVersion = ctx.meshVersion`.
- `SelectionKind::WholeBody` short-circuits to `wholeBodyFaces` and is always
  resolvable.

Engine edits:
- `include/FEAModel.h`, next to `hasVolumetricMesh`:
  ```cpp
  // Bumped whenever volumetric node/element identity changes, so cached load
  // selections know to re-resolve from their anchors.
  uint64_t meshVersion = 0;
  ```
- `src/FEAModel.cpp`: `++meshVersion;` at the end of `generateVolumetricMesh()`
  (after `nLinearNodes` is set, ~line 1212) and at the end of
  `generateMidEdgeNodes()` (after `hasQuadraticMesh = true`, ~line 1296).
- `src/SlabMesher.cpp`: `++model.meshVersion;` at the end of `meshSlabs()` and
  `meshToolpathSlabs()`.

Add `src/loads/LoadResolve.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include src tests
git commit -m "Add anchor-based selection capture and remesh re-resolution with ambiguity checks"
```

---

### Task 8: Solver adapter — force, pressure, distributed, support, gravity

**Files:**
- Create: `include/loads/LoadAdapter.h`, `src/loads/LoadAdapter.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–7.
- Produces: `Loads::LoadSet`, `Loads::AdapterContext`, `Loads::buildLoadSet(ctx, loads, out, issues) -> bool`, `Loads::addNodalForce`.

`LoadSet` is what Task 13 hands the solver.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadAdapter.h"

// Build an adapter context over the unit cube, 1 model unit = 1000 mm = 1 m.
static AdapterContext cubeCtx(SurfaceCache& sc, std::vector<glm::vec3>& P,
                              std::vector<unsigned>& T) {
    unitCubeMesh(P, T);
    std::map<std::pair<unsigned,unsigned>,unsigned> noMid;
    sc.build(P, T, {}, false, noMid);
    AdapterContext c;
    c.surface = &sc;
    c.positions = &P;
    c.tetrahedra = &T;
    c.meshVersion = 1;
    c.metersPerModelUnit = 1.0;      // 1 model unit = 1 m -> areas in m^2
    c.nodeCount = static_cast<int>(P.size());
    c.hasQuadratic = false;
    return c;
}

static glm::dvec3 sumForce(const LoadSet& s) {
    glm::dvec3 t(0.0);
    for (auto& nf : s.nodalForces) t += nf.second;
    return t;
}

static void test_adapter_basic() {
    SurfaceCache sc; std::vector<glm::vec3> P; std::vector<unsigned> T;
    AdapterContext ctx = cubeCtx(sc, P, T);
    ResolveContext rc; rc.surface = &sc; rc.meshVersion = 1; rc.modelDiagonal = 1.732;

    Ray r; r.origin = glm::dvec3(0.5,0.5,5.0); r.dir = glm::dvec3(0,0,-1);
    SurfaceHit h; CHECK(pickFace(sc, r, h));
    const FaceSet top = growConnectedRegion(sc, h.face, 15.0);   // area 1 m^2

    std::vector<ValidationIssue> issues;

    // --- Distributed force: total preserved exactly, direction respected ---
    {
        LoadDefinition L;
        L.id = 1; L.type = LoadType::DistributedForce;
        L.target = captureSelection(rc, top, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        L.dir = glm::dvec3(0,0,-1);
        L.magnitude.si = 600.0;                 // 600 N total
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        const glm::dvec3 tot = sumForce(s);
        CHECK_NEAR(tot.z, -600.0, 1e-9);
        CHECK_NEAR(tot.x, 0.0, 1e-9);
        CHECK_NEAR(tot.y, 0.0, 1e-9);
        CHECK_NEAR(s.totalForceN, 600.0, 1e-9);
    }

    // --- Pressure: resultant = p * A, and it is NOT stored as a total force ---
    {
        LoadDefinition L;
        L.id = 2; L.type = LoadType::Pressure;
        L.target = captureSelection(rc, top, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        L.magnitude.si = 250.0;                 // 250 Pa over 1 m^2 -> 250 N
        L.params.pressureOutward = false;       // push INTO the surface
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        const glm::dvec3 tot = sumForce(s);
        CHECK_NEAR(tot.z, -250.0, 1e-9);        // inward on a +Z face
        // Doubling the area doubles the resultant: pressure is per unit area.
        const FaceSet all = wholeBodyFaces(sc); // area 6 m^2
        LoadDefinition L2 = L;
        L2.target = captureSelection(rc, all, SelectionKind::WholeBody,
                                     AnchorStrategy::AnchorCloud);
        LoadSet s2; issues.clear();
        CHECK(buildLoadSet(ctx, {L2}, s2, issues));
        double mag = 0.0;
        for (auto& nf : s2.nodalForces) mag += glm::length(nf.second);
        CHECK_NEAR(mag, 250.0 * 6.0, 1e-6);     // closed box: sum |f| = p*A_total
    }

    // --- Point force: patch-distributed total is exact, single-node is exact ---
    {
        LoadDefinition L;
        L.id = 3; L.type = LoadType::PointForce;
        L.target = captureSelection(rc, top, SelectionKind::Point,
                                    AnchorStrategy::AnchorCloud);
        L.dir = glm::dvec3(0,0,-1);
        L.magnitude.si = 100.0;
        L.distribution = Distribution::AreaWeighted;
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        CHECK_NEAR(sumForce(s).z, -100.0, 1e-9);
        CHECK(s.nodalForces.size() > 1);

        L.distribution = Distribution::SingleNode;
        L.anchorModel = glm::dvec3(0.5,0.5,1.0);
        LoadSet s1; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s1, issues));
        CHECK(s1.nodalForces.size() == 1);
        CHECK_NEAR(sumForce(s1).z, -100.0, 1e-9);
        // advanced single-node loading warns about mesh-dependent peak stress
        bool warned = false;
        for (auto& i : issues)
            if (i.severity == Severity::Warning) warned = true;
        CHECK(warned);
    }

    // --- Fixed support: all 3 DOF by default, per-axis locks when asked ---
    {
        LoadDefinition L;
        L.id = 4; L.type = LoadType::FixedSupport;
        L.target = captureSelection(rc, top, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        CHECK(s.fixedNodes.size() == 4);
        CHECK(s.singleDofFixed.empty());
        CHECK(s.nodalForces.empty());

        L.params.lockX = true; L.params.lockY = false; L.params.lockZ = false;
        LoadSet s2; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s2, issues));
        CHECK(s2.fixedNodes.empty());
        CHECK(s2.singleDofFixed.size() == 4);
        for (auto& p : s2.singleDofFixed) CHECK(p.second == 0);   // X only
    }

    // --- Gravity: W = rho * V * g over the whole body, needs density ---
    {
        LoadDefinition L;
        L.id = 5; L.type = LoadType::Gravity;
        L.type = LoadType::Gravity;
        L.dir = glm::dvec3(0,0,-1);
        L.magnitude.si = 9.80665;
        L.params.densitySI = 1000.0;            // 1000 kg/m^3 over 1 m^3
        L.params.densityKnown = true;
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        CHECK_NEAR(sumForce(s).z, -9806.65, 1e-3);   // 1000 * 1 * 9.80665

        L.params.densityKnown = false;
        LoadSet s2; issues.clear();
        CHECK(!buildLoadSet(ctx, {L}, s2, issues));
        bool err = false;
        for (auto& i : issues) if (i.severity == Severity::Error) err = true;
        CHECK(err);
    }

    // --- Disabled loads contribute nothing ---
    {
        LoadDefinition L;
        L.id = 6; L.type = LoadType::DistributedForce;
        L.target = captureSelection(rc, top, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        L.dir = glm::dvec3(0,0,-1); L.magnitude.si = 500.0;
        L.enabled = false;
        LoadSet s; issues.clear();
        CHECK(buildLoadSet(ctx, {L}, s, issues));
        CHECK(s.nodalForces.empty());
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadAdapter.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadAdapter.h`:
```cpp
#pragma once
#include "loads/LoadResolve.h"
#include "loads/FaceIntegration.h"

namespace Loads {

// Immutable once built. Passed BY VALUE-SHARED-POINTER into a solve; never
// retained by the solver as a raw pointer.
struct LoadSet {
    std::vector<std::pair<int, glm::dvec3>> nodalForces;    // node -> Newtons
    std::vector<int>                        fixedNodes;     // all 3 DOF
    std::vector<std::pair<int,int>>         singleDofFixed; // (node, dof)

    double     totalForceN  = 0.0;
    glm::dvec3 totalMomentNm{0.0};
};

struct AdapterContext {
    const SurfaceCache*           surface     = nullptr;
    const std::vector<glm::vec3>* positions   = nullptr;   // model space
    const std::vector<unsigned>*  tetrahedra  = nullptr;   // 4 per element
    uint64_t                      meshVersion = 0;
    double                        metersPerModelUnit = 1.0; // Loads::metersPerModelUnit
    int                           nodeCount   = 0;
    bool                          hasQuadratic = false;
    glm::mat4                     objectToWorld{1.0f};      // identity in the app
};

// Accumulates into a node->force map, summing duplicates.
void addNodalForce(std::vector<std::pair<int, glm::dvec3>>& acc,
                   int node, const glm::dvec3& f);

// Converts every ENABLED load into SI nodal forces and constrained DOFs.
// Returns false when any load produced an Error-severity issue.
bool buildLoadSet(const AdapterContext& ctx,
                  const std::vector<LoadDefinition>& loads,
                  LoadSet& out,
                  std::vector<ValidationIssue>& issues);

} // namespace Loads
```

`src/loads/LoadAdapter.cpp` — required behaviour per type. All lengths are
converted with `k = ctx.metersPerModelUnit`: an area in model units becomes
`A * k*k` m², a volume becomes `V * k*k*k` m³.

- **Resolved direction**: `dir` is normalised; `Frame::ObjectLocal` goes through
  `dirObjectToWorld(dir, ctx.objectToWorld)`; `DirMode::SurfaceNormal` uses
  `sel.cache.normal`. A zero-length direction is an Error issue.
- **PointForce, AreaWeighted**: `consistentFaceWeights` over the patch faces,
  normalise the weights so they sum to 1, multiply by `magnitude.si * dir`. The
  user's total is reproduced exactly — never silently rescaled.
- **PointForce, SingleNode**: all force on the node nearest `anchorModel`, plus a
  `Severity::Warning` issue: *"Peak stress at a single point is mesh-dependent and
  will keep rising as you refine the mesh. Spread the force over a small area for
  a trustworthy number."*
- **Pressure**: per face, `f_face = p * A_m2 * (outward ? +n : -n)`, distributed
  onto that face's nodes with `consistentFaceWeights` scaled to the face area
  (this is where Tet10 puts 0 on corners and A/3 on midsides). Pressure is never
  converted to a total force anywhere in storage.
- **DistributedForce**: total `magnitude.si` split across all region nodes by
  **area weighting** via `consistentFaceWeights` accumulated over faces and
  normalised by the region area — not by node or triangle count. Assert-grade
  invariant: `|Σf| == magnitude.si` within `1e-9` relative.
- **FixedSupport**: if all three locks are set, push every resolved node to
  `fixedNodes`; otherwise push `(node, dof)` pairs for each set lock into
  `singleDofFixed`. No rotational DOFs are invented — solid tets have none.
- **Gravity**: requires `params.densityKnown` and `densitySI > 0`, else Error
  *"This material has no density, so gravity cannot be calculated. Choose a
  material with a density value."* Otherwise loop elements, compute each tet's
  volume in m³ from `positions` and `tetrahedra`, and add `rho * V * g * dir / 4`
  to each of its 4 corner nodes (the consistent body force for a constant field
  on a Tet4; for a Tet10 mesh the corner-node split is still exact in total,
  which is what the resultant test checks).
- **Torque**: delegated to Task 9; until then emit an Error issue so the type is
  never silently ignored.
- Unresolved / invalid targets, non-finite magnitudes, and zero magnitudes each
  produce their own Error or Warning issue carrying `loadId`.
- Finally accumulate `totalForceN = |Σ f|` and
  `totalMomentNm = Σ (r_m × f)` about the model origin for reporting.

Add `src/loads/LoadAdapter.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add solver adapter for force, pressure, distributed load, supports and gravity"
```

---

### Task 9: Torque — constrained minimum-norm distribution

**Files:**
- Create: `include/loads/TorqueSolve.h`, `src/loads/TorqueSolve.cpp`
- Modify: `src/loads/LoadAdapter.cpp` (call it), `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::NodeWeight` (Task 5), Eigen.
- Produces: `Loads::TorqueRequest`, `Loads::TorqueResult`, `Loads::solveTorqueDistribution`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/TorqueSolve.h"

static void test_torque() {
    // A square ring of 4 nodes in the z = 0 plane, equal positive weights.
    TorqueRequest req;
    req.nodes = {0,1,2,3};
    req.positions = { {1,0,0}, {0,1,0}, {-1,0,0}, {0,-1,0} };
    req.weights   = { 1.0, 1.0, 1.0, 1.0 };
    req.torqueNm  = glm::dvec3(0.0, 0.0, 5.0);      // +Z, right-hand rule

    TorqueResult res;
    CHECK(solveTorqueDistribution(req, res));
    CHECK(res.forces.size() == 4);

    // Net force is zero.
    glm::dvec3 F(0.0);
    for (auto& f : res.forces) F += f;
    CHECK_NEAR(glm::length(F), 0.0, 1e-9);

    // Net moment equals the request exactly.
    glm::dvec3 M(0.0);
    for (size_t i = 0; i < res.forces.size(); ++i)
        M += glm::cross(req.positions[i], res.forces[i]);
    CHECK_NEAR(M.x, 0.0, 1e-9);
    CHECK_NEAR(M.y, 0.0, 1e-9);
    CHECK_NEAR(M.z, 5.0, 1e-9);

    // Right-hand rule: at (1,0,0) a +Z torque pushes toward +Y.
    CHECK(res.forces[0].y > 0.0);

    // Reversing the axis reverses every force exactly.
    TorqueRequest rev = req; rev.torqueNm = -req.torqueNm;
    TorqueResult rres;
    CHECK(solveTorqueDistribution(rev, rres));
    for (size_t i = 0; i < 4; ++i) {
        CHECK_NEAR(rres.forces[i].x, -res.forces[i].x, 1e-9);
        CHECK_NEAR(rres.forces[i].y, -res.forces[i].y, 1e-9);
    }

    // Moment is reference-point invariant because net force is zero: shifting
    // every position by a constant leaves the moment unchanged.
    TorqueRequest shifted = req;
    for (auto& p : shifted.positions) p += glm::dvec3(10.0, -7.0, 3.0);
    TorqueResult sres;
    CHECK(solveTorqueDistribution(shifted, sres));
    glm::dvec3 Ms(0.0);
    for (size_t i = 0; i < 4; ++i)
        Ms += glm::cross(shifted.positions[i], sres.forces[i]);
    CHECK_NEAR(Ms.z, 5.0, 1e-9);

    // Scale invariance of the conditioning verdict: a 1000x larger ring is just
    // as solvable, which is what the C' = S*C rescaling buys.
    TorqueRequest big = req;
    for (auto& p : big.positions) p *= 1000.0;
    TorqueResult bres;
    CHECK(solveTorqueDistribution(big, bres));
    CHECK(!bres.rejected);

    // --- Degenerate: a single node cannot carry torque ---
    TorqueRequest one;
    one.nodes = {0}; one.positions = { {1,0,0} }; one.weights = {1.0};
    one.torqueNm = glm::dvec3(0,0,1);
    TorqueResult ores;
    CHECK(!solveTorqueDistribution(one, ores));
    CHECK(ores.rejected);
    CHECK(!ores.message.empty());

    // --- Degenerate: collinear nodes cannot twist about their own line ---
    TorqueRequest line;
    line.nodes = {0,1,2};
    line.positions = { {-1,0,0}, {0,0,0}, {1,0,0} };
    line.weights = {1.0,1.0,1.0};
    line.torqueNm = glm::dvec3(3.0, 0.0, 0.0);       // about the line itself
    TorqueResult lres;
    CHECK(!solveTorqueDistribution(line, lres));
    CHECK(lres.rejected);
    CHECK(lres.unattainableFraction > 0.5);

    // ...but the SAME collinear set twists fine about a perpendicular axis.
    line.torqueNm = glm::dvec3(0.0, 0.0, 3.0);
    TorqueResult lok;
    CHECK(solveTorqueDistribution(line, lok));
    glm::dvec3 Ml(0.0);
    for (size_t i = 0; i < 3; ++i)
        Ml += glm::cross(line.positions[i], lok.forces[i]);
    CHECK_NEAR(Ml.z, 3.0, 1e-9);

    // Zero-length axis / zero weights are refused, not divided by.
    TorqueRequest zw = req; zw.weights = {0.0,0.0,0.0,0.0};
    TorqueResult zres;
    CHECK(!solveTorqueDistribution(zw, zres));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/TorqueSolve.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/TorqueSolve.h`:
```cpp
#pragma once
#include "loads/LoadTypes.h"

namespace Loads {

struct TorqueRequest {
    std::vector<int>        nodes;
    std::vector<glm::dvec3> positions;   // model space, any origin
    std::vector<double>     weights;     // STRICTLY POSITIVE tributary weights
    glm::dvec3              torqueNm{0.0};

    // Reject when the unattainable part of the request exceeds this fraction.
    double maxUnattainableFraction = 1e-6;
    // Reject when |f| / |T| exceeds this (1/m). A near-degenerate region is
    // technically solvable but demands absurd opposing forces.
    double maxForceAmplification = 1.0e6;
};

struct TorqueResult {
    std::vector<glm::dvec3> forces;              // parallel to request.nodes
    bool        rejected = false;
    double      unattainableFraction = 0.0;
    double      forceAmplification   = 0.0;
    double      conditioning         = 0.0;      // sigma_min / sigma_max of G'
    std::string message;                         // student-facing
    std::string detail;                          // technical
};

// Distributes a torque over a finite region as nodal forces with
//   minimise  1/2 * sum_i (1/w_i)|f_i|^2
//   s.t.      sum_i f_i = 0            (net force zero)
//             sum_i r_i x f_i = T      (exact requested moment)
// Returns false (and sets `rejected`) when the request is unattainable or
// ill-conditioned. NEVER projects silently onto the attainable subspace.
bool solveTorqueDistribution(const TorqueRequest& req, TorqueResult& out);

} // namespace Loads
```

`src/loads/TorqueSolve.cpp`:
```cpp
#include "loads/TorqueSolve.h"
#include <Eigen/Dense>
#include <cmath>

namespace Loads {

namespace {
// [r]x such that [r]x * f == r x f
Eigen::Matrix3d crossMat(const glm::dvec3& r) {
    Eigen::Matrix3d m;
    m <<   0.0, -r.z,  r.y,
          r.z,   0.0, -r.x,
         -r.y,  r.x,   0.0;
    return m;
}
} // namespace

bool solveTorqueDistribution(const TorqueRequest& req, TorqueResult& out) {
    out = TorqueResult{};
    const int n = static_cast<int>(req.nodes.size());
    if (n < 2 || static_cast<int>(req.positions.size()) != n ||
                 static_cast<int>(req.weights.size())   != n) {
        out.rejected = true;
        out.message  = "Select a wider surface region for this twist. A twist "
                       "needs an area to push against, not a single point.";
        out.detail   = "torque region has fewer than 2 nodes";
        return false;
    }
    double wSum = 0.0;
    for (double w : req.weights) {
        if (!(w > 0.0) || !std::isfinite(w)) {
            out.rejected = true;
            out.message  = "This surface region has no usable area for a twist.";
            out.detail   = "non-positive tributary weight";
            return false;
        }
        wSum += w;
    }
    const double Tnorm = glm::length(req.torqueNm);
    if (!(Tnorm > 0.0) || !std::isfinite(Tnorm)) {
        out.rejected = true;
        out.message  = "Enter a twist amount greater than zero.";
        out.detail   = "zero or non-finite torque vector";
        return false;
    }

    // Reference the area-weighted centroid. With sum(f) = 0 the moment is
    // reference-invariant, so this is purely for conditioning.
    glm::dvec3 c(0.0);
    for (int i = 0; i < n; ++i) c += req.positions[i] * req.weights[i];
    c /= wSum;

    // Characteristic length = radius of gyration of the weighted region.
    double r2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const glm::dvec3 d = req.positions[i] - c;
        r2 += req.weights[i] * glm::dot(d, d);
    }
    const double L = std::sqrt(r2 / wSum);
    if (!(L > 0.0) || !std::isfinite(L)) {
        out.rejected = true;
        out.message  = "This surface region is a single point, so it cannot twist.";
        out.detail   = "zero radius of gyration";
        return false;
    }

    // Constraint system C f = b, rows 0-2 force, rows 3-5 moment.
    Eigen::MatrixXd C(6, 3 * n);
    C.setZero();
    for (int i = 0; i < n; ++i) {
        C.block<3,3>(0, 3*i) = Eigen::Matrix3d::Identity();
        C.block<3,3>(3, 3*i) = crossMat(req.positions[i] - c);
    }
    Eigen::VectorXd b(6);
    b << 0.0, 0.0, 0.0, req.torqueNm.x, req.torqueNm.y, req.torqueNm.z;

    // Non-dimensionalise the CONSTRAINT SYSTEM, not just G's rows: C' = S C,
    // b' = S b. S is invertible so the feasible set (and hence f) is unchanged;
    // G' = C' W C'^T = S G S^T stays SYMMETRIC, which one-sided row scaling
    // would destroy. Without this, cond(G) inflates as L^2 with part size and a
    // perfectly good region on a large part would be judged degenerate.
    Eigen::VectorXd sdiag(6);
    sdiag << 1.0, 1.0, 1.0, 1.0/L, 1.0/L, 1.0/L;
    const Eigen::MatrixXd Cp = sdiag.asDiagonal() * C;
    const Eigen::VectorXd bp = sdiag.asDiagonal() * b;

    // G' = C' W C'^T with W = blockdiag(w_i * I3).
    Eigen::MatrixXd WCt(3 * n, 6);
    for (int i = 0; i < n; ++i)
        WCt.block(3*i, 0, 3, 6) = req.weights[i] * Cp.block(0, 3*i, 6, 3).transpose();
    Eigen::MatrixXd G = Cp * WCt;

    // Scale out the remaining area dimension so the singular spectrum is pure
    // shape, then judge rank/conditioning on a relative threshold.
    const Eigen::MatrixXd Gn = G / wSum;
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(Gn, Eigen::ComputeFullU | Eigen::ComputeFullV);
    const Eigen::VectorXd sv = svd.singularValues();
    const double sMax = sv(0);
    const double tol  = 1e-10 * (sMax > 0.0 ? sMax : 1.0);
    int rank = 0;
    for (int i = 0; i < sv.size(); ++i) if (sv(i) > tol) ++rank;
    out.conditioning = (sMax > 0.0) ? sv(sv.size()-1) / sMax : 0.0;

    // Attainability: the component of b' outside range(G') can never be
    // produced. Report it and REJECT — do not project it away.
    const Eigen::VectorXd bn = bp / wSum;
    Eigen::VectorXd lambda = Eigen::VectorXd::Zero(6);
    Eigen::VectorXd recon  = Eigen::VectorXd::Zero(6);
    for (int i = 0; i < rank; ++i) {
        const double coeff = svd.matrixU().col(i).dot(bn) / sv(i);
        lambda += coeff * svd.matrixV().col(i);
        recon  += sv(i) * coeff * svd.matrixU().col(i);
    }
    const double bNorm = bn.norm();
    out.unattainableFraction = (bNorm > 0.0) ? (bn - recon).norm() / bNorm : 0.0;

    if (out.unattainableFraction > req.maxUnattainableFraction) {
        out.rejected = true;
        out.message  = "This surface cannot be twisted around that axis. Pick a "
                       "wider surface, or twist around a different direction.";
        out.detail   = "unattainable torque component fraction=" +
                       std::to_string(out.unattainableFraction) +
                       " rank=" + std::to_string(rank);
        return false;
    }

    // f = W C'^T lambda  (the scaling cancels: this is the unscaled minimiser).
    out.forces.assign(static_cast<size_t>(n), glm::dvec3(0.0));
    double fNorm2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d fi =
            req.weights[i] * Cp.block(0, 3*i, 6, 3).transpose() * lambda / wSum;
        out.forces[static_cast<size_t>(i)] = glm::dvec3(fi(0), fi(1), fi(2));
        fNorm2 += fi.squaredNorm();
    }
    out.forceAmplification = std::sqrt(fNorm2) / Tnorm;

    if (!std::isfinite(out.forceAmplification) ||
        out.forceAmplification > req.maxForceAmplification) {
        out.rejected = true;
        out.message  = "This surface is too narrow to twist. It would need "
                       "enormous opposing forces. Select a wider region.";
        out.detail   = "force amplification=" +
                       std::to_string(out.forceAmplification) + " /m";
        return false;
    }
    return true;
}

} // namespace Loads
```

In `src/loads/LoadAdapter.cpp`, replace the Torque placeholder Error with a real
call: gather region nodes and **`tributaryFaceWeights`** (never the consistent
weights — those are zero at T6 corners and would divide by zero), convert
positions to metres with `metersPerModelUnit`, set
`torqueNm = magnitude.si * normalize(resolvedDir)`, call
`solveTorqueDistribution`, and on rejection emit an Error `ValidationIssue`
carrying `res.message` / `res.detail`.

Add `src/loads/TorqueSolve.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add constrained minimum-norm torque distribution with rejection of unattainable requests"
```

---

### Task 10: Validation gate with rigid-body rank test

**Files:**
- Create: `include/loads/LoadValidation.h`, `src/loads/LoadValidation.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::LoadSet`, `Loads::AdapterContext`.
- Produces: `Loads::validateForSolve(ctx, loads, issues) -> bool`, `Loads::rigidBodyRank(ctx, set, ranksOut) -> bool`, `Loads::connectedComponents`.
- Note: `ValidationIssue` is defined in `LoadTypes.h` (Task 2), not here — the adapter raises issues too and must not depend on this module.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadValidation.h"

static void test_validation() {
    SurfaceCache sc; std::vector<glm::vec3> P; std::vector<unsigned> T;
    AdapterContext ctx = cubeCtx(sc, P, T);
    ResolveContext rc; rc.surface = &sc; rc.meshVersion = 1; rc.modelDiagonal = 1.732;

    Ray up; up.origin = glm::dvec3(0.5,0.5,5.0); up.dir = glm::dvec3(0,0,-1);
    SurfaceHit ht; CHECK(pickFace(sc, up, ht));
    const FaceSet top = growConnectedRegion(sc, ht.face, 15.0);

    Ray dn; dn.origin = glm::dvec3(0.5,0.5,-5.0); dn.dir = glm::dvec3(0,0,1);
    SurfaceHit hb; CHECK(pickFace(sc, dn, hb));
    const FaceSet bottom = growConnectedRegion(sc, hb.face, 15.0);

    auto force = [&](double N) {
        LoadDefinition L; L.id = 1; L.type = LoadType::DistributedForce;
        L.target = captureSelection(rc, top, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        L.dir = glm::dvec3(0,0,-1); L.magnitude.si = N; return L;
    };
    auto support = [&]() {
        LoadDefinition L; L.id = 2; L.type = LoadType::FixedSupport;
        L.target = captureSelection(rc, bottom, SelectionKind::Faces,
                                    AnchorStrategy::AnchorCloud);
        return L;
    };

    std::vector<ValidationIssue> is;

    // Nothing at all.
    CHECK(!validateForSolve(ctx, {}, is));
    CHECK(!is.empty());

    // Load but no support -> the whole part can drift away.
    is.clear();
    CHECK(!validateForSolve(ctx, {force(100.0)}, is));
    bool rigid = false;
    for (auto& i : is)
        if (i.message.find("move as one whole object") != std::string::npos) rigid = true;
    CHECK(rigid);

    // Support present but only ONE axis locked -> still 5 free rigid modes.
    is.clear();
    LoadDefinition partial = support();
    partial.params.lockX = true; partial.params.lockY = false; partial.params.lockZ = false;
    CHECK(!validateForSolve(ctx, {force(100.0), partial}, is));

    // Fully clamped face -> valid.
    is.clear();
    CHECK(validateForSolve(ctx, {force(100.0), support()}, is));
    for (auto& i : is) CHECK(i.severity != Severity::Error);

    // Zero magnitude is flagged.
    is.clear();
    CHECK(!validateForSolve(ctx, {force(0.0), support()}, is));

    // Non-finite magnitude is flagged.
    is.clear();
    LoadDefinition nan1 = force(std::nan(""));
    CHECK(!validateForSolve(ctx, {nan1, support()}, is));

    // Zero-length direction is flagged.
    is.clear();
    LoadDefinition zd = force(100.0); zd.dir = glm::dvec3(0.0);
    CHECK(!validateForSolve(ctx, {zd, support()}, is));

    // Stale selection (mesh moved on) is flagged, not silently reused.
    is.clear();
    LoadDefinition stale = force(100.0);
    stale.target.resolvedMeshVersion = 999;
    stale.target.valid = false;
    CHECK(!validateForSolve(ctx, {stale, support()}, is));

    // Every issue carries a plain-language message and an actionable suggestion.
    for (auto& i : is) {
        CHECK(!i.message.empty());
        CHECK(!i.suggestion.empty());
    }

    // --- Rank test directly ---
    LoadSet full;  for (int n : {0,1,2,3}) full.fixedNodes.push_back(n);
    std::vector<int> ranks;
    CHECK(rigidBodyRank(ctx, full, ranks));            // 4 non-collinear nodes -> 6
    CHECK(ranks.size() == 1);
    CHECK(ranks[0] == 6);

    LoadSet oneNode; oneNode.fixedNodes.push_back(0);  // 3 DOF -> rank 3
    ranks.clear();
    CHECK(!rigidBodyRank(ctx, oneNode, ranks));
    CHECK(ranks[0] == 3);

    LoadSet none;
    ranks.clear();
    CHECK(!rigidBodyRank(ctx, none, ranks));
    CHECK(ranks[0] == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadValidation.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadValidation.h`:
```cpp
#pragma once
#include "loads/LoadAdapter.h"

namespace Loads {

// Node -> connected-component id, via shared tet elements. A fracture-separated
// or weak-tie island is its own component and must be restrained on its own.
std::vector<int> connectedComponents(const AdapterContext& ctx, int& nComponents);

// Per connected component, the rank of the rigid-mode matrix restricted to
// constrained DOFs. Returns true only when EVERY component reaches rank 6.
// Columns are unit-normalised first: rotation columns scale with length and
// translation columns do not, so an un-normalised test is size-dependent.
bool rigidBodyRank(const AdapterContext& ctx, const LoadSet& set,
                   std::vector<int>& ranksOut);

// Full pre-solve gate. Builds the load set internally, collects every issue,
// and returns true only when no Error-severity issue was raised.
bool validateForSolve(const AdapterContext& ctx,
                      const std::vector<LoadDefinition>& loads,
                      std::vector<ValidationIssue>& issues);

} // namespace Loads
```

`src/loads/LoadValidation.cpp` — required behaviour:

- `connectedComponents`: union-find over `ctx.tetrahedra`, joining the 4 corners
  of every element; nodes referenced by no element form their own singleton and
  are ignored by the rank test.
- `rigidBodyRank`: for each component, build `R` (nDOF × 6) from positions
  relative to that component's centroid —
  translations `(1,0,0)`, `(0,1,0)`, `(0,0,1)`; rotations
  `ωx → (0,−z,y)`, `ωy → (z,0,−x)`, `ωz → (−y,x,0)`.
  Normalise each of the 6 columns to unit norm over the component, extract only
  the rows for DOFs constrained by `fixedNodes`/`singleDofFixed`, and take
  `Eigen::JacobiSVD`. Rank counts singular values above `1e-8 * σ_max`, with
  rank 0 when no DOF in the component is constrained.
- `validateForSolve` runs, in order, and appends a `ValidationIssue` for each hit:
  - no loads at all → *"There are no loads yet. Add a force, pressure or weight so there is something to simulate."*
  - no supports → *"Your part can still move as one whole object. Hold at least one surface before simulating."*
  - `rigidBodyRank` failure with supports present → *"Your part can still move as one whole object. Hold at least one surface before simulating."* with a detail naming the deficient component and its rank.
  - per load: zero magnitude, non-finite magnitude, zero-length direction,
    invalid/unresolved target, target invalidated by remeshing, gravity without
    density, pressure with zero resolved area, torque with no valid axis or a
    rejected `TorqueResult`, and the single-node point-load singularity warning.
  - every issue sets `severity`, `message`, `detail`, `loadId`, `suggestion`.
- Returns `false` if any issue has `Severity::Error`.

Add `src/loads/LoadValidation.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add pre-solve validation gate with per-component rigid-body rank test"
```

---

### Task 11: Load scene + undo/redo commands

**Files:**
- Create: `include/loads/LoadCommands.h`, `src/loads/LoadCommands.cpp`, `include/loads/LoadScene.h`, `src/loads/LoadScene.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::LoadScene` with `add`, `remove`, `setTarget`, `setDirection`, `setMagnitude`, `setEnabled`, `undo`, `redo`, `canUndo`, `canRedo`, `loads()`, `find(LoadId)`, `select(LoadId)`, `selected()`, `nextId()`.

Commands store before/after **value snapshots**, never references or indices into
the load vector, so replay is safe after unrelated edits reorder the list.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadScene.h"

static void test_scene_undo_redo() {
    LoadScene scene;
    CHECK(!scene.canUndo());
    CHECK(!scene.canRedo());

    LoadDefinition L;
    L.type = LoadType::PointForce;
    L.dir = glm::dvec3(0,0,-1);
    L.magnitude.si = 100.0;
    const LoadId id = scene.add(L);
    CHECK(id != 0);
    CHECK(scene.loads().size() == 1);
    CHECK(scene.canUndo());

    // Magnitude change round-trips.
    scene.setMagnitude(id, 250.0);
    CHECK_NEAR(scene.find(id)->magnitude.si, 250.0, 1e-12);
    scene.undo();
    CHECK_NEAR(scene.find(id)->magnitude.si, 100.0, 1e-12);
    scene.redo();
    CHECK_NEAR(scene.find(id)->magnitude.si, 250.0, 1e-12);

    // Direction change round-trips.
    scene.setDirection(id, glm::dvec3(1,0,0), Frame::World, DirMode::AxisX);
    CHECK_NEAR(scene.find(id)->dir.x, 1.0, 1e-12);
    CHECK(scene.find(id)->dirMode == DirMode::AxisX);
    scene.undo();
    CHECK_NEAR(scene.find(id)->dir.z, -1.0, 1e-12);
    CHECK(scene.find(id)->dirMode == DirMode::Free);

    // Target change round-trips.
    TargetSelection t; t.kind = SelectionKind::WholeBody;
    scene.setTarget(id, t);
    CHECK(scene.find(id)->target.kind == SelectionKind::WholeBody);
    scene.undo();
    CHECK(scene.find(id)->target.kind != SelectionKind::WholeBody);

    // Enable toggle round-trips.
    scene.setEnabled(id, false);
    CHECK(!scene.find(id)->enabled);
    scene.undo();
    CHECK(scene.find(id)->enabled);

    // Delete then undo restores the load with the SAME id and all its data.
    const double magBefore = scene.find(id)->magnitude.si;
    scene.remove(id);
    CHECK(scene.loads().empty());
    CHECK(scene.find(id) == nullptr);
    scene.undo();
    CHECK(scene.loads().size() == 1);
    CHECK(scene.find(id) != nullptr);
    CHECK_NEAR(scene.find(id)->magnitude.si, magBefore, 1e-12);

    // Undo past creation empties the scene; redo brings it back.
    while (scene.canUndo()) scene.undo();
    CHECK(scene.loads().empty());
    scene.redo();
    CHECK(scene.loads().size() == 1);

    // A new command clears the redo branch.
    while (scene.canUndo()) scene.undo();
    LoadDefinition M; M.type = LoadType::Gravity;
    scene.add(M);
    CHECK(!scene.canRedo());

    // Ids are never reused after deletion.
    const LoadId a = scene.add(L);
    scene.remove(a);
    const LoadId b = scene.add(L);
    CHECK(b != a);

    // Selection survives unrelated edits and clears when the load dies.
    scene.select(b);
    CHECK(scene.selected() == b);
    scene.remove(b);
    CHECK(scene.selected() == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadScene.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadCommands.h`:
```cpp
#pragma once
#include "loads/LoadModel.h"
#include <memory>

namespace Loads {

class LoadScene;

// Value-snapshot command. `apply` and `revert` must be exact inverses.
struct Command {
    virtual ~Command() = default;
    virtual void apply (LoadScene& s) = 0;
    virtual void revert(LoadScene& s) = 0;
};

using CommandPtr = std::unique_ptr<Command>;

CommandPtr makeAddCommand      (const LoadDefinition& after);
CommandPtr makeRemoveCommand   (const LoadDefinition& before);
CommandPtr makeEditCommand     (const LoadDefinition& before,
                                const LoadDefinition& after);

} // namespace Loads
```

`include/loads/LoadScene.h`:
```cpp
#pragma once
#include "loads/LoadCommands.h"

namespace Loads {

// Owns the authoritative load list, the current selection and the undo stack.
// Holds NO object transform of its own — callers pass the model matrix into
// LoadCoords explicitly (identity in the app today).
class LoadScene {
public:
    LoadId add(const LoadDefinition& L);          // returns the assigned id
    bool   remove(LoadId id);

    bool setTarget   (LoadId id, const TargetSelection& t);
    bool setDirection(LoadId id, const glm::dvec3& dir, Frame f, DirMode m);
    bool setMagnitude(LoadId id, double si);
    bool setEnabled  (LoadId id, bool on);

    bool undo();
    bool redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    const std::vector<LoadDefinition>& loads() const { return m_loads; }
    const LoadDefinition* find(LoadId id) const;
    LoadDefinition*       find(LoadId id);

    void   select(LoadId id) { m_selected = id; }
    LoadId selected() const  { return m_selected; }

    LoadId nextId() { return ++m_lastId; }

    // Used only by Command implementations.
    void rawInsert(const LoadDefinition& L);
    void rawErase (LoadId id);
    void rawReplace(const LoadDefinition& L);

private:
    void push(CommandPtr c);        // applies, pushes to undo, clears redo

    std::vector<LoadDefinition> m_loads;
    std::vector<CommandPtr>     m_undo, m_redo;
    LoadId                      m_lastId  = 0;
    LoadId                      m_selected = 0;
};

} // namespace Loads
```

`src/loads/LoadCommands.cpp` / `src/loads/LoadScene.cpp`:
- `AddCommand{after}`: `apply` → `rawInsert(after)`; `revert` → `rawErase(after.id)`.
- `RemoveCommand{before}`: `apply` → `rawErase`; `revert` → `rawInsert(before)`
  (restoring the original id, so undo of a delete is byte-identical).
- `EditCommand{before, after}`: `apply` → `rawReplace(after)`; `revert` →
  `rawReplace(before)`. Every setter builds one of these from a copy of the
  current value, so target/direction/magnitude/enabled all share one path.
- `push(c)`: `c->apply(*this)`, move into `m_undo`, `m_redo.clear()`.
- `undo()`: pop `m_undo`, `revert`, move to `m_redo`. `redo()` mirrors it.
- `add` assigns `nextId()` before constructing the command, so ids are monotonic
  and never reused.
- `rawErase` clears `m_selected` when it erases the selected load.

Add both `.cpp` files to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add load scene with value-snapshot undo/redo command stack"
```

---

### Task 12: Placement state machine

**Files:**
- Create: `include/loads/LoadPlacement.h`, `src/loads/LoadPlacement.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::PlacementState`, `Loads::PlacementEvent`, `Loads::Placement` with `state()`, `begin(LoadType)`, `handle(event)`, `draft()`, `commit(LoadScene&)`, `cancel()`, `beginEdit(LoadScene&, LoadId)`.

Pure logic — no GLFW types, no GL. The app translates real input into
`PlacementEvent`s; tests feed them directly.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadPlacement.h"

static void test_placement_state_machine() {
    LoadScene scene;
    Placement p;
    CHECK(p.state() == PlacementState::Idle);

    // Happy path: type -> target -> direction -> magnitude -> preview -> commit
    p.begin(LoadType::PointForce);
    CHECK(p.state() == PlacementState::SelectingTarget);

    PlacementEvent ev;
    ev.kind = PlacementEvent::Kind::TargetPicked;
    ev.target.kind = SelectionKind::Point;
    ev.target.valid = true;
    ev.anchor = glm::dvec3(0,0,1);
    p.handle(ev);
    CHECK(p.state() == PlacementState::SettingDirection);

    PlacementEvent d;
    d.kind = PlacementEvent::Kind::DirectionSet;
    d.direction = glm::dvec3(0,0,-1);
    p.handle(d);
    CHECK(p.state() == PlacementState::SettingMagnitude);

    // A zero-length direction is refused and does not advance the machine.
    Placement q; q.begin(LoadType::PointForce); q.handle(ev);
    PlacementEvent zero;
    zero.kind = PlacementEvent::Kind::DirectionSet;
    zero.direction = glm::dvec3(0.0);
    q.handle(zero);
    CHECK(q.state() == PlacementState::SettingDirection);

    PlacementEvent m;
    m.kind = PlacementEvent::Kind::MagnitudeSet;
    m.magnitudeSI = 500.0;
    p.handle(m);
    CHECK(p.state() == PlacementState::Previewing);
    CHECK_NEAR(p.draft().magnitude.si, 500.0, 1e-12);

    // Non-finite magnitude is refused.
    PlacementEvent bad;
    bad.kind = PlacementEvent::Kind::MagnitudeSet;
    bad.magnitudeSI = std::nan("");
    p.handle(bad);
    CHECK_NEAR(p.draft().magnitude.si, 500.0, 1e-12);

    const LoadId id = p.commit(scene);
    CHECK(id != 0);
    CHECK(p.state() == PlacementState::Idle);
    CHECK(scene.loads().size() == 1);
    CHECK_NEAR(scene.find(id)->magnitude.si, 500.0, 1e-12);

    // Escape cancels an incomplete placement and touches NO committed load.
    Placement c;
    c.begin(LoadType::Pressure);
    c.handle(ev);
    PlacementEvent esc; esc.kind = PlacementEvent::Kind::Cancel;
    c.handle(esc);
    CHECK(c.state() == PlacementState::Idle);
    CHECK(scene.loads().size() == 1);              // committed load survived
    CHECK_NEAR(scene.find(id)->magnitude.si, 500.0, 1e-12);

    // Reverse negates the draft direction without changing state.
    Placement r;
    r.begin(LoadType::PointForce); r.handle(ev); r.handle(d);
    const glm::dvec3 before = r.draft().dir;
    PlacementEvent rev; rev.kind = PlacementEvent::Kind::ReverseDirection;
    r.handle(rev);
    CHECK_NEAR(r.draft().dir.z, -before.z, 1e-12);
    CHECK(r.state() == PlacementState::SettingMagnitude);

    // Camera motion must never alter a committed world-space direction.
    const glm::dvec3 committedDir = scene.find(id)->dir;
    PlacementEvent cam; cam.kind = PlacementEvent::Kind::CameraChanged;
    Placement idle; idle.handle(cam);
    CHECK_NEAR(scene.find(id)->dir.z, committedDir.z, 1e-15);
    CHECK(idle.state() == PlacementState::Idle);

    // Editing an existing load starts from its current values and commits back
    // to the SAME id.
    Placement e;
    CHECK(e.beginEdit(scene, id));
    CHECK(e.state() == PlacementState::EditingExistingLoad);
    CHECK_NEAR(e.draft().magnitude.si, 500.0, 1e-12);
    PlacementEvent m2;
    m2.kind = PlacementEvent::Kind::MagnitudeSet; m2.magnitudeSI = 750.0;
    e.handle(m2);
    const LoadId id2 = e.commit(scene);
    CHECK(id2 == id);
    CHECK(scene.loads().size() == 1);
    CHECK_NEAR(scene.find(id)->magnitude.si, 750.0, 1e-12);
    // ...and that edit is undoable.
    scene.undo();
    CHECK_NEAR(scene.find(id)->magnitude.si, 500.0, 1e-12);

    // Gravity needs no target: it goes straight to direction.
    Placement g;
    g.begin(LoadType::Gravity);
    CHECK(g.state() == PlacementState::SettingDirection);

    // A target that failed to resolve lands in Invalid, not SettingDirection.
    Placement bad2;
    bad2.begin(LoadType::Pressure);
    PlacementEvent badTarget;
    badTarget.kind = PlacementEvent::Kind::TargetPicked;
    badTarget.target.valid = false;
    bad2.handle(badTarget);
    CHECK(bad2.state() == PlacementState::Invalid);
    // ...and Invalid is recoverable by picking again.
    bad2.handle(ev);
    CHECK(bad2.state() == PlacementState::SettingDirection);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadPlacement.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadPlacement.h`:
```cpp
#pragma once
#include "loads/LoadScene.h"

namespace Loads {

enum class PlacementState {
    Idle, SelectingLoadType, SelectingTarget, SettingDirection,
    SettingMagnitude, Previewing, Committed, EditingExistingLoad,
    Cancelled, Invalid
};

struct PlacementEvent {
    enum class Kind {
        TargetPicked, DirectionSet, ReverseDirection, MagnitudeSet,
        Cancel, Confirm, CameraChanged, DirModeChanged
    };
    Kind            kind = Kind::Cancel;
    TargetSelection target;
    glm::dvec3      anchor{0.0};
    glm::dvec3      direction{0.0};
    double          magnitudeSI = 0.0;
    Frame           frame   = Frame::World;
    DirMode         dirMode = DirMode::Free;
};

// Deterministic, side-effect-free except on its own draft. The only mutation of
// committed data happens in commit().
class Placement {
public:
    PlacementState        state() const { return m_state; }
    const LoadDefinition& draft() const { return m_draft; }

    void begin(LoadType t);                     // -> SelectingTarget (or
                                                //    SettingDirection for Gravity)
    bool beginEdit(LoadScene& s, LoadId id);    // -> EditingExistingLoad
    void handle(const PlacementEvent& e);
    LoadId commit(LoadScene& s);                // 0 when not committable
    void cancel();                              // -> Idle, committed data untouched

private:
    PlacementState m_state = PlacementState::Idle;
    LoadDefinition m_draft;
    bool           m_editing = false;
};

} // namespace Loads
```

`src/loads/LoadPlacement.cpp` — required transitions:

- `begin(t)`: reset the draft, set `type`, default `magnitude.display` per type
  (`Newton` for force/distributed, `NewtonM` for torque, `Megapascal` for
  pressure, `MetrePerSec2` for gravity), then
  `Gravity → SettingDirection`, `FixedSupport → SelectingTarget`, everything else
  `→ SelectingTarget`.
- `TargetPicked`: `e.target.valid == false` → `Invalid`; otherwise store the
  target and anchor and go to `SettingDirection`. `FixedSupport` has no direction
  or magnitude, so it jumps straight to `Previewing`. From `Invalid`, a valid
  pick recovers to `SettingDirection`.
- `DirectionSet`: reject non-finite or zero-length vectors (state unchanged);
  otherwise normalise, store `frame`/`dirMode`, `→ SettingMagnitude`. While in
  `SettingMagnitude`/`Previewing`/`EditingExistingLoad` a new direction updates
  the draft **without** regressing the state, so a student can keep nudging.
- `ReverseDirection`: negate `m_draft.dir`; state unchanged. This is an
  operation, never a `DirMode`.
- `MagnitudeSet`: reject non-finite; store; `→ Previewing` (or stay in
  `EditingExistingLoad`).
- `Cancel`: `→ Idle`, draft cleared. Never touches `LoadScene`.
- `CameraChanged`: explicitly a no-op on the draft — proof in code that orbiting
  cannot alter a stored world-space direction.
- `commit(scene)`: valid only from `Previewing` or `EditingExistingLoad`. When
  editing, call the matching `scene.set*` setters so the change is one undoable
  edit against the existing id; otherwise `scene.add(draft)`. Then `→ Idle`.

Add `src/loads/LoadPlacement.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add deterministic load placement state machine"
```

---

### Task 13: Engine seam — pass an immutable LoadSet into the solver

**Files:**
- Modify: `include/FEASolver.h`, `src/FEASolver.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Loads::LoadSet` (Task 8).
- Produces: `FEASolver::solveLinearStatic(model, visualScale, U_out, externalLoads)` and the matching `solveNonlinearStatic` / `solveBrittleFracture` overloads.

This is the one genuine engine change. It is additive: existing call sites keep
compiling and existing presets keep their exact behaviour.

- [ ] **Step 1: Write the failing test**

The solver needs GL-free linkage to be unit-testable, so this task's automated
check is a **compile-and-shape** test plus a runtime check through the existing
harness. Add to `tests/load_tests.cpp`:

```cpp
// Guards the invariant the seam depends on: DOF indexing is node*3 + component,
// and a LoadSet maps 1:1 onto the F / fixedNodes / singleDofFixed the solver
// already builds internally.
static void test_loadset_shape() {
    LoadSet s;
    s.nodalForces.push_back({2, glm::dvec3(1.0, -2.0, 3.0)});
    s.fixedNodes.push_back(5);
    s.singleDofFixed.push_back({7, 1});

    const int nNodes = 10;
    std::vector<double> F(static_cast<size_t>(nNodes) * 3, 0.0);
    for (auto& nf : s.nodalForces) {
        CHECK(nf.first >= 0 && nf.first < nNodes);
        F[nf.first*3 + 0] += nf.second.x;
        F[nf.first*3 + 1] += nf.second.y;
        F[nf.first*3 + 2] += nf.second.z;
    }
    CHECK_NEAR(F[2*3 + 0],  1.0, 1e-15);
    CHECK_NEAR(F[2*3 + 1], -2.0, 1e-15);
    CHECK_NEAR(F[2*3 + 2],  3.0, 1e-15);
    for (int n : s.fixedNodes) CHECK(n >= 0 && n < nNodes);
    for (auto& p : s.singleDofFixed) {
        CHECK(p.first >= 0 && p.first < nNodes);
        CHECK(p.second >= 0 && p.second <= 2);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: FAIL on the first run only if `LoadSet` is missing; if Task 8 landed,
this passes immediately and the real verification is Step 4's regression run.

- [ ] **Step 3: Write minimal implementation**

In `include/FEASolver.h`, above the class, forward-declare and include:
```cpp
#include "loads/LoadAdapter.h"   // Loads::LoadSet
#include <memory>
```

Change the three solve signatures to take an optional shared, const load set.
The parameter is passed **per invocation** — the solver never stores it, so a
worker thread cannot outlive it:
```cpp
    // When `externalLoads` is non-null, F / fixedNodes / singleDofFixed come
    // from it and the built-in loadType preset scan is skipped entirely.
    // Passed per call (never retained) so the async worker cannot dangle.
    bool solveLinearStatic(FEAModel& model, float visualScale = 1.0f,
                           Eigen::VectorXd* U_out = nullptr,
                           std::shared_ptr<const Loads::LoadSet> externalLoads = nullptr);

    bool solveNonlinearStatic(FEAModel& model,
                              float    visualScale = 1.0f,
                              NRParams params      = {},
                              std::shared_ptr<const Loads::LoadSet> externalLoads = nullptr);

    bool solveBrittleFracture(FEAModel& model,
                              float     visualScale = 1.0f,
                              int       maxIters    = 50,
                              std::shared_ptr<const Loads::LoadSet> externalLoads = nullptr);
```

In `src/FEASolver.cpp::solveLinearStatic`, the load branch currently spans from
the `if (loadType == LoadType::CantileverBendingZ)` BC scan (~line 837) through
the force assembly (~line 1240). Wrap it:

```cpp
    // ---- External (user-defined) load set: replaces the preset scan ----------
    if (externalLoads) {
        const Loads::LoadSet& ls = *externalLoads;
        for (const auto& nf : ls.nodalForces) {
            if (nf.first < 0 || nf.first >= nNodes) continue;
            F(nf.first * 3 + 0) += nf.second.x;
            F(nf.first * 3 + 1) += nf.second.y;
            F(nf.first * 3 + 2) += nf.second.z;
        }
        fixedNodes     = ls.fixedNodes;
        singleDofFixed = ls.singleDofFixed;

        model.totalAppliedForce = static_cast<float>(ls.totalForceN);
        model.appliedForcePerNode =
            ls.nodalForces.empty() ? 0.0f
            : static_cast<float>(ls.totalForceN / ls.nodalForces.size());
        model.nodalForceMagnitudes.assign(nNodes, 0.0f);
        for (const auto& nf : ls.nodalForces)
            model.nodalForceMagnitudes[nf.first] =
                static_cast<float>(glm::length(nf.second));

        std::cout << "[LOADS] external set: " << ls.nodalForces.size()
                  << " loaded nodes, " << fixedNodes.size() << " clamped nodes, "
                  << singleDofFixed.size() << " single-DOF constraints."
                  << std::endl;
    } else if (loadType == LoadType::CantileverBendingZ) {
        ...existing branch, unchanged...
    }
```

The existing chain becomes `else if` from `CantileverBendingZ` onward. Everything
after — symmetrisation, `recordAppliedForceArrows`, weld ties, the penalty BC
loop at ~line 1292, the solve cascade and `recordExternalForceArrows` — is
untouched and works unchanged, because it only ever reads `F`, `fixedNodes` and
`singleDofFixed`.

Apply the identical wrap in `solveNonlinearStatic` (its `fixedNodes` /
`singleDofFixed` declarations are at ~lines 1649–1650), and thread the parameter
straight through `solveBrittleFracture` to its nested `solveLinearStatic` call.

**Skip load symmetrisation when `externalLoads` is set** — the symmetry helpers
assume the preset's Y-only traction pattern and would silently rewrite a
user-defined 3-D load.

Add `src/loads/*.cpp` already in the list; no new test source. Note the main
executable now compiles the `loads` sources too — add all of them to `SOURCES`
in `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe` → `0 failures`
Then the critical check — **existing behaviour must be untouched**:
Run: `build\FEAPreProcessor.exe --regress all`
Expected: `=== aggregate: PASS (exit 0) ===`, identical to the recorded baseline.
Do not build while this runs; the exe is locked.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include src
git commit -m "Accept an optional immutable LoadSet per solve invocation"
```

---

### Task 14: Renderer contract + placeholder gizmos + app wiring

**Files:**
- Create: `include/loads/LoadVisual.h`, `src/loads/LoadVisual.cpp`, `src/loads/LoadGizmo.cpp`
- Modify: `src/main.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::VisualState`, `Loads::HitId`, `Loads::GizmoItem`, `Loads::buildVisuals(scene, ctx, out)`, `Loads::GizmoRenderer` (`build`, `draw`).

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadVisual.h"

static void test_visual_contract() {
    SurfaceCache sc; std::vector<glm::vec3> P; std::vector<unsigned> T;
    AdapterContext ctx = cubeCtx(sc, P, T);
    ResolveContext rc; rc.surface = &sc; rc.meshVersion = 1; rc.modelDiagonal = 1.732;

    Ray r; r.origin = glm::dvec3(0.5,0.5,5.0); r.dir = glm::dvec3(0,0,-1);
    SurfaceHit h; CHECK(pickFace(sc, r, h));
    const FaceSet top = growConnectedRegion(sc, h.face, 15.0);

    LoadScene scene;
    LoadDefinition L;
    L.type = LoadType::PointForce;
    L.target = captureSelection(rc, top, SelectionKind::Faces,
                                AnchorStrategy::AnchorCloud);
    L.dir = glm::dvec3(0,0,-1);
    L.magnitude.si = 250.0;
    L.magnitude.display = LoadUnits::UnitId::Newton;
    const LoadId id = scene.add(L);

    std::vector<GizmoItem> items;
    buildVisuals(scene, ctx, items);
    CHECK(items.size() == 1);
    const GizmoItem& g = items[0];
    CHECK(g.loadId == id);
    CHECK(g.type == LoadType::PointForce);
    CHECK(g.state == VisualState::Committed);
    CHECK_NEAR(glm::length(g.direction), 1.0, 1e-9);
    CHECK_NEAR(g.origin.z, 1.0, 1e-6);          // sits on the picked surface
    CHECK(g.label.find("250") != std::string::npos);
    CHECK(g.label.find("N")   != std::string::npos);
    CHECK(g.suggestedScale > 0.0);
    CHECK(g.hitId != HitId::None);
    CHECK(!g.regionFaces.empty());              // renderer can highlight it

    // Selection is reflected, not hard-coded.
    scene.select(id);
    items.clear();
    buildVisuals(scene, ctx, items);
    CHECK(items[0].state == VisualState::Selected);

    // Disabled loads report Disabled rather than vanishing.
    scene.setEnabled(id, false);
    items.clear();
    buildVisuals(scene, ctx, items);
    CHECK(items[0].state == VisualState::Disabled);

    // No colours leak into the algorithm layer.
    CHECK(sizeof(GizmoItem) > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadVisual.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadVisual.h` — plain structs, **no colours, no shaders, no
geometry detail**. The frontend agent owns appearance entirely.
```cpp
#pragma once
#include "loads/LoadScene.h"
#include "loads/LoadAdapter.h"

namespace Loads {

enum class VisualState { Hover, Selected, Preview, Committed, Disabled, Invalid };

// Stable handle for a draggable gizmo component, so hit-testing does not depend
// on draw order.
enum class HitId : uint32_t {
    None = 0, Shaft, Head, Ring, AxisHandle, RegionHandle, MagnitudeHandle
};

struct GizmoItem {
    LoadId      loadId = 0;
    LoadType    type   = LoadType::PointForce;
    VisualState state  = VisualState::Committed;

    glm::dvec3  origin{0.0};        // arrow tail / torque centre / region centroid
    glm::dvec3  direction{0.0};     // unit: force direction or torque axis
    double      magnitudeSI = 0.0;
    double      torqueSign  = 1.0;  // +1 = right-hand rule about `direction`

    std::vector<uint32_t> regionFaces;   // boundary faces to highlight
    glm::dvec3  regionNormal{0.0};
    double      regionArea   = 0.0;
    double      resultantN   = 0.0;      // pressure: p * A, for display only

    double      suggestedScale = 1.0;    // normalised visual size hint
    std::string label;                   // pre-formatted "250 N"
    HitId       hitId = HitId::Shaft;
};

// Snapshot the scene into renderer-ready items. Pure: reads the scene, writes
// `out`, mutates nothing.
void buildVisuals(const LoadScene& scene, const AdapterContext& ctx,
                  std::vector<GizmoItem>& out);

// Formats an SI value in its display unit, e.g. "250 N", "12.5 N.m", "2 MPa".
std::string formatMagnitude(const Magnitude& m);

} // namespace Loads
```

`src/loads/LoadVisual.cpp`: `buildVisuals` walks `scene.loads()`, uses
`target.cache` for `origin`/`regionFaces`/`regionNormal`/`regionArea`, resolves
`direction` through `dirObjectToWorld` when `frame == ObjectLocal`, computes
`suggestedScale` as a fraction of the model diagonal (so arrows stay readable at
any zoom), sets `state` from `enabled`/`selected()`/`target.valid`, and builds
`label` via `formatMagnitude`. Pressure items also fill
`resultantN = magnitude.si * regionArea_m2` for display only — the stored value
stays a pressure.

`src/loads/LoadGizmo.cpp`: a `GizmoRenderer` that builds one `GL_LINES` VBO from
`std::vector<GizmoItem>`, following the existing overlay pattern in
`FEAModel::buildForceArrowBuffers` / `buildSlicePreview` (dedicated VAO/VBO,
rebuilt only when the item list changes, drawn after the model). Placeholder
geometry only: a shaft-and-vee arrow for forces, a segmented circle plus axis
line for torque, outlined triangles for regions, small crosses for supports, a
down arrow for gravity. Flat default colour; the frontend agent replaces it.

`src/main.cpp` wiring:
- Add a `LoadScene`, `Placement`, `SurfaceCache` (rebuilt when
  `model.meshVersion` changes) and `GizmoRenderer` alongside the existing model.
- Add a **LOADS** block to the right-hand panel using existing `SimpleUI`
  widgets: six load-type buttons, a magnitude text field reusing the
  `showcaseMagText` char-callback pattern, a unit cycle button, direction-mode
  buttons (Free / ±X / ±Y / ±Z / Normal / Reverse), a scrollable list of
  committed loads with per-row select and delete, plus UNDO / REDO and a
  VALIDATE readout that renders `ValidationIssue::message` lines.
  Do **not** expose an ObjectLocal control — it is identical to World today.
- Implement the input priority chain in the render loop, before the camera sees
  anything:
  ```cpp
  // UI -> gizmo -> placement picking -> camera. Each layer may consume the
  // click; whatever is left falls through to the existing camera handlers.
  bool consumed = false;
  if (mouseX >= panelX) consumed = true;                     // 1. UI panel
  if (!consumed) consumed = gizmos.handleMouse(...);         // 2. gizmo drag
  if (!consumed && placement.state() != PlacementState::Idle)
      consumed = handlePlacementClick(...);                  // 3. picking
  // 4. camera: unchanged right/middle drag paths, never reached when consumed
  ```
  Left-click is claimed only by layers 2–3, and only while they are active, so
  camera orbit/pan/zoom behaviour is unchanged.
- Brush selection: while the left button is held during `SelectingTarget`, call
  `pickFace` each frame and `toggleFace` the hit into an accumulating `FaceSet`
  (guarding against re-toggling the same face within one drag). Shift-click uses
  the same `toggleFace` path for single faces.
- Pass the live section state into every pick as
  `SectionClip{model.sectionEnabled, model.sectionZModel}` so cut-away geometry
  is never selectable.
- Bind `Esc` to `placement.cancel()` **before** the existing
  `glfwSetWindowShouldClose` in `processInput` — cancel an in-progress placement
  first, and only close the window when nothing is in progress.
- Bind Delete/Backspace to `scene.remove(scene.selected())`, guarded so it does
  not fire while the magnitude text field has focus.
- Add a RUN button that calls `validateForSolve`, and on success builds the
  `LoadSet` and launches the existing `startComputeJob` with
  `solver->solveLinearStatic(model, 10.0f, nullptr, loadSet)`, capturing the
  `shared_ptr` in the worker lambda so it outlives the job.

Add `src/loads/LoadVisual.cpp` to `load_tests`; add `LoadVisual.cpp` and
`LoadGizmo.cpp` to the main `SOURCES`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe` → `0 failures`
Then launch the app: `build\FEAPreProcessor.exe`
Expected: the LOADS panel appears; clicking the part places a force; orbit still
works with right-drag; `Esc` cancels a placement without closing the window.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads src/main.cpp tests
git commit -m "Add renderer-facing visual contract, placeholder gizmos and app wiring"
```

---

### Task 15: Geometry-rebake pure helper

**Files:**
- Create: `include/loads/LoadRebake.h`, `src/loads/LoadRebake.cpp`
- Modify: `tests/load_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `Loads::rebakeLoads(const std::vector<LoadDefinition>&, const glm::mat4&) -> std::vector<LoadDefinition>`.

Nothing calls this today. It exists so the contract is executable rather than a
comment, because rebaking vertices does **not** move anchors on its own.

- [ ] **Step 1: Write the failing test**

```cpp
#include "loads/LoadRebake.h"
#include <glm/gtc/matrix_transform.hpp>

static void test_rebake() {
    LoadDefinition w;                       // world-space force
    w.id = 1; w.frame = Frame::World;
    w.dir = glm::dvec3(1,0,0);
    w.anchorModel = glm::dvec3(1,0,0);
    w.target.anchors.push_back({glm::dvec3(1,0,0), glm::dvec3(1,0,0), 1.0});
    w.target.valid = true;
    w.target.resolvedMeshVersion = 5;

    LoadDefinition o = w;                   // object-local force
    o.id = 2; o.frame = Frame::ObjectLocal;

    const glm::mat4 M = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                                    glm::vec3(0,0,1));
    const std::vector<LoadDefinition> out = rebakeLoads({w, o}, M);
    CHECK(out.size() == 2);

    // Anchors ALWAYS move with the geometry — otherwise they would silently
    // re-resolve onto the wrong surface.
    CHECK_NEAR(out[0].target.anchors[0].centroid.x, 0.0, 1e-9);
    CHECK_NEAR(out[0].target.anchors[0].centroid.y, 1.0, 1e-9);
    CHECK_NEAR(out[0].target.anchors[0].normal.x,   0.0, 1e-9);
    CHECK_NEAR(out[0].target.anchors[0].normal.y,   1.0, 1e-9);
    CHECK_NEAR(out[0].anchorModel.y, 1.0, 1e-9);

    // A World direction is physically fixed and must NOT rotate.
    CHECK_NEAR(out[0].dir.x, 1.0, 1e-9);
    CHECK_NEAR(out[0].dir.y, 0.0, 1e-9);

    // An ObjectLocal direction rides along with the object.
    CHECK_NEAR(out[1].dir.x, 0.0, 1e-9);
    CHECK_NEAR(out[1].dir.y, 1.0, 1e-9);

    // The resolved cache is invalidated: the caller still owes a re-resolve.
    CHECK(!out[0].target.valid);
    CHECK(out[0].target.resolvedMeshVersion == 0);

    // Magnitudes are untouched by a pure rotation.
    CHECK_NEAR(out[0].magnitude.si, w.magnitude.si, 1e-15);

    // Identity is exactly a no-op, and M then inverse(M) round-trips.
    const std::vector<LoadDefinition> same = rebakeLoads({w}, glm::mat4(1.0f));
    CHECK_NEAR(same[0].target.anchors[0].centroid.x, 1.0, 1e-15);
    const std::vector<LoadDefinition> back = rebakeLoads(out, glm::inverse(M));
    CHECK_NEAR(back[0].target.anchors[0].centroid.x, 1.0, 1e-9);
    CHECK_NEAR(back[1].dir.x, 1.0, 1e-9);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build.bat build`
Expected: FAIL — `Cannot open include file: 'loads/LoadRebake.h'`

- [ ] **Step 3: Write minimal implementation**

`include/loads/LoadRebake.h`:
```cpp
#pragma once
#include "loads/LoadModel.h"

namespace Loads {

// Pure part of the geometry-rebake contract. Given loads and the object->world
// matrix that was baked into the vertices, returns loads whose:
//   1. anchors (centroids, normals, application points) are transformed by M
//   2. ObjectLocal directions are transformed by M
//   3. World directions are LEFT ALONE (they are physically fixed)
//   4. resolved caches are invalidated
//
// Rebaking vertices does NOT move anchors by itself: anchors are stored
// coordinates, so without this the load would silently re-resolve onto the
// wrong surface.
//
// Steps 4 and 5 of the full contract -- bumping FEAModel::meshVersion and
// re-resolving every selection -- mutate model state and are therefore the
// CALLER's responsibility, not hidden inside this pure function.
//
// Nothing calls this today; no object-transform feature exists yet.
std::vector<LoadDefinition> rebakeLoads(const std::vector<LoadDefinition>& loads,
                                        const glm::mat4& M);

} // namespace Loads
```

`src/loads/LoadRebake.cpp`:
```cpp
#include "loads/LoadRebake.h"
#include "loads/LoadCoords.h"

namespace Loads {

std::vector<LoadDefinition> rebakeLoads(const std::vector<LoadDefinition>& loads,
                                        const glm::mat4& M) {
    std::vector<LoadDefinition> out;
    out.reserve(loads.size());
    for (LoadDefinition L : loads) {
        L.anchorModel = objectToWorld(L.anchorModel, M);

        for (auto& a : L.target.anchors) {
            a.centroid = objectToWorld(a.centroid, M);
            const glm::dvec3 n = dirObjectToWorld(a.normal, M);
            const double len = glm::length(n);
            if (len > 1e-15) a.normal = n / len;
            // area is preserved: this app's transforms are rigid.
        }

        // World directions are physically fixed; only object-local ones ride along.
        if (L.frame == Frame::ObjectLocal) {
            const glm::dvec3 d = dirObjectToWorld(L.dir, M);
            const double len = glm::length(d);
            if (len > 1e-15) L.dir = d / len;
        }

        // The caller still owes: bump meshVersion, then re-resolve.
        L.target.cache = Resolved{};
        L.target.valid = false;
        L.target.resolvedMeshVersion = 0;

        out.push_back(std::move(L));
    }
    return out;
}

} // namespace Loads
```

Add `src/loads/LoadRebake.cpp` to `load_tests`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build.bat build` then `build\load_tests.exe`
Expected: `0 failures`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/loads src/loads tests
git commit -m "Add pure geometry-rebake transform helper for the future rebake contract"
```

---

### Task 16: Renderer API documentation + manual verification

**Files:**
- Create: `docs/loads-renderer-api.md`
- Modify: `README.md` (roadmap + capability lines)

- [ ] **Step 1: Write the renderer-facing API document**

`docs/loads-renderer-api.md` must give the visual agent everything and assume no
context. Required contents:
- What the algorithm layer owns vs what the visual agent owns (colours,
  typography, icons, animation, shaders, final gizmo geometry).
- Full `GizmoItem` field reference with units and meaning, including that
  `magnitudeSI` is SI and `label` is pre-formatted for display.
- The `VisualState` and `HitId` enums, and how hit-test ids map to draggable
  components.
- The command interface with exact signatures: begin/update/end/cancel drag,
  `setMagnitude`, `setDirection`, `ReverseDirection`, `setTarget`, `commit`,
  `remove`, `undo`, `redo`.
- The input priority chain and the rule that a consumed event must not reach the
  camera.
- Worked example: replacing the placeholder arrow with custom geometry without
  touching the algorithm layer.
- Explicit statement that `GizmoItem` carries no colour and that
  `LoadVisual.h`/`LoadGizmo.cpp` are the only files the visual agent should need
  to edit.

- [ ] **Step 2: Run the manual verification matrix**

Launch `build\FEAPreProcessor.exe` and record the result of each, confirming that
visual direction, stored data and solver response agree:

1. **Cantilever** — fix one end face, push the other. Deflection direction matches the arrow.
2. **Uniform pressure** — pressure on one face; check the reported resultant equals `p × A`.
3. **Torque** — twist a shaft/tab; check the sign follows the right-hand rule and reversing flips it.
4. **Distributed force** — total force preserved; compare the reported total against the entered value.
5. **Gravity with density** — part sags downward. **Without density** — blocked with the plain-language message.
6. **Rotated camera** — orbit after committing a load; the arrow stays pinned to the same physical direction.
7. **Remeshed model** — change mesh quality, regenerate, confirm selections re-resolve or invalidate cleanly with a repair message.

The rotated-*object* case is deliberately absent: no object-transform feature
exists. Object-frame correctness is covered by the synthetic-matrix unit tests in
Tasks 3 and 15.

- [ ] **Step 3: Run the full verification suite**

```bash
build\load_tests.exe
build\FEAPreProcessor.exe --regress all
```
Expected: `0 failures`, and `=== aggregate: PASS (exit 0) ===` matching the
baseline recorded before implementation began.

- [ ] **Step 4: Update the README**

Add to the capability list and tick the roadmap: interactive load and
boundary-condition input (force, pressure, distributed force, torque, fixed
supports, gravity) with picking, validation and undo/redo. Mark
`- [x] Pressure boundary conditions`.

- [ ] **Step 5: Commit**

```bash
git add docs README.md
git commit -m "Document renderer-facing load API and record manual verification results"
```

---

## Verification Summary

| Gate | Command | Expected |
|---|---|---|
| Unit tests | `build\load_tests.exe` | `0 failures`, exit 0 |
| No regression | `build\FEAPreProcessor.exe --regress all` | `aggregate: PASS (exit 0)` |
| App runs | `build\FEAPreProcessor.exe` | LOADS panel usable; camera unchanged |

**Baseline recorded 2026-08-09 before any changes:** build exit 0;
`--regress all` → `=== aggregate: PASS (exit 0) ===` across 27 scenarios.
