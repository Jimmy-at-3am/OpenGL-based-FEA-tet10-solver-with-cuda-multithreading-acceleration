#include "UIDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ui_design {

std::vector<ViewportSurface> viewportSurfacePaintOrder(
    bool showHelp, bool showProgress) {
    std::vector<ViewportSurface> order{ViewportSurface::SolverStatus};
    if (showHelp) {
        order.push_back(ViewportSurface::Help);
    }
    order.push_back(ViewportSurface::Tooltip);
    if (showProgress) {
        order.push_back(ViewportSurface::Progress);
    }
    return order;
}

namespace {

std::string groupDigits(std::uint64_t value) {
    std::string grouped = std::to_string(value);
    for (std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(grouped.size()) - 3;
         offset > 0; offset -= 3) {
        grouped.insert(static_cast<std::size_t>(offset), 1, ',');
    }
    return grouped;
}

std::string toneLabel(ReceiptTone tone) {
    switch (tone) {
    case ReceiptTone::Neutral:
        return {};
    case ReceiptTone::Available:
        return "Available";
    case ReceiptTone::Approximate:
        return "Approximate";
    case ReceiptTone::Blocked:
        return "Blocked";
    }
    return {};
}

std::string describeTone(ReceiptTone tone, std::string_view detail) {
    const std::string label = toneLabel(tone);
    if (label.empty()) {
        return std::string(detail);
    }
    if (detail.empty()) {
        return label;
    }
    return label + " - " + std::string(detail);
}

}  // namespace

WindowLayout computeWindowLayout(int widthPx, int heightPx) {
    if (widthPx <= 0 || heightPx <= 0) {
        throw std::invalid_argument("window dimensions must be positive");
    }

    const float width = static_cast<float>(widthPx);
    const float height = static_cast<float>(heightPx);
    const float panelW = std::clamp(width * 0.28f, 320.0f, 380.0f);
    constexpr float titleBarH = 44.0f;

    return {
        {0.0f, 0.0f, width, titleBarH},
        {0.0f, titleBarH, width - panelW, height - titleBarH},
        {width - panelW, titleBarH, panelW, height - titleBarH},
    };
}

