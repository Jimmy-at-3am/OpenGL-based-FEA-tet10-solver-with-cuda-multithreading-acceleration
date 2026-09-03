#include "UIActionBindings.h"
#include "UIDesign.h"

#include <cstddef>
#include <optional>
#include <stdexcept>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RecordingActionHandler {
    std::optional<ui_design::WidgetId> lastWidget;
    std::optional<double> lastValue;
    std::size_t activationCount = 0;

    void activate(const ui_action_wiring::InspectorEvent& event) {
        lastWidget = event.widget;
        lastValue = event.value;
        ++activationCount;
    }
};

void testEveryRequiredInspectorControlReachesItsProductionBinding() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);

    for (const auto id : ui_design::requiredInspectorControls()) {
        const std::size_t previousCount = actions.activationCount;
        expectTrue(bindings.activate({id, 0}),
                   "required inspector control must activate");
        expectTrue(actions.lastWidget == ui_design::WidgetId{id, 0},
                   "activation must reach its intended stable action");
        expectTrue(actions.activationCount == previousCount + 1,
                   "one control event must invoke exactly one action");
    }
}

void testRepeatedRowsPreserveWidgetInstance() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);

    const ui_design::WidgetId modelRow{ui_design::ControlId::SelectModelFile, 7};
    expectTrue(bindings.activate(modelRow), "model row binding must activate");
    expectTrue(actions.lastWidget == modelRow,
               "model row binding must preserve its instance index");

    const ui_design::WidgetId materialRow{ui_design::ControlId::SelectMaterial, 11};
    expectTrue(bindings.activate(materialRow), "material row binding must activate");
    expectTrue(actions.lastWidget == materialRow,
               "material row binding must preserve its instance index");
}

void testValueEventPreservesStableWidgetAndValue() {
    RecordingActionHandler actions;
    const auto bindings = ui_action_wiring::makeInspectorBindings(actions);
    const ui_design::WidgetId slider{ui_design::ControlId::EditSizeX, 0};

    expectTrue(bindings.change(slider, 2.75),
               "required value binding must activate");
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
    testEveryRequiredInspectorControlReachesItsProductionBinding();
    testRepeatedRowsPreserveWidgetInstance();
    testValueEventPreservesStableWidgetAndValue();
    testUnknownBindingIsRejectedWithoutInvokingHandler();
    testInspectorClipRejectsHiddenPointerArea();
    testRetainedInspectorContentExtendsScrollRange();
    return 0;
}
