#pragma once

#include "UIDesign.h"

#include <array>
#include <optional>
#include <vector>

namespace ui_interaction {

enum class Key { Tab, Enter, Space, Left, Right, Up, Down, Escape, Other };
enum class KeyIntent { None, FocusNext, FocusPrevious, Activate, Decrease, Increase, Cancel };

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
KeyIntent translateKey(Key key, bool pressed, bool shift);

}  // namespace ui_interaction
