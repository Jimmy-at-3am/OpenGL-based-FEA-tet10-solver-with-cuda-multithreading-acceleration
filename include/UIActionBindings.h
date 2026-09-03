#pragma once

#include "UIDesign.h"

#include <algorithm>
#include <optional>

namespace ui_action_wiring {

struct InspectorEvent {
    ui_design::WidgetId widget;
    std::optional<double> value;
};

template <typename ActionHandler>
class InspectorBindings {
public:
    explicit InspectorBindings(ActionHandler& handler) : handler_(handler) {}

    bool activate(ui_design::WidgetId widget) const {
        return dispatch({widget, std::nullopt});
    }

    bool change(ui_design::WidgetId widget, double value) const {
        return dispatch({widget, value});
    }

private:
    bool dispatch(const InspectorEvent& event) const {
        const auto& required = ui_design::requiredInspectorControls();
        if (std::find(required.begin(), required.end(), event.widget.control) == required.end()) {
            return false;
        }
        handler_.activate(event);
        return true;
    }

    ActionHandler& handler_;
};

template <typename ActionHandler>
InspectorBindings<ActionHandler> makeInspectorBindings(ActionHandler& handler) {
    return InspectorBindings<ActionHandler>(handler);
}

}  // namespace ui_action_wiring
