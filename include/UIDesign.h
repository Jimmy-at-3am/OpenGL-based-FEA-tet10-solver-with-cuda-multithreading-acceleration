#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ui_design {

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

struct WindowLayout {
    Rect titleBar;
    Rect viewport;
    Rect inspector;
};

enum class InspectorTab { Model, Mesh, Solve };

enum class ColorToken {
    FrostCanvas,
    SnowSurface,
    PrimaryInk,
    Graphite,
    SystemBlue,
    BlockedRed,
};

enum class ControlId {
    CancelJob,
    SelectCubeMode,
    SelectImportMode,
    SelectModelFile,
    PreviousModelPage,
    NextModelPage,
    SelectMaterial,
    ToggleVertexSmoothing,
    SelectSurfaceView,
    SelectVolumeView,
    GenerateVolumeMesh,
    EditSizeX,
    EditSizeY,
    EditSizeZ,
    EditSubdivisions,
    EditMeshQuality,
    EditMaxVolumePercent,
    ToggleSlicing,
    EditLayerThickness,
    SelectSliceAxisX,
    SelectSliceAxisY,
    SelectSliceAxisZ,
    EditMaxSlabs,
    EditWallWidth,
    PreviewSlice,
    SelectPreviewLayer,
    EditShowcaseMagnitude,
    ResetShowcaseMagnitude,
    RunShowcaseFracture,
    ToggleMultithreading,
    ToggleGpuAcceleration,
    SelectBuildAxis,
    SelectLoadPreset,
    EditLoadMagnitude,
    RunLinearAnalysis,
    RunNonlinearAnalysis,
    EditCurvatureAngle,
    EditCurvatureFraction,
    RunAdaptiveAnalysis,
    ToggleFdmAnisotropy,
    RunBrittleFracture,
    SelectOriginalResult,
    SelectDeformedResult,
    SelectFractureView,
    SelectDeadElementView,
    ToggleForceMap,
    EditSectionPosition,
    OpenHelp,
    ResetView,
};

struct WidgetId {
    ControlId control;
    int instance = 0;

    friend bool operator==(const WidgetId& lhs, const WidgetId& rhs) {
        return lhs.control == rhs.control && lhs.instance == rhs.instance;
    }
};

struct FormattedValue {
    std::string number;
    std::string unit;
};

WindowLayout computeWindowLayout(int widthPx, int heightPx);
FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit);
std::string_view hex(ColorToken token);
const std::vector<ControlId>& requiredControls();
const std::vector<ControlId>& requiredInspectorControls();
const std::vector<ControlId>& requiredOverlayControls();
std::string_view controlToken(ControlId id);

}  // namespace ui_design
