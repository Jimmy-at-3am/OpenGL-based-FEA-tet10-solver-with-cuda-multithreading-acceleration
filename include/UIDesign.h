#pragma once

#include <cstdint>
#include <filesystem>
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

enum class FontRole { Display, Interface, Data };
enum class ControlRole { Primary, Secondary, Ghost, Destructive };
enum class ControlState { Rest, Hover, Pressed, Selected, Disabled, Focused };

enum class ColorToken {
    FrostCanvas,
    SnowSurface,
    PrimaryInk,
    Graphite,
    SystemBlue,
    BlockedRed,
};

struct ControlVisual {
    ColorToken fill;
    ColorToken text;
    float fillOpacity;
    float contentOpacity;
    float focusOpacity;
};

struct Rgba {
    float r;
    float g;
    float b;
    float a;
};

enum class TextBackend { FontAtlas, Stroke };

struct TextDrawPolicy {
    TextBackend backend;
    FontRole role;
    Rgba color;
};

enum class ControlId {
    None,
    SelectModelTab,
    SelectMeshTab,
    SelectSolveTab,
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

struct TextRun {
    std::string text;
    float x;
    FontRole role;
};

struct FormattedValueTextLayout {
    TextRun number;
    TextRun unit;
    float numberRight;
};

enum class ReceiptTone { Neutral, Available, Approximate, Blocked };

struct ReceiptLine {
    std::string label;
    std::string value;
    ReceiptTone tone = ReceiptTone::Neutral;
};

WindowLayout computeWindowLayout(int widthPx, int heightPx);
bool containsPoint(const Rect& rect, float x, float y);
float extendContentBottom(float currentBottom, const Rect& drawnRect);
FormattedValue formatValue(double value, int decimals, bool scientific, std::string_view unit);
FormattedValueTextLayout layoutFormattedValueText(
    const FormattedValue& display, float fieldRight, float numberWidth,
    float unitColumnWidth, float gap);
std::vector<ReceiptLine> makeModelReceipt(
    std::string_view format, bool brepRetained, int objectCount,
    std::string_view physicalSize);
std::vector<ReceiptLine> makeMeshReceipt(
    std::string_view source, std::string_view elementType,
    std::uint64_t elementCount, int printLayers, int slabs,
    int layersPerSlab);
std::vector<ReceiptLine> makeSolveReceipt(
    std::string_view load, std::string_view scope,
    std::string_view distribution, std::string_view support,
    std::string_view capability, ReceiptTone capabilityTone);
std::string_view hex(ColorToken token);
ControlVisual resolveControlVisual(ControlRole role, ControlState state);
Rgba rgba(ColorToken token, float opacity = 1.0f);
TextDrawPolicy resolveTextDrawPolicy(bool fontReady, FontRole role, Rgba color);
std::vector<std::filesystem::path> fontCandidates(FontRole role);
const std::vector<ControlId>& requiredControls();
const std::vector<ControlId>& requiredInspectorControls();
const std::vector<ControlId>& requiredOverlayControls();
std::string_view controlToken(ControlId id);

}  // namespace ui_design
