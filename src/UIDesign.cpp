#include "UIDesign.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ui_design {

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

float extendContentBottom(float currentBottom, const Rect& drawnRect) {
    return std::max(currentBottom, drawnRect.y + drawnRect.h);
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
        visual = {ColorToken::BlockedRed, ColorToken::SnowSurface, 1.0f, 1.0f, 0.0f};
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

}  // namespace ui_design