bool containsPoint(const Rect& rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

bool intersects(const Rect& first, const Rect& second) {
    return first.x < second.x + second.w && first.x + first.w > second.x &&
           first.y < second.y + second.h && first.y + first.h > second.y;
}

float extendContentBottom(float currentBottom, const Rect& drawnRect) {
    return std::max(currentBottom, drawnRect.y + drawnRect.h);
}

float essentialTextPixelSize(float requested) {
    return std::max(11.0f, requested);
}

float conservativeTextWidth(std::string_view text, float pixelSize) {
    return static_cast<float>(text.size()) * essentialTextPixelSize(pixelSize) * 1.2f;
}

LongLabelPresentation presentLongLabel(
    std::string_view fullName, float maxWidth, float pixelSize) {
    const std::string full(fullName);
    if (conservativeTextWidth(full, pixelSize) <= maxWidth) {
        return {full, full, false};
    }
    const float advance = essentialTextPixelSize(pixelSize) * 1.2f;
    const std::size_t maxCharacters = maxWidth > 0.0f
        ? static_cast<std::size_t>(std::floor(maxWidth / advance))
        : 0;
    if (maxCharacters <= 3) {
        return {std::string(maxCharacters, '.'), full, true};
    }
    const std::size_t retained = maxCharacters - 3;
    const std::size_t prefix = retained / 2;
    const std::size_t suffix = retained - prefix;
    return {
        full.substr(0, prefix) + "..." + full.substr(full.size() - suffix),
        full,
        true,
    };
}

std::vector<std::string> wrapTextToWidth(
    std::string_view text, float maxWidth, float pixelSize) {
    if (text.empty()) {
        return {};
    }
    const float advance = essentialTextPixelSize(pixelSize) * 1.2f;
    const std::size_t charactersPerLine = std::max<std::size_t>(
        1, maxWidth > 0.0f
            ? static_cast<std::size_t>(std::floor(maxWidth / advance))
            : 1);
    std::vector<std::string> lines;
    for (std::size_t offset = 0; offset < text.size();
         offset += charactersPerLine) {
        lines.emplace_back(text.substr(offset, charactersPerLine));
    }
    return lines;
}

HorizontalSliderGeometry horizontalSliderGeometry(
    const Rect& field, float normalizedPosition) {
    const Rect track{field.x, field.y + field.h - 9.0f, field.w, 4.0f};
    const float position = std::clamp(normalizedPosition, 0.0f, 1.0f);
    return {
        track,
        {track.x + track.w * position - 8.0f, track.y - 6.0f, 16.0f, 16.0f},
    };
}

SectionHeaderLayout sectionHeaderLayout(
    float topY, float requestedTextSize) {
    const float effectiveTextSize = essentialTextPixelSize(requestedTextSize);
    const float labelBaselineY = topY + effectiveTextSize;
    const float dividerY = labelBaselineY + 6.0f;
    return {labelBaselineY, dividerY, dividerY + 14.0f};
}

BinarySegmentPresentation surfaceVolumePresentation(
    bool hasVolumetricMesh, bool showVolumetricMesh) {
    return {
        hasVolumetricMesh && showVolumetricMesh ? 1 : 0,
        {false, !hasVolumetricMesh},
    };
}

SurfaceVolumeAction resolveSurfaceVolumeAction(
    bool selectionChanged, int selectedIndex) {
    if (!selectionChanged) {
        return SurfaceVolumeAction::None;
    }
    return selectedIndex == 0
        ? SurfaceVolumeAction::SelectSurface
        : SurfaceVolumeAction::SelectVolume;
}

ViewportOverlayLayout computeViewportOverlayLayout(
    const Rect& viewport, float windowHeight, float solverStatusWidth,
    int solverStatusRows, bool showProgress) {
    constexpr float progressHeight = 82.0f;
    const float progressWidth = std::min(380.0f, std::max(0.0f, viewport.w - 32.0f));
    const Rect progress{
        viewport.x + 16.0f, windowHeight - progressHeight - 16.0f,
        progressWidth, progressHeight};

    constexpr float rowHeight = 18.0f;
    const float statusHeight = std::max(0, solverStatusRows) * rowHeight;
    const float statusWidth = std::min(
        std::max(0.0f, solverStatusWidth), std::max(0.0f, viewport.w - 24.0f));
    const float statusX = std::max(
        viewport.x + 12.0f, viewport.x + viewport.w - 16.0f - statusWidth);
    float statusY = windowHeight - 14.0f - statusHeight;
    if (showProgress) {
        statusY = std::min(statusY, progress.y - 16.0f - statusHeight);
    }
    statusY = std::max(viewport.y + 8.0f, statusY);
    return {{statusX, statusY, statusWidth, statusHeight}, progress};
}

TitleReadiness makeTitleReadiness(
    bool hasVolumetricMesh, ActiveComputation activeComputation,
    bool hasResults) {
    TitleReadiness readiness{
        hasVolumetricMesh ? "Mesh ready" : "Mesh needed",
        hasVolumetricMesh ? "Solve ready" : "Solve needs mesh",
    };
    if (activeComputation == ActiveComputation::Meshing) {
        readiness.mesh = "Mesh running";
        readiness.solve = "Solve waiting";
        readiness.meshActive = true;
    } else if (activeComputation == ActiveComputation::Solving) {
        readiness.solve = "Solve running";
        readiness.solveActive = true;
    } else if (hasResults) {
        readiness.solve = "Results ready";
    }
    return readiness;
}

ActiveComputation activeComputationForJobTitle(std::string_view title) {
    if (title.empty()) {
        return ActiveComputation::Idle;
    }
    if (title.find("MESHING") != std::string_view::npos) {
        return ActiveComputation::Meshing;
    }
    return ActiveComputation::Solving;
}

FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit) {
    std::ostringstream number;
    number << (scientific ? std::scientific : std::fixed) << std::setprecision(decimals) << value;
    return {number.str(), std::string(unit)};
}

