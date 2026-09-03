#pragma once

#include "UIDesign.h"

#include <array>
#include <optional>
#include <vector>

namespace ui_interaction {

enum class Key { Tab, Enter, Space, Left, Right, Up, Down, Escape, Other };
enum class KeyIntent { None, FocusNext, FocusPrevious, Activate, Decrease, Increase, Cancel };
enum class EscapeAction { None, CancelJob, CloseHelp };

struct MotionDurations {
    int selectionMs;
    int progressMs;
};

struct InspectorState {
    ui_design::InspectorTab activeTab = ui_design::InspectorTab::Model;
    std::array<float, 3> scrollOffset{0.0f, 0.0f, 0.0f};
    std::optional<ui_design::ControlId> focused;
};

bool ownsPoint(const ui_design::WindowLayout& layout, float x, float y);
void selectTab(InspectorState& state, ui_design::InspectorTab tab);
float applyScroll(float current, float wheelDelta,
                  float contentHeight, float viewportHeight);
std::optional<ui_design::ControlId> nextFocus(
    const std::vector<ui_design::ControlId>& visible,
    std::optional<ui_design::ControlId> current,
    int direction);
std::optional<ui_design::WidgetId> nextWidgetFocus(
    const std::vector<ui_design::WidgetId>& visible,
    std::optional<ui_design::WidgetId> current,
    int direction);
KeyIntent translateKey(Key key, bool pressed, bool shift);
bool queueKeyIntent(std::optional<KeyIntent>& pending, KeyIntent candidate);
void appendVisibleFocus(
    std::vector<ui_design::WidgetId>& visible,
    ui_design::WidgetId widget,
    const ui_design::Rect& bounds,
    const ui_design::Rect& visibleBounds);
bool allowsKeyboardMutation(KeyIntent intent, bool disabled, bool inputLocked);
float adjustSlider(float value, float min, float max,
                   bool exponential, int direction);
int adjustSegmentIndex(int current, int count, int direction);
EscapeAction resolveEscape(bool jobRunning, bool cancellable, bool helpOpen);
float effectiveContentScale(float xScale, float yScale);
bool contentScaleChanged(float previous, float current);
MotionDurations motionDurations(bool reducedMotion);

}  // namespace ui_interaction
