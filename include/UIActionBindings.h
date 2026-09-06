#pragma once

#include "UIDesign.h"

#include <optional>

namespace ui_action_wiring {

enum class InspectorAction {
    SelectModelTab,
    SelectMeshTab,
    SelectSolveTab,
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
    CancelJob,
    EditSectionPosition,
    OpenHelp,
    ResetView,
};

inline std::optional<InspectorAction> actionFor(ui_design::ControlId control) {
    using ui_design::ControlId;
    switch (control) {
    case ControlId::SelectModelTab: return InspectorAction::SelectModelTab;
    case ControlId::SelectMeshTab: return InspectorAction::SelectMeshTab;
    case ControlId::SelectSolveTab: return InspectorAction::SelectSolveTab;
    case ControlId::SelectCubeMode: return InspectorAction::SelectCubeMode;
    case ControlId::SelectImportMode: return InspectorAction::SelectImportMode;
    case ControlId::SelectModelFile: return InspectorAction::SelectModelFile;
    case ControlId::PreviousModelPage: return InspectorAction::PreviousModelPage;
    case ControlId::NextModelPage: return InspectorAction::NextModelPage;
    case ControlId::SelectMaterial: return InspectorAction::SelectMaterial;
    case ControlId::ToggleVertexSmoothing: return InspectorAction::ToggleVertexSmoothing;
    case ControlId::SelectSurfaceView: return InspectorAction::SelectSurfaceView;
    case ControlId::SelectVolumeView: return InspectorAction::SelectVolumeView;
    case ControlId::GenerateVolumeMesh: return InspectorAction::GenerateVolumeMesh;
    case ControlId::EditSizeX: return InspectorAction::EditSizeX;
    case ControlId::EditSizeY: return InspectorAction::EditSizeY;
    case ControlId::EditSizeZ: return InspectorAction::EditSizeZ;
    case ControlId::EditSubdivisions: return InspectorAction::EditSubdivisions;
    case ControlId::EditMeshQuality: return InspectorAction::EditMeshQuality;
    case ControlId::EditMaxVolumePercent: return InspectorAction::EditMaxVolumePercent;
    case ControlId::ToggleSlicing: return InspectorAction::ToggleSlicing;
    case ControlId::EditLayerThickness: return InspectorAction::EditLayerThickness;
    case ControlId::SelectSliceAxisX: return InspectorAction::SelectSliceAxisX;
    case ControlId::SelectSliceAxisY: return InspectorAction::SelectSliceAxisY;
    case ControlId::SelectSliceAxisZ: return InspectorAction::SelectSliceAxisZ;
    case ControlId::EditMaxSlabs: return InspectorAction::EditMaxSlabs;
    case ControlId::EditWallWidth: return InspectorAction::EditWallWidth;
    case ControlId::PreviewSlice: return InspectorAction::PreviewSlice;
    case ControlId::SelectPreviewLayer: return InspectorAction::SelectPreviewLayer;
    case ControlId::EditShowcaseMagnitude: return InspectorAction::EditShowcaseMagnitude;
    case ControlId::ResetShowcaseMagnitude: return InspectorAction::ResetShowcaseMagnitude;
    case ControlId::RunShowcaseFracture: return InspectorAction::RunShowcaseFracture;
    case ControlId::ToggleMultithreading: return InspectorAction::ToggleMultithreading;
    case ControlId::ToggleGpuAcceleration: return InspectorAction::ToggleGpuAcceleration;
    case ControlId::SelectBuildAxis: return InspectorAction::SelectBuildAxis;
    case ControlId::SelectLoadPreset: return InspectorAction::SelectLoadPreset;
    case ControlId::EditLoadMagnitude: return InspectorAction::EditLoadMagnitude;
    case ControlId::RunLinearAnalysis: return InspectorAction::RunLinearAnalysis;
    case ControlId::RunNonlinearAnalysis: return InspectorAction::RunNonlinearAnalysis;
    case ControlId::EditCurvatureAngle: return InspectorAction::EditCurvatureAngle;
    case ControlId::EditCurvatureFraction: return InspectorAction::EditCurvatureFraction;
    case ControlId::RunAdaptiveAnalysis: return InspectorAction::RunAdaptiveAnalysis;
    case ControlId::ToggleFdmAnisotropy: return InspectorAction::ToggleFdmAnisotropy;
    case ControlId::RunBrittleFracture: return InspectorAction::RunBrittleFracture;
    case ControlId::SelectOriginalResult: return InspectorAction::SelectOriginalResult;
    case ControlId::SelectDeformedResult: return InspectorAction::SelectDeformedResult;
    case ControlId::SelectFractureView: return InspectorAction::SelectFractureView;
    case ControlId::SelectDeadElementView: return InspectorAction::SelectDeadElementView;
    case ControlId::ToggleForceMap: return InspectorAction::ToggleForceMap;
    case ControlId::CancelJob: return InspectorAction::CancelJob;
    case ControlId::EditSectionPosition: return InspectorAction::EditSectionPosition;
    case ControlId::OpenHelp: return InspectorAction::OpenHelp;
    case ControlId::ResetView: return InspectorAction::ResetView;
    case ControlId::None:
        return std::nullopt;
    }
    return std::nullopt;
}

struct InspectorEvent {
    InspectorAction action;
    ui_design::WidgetId widget;
    std::optional<double> value;
};

template <typename ActionHandler>
class InspectorBindings {
public:
    explicit InspectorBindings(ActionHandler& handler) : handler_(handler) {}

    bool activate(ui_design::WidgetId widget) const {
        return dispatch(widget, std::nullopt);
    }

    bool change(ui_design::WidgetId widget, double value) const {
        return dispatch(widget, value);
    }

private:
    bool dispatch(ui_design::WidgetId widget, std::optional<double> value) const {
        const auto action = actionFor(widget.control);
        if (!action) {
            return false;
        }
        handler_.handle({*action, widget, value});
        return true;
    }

    ActionHandler& handler_;
};

template <typename ActionHandler>
InspectorBindings<ActionHandler> makeInspectorBindings(ActionHandler& handler) {
    return InspectorBindings<ActionHandler>(handler);
}

template <InspectorAction ExpectedAction, typename Callback>
bool invokeInspectorAction(ui_design::WidgetId widget, Callback callback) {
    struct ExactActionHandler {
        Callback& callback;
        bool invoked = false;

        void handle(const InspectorEvent& event) {
            if (event.action == ExpectedAction) {
                callback(event);
                invoked = true;
            }
        }
    } handler{callback};

    const bool accepted = makeInspectorBindings(handler).activate(widget);
    return accepted && handler.invoked;
}

template <InspectorAction ExpectedAction, typename Callback>
bool invokeInspectorValue(
    ui_design::WidgetId widget, double value, Callback callback) {
    struct ExactActionHandler {
        Callback& callback;
        bool invoked = false;

        void handle(const InspectorEvent& event) {
            if (event.action == ExpectedAction) {
                callback(event);
                invoked = true;
            }
        }
    } handler{callback};

    const bool accepted = makeInspectorBindings(handler).change(widget, value);
    return accepted && handler.invoked;
}

}  // namespace ui_action_wiring