FormattedValueTextLayout layoutFormattedValueText(
    const FormattedValue& display, float fieldRight, float numberWidth,
    float unitColumnWidth, float gap) {
    const float numberRight = fieldRight - unitColumnWidth - gap;
    return {
        {display.number, numberRight - numberWidth, FontRole::Data},
        {display.unit, numberRight + gap, FontRole::Interface},
        numberRight,
    };
}

std::vector<ReceiptLine> makeModelReceipt(
    std::string_view format, bool brepRetained, int objectCount,
    std::string_view physicalSize, bool proceduralCube) {
    const bool honestBrepRetained = !proceduralCube && brepRetained;
    return {
        {"Source", std::string(format)},
        {"B-rep", honestBrepRetained ? "Retained" : "Not retained"},
        {"Objects", std::to_string(objectCount)},
        {"Physical size", std::string(physicalSize)},
    };
}

std::vector<ReceiptLine> makeMeshReceipt(
    std::string_view source, std::string_view elementType,
    std::uint64_t elementCount, int printLayers, int slabs,
    int layersPerSlab) {
    std::vector<ReceiptLine> lines{
        {"Path", std::string(source)},
        {"Elements", std::string(elementType) + " / " +
                         groupDigits(elementCount) + " elements"},
    };
    if (printLayers > 0 || slabs > 0) {
        lines.push_back({
            "Layer mapping",
            std::to_string(printLayers) + " print layers / " +
                std::to_string(slabs) + " FE slabs / k=" +
                std::to_string(layersPerSlab),
        });
    }
    return lines;
}

std::string meshReceiptSource(
    bool cubeMode, bool hasSelectedSource, bool hasToolpath,
    bool brepRetained) {
    if (cubeMode) {
        return "Cube / TetGen";
    }
    if (!hasSelectedSource) {
        return "No model selected";
    }
    if (hasToolpath) {
        return "Toolpath / slab mesher";
    }
    if (brepRetained) {
        return "STEP B-rep / TetGen";
    }
    return "Surface / TetGen";
}

SolvePresentationPolicy solvePresentationPolicy(
    bool cubeMode, bool hasToolpath) {
    const bool showToolpathWorkflow = !cubeMode && hasToolpath;
    return {!showToolpathWorkflow, showToolpathWorkflow};
}

std::string makeSolveCapabilitySummary(
    std::string_view linear, std::string_view nonlinear,
    std::string_view fracture, std::string_view blockedReason) {
    std::string summary =
        "Linear " + std::string(linear) + " / Nonlinear " +
        std::string(nonlinear) + " / Fracture " + std::string(fracture);
    if (!blockedReason.empty()) {
        summary += " / Reason: " + std::string(blockedReason);
    }
    return summary;
}

std::vector<ReceiptLine> makeSolveReceipt(
    std::string_view load, std::string_view scope,
    std::string_view distribution, std::string_view support,
    std::string_view capability, ReceiptTone capabilityTone) {
    return {
        {"Load", std::string(load)},
        {"Scope", std::string(scope)},
        {"Distribution", std::string(distribution)},
        {"Support", std::string(support)},
        {"Capability", describeTone(capabilityTone, capability), capabilityTone},
    };
}

std::string_view hex(ColorToken token) {
    switch (token) {
    case ColorToken::FrostCanvas:
        return "#E9EEF5";
    case ColorToken::SnowSurface:
        return "#F7F7FA";
    case ColorToken::PrimaryInk:
        return "#1D1D1F";
    case ColorToken::Graphite:
        return "#6E6E73";
    case ColorToken::SystemBlue:
        return "#007AFF";
    case ColorToken::BlockedRed:
        return "#C9342E";
    }
    return {};
}

