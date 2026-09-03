#include "UIInteraction.h"

#include <algorithm>
#include <cmath>

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

std::optional<ui_design::WidgetId> nextWidgetFocus(
    const std::vector<ui_design::WidgetId>& visible,
    std::optional<ui_design::WidgetId> current,
    int direction) {
    if (visible.empty()) {
        return std::nullopt;
    }

    const bool backwards = direction < 0;
    const auto first = visible.begin();
    const auto last = visible.end() - 1;
    const auto currentIt = current
        ? std::find(first, visible.end(), *current)
        : visible.end();
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

bool queueKeyIntent(std::optional<KeyIntent>& pending, KeyIntent candidate) {
    if (candidate == KeyIntent::None || pending.has_value()) {
        return false;
    }
    pending = candidate;
    return true;
}

void appendVisibleFocus(
    std::vector<ui_design::WidgetId>& visible,
    ui_design::WidgetId widget,
    const ui_design::Rect& bounds,
    const ui_design::Rect& visibleBounds) {
    const float right = std::min(bounds.x + bounds.w,
                                 visibleBounds.x + visibleBounds.w);
    const float bottom = std::min(bounds.y + bounds.h,
                                  visibleBounds.y + visibleBounds.h);
    const bool intersects = right > std::max(bounds.x, visibleBounds.x) &&
                            bottom > std::max(bounds.y, visibleBounds.y);
    if (!intersects || std::find(visible.begin(), visible.end(), widget) != visible.end()) {
        return;
    }
    visible.push_back(widget);
}

bool allowsKeyboardMutation(KeyIntent intent, bool disabled, bool inputLocked) {
    if (disabled || inputLocked) {
        return false;
    }
    return intent == KeyIntent::Activate || intent == KeyIntent::Decrease ||
           intent == KeyIntent::Increase;
}

float adjustSlider(float value, float min, float max,
                   bool exponential, int direction) {
    if (!(max > min) || direction == 0) {
        return std::clamp(value, min, max);
    }

    const float stepDirection = direction < 0 ? -1.0f : 1.0f;
    if (exponential && min > 0.0f && max > 0.0f) {
        const float clamped = std::clamp(value, min, max);
        const float logMin = std::log(min);
        const float logMax = std::log(max);
        const float logValue = std::log(clamped) +
                               stepDirection * 0.01f * (logMax - logMin);
        return std::clamp(std::exp(logValue), min, max);
    }
    return std::clamp(value + stepDirection * 0.01f * (max - min), min, max);
}

int adjustSegmentIndex(int current, int count, int direction) {
    if (count <= 0) {
        return 0;
    }
    const int delta = direction < 0 ? -1 : direction > 0 ? 1 : 0;
    return std::clamp(current + delta, 0, count - 1);
}

EscapeAction resolveEscape(bool jobRunning, bool cancellable, bool helpOpen) {
    if (jobRunning && cancellable) {
        return EscapeAction::CancelJob;
    }
    return helpOpen ? EscapeAction::CloseHelp : EscapeAction::None;
}

float effectiveContentScale(float xScale, float yScale) {
    const float scale = std::max(xScale, yScale);
    return scale > 0.0f ? scale : 1.0f;
}

bool contentScaleChanged(float previous, float current) {
    return std::abs(previous - current) > 0.001f;
}

MotionDurations motionDurations(bool reducedMotion) {
    return reducedMotion ? MotionDurations{0, 0} : MotionDurations{160, 220};
}

FocusRingPresentation focusRingPresentation(
    const ui_design::Rect& targetBounds, float targetRadius) {
    constexpr float thickness = 3.0f;
    return {
        {targetBounds.x - thickness, targetBounds.y - thickness,
         targetBounds.w + 2.0f * thickness,
         targetBounds.h + 2.0f * thickness},
        targetBounds,
        std::max(0.0f, targetRadius) + thickness,
        std::max(0.0f, targetRadius),
        thickness,
        ui_design::ColorToken::SystemBlue,
        0.24f,
    };
}

void DiscreteSliderAccumulator::synchronize(int selection, int count) {
    if (count <= 0) {
        position = 0.0f;
        selected = 0;
        return;
    }
    selected = std::clamp(selection, 0, count - 1);
    position = static_cast<float>(selected);
}

int DiscreteSliderAccumulator::commit(
    float continuousPosition, int count, SliderChangeSource source) {
    if (count <= 0) {
        synchronize(0, 0);
        return selected;
    }
    const float maximum = static_cast<float>(count - 1);
    position = std::isfinite(continuousPosition)
        ? std::clamp(continuousPosition, 0.0f, maximum)
        : std::clamp(position, 0.0f, maximum);
    selected = std::clamp(
        static_cast<int>(std::floor(position + 0.5f)), 0, count - 1);
    if (source != SliderChangeSource::Keyboard) {
        position = static_cast<float>(selected);
    }
    return selected;
}

}  // namespace ui_interaction
