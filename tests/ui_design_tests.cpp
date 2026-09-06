#include "UIDesign.h"
#include "UIInteraction.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expectNear(float actual, float expected, float tolerance = 1.0e-5f) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            "expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
    }
}

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectFalse(bool condition, const char* message) {
    expectTrue(!condition, message);
}

void expectEqual(const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        throw std::runtime_error("expected " + expected + ", got " + actual);
    }
}

void expectEqual(std::string_view actual, std::string_view expected) {
    if (actual != expected) {
        throw std::runtime_error(
            "expected " + std::string(expected) + ", got " + std::string(actual));
    }
}

template <typename T>
void expectEqual(T actual, T expected) {
    if (actual != expected) {
        throw std::runtime_error("values are not equal");
    }
}

void expectContainsFilename(
    const std::vector<std::filesystem::path>& paths,
    std::string_view expected) {
    const auto found = std::find_if(paths.begin(), paths.end(), [&](const auto& path) {
        return path.filename().string() == expected;
    });
    if (found == paths.end()) {
        throw std::runtime_error("font fallback list omits " + std::string(expected));
    }
}

void expectAllUnique(const std::vector<ui_design::ControlId>& ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            if (ids[i] == ids[j]) {
                throw std::runtime_error("control manifest contains a duplicate");
            }
        }
    }
}

void expectContains(
    const std::vector<ui_design::ControlId>& ids,
    ui_design::ControlId expected) {
    if (std::find(ids.begin(), ids.end(), expected) == ids.end()) {
        throw std::runtime_error("control manifest omits a required action");
    }
}

void expectReceiptContains(
    const std::vector<ui_design::ReceiptLine>& lines,
    std::string_view expected) {
    const auto found = std::find_if(lines.begin(), lines.end(), [&](const auto& line) {
        return line.label.find(expected) != std::string::npos ||
               line.value.find(expected) != std::string::npos;
    });
    if (found == lines.end()) {
        throw std::runtime_error("receipt omits " + std::string(expected));
    }
}

void expectReceiptIsAscii(const std::vector<ui_design::ReceiptLine>& lines) {
    for (const auto& line : lines) {
        for (const auto* text : {&line.label, &line.value}) {
            const auto nonAscii = std::find_if(
                text->begin(), text->end(),
                [](unsigned char character) { return character > 0x7f; });
            if (nonAscii != text->end()) {
                throw std::runtime_error("receipt contains a non-ASCII separator");
            }
        }
    }
}

template <typename Fn>
void expectInvalidArgument(Fn&& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("expected std::invalid_argument");
}

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

