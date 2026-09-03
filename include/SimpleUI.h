#pragma once
#include "UIDesign.h"
#include "UIFontRenderer.h"
#include "UIInteraction.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

class SimpleUI {
private:
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int roundedProgramID = 0;
    unsigned int roundedVAO = 0;
    unsigned int roundedVBO = 0;
    glm::mat4 projection;
    std::string activeUIID = "";
    std::optional<ui_design::WidgetId> activeWidgetID;
    std::optional<ui_design::WidgetId> focusedWidgetID;
    std::vector<ui_design::Rect> clipStack;
    UIFontRenderer fontRenderer;
    int viewportWidth = 0;
    int viewportHeight = 0;
    float contentScale = 1.0f;
    bool fontInitialized = false;
    bool inputLocked = false;   // true while a compute job runs: widgets render but ignore input
    std::vector<ui_design::WidgetId> visibleFocusOrder;
    std::optional<ui_interaction::KeyIntent> pendingKeyIntent;
    std::optional<ui_design::WidgetId> keyboardTargetWidgetID;
    bool keyIntentConsumed = false;
    bool reducedMotion = false;
    struct SegmentMotion {
        ui_design::ControlId group;
        float fromIndex;
        int targetIndex;
        double startSeconds;
    };
    std::vector<SegmentMotion> segmentMotions;

    void drawStrokeText(std::string_view text, float x, float y, float size,
                        const glm::vec4& color);
    void applyClip();
    bool pointerInsideActiveClip(float x, float y) const;
    void registerFocusable(ui_design::WidgetId id, const ui_design::Rect& rect);
    bool keyboardTriggers(ui_design::WidgetId id,
                          ui_interaction::KeyIntent intent,
                          bool disabled);
    void drawRoundedOutline(
        const ui_interaction::FocusRingPresentation& presentation);
    void drawFocusRing(ui_design::WidgetId id, const ui_design::Rect& rect,
                       float radius);
public:
    unsigned int programID = 0;

    void init(int width, int height);
    void init(int width, int height, float contentScale);
    void resize(int width, int height);
    void resize(int width, int height, float contentScale);
    void beginInteractionFrame(std::optional<ui_interaction::KeyIntent> intent);
    void endInteractionFrame();
    std::optional<ui_design::WidgetId> focusedWidget() const {
        return focusedWidgetID;
    }
    void clearFocus() { focusedWidgetID.reset(); }
    void setReducedMotion(bool reduced) { reducedMotion = reduced; }
    // Blocks clicks/drags for widgets drawn while locked (compute in progress).
    void setInputLocked(bool locked) { inputLocked = locked; }
    void drawRect(float x, float y, float w, float h, glm::vec3 color);
    // Alpha-blended rect (dim overlays); GL_BLEND must be enabled by the caller's frame setup.
    void drawRectA(float x, float y, float w, float h, glm::vec3 color, float alpha);
    void drawLine(float x1, float y1, float x2, float y2, glm::vec3 color, float thickness = 1.0f);
    void drawText(std::string text, float x, float y, float size, glm::vec3 color);
    void drawRoundedRect(const ui_design::Rect& rect, float radius,
                         const glm::vec4& color);
    void drawShadow(const ui_design::Rect& rect, float radius, float opacity);
    void drawText(std::string_view text, float x, float baselineY, float pixelSize,
                  const glm::vec4& color, ui_design::FontRole role);
    bool button(std::string label, float x, float y, float w, float h, bool active = false, bool disabled = false);
    bool button(ui_design::ControlId id, std::string_view label,
                const ui_design::Rect& rect, ui_design::ControlRole role,
                bool selected = false, bool disabled = false);
    bool button(ui_design::WidgetId id, std::string_view label,
                const ui_design::Rect& rect, ui_design::ControlRole role,
                bool selected = false, bool disabled = false);
    bool segmentedControl(const std::vector<ui_design::WidgetId>& ids,
                          const ui_design::Rect& rect,
                          const std::vector<std::string>& labels, int& selectedIndex,
                          bool disabled = false);
    bool toggle(ui_design::ControlId id, std::string_view label,
                const ui_design::Rect& rect, bool& value, bool disabled = false);
    bool sliderField(ui_design::ControlId id, std::string_view label,
                     float& value, float min, float max,
                     const ui_design::Rect& rect,
                     const ui_design::FormattedValue& display,
                     bool exponential = false, bool disabled = false,
                     ui_interaction::SliderChangeSource* changeSource = nullptr);
    bool slider(std::string label, float& value, float min, float max, float x, float y, float w, float h, bool exponential = false);
    // Vertical slider: value = min at the BOTTOM of the track, max at the TOP.
    bool vslider(ui_design::ControlId id, float& value, float min, float max,
                 const ui_design::Rect& rect, bool disabled = false);
    void pushClip(const ui_design::Rect& rect);
    void popClip();
    glm::vec4 themeColor(ui_design::ColorToken token, float opacity = 1.0f) const;
    void shutdown();
};
