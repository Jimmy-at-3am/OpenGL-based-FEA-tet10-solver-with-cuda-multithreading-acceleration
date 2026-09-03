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

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"window layout", testWindowLayout},
        {"invalid window dimensions", testWindowLayoutRejectsInvalidDimensions},
        {"numeric formatting", testNumericFormatting},
        {"palette and control manifest", testPaletteAndControlManifest},
        {"control partitions and widget identity", testControlManifestPartitionsAndWidgetIdentity},
        {"control visual states", testControlVisualStates},
        {"font candidate order", testFontCandidateOrder},
        {"theme color conversion", testThemeColorConversion},
        {"formatted value separates number and unit", testFormattedValueLayoutSeparatesNumberAndUnit},
        {"receipt presentation", testReceiptPresentation},
        {"cube routes to generic solve workflow", testSolvePresentationRoutesCubeToGenericWorkflow},
        {"empty import has no mesh source claim", testEmptyImportHasNoMeshSourceClaim},
        {"solve capability omits adaptive", testSolveCapabilitySummaryOmitsUnassessedAdaptiveMode},
        {"stroke fallback preserves requested opacity", testStrokeFallbackPreservesRequestedOpacity},
        {"inspector scroll ownership", testInspectorScrollOwnership},
        {"focus order skips hidden controls", testFocusOrderSkipsHiddenControls},
        {"selecting inspector tab clears focus", testSelectingInspectorTabClearsFocus},
        {"keyboard navigation maps pressed keys to intents", testKeyboardNavigationMapsPressedKeysToIntents},
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