void testWindowLayoutRejectsInvalidDimensions() {
    expectInvalidArgument([] { ui_design::computeWindowLayout(0, 768); });
    expectInvalidArgument([] { ui_design::computeWindowLayout(1024, 0); });
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
    expectEqual(ui_design::hex(ui_design::ColorToken::FrostCanvas), "#E9EEF5");
    expectEqual(ui_design::hex(ui_design::ColorToken::SnowSurface), "#F7F7FA");
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

void testControlManifestPartitionsAndWidgetIdentity() {
    const auto& all = ui_design::requiredControls();
    const auto& inspector = ui_design::requiredInspectorControls();
    const auto& overlay = ui_design::requiredOverlayControls();

    const std::vector<std::pair<ui_design::ControlId, std::string_view>> inspectorSliders = {
        {ui_design::ControlId::EditSizeX, "edit-size-x"},
        {ui_design::ControlId::EditSizeY, "edit-size-y"},
        {ui_design::ControlId::EditSizeZ, "edit-size-z"},
        {ui_design::ControlId::EditSubdivisions, "edit-subdivisions"},
        {ui_design::ControlId::EditMeshQuality, "edit-mesh-quality"},
        {ui_design::ControlId::EditMaxVolumePercent, "edit-max-volume-percent"},
        {ui_design::ControlId::EditLayerThickness, "edit-layer-thickness"},
        {ui_design::ControlId::EditMaxSlabs, "edit-max-slabs"},
        {ui_design::ControlId::EditWallWidth, "edit-wall-width"},
        {ui_design::ControlId::SelectPreviewLayer, "select-preview-layer"},
        {ui_design::ControlId::EditLoadMagnitude, "edit-load-magnitude"},
        {ui_design::ControlId::EditCurvatureAngle, "edit-curvature-angle"},
        {ui_design::ControlId::EditCurvatureFraction, "edit-curvature-fraction"},
    };
    for (const auto& slider : inspectorSliders) {
        expectContains(inspector, slider.first);
        expectContains(all, slider.first);
        expectEqual(ui_design::controlToken(slider.first), slider.second);
    }

    expectContains(overlay, ui_design::ControlId::EditSectionPosition);
    expectContains(all, ui_design::ControlId::EditSectionPosition);
    expectEqual(
        ui_design::controlToken(ui_design::ControlId::EditSectionPosition),
        "edit-section-position");

    std::vector<ui_design::ControlId> partitioned = inspector;
    partitioned.insert(partitioned.end(), overlay.begin(), overlay.end());
    expectAllUnique(partitioned);
    expectTrue(partitioned.size() == all.size(), "control partitions must cover the manifest");
    for (const auto id : all) {
        expectContains(partitioned, id);
        expectTrue(!ui_design::controlToken(id).empty(), "every control needs a stable token");
    }

    const ui_design::WidgetId firstMaterial{ui_design::ControlId::SelectMaterial, 0};
    const ui_design::WidgetId sameMaterial{ui_design::ControlId::SelectMaterial, 0};
    const ui_design::WidgetId secondMaterial{ui_design::ControlId::SelectMaterial, 1};
    expectTrue(firstMaterial == sameMaterial, "equal widgets must share identity");
    expectTrue(!(firstMaterial == secondMaterial), "repeated controls need distinct identities");
}

void testControlVisualStates() {
    const auto primary = ui_design::resolveControlVisual(
        ui_design::ControlRole::Primary, ui_design::ControlState::Rest);
    expectEqual(primary.fill, ui_design::ColorToken::SystemBlue);
    expectEqual(primary.text, ui_design::ColorToken::SnowSurface);

    const auto blocked = ui_design::resolveControlVisual(
        ui_design::ControlRole::Secondary, ui_design::ControlState::Disabled);
    expectNear(blocked.contentOpacity, 0.38f);

    const auto cancel = ui_design::resolveControlVisual(
        ui_design::ControlRole::Destructive, ui_design::ControlState::Rest);
    expectEqual(cancel.fill, ui_design::ColorToken::BlockedRed);
    expectEqual(cancel.text, ui_design::ColorToken::BlockedRed);
    expectNear(cancel.fillOpacity, 0.0f);
}

void testControlHomesMatchApprovedA1InformationArchitecture() {
    expectEqual(ui_design::controlSurface(ui_design::ControlId::EditSizeX),
                ui_design::ControlSurface::Model);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::EditSizeY),
                ui_design::ControlSurface::Model);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::EditSizeZ),
                ui_design::ControlSurface::Model);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::EditSubdivisions),
                ui_design::ControlSurface::Model);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::SelectSurfaceView),
                ui_design::ControlSurface::Mesh);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::SelectVolumeView),
                ui_design::ControlSurface::Mesh);
    expectEqual(ui_design::controlSurface(ui_design::ControlId::RunLinearAnalysis),
                ui_design::ControlSurface::Solve);

    for (const auto id : ui_design::requiredInspectorControls()) {
        expectTrue(ui_design::controlSurface(id) != ui_design::ControlSurface::Unknown,
                   "every inspector control must have a production IA home");
    }
}

void testFontCandidateOrder() {
    const auto interfaceFonts = ui_design::fontCandidates(ui_design::FontRole::Interface);
    const auto dataFonts = ui_design::fontCandidates(ui_design::FontRole::Data);
    expectTrue(!interfaceFonts.empty(), "interface font fallback list must not be empty");
    expectTrue(!dataFonts.empty(), "data font fallback list must not be empty");
    expectContainsFilename(interfaceFonts, "SegUIVar.ttf");
    expectContainsFilename(dataFonts, "CascadiaMono.ttf");
}

void testThemeColorConversion() {
    const auto blue = ui_design::rgba(ui_design::ColorToken::SystemBlue, 0.5f);
    expectNear(blue.r, 0.0f);
    expectNear(blue.g, 122.0f / 255.0f);
    expectNear(blue.b, 1.0f);
    expectNear(blue.a, 0.5f);
}

