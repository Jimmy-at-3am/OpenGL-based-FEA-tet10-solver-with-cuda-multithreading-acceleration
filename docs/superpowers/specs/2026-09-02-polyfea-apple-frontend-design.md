# PolyFEA Apple-Inspired Frontend Design

Date: 2026-09-02
Status: Approved visual direction; implementation not started

## 1. Objective

Replace PolyFEA's current dense, dark, two-column immediate-mode control panel with a precise Apple-inspired desktop interface. The result must make model setup, meshing, solver configuration, and result inspection easier to understand without changing any geometry, meshing, load, solver, fracture, or post-processing computation.

The approved direction is **A1: a persistent macOS-style right inspector organized into Model, Mesh, and Solve tabs**.

## 2. Scope boundary

### In scope

- The custom UI primitives in `src/SimpleUI.cpp` and `include/SimpleUI.h`.
- Frontend composition, labels, grouping, and interaction routing in `src/main.cpp`.
- Font rendering needed to support readable sentence-case interface text and tabular values.
- Pointer and keyboard behavior for frontend controls.
- Responsive inspector sizing, inspector scrolling, visual state, progress presentation, and help presentation.
- Frontend-only tests or deterministic UI-state checks.

### Out of scope

- `FEASolver`, `LoadPhysics`, `FEAModel`, TetGen, toolpath meshing, layer-slicing mathematics, material values, capability assessments, and result calculations.
- Changing solver defaults, load magnitudes, ranges, units, enablement conditions, or async job behavior.
- Adding a second UI framework or replacing the OpenGL application shell.
- Saving UI layout or tab selection to a new sidecar or scenario format.
- Removing any existing user action.

Existing callbacks remain the source of truth. The frontend may relocate a callback behind a clearer control, but it must not duplicate, bypass, or reinterpret the operation.

## 3. Approved information architecture

The application keeps a dominant 3D viewport and replaces the fixed 600 px two-column panel with one persistent right inspector.

### Window chrome

- A 44 px title bar spans the window.
- The left side identifies the app and loaded document.
- The right side contains mesh/solve readiness, Help, and Reset view.
- Readiness must be derived from existing model/job state; it is not a new computation.

### Inspector sizing

- Target width: 348 px.
- Responsive width: `clamp(320 px, 28% of window width, 380 px)`.
- The inspector remains a single column.
- Each active tab scrolls vertically when content exceeds the available height.
- Mouse-wheel events over the inspector scroll it and must not zoom the camera.
- Mouse-wheel events over the viewport keep the existing camera behavior.

### Top-level tabs

#### Model

- Cube/Import segmented selector.
- Imported model list and format badges.
- Model-list pagination if retained by implementation, or a scrollable list with equivalent reachability.
- Cube dimensions and subdivisions when Cube is selected.
- Material list/picker and material properties.
- Source metadata including format, retained B-rep state, object count, and physical dimensions.

#### Mesh

- Vertex smoothing for imported models.
- Mesh quality and maximum-volume sliders.
- Surface/Volume representation selector.
- Generate volume mesh action.
- Layer-slicing switch, thickness, build-axis selector, maximum slabs, wall width, slice-preview action, preview-layer slider, and layer/slab readout.
- Toolpath-specific physical layer/slab receipt when applicable.

#### Solve

- Multithreading and GPU acceleration switches.
- Generic build-axis selector.
- Load-preset picker exposing all six current presets.
- Magnitude slider with explicit value and unit.
- Load, distribution, support, and capability receipt.
- Linear static, nonlinear Newton-Raphson, adaptive, and brittle-fracture actions with current enablement rules.
- Curvature angle and fraction controls for adaptive analysis.
- FDM anisotropy switch with current material-data gating.
- G-code magnitude field, Reset to default, and Run showcase fracture analysis for toolpath models.
- Result controls: Original/Deformed, fracture view mode, dead-element view mode, and force-map visibility.
- Total-force, nodal-force, scalar-range, and failure-mode legends.

### Viewport controls

- The sectional-view slider remains at the left edge of the viewport.
- Solver-stage status remains in the viewport and is aligned to the lower-right safe area.
- The compute-progress surface remains lower-left and keeps a working Cancel action.
- Help replaces the standalone README button but opens the same explanatory content.

## 4. No-deletion control contract

No current action is deleted. The following presentation substitutions retain the same variables, callbacks, conditions, and side effects.

| Current control | Approved presentation |
|---|---|
| Cube mode / Import file | Two-option segmented control |
| Model filename buttons | Selectable file rows |
| Model-list previous / next | Pagination or equivalent scroll reachability |
| Material filename buttons | Selectable material rows or picker |
| Vertex smoothing on/off | Switch |
| Surface mesh / Volume mesh | Two-option segmented control |
| Generate 3D mesh | Primary Mesh action |
| Slice on/off | Switch |
| Slice Axis X / Y / Z | Three-option segmented control |
| Slice preview | Secondary Mesh action |
| G-code magnitude pseudo-field | Numeric input field |
| Default | Reset to default action |
| Run showcase FEA | Primary toolpath Solve action |
| Multithreading / GPU acceleration | Switches |
| Cyclic build-axis button | Three-option segmented control |
| Cyclic force-preset button | Picker listing all six presets |
| Linear static / Nonlinear / Adaptive / Brittle fracture | Separate analysis actions |
| FDM anisotropy | Switch |
| Original / Deformed | Two-option result selector |
| Fracture view mode | Result-mode picker |
| Dead-element mode | Result-mode picker |
| Force map | Switch |
| README | Help toolbar action |
| Progress X | Labeled Cancel action |

