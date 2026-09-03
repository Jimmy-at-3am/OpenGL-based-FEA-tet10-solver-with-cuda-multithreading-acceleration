#include "UIActionBindings.h"
#include "UIDesign.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RecordingActionHandler {
    std::optional<ui_action_wiring::InspectorAction> lastAction;
    std::optional<ui_design::WidgetId> lastWidget;
    std::optional<double> lastValue;
    std::size_t activationCount = 0;

    void handle(const ui_action_wiring::InspectorEvent& event) {
        lastAction = event.action;
        lastWidget = event.widget;
        lastValue = event.value;
        ++activationCount;
    }
};

struct ExpectedBinding {
    ui_design::ControlId control;
    ui_action_wiring::InspectorAction action;
};

const std::vector<ExpectedBinding> expectedProductionBindings{
    {ui_design::ControlId::SelectModelTab,
     ui_action_wiring::InspectorAction::SelectModelTab},
    {ui_design::ControlId::SelectMeshTab,
     ui_action_wiring::InspectorAction::SelectMeshTab},
    {ui_design::ControlId::SelectSolveTab,
     ui_action_wiring::InspectorAction::SelectSolveTab},
    {ui_design::ControlId::SelectCubeMode,
     ui_action_wiring::InspectorAction::SelectCubeMode},
    {ui_design::ControlId::SelectImportMode,
     ui_action_wiring::InspectorAction::SelectImportMode},
    {ui_design::ControlId::SelectModelFile,
     ui_action_wiring::InspectorAction::SelectModelFile},
    {ui_design::ControlId::PreviousModelPage,
     ui_action_wiring::InspectorAction::PreviousModelPage},
    {ui_design::ControlId::NextModelPage,
     ui_action_wiring::InspectorAction::NextModelPage},
    {ui_design::ControlId::SelectMaterial,
     ui_action_wiring::InspectorAction::SelectMaterial},
    {ui_design::ControlId::ToggleVertexSmoothing,
     ui_action_wiring::InspectorAction::ToggleVertexSmoothing},
    {ui_design::ControlId::SelectSurfaceView,
     ui_action_wiring::InspectorAction::SelectSurfaceView},
    {ui_design::ControlId::SelectVolumeView,
     ui_action_wiring::InspectorAction::SelectVolumeView},
    {ui_design::ControlId::GenerateVolumeMesh,
     ui_action_wiring::InspectorAction::GenerateVolumeMesh},
    {ui_design::ControlId::EditSizeX,
     ui_action_wiring::InspectorAction::EditSizeX},
    {ui_design::ControlId::EditSizeY,
     ui_action_wiring::InspectorAction::EditSizeY},
    {ui_design::ControlId::EditSizeZ,
     ui_action_wiring::InspectorAction::EditSizeZ},
    {ui_design::ControlId::EditSubdivisions,
     ui_action_wiring::InspectorAction::EditSubdivisions},
    {ui_design::ControlId::EditMeshQuality,
     ui_action_wiring::InspectorAction::EditMeshQuality},
    {ui_design::ControlId::EditMaxVolumePercent,
     ui_action_wiring::InspectorAction::EditMaxVolumePercent},
    {ui_design::ControlId::ToggleSlicing,
     ui_action_wiring::InspectorAction::ToggleSlicing},
    {ui_design::ControlId::EditLayerThickness,
     ui_action_wiring::InspectorAction::EditLayerThickness},
    {ui_design::ControlId::SelectSliceAxisX,
     ui_action_wiring::InspectorAction::SelectSliceAxisX},
    {ui_design::ControlId::SelectSliceAxisY,
     ui_action_wiring::InspectorAction::SelectSliceAxisY},
    {ui_design::ControlId::SelectSliceAxisZ,
     ui_action_wiring::InspectorAction::SelectSliceAxisZ},
    {ui_design::ControlId::EditMaxSlabs,
     ui_action_wiring::InspectorAction::EditMaxSlabs},
    {ui_design::ControlId::EditWallWidth,
     ui_action_wiring::InspectorAction::EditWallWidth},
    {ui_design::ControlId::PreviewSlice,
     ui_action_wiring::InspectorAction::PreviewSlice},
    {ui_design::ControlId::SelectPreviewLayer,
     ui_action_wiring::InspectorAction::SelectPreviewLayer},
    {ui_design::ControlId::EditShowcaseMagnitude,
     ui_action_wiring::InspectorAction::EditShowcaseMagnitude},
    {ui_design::ControlId::ResetShowcaseMagnitude,
     ui_action_wiring::InspectorAction::ResetShowcaseMagnitude},
    {ui_design::ControlId::RunShowcaseFracture,
     ui_action_wiring::InspectorAction::RunShowcaseFracture},
    {ui_design::ControlId::ToggleMultithreading,
     ui_action_wiring::InspectorAction::ToggleMultithreading},
    {ui_design::ControlId::ToggleGpuAcceleration,
     ui_action_wiring::InspectorAction::ToggleGpuAcceleration},
    {ui_design::ControlId::SelectBuildAxis,
     ui_action_wiring::InspectorAction::SelectBuildAxis},
    {ui_design::ControlId::SelectLoadPreset,
     ui_action_wiring::InspectorAction::SelectLoadPreset},
    {ui_design::ControlId::EditLoadMagnitude,
     ui_action_wiring::InspectorAction::EditLoadMagnitude},
    {ui_design::ControlId::RunLinearAnalysis,
     ui_action_wiring::InspectorAction::RunLinearAnalysis},
    {ui_design::ControlId::RunNonlinearAnalysis,
     ui_action_wiring::InspectorAction::RunNonlinearAnalysis},
    {ui_design::ControlId::EditCurvatureAngle,
     ui_action_wiring::InspectorAction::EditCurvatureAngle},
    {ui_design::ControlId::EditCurvatureFraction,
     ui_action_wiring::InspectorAction::EditCurvatureFraction},
    {ui_design::ControlId::RunAdaptiveAnalysis,
     ui_action_wiring::InspectorAction::RunAdaptiveAnalysis},
    {ui_design::ControlId::ToggleFdmAnisotropy,
     ui_action_wiring::InspectorAction::ToggleFdmAnisotropy},
    {ui_design::ControlId::RunBrittleFracture,
     ui_action_wiring::InspectorAction::RunBrittleFracture},
    {ui_design::ControlId::SelectOriginalResult,
     ui_action_wiring::InspectorAction::SelectOriginalResult},
    {ui_design::ControlId::SelectDeformedResult,
     ui_action_wiring::InspectorAction::SelectDeformedResult},
    {ui_design::ControlId::SelectFractureView,
     ui_action_wiring::InspectorAction::SelectFractureView},
    {ui_design::ControlId::SelectDeadElementView,
     ui_action_wiring::InspectorAction::SelectDeadElementView},
    {ui_design::ControlId::ToggleForceMap,
     ui_action_wiring::InspectorAction::ToggleForceMap},
};