void testFormattedValueLayoutSeparatesNumberAndUnit() {
    const auto shortUnit = ui_design::layoutFormattedValueText(
        {"12.50", "mm"}, 300.0f, 50.0f, 32.0f, 6.0f);
    const auto longUnit = ui_design::layoutFormattedValueText(
        {"12.50", "kilopascals"}, 300.0f, 50.0f, 32.0f, 6.0f);

    expectEqual(shortUnit.number.text, "12.50");
    expectEqual(shortUnit.number.role, ui_design::FontRole::Data);
    expectNear(shortUnit.number.x, 212.0f);
    expectNear(shortUnit.numberRight, 262.0f);
    expectEqual(shortUnit.unit.text, "mm");
    expectEqual(shortUnit.unit.role, ui_design::FontRole::Interface);
    expectNear(shortUnit.unit.x, 268.0f);
    expectNear(longUnit.number.x, shortUnit.number.x);
    expectNear(longUnit.numberRight, shortUnit.numberRight);
}

void testReceiptPresentation() {
    const auto blocked = ui_design::makeSolveReceipt(
        "Surface compression Y", "BBox face", "Linear facet tributary",
        "Y-min fixed", "NONLINEAR BLOCKED: NR -> Y COMPRESSION",
        ui_design::ReceiptTone::Blocked);
    expectReceiptContains(blocked, "Surface compression Y");
    expectReceiptContains(blocked, "Linear facet tributary");
    expectReceiptContains(blocked, "NONLINEAR BLOCKED");
    expectEqual(blocked.back().tone, ui_design::ReceiptTone::Blocked);
    expectReceiptIsAscii(blocked);

    const auto available = ui_design::makeSolveReceipt(
        "Tension X", "BBox face", "Nodal", "X-min fixed", "LINEAR EXACT",
        ui_design::ReceiptTone::Available);
    expectReceiptContains(available, "Available");

    const auto approximate = ui_design::makeSolveReceipt(
        "Point force Z", "Single node", "Nearest node", "Z-min fixed",
        "LINEAR APPROX", ui_design::ReceiptTone::Approximate);
    expectReceiptContains(approximate, "Approximate");

    const auto mesh = ui_design::makeMeshReceipt(
        "STEP / B-rep retained", "Tet10", 48216, 0, 0, 0);
    expectReceiptContains(mesh, "48,216");
    expectReceiptContains(mesh, "Tet10");
    expectReceiptIsAscii(mesh);

    const auto model = ui_design::makeModelReceipt(
        "STEP", true, 3, "20.0 x 10.0 x 5.0 mm");
    expectReceiptContains(model, "STEP");
    expectReceiptContains(model, "Retained");
    expectReceiptContains(model, "3");

    const auto proceduralCube = ui_design::makeModelReceipt(
        "Procedural cube", true, 1, "1.0 x 1.0 x 1.0 mm", true);
    expectReceiptContains(proceduralCube, "Not retained");
    expectFalse(proceduralCube[1].value.find("Retained") != std::string::npos,
                "procedural cube receipt must ignore stale imported B-rep state");
}

void testTitleReadinessIsFactualAndTextual() {
    expectEqual(ui_design::activeComputationForJobTitle(""),
                ui_design::ActiveComputation::Idle);
    expectEqual(ui_design::activeComputationForJobTitle("TETGEN MESHING"),
                ui_design::ActiveComputation::Meshing);
    expectEqual(ui_design::activeComputationForJobTitle("TOOLPATH MESHING"),
                ui_design::ActiveComputation::Meshing);
    expectEqual(ui_design::activeComputationForJobTitle("LINEAR STATIC FEA"),
                ui_design::ActiveComputation::Solving);
    expectEqual(ui_design::activeComputationForJobTitle("BRITTLE FRACTURE FEA"),
                ui_design::ActiveComputation::Solving);

    const auto empty = ui_design::makeTitleReadiness(
        false, ui_design::ActiveComputation::Idle, false);
    expectEqual(empty.mesh, "Mesh needed");
    expectEqual(empty.solve, "Solve needs mesh");
    expectFalse(empty.meshActive, "idle mesh readiness must not be emphasized");
    expectFalse(empty.solveActive, "idle solve readiness must not be emphasized");

    const auto meshed = ui_design::makeTitleReadiness(
        true, ui_design::ActiveComputation::Idle, false);
    expectEqual(meshed.mesh, "Mesh ready");
    expectEqual(meshed.solve, "Solve ready");

    const auto meshing = ui_design::makeTitleReadiness(
        false, ui_design::ActiveComputation::Meshing, false);
    expectEqual(meshing.mesh, "Mesh running");
    expectEqual(meshing.solve, "Solve waiting");
    expectTrue(meshing.meshActive, "meshing must emphasize Mesh status");
    expectFalse(meshing.solveActive, "meshing must not emphasize Solve status");

    const auto solving = ui_design::makeTitleReadiness(
        true, ui_design::ActiveComputation::Solving, false);
    expectEqual(solving.mesh, "Mesh ready");
    expectEqual(solving.solve, "Solve running");
    expectFalse(solving.meshActive, "solving must not emphasize Mesh status");
    expectTrue(solving.solveActive, "solving must emphasize Solve status");

    const auto results = ui_design::makeTitleReadiness(
        true, ui_design::ActiveComputation::Idle, true);
    expectEqual(results.solve, "Results ready");
}