An implementation is incomplete if any state-reachable current action cannot be reached in the new interface.

## 5. Exact visual system

### Color tokens

The visual system uses six named base colors. Additional surfaces use opacity blends of these colors rather than unrelated decorative colors.

| Token | Hex | Use |
|---|---:|---|
| Frost canvas | `#E9EEF5` | 3D viewport environment and neutral window background |
| Snow surface | `#F7F7FA` | Inspector, title bar, and progress surface |
| Primary ink | `#1D1D1F` | Primary labels and values |
| Graphite | `#6E6E73` | Secondary labels, units, and inactive states |
| System blue | `#007AFF` | Selection, slider fill, focus, and one primary action per tab |
| Blocked red | `#C9342E` | Unsupported actions and errors requiring intervention |

Approximate and queued states use Graphite plus explicit text or symbols. Available and active states use System blue plus explicit text or symbols. Color must never be the only state signal.

### Typography

- Display role: **Segoe UI Variable Display**, semibold, for app, inspector, and major section titles.
- Interface role: **Segoe UI Variable Text**, regular/semibold, for labels and actions.
- Data role: **Cascadia Mono**, regular, for editable values, measured values, percentages, element counts, units, and solver receipts.
- Fallback order must keep the application usable on Windows systems lacking a preferred face.
- Do not redistribute or depend on Apple's proprietary SF fonts.
- If font initialization fails, the app continues with a safe fallback and logs one diagnostic rather than failing startup.

Type scale:

- App/document title: 15 px semibold.
- Inspector title: 18 px semibold.
- Tab/action text: 13 px semibold.
- Field label and value: 12 px.
- Section eyebrow and secondary metadata: 11 px.
- No essential text smaller than 11 px.

### Spacing and geometry

- Base spacing unit: 4 px.
- Inspector horizontal padding: 16 px.
- Major group spacing: 16 px.
- Field spacing: 6 px.
- Standard field/control height: 34 px.
- Primary/secondary action height: 36 px minimum.
- Tab strip height: 38 px.
- Control corner radius: 10 px.
- Floating-surface corner radius: 14 px.
- Major-surface corner radius: 16-18 px.
- Separator: 1 px Primary ink at 8-10% opacity.
- Shadows are limited to floating progress/help surfaces and the active segmented-control thumb.

### Buttons

- At most one System-blue primary action appears in a control group.
- Secondary actions use a Primary-ink fill at 6-10% opacity.
- Destructive/cancel actions use Blocked red text and remain visually quieter than the main operation until hovered or focused.
- Hover increases neutral fill opacity; pressed state moves one luminance step darker without changing layout.
- Disabled actions render at 38% content opacity and retain a nearby textual reason.
- Keyboard focus uses a 3 px System-blue ring at approximately 24% opacity.
- Button labels use sentence case and active verbs: “Generate volume mesh,” “Run linear analysis,” and “Reset to default.”

### Segmented controls and switches

- Segmented controls are reserved for mutually exclusive choices such as Cube/Import, Surface/Volume, and axis selection.
- The selected segment uses Snow surface, Primary ink, and a small neutral shadow; inactive segments remain transparent.
- Switches are reserved for independent Boolean settings such as GPU acceleration, multithreading, vertex smoothing, slicing, FDM anisotropy, and force-map visibility.
- Disabled switches show the requirement that would enable them.

### Sliders and numbers

- Sliders use a 4 px track and a 16 px white thumb with a restrained neutral shadow.
- System blue fills the active portion; the inactive portion is Primary ink at 12-18% opacity.
- The label and numeric value occupy a separate field row above the track. Text is never painted over the slider.
- Values are right-aligned using tabular figures.
- Units are visually separated from the number and never encoded only in the label.
- Exponential values retain scientific notation.
- Existing minimum, maximum, linear/exponential mapping, and rounding behavior remain unchanged unless a separate computation change is approved.

### Panels and glass

- The persistent inspector is an opaque Snow surface at approximately 96% opacity for reliable text contrast.
- Glass treatment is restricted to the title bar and floating progress/help surfaces.
- Do not wrap every group in a card. Grouping comes from spacing, labels, and restrained separators.
- The panel-lock state during computation dims inactive inspector content but does not obscure status, stage progress, or Cancel.

### Motion

- Tab/segment selection: 140-180 ms ease-out.
- Progress panel entrance: 220 ms ease-out, preserving the existing slide behavior.
- No looping decoration, ambient animation, or unrelated hover motion.
- When operating-system reduced-motion preferences are available, transitions become immediate.

