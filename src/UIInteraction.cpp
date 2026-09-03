#include "UIInteraction.h"

#include <algorithm>

namespace ui_interaction {

bool ownsPoint(const ui_design::WindowLayout& layout, float x, float y) {
    const auto& inspector = layout.inspector;
    return x >= inspector.x && x < inspector.x + inspector.w &&
           y >= inspector.y && y < inspector.y + inspector.h;
}

void selectTab(InspectorState& state, ui_design::InspectorTab tab) {
    if (state.activeTab != tab) {
        state.activeTab = tab;
        state.focused.reset();
    }
}

float applyScroll(float current, float wheelDelta,
                  float contentHeight, float viewportHeight) {
    constexpr float scrollStep = 36.0f;
    const float maximum = std::max(0.0f, contentHeight - viewportHeight);
    return std::clamp(current - wheelDelta * scrollStep, 0.0f, maximum);
}

std::optional<ui_design::ControlId> nextFocus(
    const std::vector<ui_design::ControlId>& visible,
    std::optional<ui_design::ControlId> current,
    int direction) {
    if (visible.empty()) {
        return std::nullopt;
    }

    const bool backwards = direction < 0;
    const auto first = visible.begin();
    const auto last = visible.end() - 1;
    const auto currentIt = current ? std::find(first, visible.end(), *current) : visible.end();
    if (currentIt == visible.end()) {
        return backwards ? *last : *first;
    }
    if (backwards) {
        return currentIt == first ? *last : *(currentIt - 1);
    }
    return currentIt == last ? *first : *(currentIt + 1);
}

KeyIntent translateKey(Key key, bool pressed, bool shift) {
    if (!pressed) {
        return KeyIntent::None;
    }

    switch (key) {
    case Key::Tab:
        return shift ? KeyIntent::FocusPrevious : KeyIntent::FocusNext;
    case Key::Enter:
    case Key::Space:
        return KeyIntent::Activate;
    case Key::Left:
    case Key::Down:
        return KeyIntent::Decrease;
    case Key::Right:
    case Key::Up:
        return KeyIntent::Increase;
    case Key::Escape:
        return KeyIntent::Cancel;
    case Key::Other:
        return KeyIntent::None;
    }
    return KeyIntent::None;
}

}  // namespace ui_interaction