void testEveryProductionControlReachesItsIntendedNamedActionExactlyOnce() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);
    const auto& productionControls = ui_design::requiredInspectorControls();

    expectTrue(expectedProductionBindings.size() ==
                   productionControls.size(),
               "literal production bindings must cover the inspector manifest");
    for (std::size_t index = 0; index < expectedProductionBindings.size(); ++index) {
        const auto& expected = expectedProductionBindings[index];
        expectTrue(productionControls[index] == expected.control,
                   "production inspector manifest must retain every intended control");
        const std::size_t previousCount = actions.activationCount;
        expectTrue(bindings.activate({expected.control, 0}),
                   "production inspector control must activate");
        expectTrue(actions.lastAction == expected.action,
                   "control must reach its independently expected named action");
        expectTrue(actions.lastWidget == ui_design::WidgetId{expected.control, 0},
                   "activation must preserve its stable widget identity");
        expectTrue(actions.activationCount == previousCount + 1,
                   "one control event must invoke exactly one action");
    }
}

void testLinearAndNonlinearControlsCannotBeSwapped() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);

    expectTrue(bindings.activate({ui_design::ControlId::RunLinearAnalysis, 0}),
               "linear action must activate");
    expectTrue(actions.lastAction ==
                   ui_action_wiring::InspectorAction::RunLinearAnalysis,
               "linear control must select the linear action");
    expectTrue(bindings.activate({ui_design::ControlId::RunNonlinearAnalysis, 0}),
               "nonlinear action must activate");
    expectTrue(actions.lastAction ==
                   ui_action_wiring::InspectorAction::RunNonlinearAnalysis,
               "nonlinear control must select the nonlinear action");
}

void testProductionInvocationRejectsMismatchedNamedAction() {
    int linearInvocations = 0;
    const auto recordLinear = [&](const ui_action_wiring::InspectorEvent&) {
        ++linearInvocations;
    };

    expectTrue(
        ui_action_wiring::invokeInspectorAction<
            ui_action_wiring::InspectorAction::RunLinearAnalysis>(
                {ui_design::ControlId::RunLinearAnalysis, 0}, recordLinear),
        "matching production action must invoke its body");
    expectTrue(linearInvocations == 1,
               "matching production action must invoke exactly once");
    expectTrue(
        !ui_action_wiring::invokeInspectorAction<
            ui_action_wiring::InspectorAction::RunLinearAnalysis>(
                {ui_design::ControlId::RunNonlinearAnalysis, 0}, recordLinear),
        "mismatched production action must be rejected");
    expectTrue(linearInvocations == 1,
               "mismatched production action must not invoke the body");
}

