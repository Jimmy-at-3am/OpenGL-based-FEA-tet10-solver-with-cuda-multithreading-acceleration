# Solve Section Divider Spacing Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the visual collision between `GCODE SHOWCASE` and its blue divider in the Solve inspector without changing any control or computation behavior.

**Architecture:** Add one GL-free section-header geometry contract to `UIDesign` and consume it from the G-code Solve-panel composition in `main.cpp`. The contract incorporates the existing essential-text floor, making the label baseline, divider position, and next-content baseline explicit and testable.

**Tech Stack:** C++17, custom OpenGL `SimpleUI`, CMake/Ninja, compiled `ui_design_tests`, CTest.

## Global Constraints

- Work only in branch `codex/apple-frontend` under `.worktrees/apple-frontend`.
- Preserve the approved 11 px essential-text floor and the 1.5 px System Blue divider.
- Place the divider 6 px below the heading baseline and the following content baseline 14 px below the divider.
- Do not change controls, values, callbacks, mesh/solve/load algorithms, asynchronous job behavior, or regression baselines.
- Do not launch the GUI; native OpenGL capture remains platform-limited. Use compiled geometry tests and headless verification.

---

### Task 1: Give the G-code Solve heading collision-free geometry

**Files:**
- Modify: `include/UIDesign.h` near `HorizontalSliderGeometry`
- Modify: `src/UIDesign.cpp` near `horizontalSliderGeometry`
- Modify: `tests/ui_design_tests.cpp` near the other geometry contract tests and test registry
- Modify: `src/main.cpp` in the `GCODE SHOWCASE` block near lines 1751-1765
- Include in final commit: `docs/superpowers/plans/2026-09-06-solve-section-divider-spacing.md`

**Interfaces:**
- Consumes: `float essentialTextPixelSize(float requested)`
- Produces: `SectionHeaderLayout sectionHeaderLayout(float topY, float requestedTextSize)`
- `SectionHeaderLayout` fields: `float labelBaselineY`, `float dividerY`, `float nextContentBaselineY`

- [x] **Step 1: Write the failing production-contract test**

Add this test to `tests/ui_design_tests.cpp` and register it as
`{"section header clears divider", testSectionHeaderKeepsDividerOutOfTextBand}`:

```cpp
void testSectionHeaderKeepsDividerOutOfTextBand() {
    const auto layout = ui_design::sectionHeaderLayout(100.0f, 9.5f);

    expectNear(layout.labelBaselineY, 111.0f);
    expectNear(layout.dividerY, 117.0f);
    expectNear(layout.nextContentBaselineY, 131.0f);
    expectTrue(layout.dividerY - layout.labelBaselineY >= 6.0f,
               "section divider must clear the accessible heading text band");
    expectTrue(layout.nextContentBaselineY - layout.dividerY >= 14.0f,
               "following content must clear the section divider");
}
```

This test catches a regression back to the current 8 px line-to-baseline
arrangement because the production layout API does not yet exist and, once it
does, either collapsed separation fails a literal geometry assertion.

- [x] **Step 2: Run the build and observe RED**

Run from the isolated worktree:

```powershell
Remove-Item Env:CL -ErrorAction SilentlyContinue
$env:_CL_='/I"E:\00e\00us\00TMI\02junior\05 advanced programming\code\0100FEAtrail1 - save3\include"'
.\build.bat build
```

Expected: compilation fails because `ui_design::sectionHeaderLayout` and/or
`SectionHeaderLayout` are not defined. The failure must be from the missing
production contract, not a toolchain or include-path error.

- [x] **Step 3: Add the minimal GL-free geometry contract**

Add to `include/UIDesign.h`:

```cpp
struct SectionHeaderLayout {
    float labelBaselineY;
    float dividerY;
    float nextContentBaselineY;
};

SectionHeaderLayout sectionHeaderLayout(
    float topY, float requestedTextSize);
```

Add to `src/UIDesign.cpp`:

```cpp
SectionHeaderLayout sectionHeaderLayout(
    float topY, float requestedTextSize) {
    const float effectiveTextSize = essentialTextPixelSize(requestedTextSize);
    const float labelBaselineY = topY + effectiveTextSize;
    const float dividerY = labelBaselineY + 6.0f;
    return {labelBaselineY, dividerY, dividerY + 14.0f};
}
```

- [x] **Step 4: Run the focused test and observe GREEN**

Run the same `build.bat build` command, then:

```powershell
& 'D:\program_files_delta\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir build -R '^ui_design_tests$' --output-on-failure
```

Expected: build exits 0 and `ui_design_tests` passes.

- [x] **Step 5: Wire the approved layout into the live Solve panel**

Replace the current full-width-line-first sequence in `src/main.cpp` with:

```cpp
const auto showcaseHeader = ui_design::sectionHeaderLayout(rY, 9.5f);
ui.drawText("GCODE SHOWCASE", rX, showcaseHeader.labelBaselineY, 9.5f,
            ui.themeColor(ui_design::ColorToken::PrimaryInk),
            ui_design::FontRole::Interface);
ui.drawRect(rX, showcaseHeader.dividerY, rW, 1.5f,
            glm::vec3(ui.themeColor(
                ui_design::ColorToken::SystemBlue, 0.5f)));
rY = showcaseHeader.nextContentBaselineY;
```

Leave the existing `LOAD`, magnitude, action, print-layer, toggle, and solver
code byte-for-byte unchanged after the new `rY` assignment.

- [x] **Step 6: Verify the exact final tree**

Build again, then run:

```powershell
& 'D:\program_files_delta\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir build `
  -R '^(load_physics_tests|ui_design_tests|ui_action_wiring_tests)$' `
  --output-on-failure

& 'D:\program_files_delta\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build --target ui_font_fallback_compile

git diff --check
git diff --name-only -- src/FEASolver.cpp src/FEAModel.cpp src/TetMesh.cpp `
  src/LoadPhysics.cpp regression scenarios
```

Expected: anchored tests pass 3/3, fallback compilation exits 0,
`git diff --check` exits 0, and the computation/regression path query prints
nothing.

Run the unchanged regression executable with vcpkg removed from only the child
process PATH:

```powershell
$env:PATH = (($env:PATH -split ';') |
  Where-Object { $_ -and ($_ -notmatch '(?i)vcpkg') }) -join ';'
Push-Location build
.\FEAPreProcessor.exe --regress all *> solve-divider-regression.log
$code = $LASTEXITCODE
Pop-Location
Get-Content build\solve-divider-regression.log -Tail 120
exit $code
```

Expected: 27 scenario PASS lines, zero FAIL lines, and
`=== aggregate: PASS (exit 0) ===`.

- [x] **Step 7: Review and commit the focused fix**

Inspect the staged diff and commit only the four production/test files plus this
plan:

```powershell
git add -- include/UIDesign.h src/UIDesign.cpp src/main.cpp `
  tests/ui_design_tests.cpp `
  docs/superpowers/plans/2026-09-06-solve-section-divider-spacing.md
git diff --cached --check
git diff --cached --stat
git commit -m "Fix Solve section divider spacing"
```

Expected: a clean worktree and a commit limited to the documented
presentation-only geometry change.