void testLongLabelsUseMiddleEllipsisAndRetainFullName() {
    const std::string full = "very-long-model-filename-with-identity.step";
    constexpr float compactRowWidth = 206.0f;
    constexpr float rowTextSize = 12.16f;
    const auto presentation = ui_design::presentLongLabel(
        full, compactRowWidth, rowTextSize);
    expectTrue(presentation.truncated, "long labels must be truncated deterministically");
    expectTrue(ui_design::conservativeTextWidth(
                   presentation.visible, rowTextSize) <= compactRowWidth,
               "middle ellipsis must fit the supported stroke fallback width");
    expectTrue(presentation.visible.find("...") != std::string::npos,
               "middle ellipsis must carry an explicit omission marker");
    expectEqual(presentation.full, full);
    expectTrue(presentation.visible.rfind(".step") == presentation.visible.size() - 5,
               "middle ellipsis must preserve the distinguishing extension");

    const auto shortName = ui_design::presentLongLabel(
        "steel.mat", compactRowWidth, rowTextSize);
    expectFalse(shortName.truncated, "short labels must remain unchanged");
    expectEqual(shortName.visible, "steel.mat");

    const auto tooltipLines = ui_design::wrapTextToWidth(
        full, 340.0f, 11.0f);
    std::string recombined;
    for (const auto& line : tooltipLines) {
        expectTrue(ui_design::conservativeTextWidth(line, 11.0f) <= 340.0f,
                   "tooltip lines must fit their actual inner width");
        recombined += line;
    }
    expectEqual(recombined, full);
}

void testEssentialTextNeverFallsBelowElevenPixels() {
    expectNear(ui_design::essentialTextPixelSize(7.5f), 11.0f);
    expectNear(ui_design::essentialTextPixelSize(11.0f), 11.0f);
    expectNear(ui_design::essentialTextPixelSize(15.0f), 15.0f);
}

void testSurfaceVolumeSegmentPreservesAvailability() {
    const auto surfaceOnly = ui_design::surfaceVolumePresentation(false, false);
    expectEqual(surfaceOnly.selectedIndex, 0);
    expectFalse(surfaceOnly.disabled[0], "surface must remain selectable");
    expectTrue(surfaceOnly.disabled[1], "volume must remain disabled before meshing");

    const auto volume = ui_design::surfaceVolumePresentation(true, true);
    expectEqual(volume.selectedIndex, 1);
    expectFalse(volume.disabled[0], "surface must remain selectable after meshing");
    expectFalse(volume.disabled[1], "volume must become selectable after meshing");
}

void testSurfaceVolumeActionRequiresASelectionChange() {
    expectEqual(ui_design::resolveSurfaceVolumeAction(false, 1),
                ui_design::SurfaceVolumeAction::None);
    expectEqual(ui_design::resolveSurfaceVolumeAction(true, 0),
                ui_design::SurfaceVolumeAction::SelectSurface);
    expectEqual(ui_design::resolveSurfaceVolumeAction(true, 1),
                ui_design::SurfaceVolumeAction::SelectVolume);
}

void testHorizontalSliderUsesFourPixelTrackAndSixteenPixelThumb() {
    const auto geometry = ui_design::horizontalSliderGeometry(
        {20.0f, 30.0f, 240.0f, 48.0f}, 0.25f);
    expectNear(geometry.track.h, 4.0f);
    expectNear(geometry.thumb.w, 16.0f);
    expectNear(geometry.thumb.h, 16.0f);
    expectNear(geometry.thumb.x + geometry.thumb.w * 0.5f,
               geometry.track.x + geometry.track.w * 0.25f);
}

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

void testViewportOverlayLayoutStacksStatusAboveProgress() {
    const auto window = ui_design::computeWindowLayout(1024, 768);
    const auto overlays = ui_design::computeViewportOverlayLayout(
        window.viewport, 768.0f, 620.0f, 12, true);
    expectFalse(ui_design::intersects(overlays.solverStatus, overlays.progress),
                "solver status and progress must not intersect at 1024x768");
    expectTrue(overlays.solverStatus.y >= window.viewport.y,
               "solver status must stay inside the viewport safe area");
}