void testProductionValueInvocationPreservesEmittedValue() {
    std::optional<double> observedValue;
    const auto recordValue = [&](const ui_action_wiring::InspectorEvent& event) {
        observedValue = event.value;
    };

    expectTrue(
        ui_action_wiring::invokeInspectorValue<
            ui_action_wiring::InspectorAction::EditLoadMagnitude>(
                {ui_design::ControlId::EditLoadMagnitude, 0}, 125.5, recordValue),
        "matching production value action must invoke its body");
    expectTrue(observedValue == 125.5,
               "production value action must preserve the emitted value");
    observedValue.reset();
    expectTrue(
        !ui_action_wiring::invokeInspectorValue<
            ui_action_wiring::InspectorAction::EditLoadMagnitude>(
                {ui_design::ControlId::EditCurvatureAngle, 0}, 17.0, recordValue),
        "mismatched production value action must be rejected");
    expectTrue(!observedValue.has_value(),
               "mismatched value action must not invoke the body");
}

void testRepeatedRowsPreserveWidgetInstance() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);

    const ui_design::WidgetId modelRow{ui_design::ControlId::SelectModelFile, 7};
    expectTrue(bindings.activate(modelRow), "model row binding must activate");
    expectTrue(actions.lastAction ==
                   ui_action_wiring::InspectorAction::SelectModelFile,
               "model row must reach the model-selection action");
    expectTrue(actions.lastWidget == modelRow,
               "model row binding must preserve its instance index");

    const ui_design::WidgetId materialRow{ui_design::ControlId::SelectMaterial, 11};
    expectTrue(bindings.activate(materialRow), "material row binding must activate");
    expectTrue(actions.lastAction ==
                   ui_action_wiring::InspectorAction::SelectMaterial,
               "material row must reach the material-selection action");
    expectTrue(actions.lastWidget == materialRow,
               "material row binding must preserve its instance index");
}

void testValueEventPreservesStableWidgetAndValue() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);
    const ui_design::WidgetId slider{ui_design::ControlId::EditSizeX, 0};

    expectTrue(bindings.change(slider, 2.75),
               "required value binding must activate");
    expectTrue(actions.lastAction == ui_action_wiring::InspectorAction::EditSizeX,
               "value binding must reach the X-size edit action");
    expectTrue(actions.lastWidget == slider,
               "value binding must preserve its stable widget identity");
    expectTrue(actions.lastValue == 2.75,
               "value binding must preserve the emitted value");
}

void testUnknownBindingIsRejectedWithoutInvokingHandler() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);

    expectTrue(!bindings.activate({ui_design::ControlId::None, 0}),
               "unknown control must be rejected");
    expectTrue(actions.activationCount == 0,
               "rejected controls must not invoke an action");
    expectTrue(!actions.lastWidget.has_value(),
               "rejected controls must not produce a widget event");
}

void testInspectorClipRejectsHiddenPointerArea() {
    const ui_design::Rect clip{100.0f, 120.0f, 320.0f, 500.0f};
    expectTrue(ui_design::containsPoint(clip, 100.0f, 120.0f),
               "clip must own its visible top-left boundary");
    expectTrue(ui_design::containsPoint(clip, 419.0f, 619.0f),
               "clip must own visible interior points");
    expectTrue(!ui_design::containsPoint(clip, 110.0f, 119.0f),
               "clip must reject visually hidden points above it");
    expectTrue(!ui_design::containsPoint(clip, 420.0f, 200.0f),
               "clip must reject points at its exclusive right edge");
}

void testRetainedInspectorContentExtendsScrollRange() {
    const ui_design::Rect retainedLegend{100.0f, 512.0f, 280.0f, 120.0f};
    expectTrue(ui_design::extendContentBottom(540.0f, retainedLegend) == 632.0f,
               "retained content below the cursor must extend the scroll range");
    expectTrue(ui_design::extendContentBottom(700.0f, retainedLegend) == 700.0f,
               "earlier content must not shrink the scroll range");
}

}  // namespace

int main() {
    testEveryProductionControlReachesItsIntendedNamedActionExactlyOnce();
    testLinearAndNonlinearControlsCannotBeSwapped();
    testProductionInvocationRejectsMismatchedNamedAction();
    testProductionValueInvocationPreservesEmittedValue();
    testRepeatedRowsPreserveWidgetInstance();
    testValueEventPreservesStableWidgetAndValue();
    testUnknownBindingIsRejectedWithoutInvokingHandler();
    testInspectorClipRejectsHiddenPointerArea();
    testRetainedInspectorContentExtendsScrollRange();
    return 0;
}