ControlVisual resolveControlVisual(ControlRole role, ControlState state) {
    ControlVisual visual{};
    switch (role) {
    case ControlRole::Primary:
        visual = {ColorToken::SystemBlue, ColorToken::SnowSurface, 1.0f, 1.0f, 0.0f};
        break;
    case ControlRole::Secondary:
        visual = {ColorToken::PrimaryInk, ColorToken::PrimaryInk, 0.08f, 1.0f, 0.0f};
        break;
    case ControlRole::Ghost:
        visual = {ColorToken::SystemBlue, ColorToken::SystemBlue, 0.0f, 1.0f, 0.0f};
        break;
    case ControlRole::Destructive:
        visual = {ColorToken::BlockedRed, ColorToken::BlockedRed, 0.0f, 1.0f, 0.0f};
        break;
    }

    switch (state) {
    case ControlState::Rest:
        break;
    case ControlState::Hover:
        visual.fillOpacity = std::min(1.0f, visual.fillOpacity + 0.08f);
        break;
    case ControlState::Pressed:
        visual.fillOpacity = std::min(1.0f, visual.fillOpacity + 0.16f);
        visual.contentOpacity = 0.88f;
        break;
    case ControlState::Selected:
        visual.fill = ColorToken::SystemBlue;
        visual.text = ColorToken::SnowSurface;
        visual.fillOpacity = 1.0f;
        break;
    case ControlState::Disabled:
        visual.contentOpacity = 0.38f;
        visual.fillOpacity *= 0.5f;
        break;
    case ControlState::Focused:
        visual.focusOpacity = 0.24f;
        break;
    }
    return visual;
}

Rgba rgba(ColorToken token, float opacity) {
    const std::string_view value = hex(token);
    const auto nibble = [](char digit) {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'A' && digit <= 'F') {
            return digit - 'A' + 10;
        }
        return digit - 'a' + 10;
    };
    const auto channel = [&](std::size_t offset) {
        return static_cast<float>(nibble(value[offset]) * 16 + nibble(value[offset + 1])) / 255.0f;
    };
    return {channel(1), channel(3), channel(5), opacity};
}

TextDrawPolicy resolveTextDrawPolicy(bool fontReady, FontRole role, Rgba color) {
    return {fontReady ? TextBackend::FontAtlas : TextBackend::Stroke, role, color};
}

std::vector<std::filesystem::path> fontCandidates(FontRole role) {
#ifdef _WIN32
    std::filesystem::path windowsDirectory = "C:\\Windows";
    if (const char* windir = std::getenv("WINDIR")) {
        windowsDirectory = windir;
    }
    const auto fonts = windowsDirectory / "Fonts";
    switch (role) {
    case FontRole::Display:
        return {fonts / "SegUIVar.ttf", fonts / "segoeuib.ttf", fonts / "segoeui.ttf"};
    case FontRole::Interface:
        return {fonts / "SegUIVar.ttf", fonts / "segoeui.ttf"};
    case FontRole::Data:
        return {fonts / "CascadiaMono.ttf", fonts / "consola.ttf", fonts / "SegUIVar.ttf"};
    }
#else
    switch (role) {
    case FontRole::Display:
        return {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        };
    case FontRole::Interface:
        return {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        };
    case FontRole::Data:
        return {
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        };
    }
#endif
    return {};
}

const std::vector<ControlId>& requiredControls() {
    static const std::vector<ControlId> controls = [] {
        std::vector<ControlId> result;
        for (const auto id : requiredInspectorControls()) {
            result.push_back(id);
        }
        for (const auto id : requiredOverlayControls()) {
            if (std::find(result.begin(), result.end(), id) == result.end()) {
                result.push_back(id);
            }
        }
        return result;
    }();
    return controls;
}

