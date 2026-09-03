#include "UIDesign.h"

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
