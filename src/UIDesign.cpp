#include "UIDesign.h"

#include <algorithm>
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

FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit) {
    std::ostringstream number;
    number << (scientific ? std::scientific : std::fixed) << std::setprecision(decimals) << value;
    return {number.str(), std::string(unit)};
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

const std::vector<ControlId>& requiredControls() {
    static const std::vector<ControlId> controls = {
        ControlId::CancelJob,
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
        ControlId::ToggleSlicing,
        ControlId::SelectSliceAxisX,
        ControlId::SelectSliceAxisY,
        ControlId::SelectSliceAxisZ,
        ControlId::PreviewSlice,
        ControlId::EditShowcaseMagnitude,
        ControlId::ResetShowcaseMagnitude,
        ControlId::RunShowcaseFracture,
        ControlId::ToggleMultithreading,
        ControlId::ToggleGpuAcceleration,
        ControlId::SelectBuildAxis,
        ControlId::SelectLoadPreset,
        ControlId::RunLinearAnalysis,
        ControlId::RunNonlinearAnalysis,
        ControlId::RunAdaptiveAnalysis,
        ControlId::ToggleFdmAnisotropy,
        ControlId::RunBrittleFracture,
        ControlId::SelectOriginalResult,
        ControlId::SelectDeformedResult,
        ControlId::SelectFractureView,
        ControlId::SelectDeadElementView,
        ControlId::ToggleForceMap,
        ControlId::OpenHelp,
        ControlId::ResetView,
    };
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
        ControlId::ToggleSlicing,
        ControlId::SelectSliceAxisX,
        ControlId::SelectSliceAxisY,
        ControlId::SelectSliceAxisZ,
        ControlId::PreviewSlice,
        ControlId::EditShowcaseMagnitude,
        ControlId::ResetShowcaseMagnitude,
        ControlId::RunShowcaseFracture,
        ControlId::ToggleMultithreading,
        ControlId::ToggleGpuAcceleration,
        ControlId::SelectBuildAxis,
        ControlId::SelectLoadPreset,
        ControlId::RunLinearAnalysis,
        ControlId::RunNonlinearAnalysis,
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
        ControlId::OpenHelp,
        ControlId::ResetView,
    };
    return controls;
}

std::string_view controlToken(ControlId id) {
    switch (id) {
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
    case ControlId::ToggleSlicing:
        return "toggle-slicing";
    case ControlId::SelectSliceAxisX:
        return "select-slice-axis-x";
    case ControlId::SelectSliceAxisY:
        return "select-slice-axis-y";
    case ControlId::SelectSliceAxisZ:
        return "select-slice-axis-z";
    case ControlId::PreviewSlice:
        return "preview-slice";
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
    case ControlId::RunLinearAnalysis:
        return "run-linear-analysis";
    case ControlId::RunNonlinearAnalysis:
        return "run-nonlinear-analysis";
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
    case ControlId::OpenHelp:
        return "open-help";
    case ControlId::ResetView:
        return "reset-view";
    }
    return {};
}

}  // namespace ui_design