## 6. Signature element: the analysis receipt

The interface's one distinctive element is a compact contextual **receipt**. It expresses the facts PolyFEA users need before trusting an analysis rather than adding decorative Apple-like chrome.

The receipt changes with the active tab:

- Model receipt: input format, B-rep retention, object count, and physical size.
- Mesh receipt: meshing path, element type/count, and physical-layer-to-FE-slab mapping when relevant.
- Solve receipt: load scope, distribution, support, and Linear/Nonlinear/Fracture capability.

Receipt content comes only from existing model metadata, meshing statistics, and `LoadPhysics` capability descriptions. It must not imply fidelity the underlying computation does not provide. Unsupported and approximate states include explicit text, not just color.

## 7. Interaction and accessibility

- Pointer hit targets are at least 32 px high in the dense desktop inspector.
- Tab and Shift+Tab traverse active-tab controls in visual order.
- Enter/Space activates buttons, segments, and switches.
- Arrow keys adjust sliders and move within segmented controls; slider values remain clamped to existing ranges.
- Escape cancels a cancellable compute job using the existing cancellation path or closes Help when no job is active.
- Focus never moves to hidden-tab controls.
- The inspector traps no viewport interaction when the pointer is outside its bounds.
- Essential labels and values meet WCAG 2.1 AA contrast against their rendered background.
- Blocked, disabled, approximate, active, and selected states have non-color labels or symbols.
- Long filenames truncate visually but expose an unambiguous full-name view in Help/tooltip or an expanded row.

## 8. Empty, busy, and error states

- No models: show where supported files belong and retain the Import choice.
- No materials: show the expected `materials/` location without disabling unrelated Model controls.
- Mesh absent: Volume representation and solver actions remain visible but disabled with the reason “Generate a volume mesh first.”
- Capability blocked: retain the solver action and show the exact `LoadPhysics` reason in the receipt.
- Compute busy: freeze inspector mutation, preserve visible values, show solver stages and progress, and leave Cancel operational.
- Cancel requested: change the progress label to “Cancelling…” without presenting completion early.
- Font failure: use fallback text rendering and log one diagnostic.

## 9. Implementation boundaries

- Extend the existing SimpleUI/OpenGL renderer; do not introduce ImGui, Qt, Electron, or a web layer.
- Reuse current state variables and callbacks in `src/main.cpp`.
- Keep algorithm objects and solver construction unchanged except for moving their existing invocation into the selected tab's control.
- Isolate visual tokens and reusable primitives from application-specific layout.
- A font-backed path may reuse ideas from the existing `include/UIManager.h`, but that header is not assumed production-ready. The implementation plan must verify ownership, cleanup, shader compatibility, resize behavior, and optional dependency behavior before reuse.
- UI layout helpers must not own model or solver state; they receive current state and return user intent.

## 10. Verification plan

### Computational invariance

- Capture the current regression baseline before implementation.
- Run the existing full headless regression suite after implementation.
- Run focused load-physics tests.
- Verify that every solver/mesh action still reaches the same callback with the same parameter values for the same UI state.
- Investigate any numerical or output difference as a regression; visual work does not authorize baseline updates.

### Control-reachability matrix

Exercise every current action in these contexts:

- Cube before and after volume meshing.
- STL/3MF/STEP import before and after volume meshing.
- G-code import before and after toolpath meshing.
- Material with and without FDM anisotropy data.
- Available, approximate, and blocked capability states.
- Linear, nonlinear, adaptive, showcase-fracture, and brittle-fracture initiation.
- Original/deformed, fracture mode, dead-element mode, and force-map result states.
- Busy, cancelling, completed, failed, and empty states.

### Visual matrix

- 1024 x 768, 1280 x 800, and 1920 x 1080.
- Default Windows scaling and one high-DPI configuration available on the test machine.
- Long model/material names and maximum formatted numeric values.
- Inspector scrolled to top, middle, and bottom.
- Keyboard focus visible on every control type.
- Check that overlays do not cover each other or steal unintended viewport input.

### Completion criteria

- Build succeeds from the project's supported PowerShell build path.
- Existing automated tests and regression scenarios pass without accepted numerical changes.
- Every current button action is reachable in the new interface.
- No essential label is clipped at the supported sizes.
- Numeric values and units remain visible and unambiguous.
- Disabled and blocked actions explain why they cannot run.
- The final visual result matches the approved A1 direction and token system.

## 11. Self-review record

- Placeholder scan: no TBD or TODO requirements remain.
- Consistency: the three-tab structure, no-deletion contract, component rules, and verification matrix describe the same scope.
- Scope: computation changes, new persistence, and a parallel UI framework are explicitly excluded.
- Ambiguity: all current button families have an approved destination or equivalent control.
- Generic-design check: blanket glass cards and decorative gradients were removed. Glass is limited to functional floating surfaces, and the analysis receipt grounds the visual identity in FEA source, discretization, load, and capability evidence.