void testSolvePresentationRoutesCubeToGenericWorkflow() {
    const auto cube = ui_design::solvePresentationPolicy(true, false);
    expectTrue(cube.showGenericWorkflow,
               "cube mode must expose the generic solve workflow");
    expectFalse(cube.showToolpathWorkflow,
                "cube mode must not expose the toolpath workflow");

    const auto toolpath = ui_design::solvePresentationPolicy(false, true);
    expectFalse(toolpath.showGenericWorkflow,
                "toolpath imports must not expose generic solver controls");
    expectTrue(toolpath.showToolpathWorkflow,
               "toolpath imports must retain their showcase workflow");

    const auto surface = ui_design::solvePresentationPolicy(false, false);
    expectTrue(surface.showGenericWorkflow,
               "non-toolpath imports must expose the generic solve workflow");
}

void testEmptyImportHasNoMeshSourceClaim() {
    expectEqual(
        ui_design::meshReceiptSource(false, false, false, false),
        "No model selected");
    expectEqual(
        ui_design::meshReceiptSource(false, true, false, false),
        "Surface / TetGen");
}

void testSolveCapabilitySummaryOmitsUnassessedAdaptiveMode() {
    const auto summary = ui_design::makeSolveCapabilitySummary(
        "Available (LINEAR EXACT)", "Blocked (NONLINEAR BLOCKED)",
        "Approximate (MESH-DEP/LINEAR APPROX)", "NR -> Y COMPRESSION");
    expectEqual(
        summary,
        "Linear Available (LINEAR EXACT) / Nonlinear Blocked "
        "(NONLINEAR BLOCKED) / Fracture Approximate "
        "(MESH-DEP/LINEAR APPROX) / Reason: NR -> Y COMPRESSION");
    expectTrue(summary.find("Adaptive") == std::string::npos,
               "receipt must not invent an Adaptive capability result");
}

void testStrokeFallbackPreservesRequestedOpacity() {
    const auto fallback = ui_design::resolveTextDrawPolicy(
        false, ui_design::FontRole::Interface, {0.2f, 0.3f, 0.4f, 0.38f});

    expectEqual(fallback.backend, ui_design::TextBackend::Stroke);
    expectEqual(fallback.role, ui_design::FontRole::Interface);
    expectNear(fallback.color.r, 0.2f);
    expectNear(fallback.color.g, 0.3f);
    expectNear(fallback.color.b, 0.4f);
    expectNear(fallback.color.a, 0.38f);
}

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

void testSelectingInspectorTabClearsFocus() {
    ui_interaction::InspectorState state;
    state.focused = ui_design::ControlId::EditSizeX;

    ui_interaction::selectTab(state, ui_design::InspectorTab::Solve);

    expectEqual(state.activeTab, ui_design::InspectorTab::Solve);
    expectTrue(!state.focused.has_value(), "switching tabs must clear focused controls");
}

void testKeyboardNavigationMapsPressedKeysToIntents() {
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Tab, true, false),
                ui_interaction::KeyIntent::FocusNext);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Tab, true, true),
                ui_interaction::KeyIntent::FocusPrevious);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Enter, true, false),
                ui_interaction::KeyIntent::Activate);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Space, true, false),
                ui_interaction::KeyIntent::Activate);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Left, true, false),
                ui_interaction::KeyIntent::Decrease);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Down, true, false),
                ui_interaction::KeyIntent::Decrease);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Right, true, false),
                ui_interaction::KeyIntent::Increase);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Up, true, false),
                ui_interaction::KeyIntent::Increase);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Escape, true, false),
                ui_interaction::KeyIntent::Cancel);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Tab, false, false),
                ui_interaction::KeyIntent::None);
    expectEqual(ui_interaction::translateKey(ui_interaction::Key::Other, true, false),
                ui_interaction::KeyIntent::None);
}

void testCompactLayoutKeepsPositiveViewport() {
    const auto layout = ui_design::computeWindowLayout(800, 600);
    expectTrue(layout.viewport.w > 0.0f,
               "compact viewport width must remain positive");
    expectTrue(layout.inspector.w <= 380.0f,
               "inspector must respect maximum width");
}

void testKeyIntentQueueKeepsOnlyFirstIntentPerFrame() {
    std::optional<ui_interaction::KeyIntent> pending;
    expectTrue(ui_interaction::queueKeyIntent(
                   pending, ui_interaction::KeyIntent::FocusNext),
               "first key intent in a frame must be queued");
    expectFalse(ui_interaction::queueKeyIntent(
                    pending, ui_interaction::KeyIntent::Activate),
                "a second key intent in the same frame must be rejected");
    expectEqual(*pending, ui_interaction::KeyIntent::FocusNext);
    expectFalse(ui_interaction::queueKeyIntent(
                    pending, ui_interaction::KeyIntent::None),
                "None must never occupy the pending key slot");
}