const std::vector<ControlId>& requiredInspectorControls() {
    static const std::vector<ControlId> controls = {
        ControlId::SelectModelTab,
        ControlId::SelectMeshTab,
        ControlId::SelectSolveTab,
        ControlId::SelectCubeMode,
        ControlId::SelectImportMode,
        ControlId::SelectModelFile,
        ControlId::PreviousModelPage,
        ControlId::NextModelPage,
        ControlId::SelectMaterial,
        ControlId::ToggleVertexSmoothing,
        ControlId::SelectSurfaceView,
        ControlId::SelectVolumeView,
        ControlId::GenerateVolumeMesh,
        ControlId::EditSizeX,
        ControlId::EditSizeY,
        ControlId::EditSizeZ,
        ControlId::EditSubdivisions,
        ControlId::EditMeshQuality,
        ControlId::EditMaxVolumePercent,
        ControlId::ToggleSlicing,
        ControlId::EditLayerThickness,
        ControlId::SelectSliceAxisX,
        ControlId::SelectSliceAxisY,
        ControlId::SelectSliceAxisZ,
        ControlId::EditMaxSlabs,
        ControlId::EditWallWidth,
        ControlId::PreviewSlice,
        ControlId::SelectPreviewLayer,
        ControlId::EditShowcaseMagnitude,
        ControlId::ResetShowcaseMagnitude,
        ControlId::RunShowcaseFracture,
        ControlId::ToggleMultithreading,
        ControlId::ToggleGpuAcceleration,
        ControlId::SelectBuildAxis,
        ControlId::SelectLoadPreset,
        ControlId::EditLoadMagnitude,
        ControlId::RunLinearAnalysis,
        ControlId::RunNonlinearAnalysis,
        ControlId::EditCurvatureAngle,
        ControlId::EditCurvatureFraction,
        ControlId::RunAdaptiveAnalysis,
        ControlId::ToggleFdmAnisotropy,
        ControlId::RunBrittleFracture,
        ControlId::SelectOriginalResult,
        ControlId::SelectDeformedResult,
        ControlId::SelectFractureView,
        ControlId::SelectDeadElementView,
        ControlId::ToggleForceMap,
    };
    return controls;
}

const std::vector<ControlId>& requiredOverlayControls() {
    static const std::vector<ControlId> controls = {
        ControlId::CancelJob,
        ControlId::EditSectionPosition,
        ControlId::OpenHelp,
        ControlId::ResetView,
    };
    return controls;
}

std::string_view controlToken(ControlId id) {
    switch (id) {
    case ControlId::None:
        return {};
    case ControlId::SelectModelTab:
        return "select-model-tab";
    case ControlId::SelectMeshTab:
        return "select-mesh-tab";
    case ControlId::SelectSolveTab:
        return "select-solve-tab";
    case ControlId::CancelJob:
        return "cancel-job";
    case ControlId::SelectCubeMode:
        return "select-cube-mode";
    case ControlId::SelectImportMode:
        return "select-import-mode";
    case ControlId::SelectModelFile:
        return "select-model-file";
    case ControlId::PreviousModelPage:
        return "previous-model-page";
    case ControlId::NextModelPage:
        return "next-model-page";
    case ControlId::SelectMaterial:
        return "select-material";
    case ControlId::ToggleVertexSmoothing:
        return "toggle-vertex-smoothing";
    case ControlId::SelectSurfaceView:
        return "select-surface-view";
    case ControlId::SelectVolumeView:
        return "select-volume-view";
    case ControlId::GenerateVolumeMesh:
        return "generate-volume-mesh";
    case ControlId::EditSizeX:
        return "edit-size-x";
    case ControlId::EditSizeY:
        return "edit-size-y";
    case ControlId::EditSizeZ:
        return "edit-size-z";
    case ControlId::EditSubdivisions:
        return "edit-subdivisions";
    case ControlId::EditMeshQuality:
        return "edit-mesh-quality";
    case ControlId::EditMaxVolumePercent:
        return "edit-max-volume-percent";
    case ControlId::ToggleSlicing:
        return "toggle-slicing";
    case ControlId::EditLayerThickness:
        return "edit-layer-thickness";
    case ControlId::SelectSliceAxisX:
        return "select-slice-axis-x";
    case ControlId::SelectSliceAxisY:
        return "select-slice-axis-y";
    case ControlId::SelectSliceAxisZ:
        return "select-slice-axis-z";
    case ControlId::EditMaxSlabs:
        return "edit-max-slabs";
    case ControlId::EditWallWidth:
        return "edit-wall-width";
    case ControlId::PreviewSlice:
        return "preview-slice";
    case ControlId::SelectPreviewLayer:
        return "select-preview-layer";
    case ControlId::EditShowcaseMagnitude:
        return "edit-showcase-magnitude";
    case ControlId::ResetShowcaseMagnitude:
        return "reset-showcase-magnitude";
    case ControlId::RunShowcaseFracture:
        return "run-showcase-fracture";
    case ControlId::ToggleMultithreading:
        return "toggle-multithreading";
    case ControlId::ToggleGpuAcceleration:
        return "toggle-gpu-acceleration";
    case ControlId::SelectBuildAxis:
        return "select-build-axis";
    case ControlId::SelectLoadPreset:
        return "select-load-preset";
    case ControlId::EditLoadMagnitude:
        return "edit-load-magnitude";
    case ControlId::RunLinearAnalysis:
        return "run-linear-analysis";
    case ControlId::RunNonlinearAnalysis:
        return "run-nonlinear-analysis";
    case ControlId::EditCurvatureAngle:
        return "edit-curvature-angle";
    case ControlId::EditCurvatureFraction:
        return "edit-curvature-fraction";
    case ControlId::RunAdaptiveAnalysis:
        return "run-adaptive-analysis";
    case ControlId::ToggleFdmAnisotropy:
        return "toggle-fdm-anisotropy";
    case ControlId::RunBrittleFracture:
        return "run-brittle-fracture";
    case ControlId::SelectOriginalResult:
        return "select-original-result";
    case ControlId::SelectDeformedResult:
        return "select-deformed-result";
    case ControlId::SelectFractureView:
        return "select-fracture-view";
    case ControlId::SelectDeadElementView:
        return "select-dead-element-view";
    case ControlId::ToggleForceMap:
        return "toggle-force-map";
    case ControlId::EditSectionPosition:
        return "edit-section-position";
    case ControlId::OpenHelp:
        return "open-help";
    case ControlId::ResetView:
        return "reset-view";
    }
    return {};
}

