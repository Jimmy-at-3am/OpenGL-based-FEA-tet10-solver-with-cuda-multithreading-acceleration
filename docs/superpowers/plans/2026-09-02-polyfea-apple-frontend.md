# PolyFEA Apple-Inspired Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current dense two-column control panel with the approved A1 Apple-inspired Model/Mesh/Solve inspector while preserving every existing computation path and user action.

**Architecture:** Keep the C++17 OpenGL/GLFW application and extend `SimpleUI`; do not add a parallel UI framework. Put pure layout, token, formatting, receipt, and input-navigation logic in GL-free modules so it can be tested independently, while `src/main.cpp` retains ownership of model and solver state and invokes the existing callbacks unchanged.

**Tech Stack:** C++17, OpenGL 3.3, GLFW, GLM, optional FreeType, CMake/Ninja/MSVC, CTest, existing headless `ScenarioRunner` regressions.

## Global Constraints

- Do not change `FEASolver`, `LoadPhysics`, `FEAModel`, TetGen, toolpath meshing, layer-slicing mathematics, material values, capability assessments, or result calculations.
- Do not change solver defaults, load magnitudes, ranges, units, enablement conditions, or async job behavior.
- Reuse existing state variables and callback bodies in `src/main.cpp`; relocation behind a new control must not duplicate, bypass, or reinterpret an operation.
- Extend the existing SimpleUI/OpenGL renderer; do not introduce ImGui, Qt, Electron, or a web layer.
- Do not remove a state-reachable current action.
- Do not add UI persistence, a new sidecar, or scenario-format fields.
- Inspector width is `clamp(320 px, 28% of the window width, 380 px)` with a 44 px title bar.
- Standard controls are at least 32 px high; essential text is at least 11 px.
- Use Frost `#E9EEF5`, Snow `#F7F7FA`, Ink `#1D1D1F`, Graphite `#6E6E73`, System blue `#007AFF`, and Blocked red `#C9342E` as the six base colors.
- Use Segoe UI Variable Display, Segoe UI Variable Text, and Cascadia Mono when present; never redistribute or depend on Apple's proprietary SF fonts.
- If font initialization fails, continue with the current stroke renderer and emit one diagnostic.
- Keep numeric values right-aligned, use tabular figures, and display units separately from numbers.
- Preserve the progress panel's reliable cancel path and the inspector's compute-time input lock.
- Treat any numerical regression as a failure; frontend work does not authorize baseline updates.

---

## Planned File Structure

| File | Responsibility |
|---|---|
| `include/UIDesign.h` | GL-free design tokens, geometry types, control IDs, number formatting, and receipt presentation types |
| `src/UIDesign.cpp` | Deterministic layout, color/state resolution, control manifest, formatting, and receipt helpers |
| `include/UIInteraction.h` | GL-free inspector tab, scrolling, focus, and key-action state |
| `src/UIInteraction.cpp` | Deterministic pointer ownership, scroll clamping, tab selection, and focus navigation |
| `include/UIFontRenderer.h` | RAII interface for optional font-backed text rendering |
| `src/UIFontRenderer.cpp` | FreeType glyph atlas, font fallback discovery, measurement, rendering, and cleanup |
| `include/SimpleUI.h` | Reusable rounded controls, clipping, text roles, and stable control-ID API |
| `src/SimpleUI.cpp` | OpenGL implementation of the approved component system; retains legacy overloads during migration |
| `include/ShaderSources.h` | Rounded-surface and font-text shader sources |
| `src/main.cpp` | A1 title bar, persistent inspector, three tabs, existing callbacks, receipts, overlays, and input routing |
| `tests/ui_design_tests.cpp` | GL-free tests for layout, tokens, formatting, manifests, receipts, scrolling, and focus |
| `tests/ui_source_contract_tests.cpp` | Source contract proving every required control ID is wired and the legacy two-column panel is removed |
| `CMakeLists.txt` | New sources, optional FreeType linkage, and two focused CTest targets |

---

### Task 1: Capture the Baseline and Add the GL-Free UI Contract

**Files:**
- Create: `include/UIDesign.h`
- Create: `src/UIDesign.cpp`
- Create: `tests/ui_design_tests.cpp`
- Modify: `CMakeLists.txt:220-260`
- Modify: `CMakeLists.txt:496-507`

**Interfaces:**
- Consumes: no new project interface; uses only the C++17 standard library.
- Produces:
  - `ui_design::Rect { float x, y, w, h; }`
  - `ui_design::WindowLayout { Rect titleBar, viewport, inspector; }`
  - `ui_design::InspectorTab { Model, Mesh, Solve }`
  - `ui_design::ColorToken { FrostCanvas, SnowSurface, PrimaryInk, Graphite, SystemBlue, BlockedRed }`
  - `ui_design::ControlId` containing every current action.
  - `ui_design::WidgetId { ControlId control; int instance; }` so repeated model/material rows never share pointer capture or focus identity.
  - `ui_design::FormattedValue { std::string number, unit; }`
  - `WindowLayout computeWindowLayout(int widthPx, int heightPx)`
  - `FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit)`
  - `const std::vector<ControlId>& requiredControls()`
  - `const std::vector<ControlId>& requiredInspectorControls()`
  - `const std::vector<ControlId>& requiredOverlayControls()`
  - `std::string_view controlToken(ControlId id)`

- [ ] **Step 1: Record the pre-change computational baseline**

Run from the repository root. Persist the exact pre-implementation commit in the ignored build directory for the final isolation audit:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
git rev-parse HEAD | Set-Content build/ui-baseline-commit.txt
& .\build.bat build
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: build exit code `0`, CTest exit code `0`, and `--regress all` exit code `0`. Save the terminal counts in the implementation notes; do not edit expected regression outputs.

- [ ] **Step 2: Write the failing layout, formatting, palette, and manifest tests**

Create `tests/ui_design_tests.cpp` with the repository's existing exception-based test harness pattern. Include these exact cases:

```cpp
void testWindowLayout() {
    const auto compact = ui_design::computeWindowLayout(1024, 768);
    expectNear(compact.titleBar.h, 44.0f);
    expectNear(compact.inspector.w, 320.0f);
    expectNear(compact.viewport.w + compact.inspector.w, 1024.0f);
    expectNear(compact.viewport.y, 44.0f);

    const auto wide = ui_design::computeWindowLayout(1920, 1080);
    expectNear(wide.inspector.w, 380.0f);
    expectTrue(wide.viewport.w >= 640.0f, "viewport must remain dominant");
}

void testNumericFormatting() {
    const auto fixed = ui_design::formatValue(1.4, 3, false, "p");
    expectEqual(fixed.number, "1.400");
    expectEqual(fixed.unit, "p");
    const auto exponential = ui_design::formatValue(0.01, 2, true, "%");
    expectEqual(exponential.number, "1.00e-02");
    expectEqual(exponential.unit, "%");
}

void testPaletteAndControlManifest() {
    expectEqual(ui_design::hex(ui_design::ColorToken::SystemBlue), "#007AFF");
    expectEqual(ui_design::hex(ui_design::ColorToken::BlockedRed), "#C9342E");
    const auto& ids = ui_design::requiredControls();
    expectAllUnique(ids);
    expectContains(ids, ui_design::ControlId::GenerateVolumeMesh);
    expectContains(ids, ui_design::ControlId::RunShowcaseFracture);
    expectContains(ids, ui_design::ControlId::RunLinearAnalysis);
    expectContains(ids, ui_design::ControlId::RunNonlinearAnalysis);
    expectContains(ids, ui_design::ControlId::RunAdaptiveAnalysis);
    expectContains(ids, ui_design::ControlId::RunBrittleFracture);
    expectContains(ids, ui_design::ControlId::CancelJob);
}
```

Register these cases in `main()` with names `window layout`, `numeric formatting`, and `palette and control manifest`.

- [ ] **Step 3: Register the test target and verify it fails**

Add to `CMakeLists.txt`:

```cmake
add_executable(ui_design_tests
    tests/ui_design_tests.cpp
    src/UIDesign.cpp)
target_include_directories(ui_design_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(ui_design_tests PRIVATE /W4 /WX)
else()
    target_compile_options(ui_design_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME ui_design_tests COMMAND ui_design_tests)
```

Run:

```powershell
& .\build.bat configure
& .\build.bat build
ctest --test-dir build -R ui_design_tests --output-on-failure
```

Expected: compilation fails because the declared UI design interfaces are not defined.

- [ ] **Step 4: Implement the minimal GL-free contract**

Define the public API in `include/UIDesign.h`:

```cpp
namespace ui_design {

struct Rect { float x, y, w, h; };
struct WindowLayout { Rect titleBar, viewport, inspector; };
enum class InspectorTab { Model, Mesh, Solve };
enum class ColorToken { FrostCanvas, SnowSurface, PrimaryInk, Graphite, SystemBlue, BlockedRed };

enum class ControlId {
    CancelJob, SelectCubeMode, SelectImportMode, SelectModelFile,
    PreviousModelPage, NextModelPage, SelectMaterial,
    ToggleVertexSmoothing, SelectSurfaceView, SelectVolumeView,
    GenerateVolumeMesh, ToggleSlicing, SelectSliceAxisX,
    SelectSliceAxisY, SelectSliceAxisZ, PreviewSlice,
    EditShowcaseMagnitude, ResetShowcaseMagnitude, RunShowcaseFracture,
    ToggleMultithreading, ToggleGpuAcceleration, SelectBuildAxis,
    SelectLoadPreset, RunLinearAnalysis, RunNonlinearAnalysis,
    RunAdaptiveAnalysis, ToggleFdmAnisotropy, RunBrittleFracture,
    SelectOriginalResult, SelectDeformedResult, SelectFractureView,
    SelectDeadElementView, ToggleForceMap, OpenHelp, ResetView
};

struct WidgetId {
    ControlId control;
    int instance = 0;
    friend bool operator==(const WidgetId& lhs, const WidgetId& rhs) {
        return lhs.control == rhs.control && lhs.instance == rhs.instance;
    }
};

struct FormattedValue { std::string number, unit; };
WindowLayout computeWindowLayout(int widthPx, int heightPx);
FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit);
std::string_view hex(ColorToken token);
const std::vector<ControlId>& requiredControls();
const std::vector<ControlId>& requiredInspectorControls();
const std::vector<ControlId>& requiredOverlayControls();
std::string_view controlToken(ControlId id);

}
```

In `computeWindowLayout`, reject non-positive dimensions with `std::invalid_argument`, compute `panelW = std::clamp(widthPx * 0.28f, 320.0f, 380.0f)`, use a 44 px title bar, and make viewport plus inspector equal the full width without overlap.

- [ ] **Step 5: Run focused tests and the full existing CTest set**

```powershell
& .\build.bat build
ctest --test-dir build -R ui_design_tests --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit the contract**

```powershell
git add CMakeLists.txt include/UIDesign.h src/UIDesign.cpp tests/ui_design_tests.cpp
git commit -m "Add testable frontend design contract"
```

---

### Task 2: Add Font-Backed Text and Apple-Style Rendering Primitives

**Files:**
- Create: `include/UIFontRenderer.h`
- Create: `src/UIFontRenderer.cpp`
- Modify: `include/UIDesign.h`
- Modify: `src/UIDesign.cpp`
- Modify: `tests/ui_design_tests.cpp`
- Modify: `include/ShaderSources.h`
- Modify: `include/SimpleUI.h`
- Modify: `src/SimpleUI.cpp`
- Modify: `CMakeLists.txt:220-325`

**Interfaces:**
- Consumes: `ui_design::Rect`, `ColorToken`, `FormattedValue`, and `ControlId` from Task 1.
- Produces:
  - `ui_design::FontRole { Display, Interface, Data }`
  - `ui_design::ControlRole { Primary, Secondary, Ghost, Destructive }`
  - `ui_design::ControlState { Rest, Hover, Pressed, Selected, Disabled, Focused }`
  - `ui_design::ControlVisual resolveControlVisual(ControlRole, ControlState)`
  - `ui_design::Rgba rgba(ColorToken, float opacity = 1.0f)`
  - `std::vector<std::filesystem::path> fontCandidates(FontRole)`
  - `UIFontRenderer::initialize`, `resize`, `measure`, `draw`, and `shutdown`.
  - New `SimpleUI` methods `drawRoundedRect`, `drawShadow`, `drawText` with a font role, `button` with stable ID and role, `segmentedControl`, `toggle`, `sliderField`, `pushClip`, and `popClip`.

- [ ] **Step 1: Write failing visual-state and font-fallback tests**

Append these cases to `tests/ui_design_tests.cpp`:

```cpp
void testControlVisualStates() {
    const auto primary = ui_design::resolveControlVisual(
        ui_design::ControlRole::Primary, ui_design::ControlState::Rest);
    expectEqual(primary.fill, ui_design::ColorToken::SystemBlue);
    expectEqual(primary.text, ui_design::ColorToken::SnowSurface);

    const auto blocked = ui_design::resolveControlVisual(
        ui_design::ControlRole::Secondary, ui_design::ControlState::Disabled);
    expectNear(blocked.contentOpacity, 0.38f);
}