void testVisibleFocusOrderUsesStableWidgetsAndIncludesDisabledControls() {
    std::vector<ui_design::WidgetId> visible;
    ui_interaction::appendContextualFocus(
        visible, {ui_design::ControlId::SelectModelFile, 3});
    ui_interaction::appendContextualFocus(
        visible, {ui_design::ControlId::RunLinearAnalysis, 0});
    ui_interaction::appendContextualFocus(
        visible, {ui_design::ControlId::RunNonlinearAnalysis, 0});
    ui_interaction::appendContextualFocus(
        visible, {ui_design::ControlId::RunLinearAnalysis, 0});

    expectTrue(visible.size() == 3,
               "all contextually present controls, including below-fold controls, are focusable");
    expectTrue(visible.front() ==
                   ui_design::WidgetId{ui_design::ControlId::SelectModelFile, 3},
               "focus order must preserve repeated-row widget identity");
    expectTrue(visible.back() ==
                   ui_design::WidgetId{ui_design::ControlId::RunNonlinearAnalysis, 0},
               "disabled visible controls remain in focus order");
    expectTrue(*ui_interaction::nextWidgetFocus(visible, visible.back(), 1) ==
                   visible.front(),
               "widget focus must wrap through visible controls");
}

void testKeyboardFocusRevealAdjustsInspectorScroll() {
    const ui_design::Rect viewport{100.0f, 100.0f, 320.0f, 400.0f};
    expectNear(ui_interaction::revealFocusedScroll(
                   0.0f, {110.0f, 620.0f, 280.0f, 40.0f}, viewport,
                   900.0f),
               160.0f);
    expectNear(ui_interaction::revealFocusedScroll(
                   200.0f, {110.0f, 60.0f, 280.0f, 32.0f}, viewport,
                   900.0f),
               160.0f);
    expectNear(ui_interaction::revealFocusedScroll(
                   120.0f, {110.0f, 180.0f, 280.0f, 32.0f}, viewport,
                   900.0f),
               120.0f);
}

void testHelpOwnsViewportNavigationInput() {
    expectTrue(ui_interaction::allowsViewportNavigation(false, false),
               "viewport navigation must remain available outside the inspector");
    expectFalse(ui_interaction::allowsViewportNavigation(false, true),
                "inspector must retain pointer ownership");
    expectFalse(ui_interaction::allowsViewportNavigation(true, false),
                "open Help must suppress camera navigation over the viewport");
}

void testKeyboardMutationHonorsDisabledAndBusyGates() {
    expectTrue(ui_interaction::allowsKeyboardMutation(
                   ui_interaction::KeyIntent::Activate, false, false),
               "enabled idle controls must accept keyboard activation");
    expectFalse(ui_interaction::allowsKeyboardMutation(
                    ui_interaction::KeyIntent::Activate, true, false),
                "disabled controls must not activate");
    expectFalse(ui_interaction::allowsKeyboardMutation(
                    ui_interaction::KeyIntent::Increase, false, true),
                "busy inspector controls must not adjust");
}

void testKeyboardSliderAdjustmentUsesLinearAndLogarithmicRanges() {
    expectNear(ui_interaction::adjustSlider(5.0f, 0.0f, 10.0f, false, 1), 5.1f);
    expectNear(ui_interaction::adjustSlider(0.0f, 0.0f, 10.0f, false, -1), 0.0f);
    expectNear(ui_interaction::adjustSlider(10.0f, 0.0f, 10.0f, false, 1), 10.0f);

    const float increased = ui_interaction::adjustSlider(
        0.01f, 0.00001f, 0.2f, true, 1);
    const float expected = 0.01f * std::pow(0.2f / 0.00001f, 0.01f);
    expectNear(increased, expected, 1e-6f);
    expectNear(ui_interaction::adjustSlider(
                   0.00001f, 0.00001f, 0.2f, true, -1),
               0.00001f, 1e-8f);
}