ControlSurface controlSurface(ControlId id) {
    switch (id) {
    case ControlId::None:
        return ControlSurface::Unknown;
    case ControlId::OpenHelp:
    case ControlId::ResetView:
        return ControlSurface::TitleBar;
    case ControlId::SelectModelTab:
    case ControlId::SelectMeshTab:
    case ControlId::SelectSolveTab:
        return ControlSurface::TabStrip;
    case ControlId::SelectCubeMode:
    case ControlId::SelectImportMode:
    case ControlId::SelectModelFile:
    case ControlId::PreviousModelPage:
    case ControlId::NextModelPage:
    case ControlId::SelectMaterial:
    case ControlId::EditSizeX:
    case ControlId::EditSizeY:
    case ControlId::EditSizeZ:
    case ControlId::EditSubdivisions:
        return ControlSurface::Model;
    case ControlId::ToggleVertexSmoothing:
    case ControlId::SelectSurfaceView:
    case ControlId::SelectVolumeView:
    case ControlId::GenerateVolumeMesh:
    case ControlId::EditMeshQuality:
    case ControlId::EditMaxVolumePercent:
    case ControlId::ToggleSlicing:
    case ControlId::EditLayerThickness:
    case ControlId::SelectSliceAxisX:
    case ControlId::SelectSliceAxisY:
    case ControlId::SelectSliceAxisZ:
    case ControlId::EditMaxSlabs:
    case ControlId::EditWallWidth:
    case ControlId::PreviewSlice:
    case ControlId::SelectPreviewLayer:
        return ControlSurface::Mesh;
    case ControlId::EditShowcaseMagnitude:
    case ControlId::ResetShowcaseMagnitude:
    case ControlId::RunShowcaseFracture:
    case ControlId::ToggleMultithreading:
    case ControlId::ToggleGpuAcceleration:
    case ControlId::SelectBuildAxis:
    case ControlId::SelectLoadPreset:
    case ControlId::EditLoadMagnitude:
    case ControlId::RunLinearAnalysis:
    case ControlId::RunNonlinearAnalysis:
    case ControlId::EditCurvatureAngle:
    case ControlId::EditCurvatureFraction:
    case ControlId::RunAdaptiveAnalysis:
    case ControlId::ToggleFdmAnisotropy:
    case ControlId::RunBrittleFracture:
    case ControlId::SelectOriginalResult:
    case ControlId::SelectDeformedResult:
    case ControlId::SelectFractureView:
    case ControlId::SelectDeadElementView:
    case ControlId::ToggleForceMap:
        return ControlSurface::Solve;
    case ControlId::CancelJob:
    case ControlId::EditSectionPosition:
        return ControlSurface::Viewport;
    }
    return ControlSurface::Unknown;
}

}  // namespace ui_design