void testFontCandidateOrder() {
    const auto interfaceFonts = ui_design::fontCandidates(ui_design::FontRole::Interface);
    const auto dataFonts = ui_design::fontCandidates(ui_design::FontRole::Data);
    expectTrue(!interfaceFonts.empty(), "interface font fallback list must not be empty");
    expectTrue(!dataFonts.empty(), "data font fallback list must not be empty");
    expectContainsFilename(interfaceFonts, "SegUIVar.ttf");
    expectContainsFilename(dataFonts, "CascadiaMono.ttf");
}
```

Run `ctest --test-dir build -R ui_design_tests --output-on-failure` and expect a compile failure for the missing types/functions.

- [ ] **Step 2: Implement deterministic visual states and font discovery**

Add these exact public shapes to `include/UIDesign.h`:

```cpp
enum class FontRole { Display, Interface, Data };
enum class ControlRole { Primary, Secondary, Ghost, Destructive };
enum class ControlState { Rest, Hover, Pressed, Selected, Disabled, Focused };
struct ControlVisual {
    ColorToken fill;
    ColorToken text;
    float fillOpacity;
    float contentOpacity;
    float focusOpacity;
};

struct Rgba { float r, g, b, a; };
ControlVisual resolveControlVisual(ControlRole role, ControlState state);
Rgba rgba(ColorToken token, float opacity = 1.0f);
std::vector<std::filesystem::path> fontCandidates(FontRole role);
```

On Windows, generate candidates from the Windows Fonts directory in this order:

```text
Display: SegUIVar.ttf, segoeuib.ttf, segoeui.ttf
Interface: SegUIVar.ttf, segoeui.ttf
Data: CascadiaMono.ttf, consola.ttf, SegUIVar.ttf
```

On other systems, return common system locations and always leave the stroke renderer available as the terminal fallback.

- [ ] **Step 3: Write the font renderer interface before OpenGL implementation**

Create `include/UIFontRenderer.h` with a private implementation so FreeType headers do not leak into callers:

```cpp
class UIFontRenderer {
public:
    UIFontRenderer();
    ~UIFontRenderer();
    UIFontRenderer(const UIFontRenderer&) = delete;
    UIFontRenderer& operator=(const UIFontRenderer&) = delete;