void testFocusRingPresentationIsAThreePixelOutlineWithClearInterior() {
    const ui_design::Rect target{20.0f, 30.0f, 120.0f, 44.0f};
    const auto ring = ui_interaction::focusRingPresentation(target, 8.0f);

    expectNear(ring.outerBounds.x, 17.0f);
    expectNear(ring.outerBounds.y, 27.0f);
    expectNear(ring.outerBounds.w, 126.0f);
    expectNear(ring.outerBounds.h, 50.0f);
    expectNear(ring.innerBounds.x, target.x);
    expectNear(ring.innerBounds.y, target.y);
    expectNear(ring.innerBounds.w, target.w);
    expectNear(ring.innerBounds.h, target.h);
    expectNear(ring.outerRadius, 11.0f);
    expectNear(ring.innerRadius, 8.0f);
    expectNear(ring.thickness, 3.0f);
    expectEqual(ring.color, ui_design::ColorToken::SystemBlue);
    expectNear(ring.opacity, 0.24f);
}

void testDiscreteSliderAccumulatorRetainsSubIntegerKeyboardSteps() {
    ui_interaction::DiscreteSliderAccumulator accumulator;
    accumulator.synchronize(0, 50);

    float adjusted = ui_interaction::adjustSlider(
        accumulator.position, 0.0f, 49.0f, false, 1);
    expectEqual(accumulator.commit(
                    adjusted, 50,
                    ui_interaction::SliderChangeSource::Keyboard),
                0);
    expectNear(accumulator.position, 0.49f);

    adjusted = ui_interaction::adjustSlider(
        accumulator.position, 0.0f, 49.0f, false, 1);
    expectEqual(accumulator.commit(
                    adjusted, 50,
                    ui_interaction::SliderChangeSource::Keyboard),
                1);
    expectNear(accumulator.position, 0.98f);

    accumulator.synchronize(49, 50);
    adjusted = ui_interaction::adjustSlider(
        accumulator.position, 0.0f, 49.0f, false, 1);
    expectEqual(accumulator.commit(
                    adjusted, 50,
                    ui_interaction::SliderChangeSource::Keyboard),
                49);
    expectNear(accumulator.position, 49.0f);

    expectEqual(accumulator.commit(
                    12.6f, 50,
                    ui_interaction::SliderChangeSource::Pointer),
                13);
    expectNear(accumulator.position, 13.0f);
}

void testSegmentAdjustmentMovesToAdjacentClampedOption() {
    expectEqual(ui_interaction::adjustSegmentIndex(1, 3, -1), 0);
    expectEqual(ui_interaction::adjustSegmentIndex(1, 3, 1), 2);
    expectEqual(ui_interaction::adjustSegmentIndex(0, 3, -1), 0);
    expectEqual(ui_interaction::adjustSegmentIndex(2, 3, 1), 2);
}

void testEscapePrefersCancellableJobThenHelp() {
    expectEqual(ui_interaction::resolveEscape(true, true, true),
                ui_interaction::EscapeAction::CancelJob);
    expectEqual(ui_interaction::resolveEscape(true, false, true),
                ui_interaction::EscapeAction::CloseHelp);
    expectEqual(ui_interaction::resolveEscape(false, false, true),
                ui_interaction::EscapeAction::CloseHelp);
    expectEqual(ui_interaction::resolveEscape(false, false, false),
                ui_interaction::EscapeAction::CloseWindow);
}

void testDpiScaleChangesRebuildOnlyWhenEffectiveScaleChanges() {
    expectNear(ui_interaction::effectiveContentScale(1.25f, 1.5f), 1.5f);
    expectNear(ui_interaction::effectiveContentScale(0.0f, -2.0f), 1.0f);
    expectFalse(ui_interaction::contentScaleChanged(1.5f, 1.5f),
                "unchanged scale must not rebuild font atlases");
    expectTrue(ui_interaction::contentScaleChanged(1.5f, 2.0f),
               "effective scale changes must rebuild font atlases");

    const auto normal = ui_interaction::motionDurations(false);
    const auto reduced = ui_interaction::motionDurations(true);
    expectTrue(normal.selectionMs >= 140 && normal.selectionMs <= 180,
               "normal tab/segment motion must use the approved duration");
    expectEqual(reduced.selectionMs, 0);
    expectEqual(reduced.progressMs, 0);
}

void testViewportSurfacePaintOrderKeepsHelpAboveStatusAndProgressAboveHelp() {
    const auto allSurfaces = ui_design::viewportSurfacePaintOrder(true, true);
    const std::vector<ui_design::ViewportSurface> expectedAll = {
        ui_design::ViewportSurface::SolverStatus,
        ui_design::ViewportSurface::Help,
        ui_design::ViewportSurface::Tooltip,
        ui_design::ViewportSurface::Progress,
    };
    expectEqual(allSurfaces, expectedAll);

    const auto helpOnly = ui_design::viewportSurfacePaintOrder(true, false);
    const std::vector<ui_design::ViewportSurface> expectedHelpOnly = {
        ui_design::ViewportSurface::SolverStatus,
        ui_design::ViewportSurface::Help,
        ui_design::ViewportSurface::Tooltip,
    };
    expectEqual(helpOnly, expectedHelpOnly);

    const auto progressOnly = ui_design::viewportSurfacePaintOrder(false, true);
    const std::vector<ui_design::ViewportSurface> expectedProgressOnly = {
        ui_design::ViewportSurface::SolverStatus,
        ui_design::ViewportSurface::Tooltip,
        ui_design::ViewportSurface::Progress,
    };
    expectEqual(progressOnly, expectedProgressOnly);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"window layout", testWindowLayout},
        {"invalid window dimensions", testWindowLayoutRejectsInvalidDimensions},
        {"numeric formatting", testNumericFormatting},
        {"palette and control manifest", testPaletteAndControlManifest},
        {"control partitions and widget identity", testControlManifestPartitionsAndWidgetIdentity},
        {"control visual states", testControlVisualStates},
        {"control homes match A1 IA", testControlHomesMatchApprovedA1InformationArchitecture},
        {"font candidate order", testFontCandidateOrder},
        {"theme color conversion", testThemeColorConversion},
        {"formatted value separates number and unit", testFormattedValueLayoutSeparatesNumberAndUnit},
        {"receipt presentation", testReceiptPresentation},
        {"title readiness", testTitleReadinessIsFactualAndTextual},
        {"long label presentation", testLongLabelsUseMiddleEllipsisAndRetainFullName},
        {"essential text minimum", testEssentialTextNeverFallsBelowElevenPixels},
        {"surface volume segment", testSurfaceVolumeSegmentPreservesAvailability},
        {"surface volume action change gate", testSurfaceVolumeActionRequiresASelectionChange},
        {"horizontal slider geometry", testHorizontalSliderUsesFourPixelTrackAndSixteenPixelThumb},
        {"section header clears divider", testSectionHeaderKeepsDividerOutOfTextBand},
        {"viewport overlay stacking", testViewportOverlayLayoutStacksStatusAboveProgress},
        {"cube routes to generic solve workflow", testSolvePresentationRoutesCubeToGenericWorkflow},
        {"empty import has no mesh source claim", testEmptyImportHasNoMeshSourceClaim},
        {"solve capability omits adaptive", testSolveCapabilitySummaryOmitsUnassessedAdaptiveMode},
        {"stroke fallback preserves requested opacity", testStrokeFallbackPreservesRequestedOpacity},
        {"inspector scroll ownership", testInspectorScrollOwnership},
        {"focus order skips hidden controls", testFocusOrderSkipsHiddenControls},
        {"selecting inspector tab clears focus", testSelectingInspectorTabClearsFocus},
        {"keyboard navigation maps pressed keys to intents", testKeyboardNavigationMapsPressedKeysToIntents},
        {"compact layout keeps positive viewport", testCompactLayoutKeepsPositiveViewport},
        {"key intent queue keeps first", testKeyIntentQueueKeepsOnlyFirstIntentPerFrame},
        {"visible focus order uses stable widgets", testVisibleFocusOrderUsesStableWidgetsAndIncludesDisabledControls},
        {"keyboard focus reveal", testKeyboardFocusRevealAdjustsInspectorScroll},
        {"Help owns viewport navigation", testHelpOwnsViewportNavigationInput},
        {"keyboard mutation honors gates", testKeyboardMutationHonorsDisabledAndBusyGates},
        {"keyboard slider adjustment", testKeyboardSliderAdjustmentUsesLinearAndLogarithmicRanges},
        {"focus ring is outline only", testFocusRingPresentationIsAThreePixelOutlineWithClearInterior},
        {"discrete slider retains fractional steps", testDiscreteSliderAccumulatorRetainsSubIntegerKeyboardSteps},
        {"segment adjustment", testSegmentAdjustmentMovesToAdjacentClampedOption},
        {"escape routing", testEscapePrefersCancellableJobThenHelp},
        {"DPI and reduced motion", testDpiScaleChangesRebuildOnlyWhenEffectiveScaleChanges},
        {"viewport surface paint order", testViewportSurfacePaintOrderKeepsHelpAboveStatusAndProgressAboveHelp},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " focused test(s) failed\n";
        return 1;
    }

    std::cout << "All UI-design tests passed\n";
    return 0;
}