    bool initialize(int viewportWidth, int viewportHeight, float contentScale = 1.0f);
    void resize(int viewportWidth, int viewportHeight, float contentScale = 1.0f);
    bool ready(ui_design::FontRole role) const;
    float measure(std::string_view text, float pixelSize, ui_design::FontRole role) const;
    void draw(std::string_view text, float x, float baselineY, float pixelSize,
              const glm::vec4& color, ui_design::FontRole role);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 4: Add optional FreeType configuration**

Add `src/UIFontRenderer.cpp` to `SOURCES`, then add:

```cmake
find_package(Freetype QUIET)
if(Freetype_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Freetype::Freetype)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_FREETYPE_UI=1)
else()
    message(WARNING "FreeType not found: UI will use the built-in stroke font")
endif()
```

Guard every FreeType include and call in `src/UIFontRenderer.cpp` with `HAS_FREETYPE_UI`. Build one ASCII glyph atlas per font role, use one texture and per-glyph UVs, and delete all GL objects in `shutdown()`.

Add a compile-only fallback target in the same configured build so the no-FreeType branch is verified without downloading a second dependency tree:

```cmake
add_library(ui_font_fallback_compile OBJECT src/UIFontRenderer.cpp)
target_include_directories(ui_font_fallback_compile PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${glm_SOURCE_DIR})
target_compile_definitions(ui_font_fallback_compile PRIVATE UI_FONT_FORCE_FALLBACK=1)
```

Guard the FreeType branch with `#if defined(HAS_FREETYPE_UI) && !defined(UI_FONT_FORCE_FALLBACK)`.

- [ ] **Step 5: Add rounded and text shader sources**

Extend `include/ShaderSources.h` with dedicated rounded-rectangle and single-channel font shaders. The rounded fragment shader must calculate signed distance from the local rectangle coordinates and antialias the edge with `smoothstep`; the font shader must multiply text color alpha by the atlas red channel.

```glsl
float roundedBoxSdf(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}
```

- [ ] **Step 6: Extend SimpleUI without breaking legacy call sites**

Add these overloads to `include/SimpleUI.h`:

```cpp
void resize(int width, int height, float contentScale);
void drawRoundedRect(const ui_design::Rect& rect, float radius,
                     const glm::vec4& color);
void drawShadow(const ui_design::Rect& rect, float radius, float opacity);
void drawText(std::string_view text, float x, float baselineY, float pixelSize,
              const glm::vec4& color, ui_design::FontRole role);
bool button(ui_design::ControlId id, std::string_view label,
            const ui_design::Rect& rect, ui_design::ControlRole role,
            bool selected = false, bool disabled = false);
bool button(ui_design::WidgetId id, std::string_view label,
            const ui_design::Rect& rect, ui_design::ControlRole role,
            bool selected = false, bool disabled = false);
bool segmentedControl(const std::vector<ui_design::WidgetId>& ids,
                      const ui_design::Rect& rect,
                      const std::vector<std::string>& labels, int& selectedIndex,
                      bool disabled = false);
bool toggle(ui_design::ControlId id, std::string_view label,
            const ui_design::Rect& rect, bool& value, bool disabled = false);
bool sliderField(ui_design::ControlId id, std::string_view label,
                 float& value, float min, float max,
                 const ui_design::Rect& rect,
                 const ui_design::FormattedValue& display,
                 bool exponential = false, bool disabled = false);
void pushClip(const ui_design::Rect& rect);
void popClip();
glm::vec4 themeColor(ui_design::ColorToken token, float opacity = 1.0f) const;
void shutdown();
```

Keep the existing string-based `button`, `slider`, and `vslider` overloads operational until Task 4 completes migration. Stable `ControlId` values, not display text, own pointer capture and keyboard focus.

- [ ] **Step 7: Verify tests, optional-font and fallback builds**

```powershell
& .\build.bat configure
& .\build.bat build
ctest --test-dir build -R ui_design_tests --output-on-failure
```

Then compile the fallback branch explicitly:

```powershell
cmake --build build --target ui_font_fallback_compile
```

Expected: the application and fallback object target both compile, and the normal configure prints the one intended warning only when FreeType is unavailable.

- [ ] **Step 8: Commit the renderer foundation**

```powershell
git add CMakeLists.txt include/UIDesign.h src/UIDesign.cpp tests/ui_design_tests.cpp include/UIFontRenderer.h src/UIFontRenderer.cpp include/ShaderSources.h include/SimpleUI.h src/SimpleUI.cpp
git commit -m "Add Apple-style UI rendering primitives"
```

---

### Task 3: Add Testable Inspector Scrolling and Keyboard Navigation

**Files:**
- Create: `include/UIInteraction.h`
- Create: `src/UIInteraction.cpp`
- Modify: `tests/ui_design_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ui_design::Rect`, `WindowLayout`, `InspectorTab`, and `ControlId`.
- Produces:
  - `ui_interaction::InspectorState`
  - `bool ownsPoint(const WindowLayout&, float x, float y)`
  - `void selectTab(InspectorState&, InspectorTab)`
  - `float applyScroll(float current, float wheelDelta, float contentHeight, float viewportHeight)`
  - `std::optional<ControlId> nextFocus(const std::vector<ControlId>&, std::optional<ControlId>, int direction)`
  - `KeyIntent translateKey(Key key, bool pressed, bool shift)`

- [ ] **Step 1: Write failing interaction tests**

Add these cases:

```cpp
void testInspectorScrollOwnership() {
    const auto layout = ui_design::computeWindowLayout(1280, 800);
    expectTrue(ui_interaction::ownsPoint(layout, 1270.0f, 400.0f),
               "inspector must own pointer inside its bounds");
    expectFalse(ui_interaction::ownsPoint(layout, 200.0f, 400.0f),
                "viewport must retain its pointer input");
    expectNear(ui_interaction::applyScroll(20.0f, -1.0f, 900.0f, 600.0f), 56.0f);
    expectNear(ui_interaction::applyScroll(290.0f, -1.0f, 900.0f, 600.0f), 300.0f);
    expectNear(ui_interaction::applyScroll(10.0f, 1.0f, 900.0f, 600.0f), 0.0f);
}

void testFocusOrderSkipsHiddenControls() {
    const std::vector<ui_design::ControlId> visible = {
        ui_design::ControlId::SelectSurfaceView,
        ui_design::ControlId::SelectVolumeView,
        ui_design::ControlId::GenerateVolumeMesh,
    };
    expectEqual(*ui_interaction::nextFocus(visible, std::nullopt, 1), visible.front());
    expectEqual(*ui_interaction::nextFocus(visible, visible.back(), 1), visible.front());
    expectEqual(*ui_interaction::nextFocus(visible, visible.front(), -1), visible.back());
}
```

Run the focused CTest and expect compilation to fail because `UIInteraction` does not exist.

- [ ] **Step 2: Implement the pure interaction state**

Create this public state in `include/UIInteraction.h`:

```cpp
namespace ui_interaction {

enum class Key { Tab, Enter, Space, Left, Right, Up, Down, Escape, Other };
enum class KeyIntent { None, FocusNext, FocusPrevious, Activate, Decrease, Increase, Cancel };

struct InspectorState {
    ui_design::InspectorTab activeTab = ui_design::InspectorTab::Model;
    std::array<float, 3> scrollOffset{0.0f, 0.0f, 0.0f};
    std::optional<ui_design::ControlId> focused;
};

bool ownsPoint(const ui_design::WindowLayout& layout, float x, float y);
void selectTab(InspectorState& state, ui_design::InspectorTab tab);
float applyScroll(float current, float wheelDelta,
                  float contentHeight, float viewportHeight);
std::optional<ui_design::ControlId> nextFocus(
    const std::vector<ui_design::ControlId>& visible,
    std::optional<ui_design::ControlId> current,
    int direction);
KeyIntent translateKey(Key key, bool pressed, bool shift);

}
```

Use a 36 px scroll step per wheel unit, clamp to `[0, max(0, contentHeight - viewportHeight)]`, and clear focus when switching tabs.

- [ ] **Step 3: Register the source and run focused tests**

Add `src/UIInteraction.cpp` to `ui_design_tests` only; it remains GL-free.

```powershell
& .\build.bat build
ctest --test-dir build -R ui_design_tests --output-on-failure
```

Expected: all UI design and interaction tests pass.

- [ ] **Step 4: Commit the interaction model**

```powershell
git add CMakeLists.txt include/UIInteraction.h src/UIInteraction.cpp tests/ui_design_tests.cpp
git commit -m "Add testable inspector interaction state"
```

---

### Task 4: Replace the Legacy Panel with the A1 Inspector and Preserve Every Action

**Files:**
- Create: `tests/ui_source_contract_tests.cpp`
- Modify: `src/main.cpp:538-558`
- Modify: `src/main.cpp:664-1719`
- Modify: `src/main.cpp:1771-1820`
- Modify: `include/SimpleUI.h`
- Modify: `src/SimpleUI.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: all Task 1-3 APIs and every existing state variable/callback body in `src/main.cpp`.
- Produces:
  - The active A1 title bar and persistent inspector with complete Model, Mesh, and Solve tabs.
  - `void drawInspectorTabs(SimpleUI&, ui_interaction::InspectorState&, const ui_design::Rect&)`.
  - `std::optional<AppMode> drawModeSegment(SimpleUI&, AppMode, const ui_design::Rect&)`.
  - `std::optional<int> drawSelectableRows(SimpleUI&, ui_design::ControlId family, const std::vector<std::string>& labels, int firstIndex, int activeIndex, const ui_design::Rect&)`.
  - A source-level control-wiring contract.

- [ ] **Step 1: Write a failing source contract before moving controls**

Create `tests/ui_source_contract_tests.cpp`. Pass the absolute source path with a compile definition and test for all stable control tokens:

```cpp
int main() {
    const std::string source = readWholeFile(UI_MAIN_SOURCE);
    for (const auto id : ui_design::requiredInspectorControls()) {
        const std::string token = "ControlId::" + std::string(ui_design::controlToken(id));
        expectTrue(source.find(token) != std::string::npos,
                   ("missing frontend wiring: " + token).c_str());
    }
    expectTrue(source.find("float halfW  = panelW * 0.5f") == std::string::npos,
               "legacy two-column layout must be removed");
    expectTrue(source.find("ui.drawRect(divX") == std::string::npos,
               "legacy center divider must be removed");
    return 0;
}
```

Register it as:

```cmake
add_executable(ui_source_contract_tests
    tests/ui_source_contract_tests.cpp
    src/UIDesign.cpp)
target_include_directories(ui_source_contract_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ui_source_contract_tests PRIVATE
    UI_MAIN_SOURCE="${CMAKE_SOURCE_DIR}/src/main.cpp")
add_test(NAME ui_source_contract_tests COMMAND ui_source_contract_tests)
```

Apply the same `/W4 /WX` or `-Wall -Wextra -Wpedantic -Werror` policy as `ui_design_tests`.

Run `ctest --test-dir build -R ui_source_contract_tests --output-on-failure` and expect failure on missing `ControlId` wiring and the legacy divider.

- [ ] **Step 2: Introduce the title bar, inspector geometry, tabs, and clipping**

At the UI frame start, replace `panelWidth = 600.0f`, `halfW`, and `divX` composition with:

```cpp
const ui_design::WindowLayout uiLayout =
    ui_design::computeWindowLayout(static_cast<int>(scrWidth), static_cast<int>(scrHeight));
panelWidth = uiLayout.inspector.w;
const std::string documentTitle = model.loadedFileName.empty()
    ? "PolyFEA"
    : "PolyFEA · " + fs::path(model.loadedFileName).filename().string();
const ui_design::Rect inspectorContentRect{
    uiLayout.inspector.x + 16.0f,
    uiLayout.inspector.y + 60.0f,
    uiLayout.inspector.w - 32.0f,
    uiLayout.inspector.h - 76.0f};

ui.drawRoundedRect(uiLayout.titleBar, 0.0f,
                   ui.themeColor(ui_design::ColorToken::SnowSurface, 0.96f));
ui.drawText(documentTitle, 54.0f, 28.0f, 15.0f,
            ui.themeColor(ui_design::ColorToken::PrimaryInk),
            ui_design::FontRole::Display);

ui.drawRoundedRect(uiLayout.inspector, 0.0f,
                   ui.themeColor(ui_design::ColorToken::SnowSurface, 0.96f));
drawInspectorTabs(ui, inspectorState, uiLayout.inspector);
ui.pushClip(inspectorContentRect);
```

Render only the selected tab, apply its stored scroll offset, and call `ui.popClip()` after the active tab.

- [ ] **Step 3: Move Model controls without changing callbacks**

Preserve the bodies currently attached to:

```text
CUBE MODE
IMPORT FILE
each model filename
previous/next model page
each material filename
```

Use these stable controls:

```cpp
if (const auto selected = drawModeSegment(ui, currentMode, modelTabRect)) {
    if (*selected == MODE_CUBE) {
        currentMode = MODE_CUBE;
        model.needsUpdate = true;
        camera.OrbitTarget = glm::vec3(0.0f);
        camera.OrbitRadius = 5.0f;
        camera.UpdatePosition();
    } else {
        currentMode = MODE_IMPORT;
        scanForModels();
        modelListPage = 0;
    }
}
const auto selectedModel = drawSelectableRows(
    ui, ControlId::SelectModelFile, modelFiles, modelListPage * kModelsPerPage,
    activeModelIndex, modelRowsRect);
const auto selectedMaterial = drawSelectableRows(
    ui, ControlId::SelectMaterial, matFiles, 0,
    activeMaterialIndex, materialRowsRect);
```

`drawSelectableRows` must construct `WidgetId{family, index}` for each visible row. Resolve `activeModelIndex` and `activeMaterialIndex` from the same filename equality currently used for active styling; do not introduce new selection state. For each returned index, execute the existing callback body unchanged. Keep format badges, retained B-rep text, object count, active material, Young's modulus, Poisson ratio, and density visible in the Model receipt.

- [ ] **Step 4: Move Mesh controls without changing callbacks or ranges**

Preserve the exact current ranges and mappings:

```text
Cube size: X 0.1-10.0 m, Y/Z 0.1-5.0 m
Subdivisions: 1-20
Mesh quality: 1.1-3.0
Maximum volume: 0.00001-0.2%, exponential
Layer thickness: 0.01-1.0
Maximum slabs: 2-200
Wall width: 0.05-2.0
```

Wire `ControlId::ToggleVertexSmoothing`, `SelectSurfaceView`, `SelectVolumeView`, `GenerateVolumeMesh`, `ToggleSlicing`, `SelectSliceAxisX/Y/Z`, and `PreviewSlice` to the existing bodies. Keep the current `model.hasToolpath()` branch inside Generate volume mesh so G-code still uses `ToolpathSections` plus `SlabMesher`, while other imports still use TetGen.

- [ ] **Step 5: Move Solve and Results controls without changing construction of solver objects**

Wire all of these IDs to their existing state/callback bodies:

```text
EditShowcaseMagnitude
ResetShowcaseMagnitude
RunShowcaseFracture
ToggleMultithreading
ToggleGpuAcceleration
SelectBuildAxis
SelectLoadPreset
RunLinearAnalysis
RunNonlinearAnalysis
RunAdaptiveAnalysis
ToggleFdmAnisotropy
RunBrittleFracture
SelectOriginalResult
SelectDeformedResult
SelectFractureView
SelectDeadElementView
ToggleForceMap
```

The solver-launching blocks must still assign the same fields and call the same functions:

```cpp
solver->solveLinearStatic(model, 10.0f);
solver->solveNonlinearStatic(model, 10.0f, nrp);
solver->solveAdaptive(model, 10.0f);
solver->solveBrittleFracture(model, 10.0f, 50);
solver->solveBrittleFracture(model, 10.0f, 14); // showcase
```

Do not factor these calls into new solver wrappers during the frontend migration.

- [ ] **Step 6: Replace cyclic discovery controls with explicit selection while retaining values**

- Build axis: show X, Y, and Z as a three-option segment and write the same integer values `0`, `1`, and `2`.
- Load preset: show all six current `LoadPresetOption` labels in a picker and write the same `loadTypeSel` index.
- Fracture view: expose Deform, Mode, Crack order, and Stress and write the same values `1`, `3`, `4`, and `5`.
- Dead elements: expose Hidden, Ghost, and Colored and write the same `FEAModel::FractureDeadView` values.

- [ ] **Step 7: Route pointer and wheel ownership through the computed inspector bounds**

Replace `mouseX >= scrWidth - panelWidth` tests with a layout computed inside each callback:

```cpp
const auto layout = ui_design::computeWindowLayout(
    static_cast<int>(scrWidth), static_cast<int>(scrHeight));
if (ui_interaction::ownsPoint(layout, mouseX, mouseY)) {
    pendingInspectorWheel += static_cast<float>(yoffset);
    return;
}
camera.ProcessMouseScroll(static_cast<float>(yoffset));
```

Consume and clear `pendingInspectorWheel` once per UI frame. Do not alter right-button orbit or middle-button pan outside the inspector.

- [ ] **Step 8: Run source contract, build, CTest, and a startup smoke test**

```powershell
& .\build.bat build
ctest --test-dir build -R "ui_design_tests|ui_source_contract_tests" --output-on-failure
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: all commands exit `0`, and the regression count matches Task 1 exactly.

- [ ] **Step 9: Commit the active inspector**

```powershell
git add CMakeLists.txt src/main.cpp include/SimpleUI.h src/SimpleUI.cpp tests/ui_source_contract_tests.cpp
git commit -m "Replace legacy panel with tabbed inspector"
```

---

### Task 5: Add Contextual Receipts and Honest Disabled States

**Files:**
- Modify: `include/UIDesign.h`
- Modify: `src/UIDesign.cpp`
- Modify: `tests/ui_design_tests.cpp`
- Modify: `src/main.cpp:806-855`
- Modify: `src/main.cpp:1157-1165`
- Modify: `src/main.cpp:1256-1291`

**Interfaces:**
- Consumes: current model metadata, toolpath meshing statistics, and `load_physics::describePreset` / `assessPreset` results.
- Produces:
  - `ui_design::ReceiptTone { Neutral, Available, Approximate, Blocked }`
  - `ui_design::ReceiptLine { std::string label, value; ReceiptTone tone; }`
  - `std::vector<ReceiptLine> makeModelReceipt(std::string_view format, bool brepRetained, int objectCount, std::string_view physicalSize)`
  - `std::vector<ReceiptLine> makeMeshReceipt(std::string_view source, std::string_view elementType, std::uint64_t elementCount, int printLayers, int slabs, int layersPerSlab)`
  - `std::vector<ReceiptLine> makeSolveReceipt(std::string_view load, std::string_view scope, std::string_view distribution, std::string_view support, std::string_view capability, ReceiptTone capabilityTone)`

- [ ] **Step 1: Write failing receipt-presentation tests**

Add exact cases that prove text carries meaning independently of color:

```cpp
void testReceiptPresentation() {
    const auto blocked = ui_design::makeSolveReceipt(
        "Surface compression Y", "BBox face", "Linear facet tributary",
        "Y-min fixed", "NONLINEAR BLOCKED: NR -> Y COMPRESSION",
        ui_design::ReceiptTone::Blocked);
    expectReceiptContains(blocked, "Surface compression Y");
    expectReceiptContains(blocked, "Linear facet tributary");
    expectReceiptContains(blocked, "NONLINEAR BLOCKED");
    expectEqual(blocked.back().tone, ui_design::ReceiptTone::Blocked);

    const auto mesh = ui_design::makeMeshReceipt(
        "STEP · B-rep retained", "Tet10", 48216, 0, 0, 0);
    expectReceiptContains(mesh, "48,216");
    expectReceiptContains(mesh, "Tet10");
}
```

Implement `expectReceiptContains` in the test file by searching both `ReceiptLine::label` and `ReceiptLine::value` and throwing when neither contains the requested text.

Run the focused test and expect compilation to fail for the missing receipt types/functions.

- [ ] **Step 2: Implement presentation-only receipt helpers**

Receipt helpers accept already-computed strings and counts. They may format and label those values, but they must not assess physics, infer support, recompute element type, or convert an unsupported state into an available one.

```cpp
struct ReceiptLine {
    std::string label;
    std::string value;
    ReceiptTone tone = ReceiptTone::Neutral;
};
```

Use comma-grouping for non-negative element counts. Use explicit words `Available`, `Approximate`, and `Blocked` in the value text.

- [ ] **Step 3: Render one contextual receipt in each tab**

- Model: source format, B-rep retention, object count, and physical dimensions.
- Mesh: meshing path, current element kind/count, and G-code printed-layer/slab mapping.
- Solve: current `scopeSummary`, `distributionSummary`, `supportSummary`, mode capability names, and exact blocked reason.

The receipt uses an Ink-at-8%-opacity background and a 3 px System-blue leading rule. Blocked rows use Blocked-red text and the word `Blocked`.

- [ ] **Step 4: Keep blocked actions visible and explanatory**

Render Linear, Nonlinear, Adaptive, and Brittle fracture actions even when disabled. Pass the existing `canRun()` result to the disabled state and render the existing capability reason immediately above the action group.

- [ ] **Step 5: Run focused and full verification**

```powershell
& .\build.bat build
ctest --test-dir build -R ui_design_tests --output-on-failure
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: all exit `0` with the unchanged Task 1 regression count.

- [ ] **Step 6: Commit the receipts**

```powershell
git add include/UIDesign.h src/UIDesign.cpp tests/ui_design_tests.cpp src/main.cpp
git commit -m "Add contextual analysis receipts"
```

---

### Task 6: Restyle Viewport Help, Section, Status, and Progress Surfaces

**Files:**
- Modify: `src/main.cpp:308-497`
- Modify: `src/main.cpp:485-497`
- Modify: `src/main.cpp:1583-1719`
- Modify: `include/SimpleUI.h`
- Modify: `src/SimpleUI.cpp`
- Modify: `tests/ui_source_contract_tests.cpp`

**Interfaces:**
- Consumes: approved SimpleUI primitives, `ControlId::OpenHelp`, `ControlId::ResetView`, `ControlId::CancelJob`, current `SolverStatus` snapshots, current section state, and current `ComputeJob` fields.
- Produces: matching title-bar Help/Reset view actions, section control, solver status, and progress surface.

- [ ] **Step 1: Extend the source contract for overlay actions**

Extend the source contract to iterate `requiredOverlayControls()` in addition to the inspector controls, then reject the legacy README label:

```cpp
for (const auto id : ui_design::requiredOverlayControls()) {
    const std::string token = "ControlId::" + std::string(ui_design::controlToken(id));
    expectTrue(source.find(token) != std::string::npos,
               ("missing overlay wiring: " + token).c_str());
}
expectContainsSource(source, "ControlId::OpenHelp");
expectContainsSource(source, "ControlId::ResetView");
expectContainsSource(source, "ControlId::CancelJob");
expectTrue(source.find("ui.button(\"README\"") == std::string::npos,
           "README button must be replaced by Help");
```

Run the source contract and expect the README assertion to fail.

- [ ] **Step 2: Move README content to Help without deleting content**

Keep every current help topic and description. Open it from the title bar as a Snow-surface floating panel with a 14 px radius, readable paragraph wrapping, and a close action. Help must hide the sectional slider while open, matching the current click-conflict prevention.

- [ ] **Step 3: Restyle the sectional slider**

Keep its value range `0..zSpanMM`, physical-mm conversion, `sectionEnabled` threshold, and `sectionZModel` calculation unchanged. Draw a 4 px track, 16 px thumb, separate maximum and current-value labels, and the same left-edge placement.

- [ ] **Step 4: Restyle solver-stage status without changing snapshots**

Keep `SolverStatus::snapshot`, the 11-row cap, newest-biased completed-stage retention, device names, thread counts, elapsed time, percentages, and active-stage progress bars. Change only type roles, spacing, and token-derived colors.

- [ ] **Step 5: Restyle progress and preserve cancel reliability**

Keep the 220 ms ease-out entrance, determinate/indeterminate modes, current progress source, `mouseClickLatch` behavior, and `g_job.cancel = true` assignment. Replace the small `X` with the labeled destructive `Cancel` control using `ControlId::CancelJob`.

- [ ] **Step 6: Add Reset view through the existing camera operations**

The new title-bar action must perform the same reset already used after selecting Cube or loading a model:

```cpp
camera.OrbitTarget = glm::vec3(0.0f);
camera.OrbitRadius = 5.0f;
camera.UpdatePosition();
```

This is a frontend convenience calling existing camera behavior, not a new geometry operation.

- [ ] **Step 7: Run build and source contract**

```powershell
& .\build.bat build
ctest --test-dir build -R "ui_design_tests|ui_source_contract_tests" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 8: Commit the viewport surfaces**

```powershell
git add src/main.cpp include/SimpleUI.h src/SimpleUI.cpp tests/ui_source_contract_tests.cpp
git commit -m "Restyle viewport controls and progress"
```

---

### Task 7: Complete Keyboard Access, Busy-State Locking, and Responsive QA Guards

**Files:**
- Modify: `include/UIInteraction.h`
- Modify: `src/UIInteraction.cpp`
- Modify: `tests/ui_design_tests.cpp`
- Modify: `include/SimpleUI.h`
- Modify: `src/SimpleUI.cpp`
- Modify: `src/main.cpp:538-558`
- Modify: `src/main.cpp:1583-1719`
- Modify: `src/main.cpp:1771-1820`

**Interfaces:**
- Consumes: Task 3 `KeyIntent`, active-tab visible-control order, existing `inputLocked`, existing `g_job.cancel`, and GLFW callbacks.
- Produces: visible focus, Tab/Shift+Tab traversal, Enter/Space activation, arrow-key adjustments, Escape cancellation/Help closure, and deterministic responsive guards.

- [ ] **Step 1: Add failing key-translation and busy-state tests**

```cpp
void testKeyTranslation() {
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Tab, true, false),
                ui_interaction::KeyIntent::FocusNext);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Tab, true, true),
                ui_interaction::KeyIntent::FocusPrevious);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Space, true, false),
                ui_interaction::KeyIntent::Activate);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Escape, true, false),
                ui_interaction::KeyIntent::Cancel);
}

void testCompactLayoutKeepsPositiveViewport() {
    const auto layout = ui_design::computeWindowLayout(800, 600);
    expectTrue(layout.viewport.w > 0.0f, "compact viewport width must remain positive");
    expectTrue(layout.inspector.w <= 380.0f, "inspector must respect maximum width");
}
```

Run the focused test and expect failure until key translation and compact-width behavior match.

- [ ] **Step 2: Queue key intents from the GLFW callback**

Preserve Backspace/Enter handling for the G-code numeric field, map GLFW keys to the GL-free enum, then add one pending `KeyIntent` per frame. Do not mutate solver or UI state directly inside the callback.

```cpp
ui_interaction::Key mapGlfwKey(int key) {
    switch (key) {
        case GLFW_KEY_TAB: return ui_interaction::Key::Tab;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return ui_interaction::Key::Enter;
        case GLFW_KEY_SPACE: return ui_interaction::Key::Space;
        case GLFW_KEY_LEFT: return ui_interaction::Key::Left;
        case GLFW_KEY_RIGHT: return ui_interaction::Key::Right;
        case GLFW_KEY_UP: return ui_interaction::Key::Up;
        case GLFW_KEY_DOWN: return ui_interaction::Key::Down;
        case GLFW_KEY_ESCAPE: return ui_interaction::Key::Escape;
        default: return ui_interaction::Key::Other;
    }
}
```

- [ ] **Step 3: Build visible focus order from the active tab only**

Each tab appends enabled and disabled visible `ControlId` values in draw order. Tab/Shift+Tab wraps within that vector. Hidden-tab IDs and contextually absent controls never receive focus.

- [ ] **Step 4: Implement keyboard activation and adjustment**

- Enter/Space calls the same returned-intent branch as a pointer click.
- Left/Right changes segmented controls to the adjacent option.
- Arrow keys adjust sliders by one percent of a linear range; exponential sliders move by one percent in logarithmic parameter space.
- Clamp through the same min/max code used by pointer dragging.
- Escape sets `g_job.cancel = true` only when the current job is cancellable; otherwise it closes Help.

- [ ] **Step 5: Preserve compute-time locking**

When `computeBusy()` is true, Model/Mesh/Solve mutation controls ignore pointer and keyboard activation. Help, status, and Cancel remain interactive. Apply Graphite overlay/dimming without changing underlying control values.

- [ ] **Step 6: Add focus and reduced-motion presentation**

Draw a 3 px System-blue ring at 24% opacity around the focused control. Query the Windows client-animation preference when available; set tab/segment transition durations to zero when animations are disabled. Non-Windows builds use the normal 140-180 ms transitions.

At initialization and whenever GLFW reports a content-scale change, call:

```cpp
float xScale = 1.0f;
float yScale = 1.0f;
glfwGetWindowContentScale(window, &xScale, &yScale);
ui.resize(static_cast<int>(scrWidth), static_cast<int>(scrHeight),
          std::max(xScale, yScale));
```

Rebuild font atlases only when the effective content scale changes, so high-DPI text remains sharp without per-frame texture churn.

- [ ] **Step 7: Run focused tests, full CTest, and regressions**

```powershell
& .\build.bat build
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: all exit `0`; regression count matches Task 1.

- [ ] **Step 8: Commit accessibility and state handling**

```powershell
git add include/UIInteraction.h src/UIInteraction.cpp tests/ui_design_tests.cpp include/SimpleUI.h src/SimpleUI.cpp src/main.cpp
git commit -m "Add accessible inspector interaction"
```

---

### Task 8: Perform Full Control-Reachability and Visual Verification

**Files:**
- Modify only if a verified defect is found: `src/main.cpp`, `src/SimpleUI.cpp`, `include/SimpleUI.h`, `src/UIDesign.cpp`, `include/UIDesign.h`, `src/UIInteraction.cpp`, `include/UIInteraction.h`, `src/UIFontRenderer.cpp`, `include/UIFontRenderer.h`, or focused tests.
- Do not modify: regression expected outputs or computation sources.

**Interfaces:**
- Consumes: completed Tasks 1-7.
- Produces: evidence that the UI matches the approved A1 design and the computational baseline is unchanged.

- [ ] **Step 1: Run formatting, build, test, and regression gates**

```powershell
git diff --check
& .\build.bat build
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: every command exits `0`, with the same regression count as Task 1.

- [ ] **Step 2: Verify the optional-font fallback branch again**

```powershell
cmake --build build --target ui_font_fallback_compile
ctest --test-dir build -R "ui_design_tests|ui_source_contract_tests" --output-on-failure
```

Expected: the no-FreeType source branch compiles and both GL-free tests pass.

- [ ] **Step 3: Exercise the control-reachability matrix manually**

At minimum, observe and record each state:

```text
Cube: dimensions, subdivisions, mesh settings, generation, slicing
STL/3MF/STEP: file selection, metadata, material, smoothing, meshing, slicing
G-code: toolpath meshing, magnitude edit/reset, CPU/GPU, showcase fracture
Generic solve: all six load presets and three build axes
Solvers: linear, nonlinear blocked/available state, adaptive, brittle fracture
Results: original/deformed, four fracture modes, three dead-element modes, force map
Overlays: Help, Reset view, section slider, solver stages, progress, Cancel
```

For each item, verify both pointer and keyboard reachability and compare its resulting console/job behavior with the legacy callback documented in the spec.

- [ ] **Step 4: Inspect the visual matrix**

Run the app at:

```text
1024 x 768
1280 x 800
1920 x 1080
one available high-DPI Windows scale
```

Capture the Model, Mesh, Solve, Help, busy/progress, blocked-capability, and deformation/fracture-result states. Confirm:

```text
No essential text below 11 px
No clipped labels or numeric values
Units remain separate and visible
Only one blue primary action per group
Inspector scroll does not zoom the camera
Viewport scroll still zooms the camera
Long filenames remain distinguishable
Progress, solver status, and section controls do not overlap
Focus ring is visible on every control type
```

- [ ] **Step 5: Fix only observed frontend defects and rerun the smallest relevant gate**

For a layout or state defect, add or tighten a focused assertion in `ui_design_tests` or `ui_source_contract_tests`, run it to observe failure, make the smallest frontend correction, then rerun that test and the full Task 8 Step 1 gate. Do not update solver/regression baselines.

- [ ] **Step 6: Review the final diff for computation isolation**

```powershell
$preImplementationCommit = Get-Content build/ui-baseline-commit.txt
git diff --stat "$preImplementationCommit..HEAD"
git diff "$preImplementationCommit..HEAD" -- src/FEASolver.cpp src/LoadPhysics.cpp src/FEAModel.cpp src/LayerSlicer.cpp src/SlabMesher.cpp src/ToolpathSections.cpp regression scenarios
```

Expected: the second command is empty.

- [ ] **Step 7: Commit any verified polish fixes**

If Step 5 changed files:

```powershell
git add CMakeLists.txt include/UIDesign.h include/UIInteraction.h include/UIFontRenderer.h include/SimpleUI.h src/UIDesign.cpp src/UIInteraction.cpp src/UIFontRenderer.cpp src/SimpleUI.cpp src/main.cpp tests/ui_design_tests.cpp tests/ui_source_contract_tests.cpp
git commit -m "Polish and verify PolyFEA inspector"
```

If Step 5 made no changes, do not create an empty commit.

---

## Plan Self-Review

### Spec coverage

- A1 inspector, sizing, title bar, three tabs, scrolling: Tasks 1, 3, and 4.
- Exact six-color system, typography, geometry, buttons, segments, switches, sliders, numbers, glass restraint, and motion: Tasks 1, 2, 6, and 7.
- No-deletion contract and all current button families: Tasks 1, 4, 6, and 8.
- Analysis receipts and physically honest capability text: Task 5.
- Keyboard, focus, hit targets, reduced motion, input ownership, and busy-state locking: Tasks 3, 4, and 7.
- Empty, disabled, blocked, busy, cancelling, and font-failure states: Tasks 2, 5, 6, and 7.
- Computation isolation, focused tests, full CTest, headless regressions, fallback build, and visual matrix: Tasks 1 and 8, with gates repeated after risky tasks.

### Placeholder scan

The plan contains no unresolved requirement, unnamed implementation step, or deferred error-handling instruction. Every created interface has a named file, signature, consumer, test, and verification command.

### Type consistency

- `ui_design::Rect`, `WindowLayout`, `InspectorTab`, `ColorToken`, `ControlId`, `WidgetId`, `FormattedValue`, `FontRole`, `ControlRole`, `ControlState`, and `ReceiptLine` originate in `UIDesign.h` and retain those names in every later task.
- `ui_interaction::InspectorState`, `Key`, `KeyIntent`, `ownsPoint`, `applyScroll`, `nextFocus`, and `translateKey` originate in `UIInteraction.h` and retain those names in integration steps.
- `UIFontRenderer` owns optional font GL resources and remains separate from pure UI tests.
- Stable `ControlId` values, rather than display strings, are the identity used by SimpleUI, source-contract tests, focus order, and main UI wiring.
