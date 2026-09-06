#include "SimpleUI.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include "ShaderSources.h"
#include "Globals.h"
#include "BuiltInShader.h"

namespace {

double monotonicSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

}  // namespace

void SimpleUI::init(int width, int height) {
    init(width, height, 1.0f);
}

void SimpleUI::init(int width, int height, float scale) {
    BuiltInShader uiShader(uiVertexShaderSource, uiFragmentShaderSource);
    programID = uiShader.ID;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0); glBindVertexArray(0);

    BuiltInShader roundedShader(roundedRectVertexShaderSource, roundedRectFragmentShaderSource);
    roundedProgramID = roundedShader.ID;
    glGenVertexArrays(1, &roundedVAO); glGenBuffers(1, &roundedVBO);
    glBindVertexArray(roundedVAO); glBindBuffer(GL_ARRAY_BUFFER, roundedVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1); glBindVertexArray(0);

    viewportWidth = width;
    viewportHeight = height;
    contentScale = scale > 0.0f ? scale : 1.0f;
    projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f);
    fontRenderer.initialize(width, height, contentScale);
    fontInitialized = true;
}

void SimpleUI::resize(int width, int height) {
    resize(width, height, contentScale);
}

void SimpleUI::resize(int width, int height, float scale) {
    const float nextScale = scale > 0.0f ? scale : 1.0f;
    const bool rebuildFonts = fontInitialized &&
                              ui_interaction::contentScaleChanged(contentScale, nextScale);
    viewportWidth = width;
    viewportHeight = height;
    contentScale = nextScale;
    projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f);
    if (rebuildFonts) {
        fontRenderer.initialize(width, height, contentScale);
    } else {
        fontRenderer.resize(width, height, contentScale);
    }
    if (!clipStack.empty()) {
        applyClip();
    }
}

void SimpleUI::beginInteractionFrame(
    std::optional<ui_interaction::KeyIntent> intent) {
    visibleFocusOrder.clear();
    focusableBounds.clear();
    pendingTooltip.reset();
    pendingKeyIntent = intent;
    keyboardTargetWidgetID = focusedWidgetID;
    keyIntentConsumed = false;
}

void SimpleUI::endInteractionFrame() {
    if (pendingKeyIntent == ui_interaction::KeyIntent::FocusNext) {
        focusedWidgetID = ui_interaction::nextWidgetFocus(
            visibleFocusOrder, focusedWidgetID, 1);
        keyIntentConsumed = true;
    } else if (pendingKeyIntent == ui_interaction::KeyIntent::FocusPrevious) {
        focusedWidgetID = ui_interaction::nextWidgetFocus(
            visibleFocusOrder, focusedWidgetID, -1);
        keyIntentConsumed = true;
    } else if (focusedWidgetID &&
               std::find(visibleFocusOrder.begin(), visibleFocusOrder.end(),
                         *focusedWidgetID) == visibleFocusOrder.end()) {
        focusedWidgetID.reset();
    }
    pendingKeyIntent.reset();
    keyboardTargetWidgetID.reset();
    keyIntentConsumed = false;
}

void SimpleUI::registerFocusable(
    ui_design::WidgetId id, const ui_design::Rect& rect) {
    ui_interaction::appendContextualFocus(visibleFocusOrder, id);
    const auto found = std::find_if(
        focusableBounds.begin(), focusableBounds.end(), [&](const auto& entry) {
            return entry.first == id;
        });
    if (found == focusableBounds.end()) {
        focusableBounds.push_back({id, rect});
    }
}

std::optional<ui_design::Rect> SimpleUI::focusedWidgetBounds() const {
    if (!focusedWidgetID) {
        return std::nullopt;
    }
    const auto found = std::find_if(
        focusableBounds.begin(), focusableBounds.end(), [&](const auto& entry) {
            return entry.first == *focusedWidgetID;
        });
    return found == focusableBounds.end()
        ? std::nullopt
        : std::optional<ui_design::Rect>(found->second);
}

bool SimpleUI::keyboardTriggers(
    ui_design::WidgetId id, ui_interaction::KeyIntent intent, bool disabled) {
    if (keyIntentConsumed || pendingKeyIntent != intent ||
        !keyboardTargetWidgetID || !(*keyboardTargetWidgetID == id) ||
        !ui_interaction::allowsKeyboardMutation(intent, disabled, inputLocked)) {
        return false;
    }
    keyIntentConsumed = true;
    return true;
}

void SimpleUI::drawFocusRing(
    ui_design::WidgetId id, const ui_design::Rect& rect, float radius) {
    if (!focusedWidgetID || !(*focusedWidgetID == id)) {
        return;
    }
    drawRoundedOutline(ui_interaction::focusRingPresentation(rect, radius));
}

void SimpleUI::drawRect(float x, float y, float w, float h, glm::vec3 color) {
    drawRectA(x, y, w, h, color, 1.0f);
}

void SimpleUI::drawRectA(float x, float y, float w, float h, glm::vec3 color, float alpha) {
    glUseProgram(programID);
    glUniformMatrix4fv(glGetUniformLocation(programID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(programID, "color"), 1, &color[0]);
    glUniform1f(glGetUniformLocation(programID, "uiAlpha"), alpha);
    float vertices[] = { x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h };
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6); glBindVertexArray(0);
}

void SimpleUI::drawLine(float x1, float y1, float x2, float y2, glm::vec3 color, float thickness) {
    glUseProgram(programID);
    glUniformMatrix4fv(glGetUniformLocation(programID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(programID, "color"), 1, &color[0]);
    glUniform1f(glGetUniformLocation(programID, "uiAlpha"), 1.0f);
    float vertices[] = { x1, y1, x2, y2 };
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glLineWidth(thickness);
    glDrawArrays(GL_LINES, 0, 2);
    glLineWidth(1.0f);
    glBindVertexArray(0);
}

void SimpleUI::drawText(std::string text, float x, float y, float size, glm::vec3 color) {
    size = ui_design::essentialTextPixelSize(size);
    if (fontRenderer.ready(ui_design::FontRole::Interface)) {
        fontRenderer.draw(text, x, y + size, size, glm::vec4(color, 1.0f),
                          ui_design::FontRole::Interface);
        return;
    }
    drawStrokeText(text, x, y, size, glm::vec4(color, 1.0f));
}

void SimpleUI::drawStrokeText(
    std::string_view text, float x, float y, float size, const glm::vec4& color) {
    glUseProgram(programID);
    glUniformMatrix4fv(glGetUniformLocation(programID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(programID, "color"), 1, &color[0]);
    glUniform1f(glGetUniformLocation(programID, "uiAlpha"), color.a);

    std::vector<float> lines; float cursorX = x;

    struct LineAdder {
        std::vector<float>& linesRef; float& cursorXRef; float yVal; float sizeVal;
        void operator()(float x1, float y1, float x2, float y2) {
            linesRef.push_back(cursorXRef + x1 * sizeVal); linesRef.push_back(yVal + y1 * sizeVal);
            linesRef.push_back(cursorXRef + x2 * sizeVal); linesRef.push_back(yVal + y2 * sizeVal);
        }
    };
    LineAdder addLine = { lines, cursorX, y, size };

    for (char c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (c == 'A') { addLine(0, 1, 0.5, 0); addLine(0.5, 0, 1, 1); addLine(0.2, 0.6, 0.8, 0.6); }
        else if (c == 'B') { addLine(0, 0, 0, 1); addLine(0, 0, 0.8, 0); addLine(0.8, 0, 0.8, 0.5); addLine(0.8, 0.5, 0, 0.5); addLine(0.8, 0.5, 1, 0.5); addLine(1, 0.5, 1, 1); addLine(1, 1, 0, 1); }
        else if (c == 'C') { addLine(1, 0, 0, 0); addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); }
        else if (c == 'D') { addLine(0, 0, 0, 1); addLine(0, 0, 0.8, 0); addLine(0.8, 0, 1, 0.5); addLine(1, 0.5, 0.8, 1); addLine(0.8, 1, 0, 1); }
        else if (c == 'E') { addLine(1, 0, 0, 0); addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); addLine(0, 0.5, 0.8, 0.5); }
        else if (c == 'F') { addLine(0, 0, 0, 1); addLine(0, 0, 1, 0); addLine(0, 0.5, 0.8, 0.5); }
        else if (c == 'G') { addLine(1, 0, 0, 0); addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); addLine(1, 1, 1, 0.5); addLine(1, 0.5, 0.5, 0.5); }
        else if (c == 'H') { addLine(0, 0, 0, 1); addLine(1, 0, 1, 1); addLine(0, 0.5, 1, 0.5); }
        else if (c == 'I') { addLine(0.5, 0, 0.5, 1); addLine(0, 0, 1, 0); addLine(0, 1, 1, 1); }
        else if (c == 'J') { addLine(1, 0, 1, 1); addLine(1, 1, 0.5, 1); addLine(0.5, 1, 0, 0.8); }
        else if (c == 'K') { addLine(0, 0, 0, 1); addLine(1, 0, 0, 0.5); addLine(0, 0.5, 1, 1); }
        else if (c == 'L') { addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); }
        else if (c == 'M') { addLine(0, 1, 0, 0); addLine(0, 0, 0.5, 0.5); addLine(0.5, 0.5, 1, 0); addLine(1, 0, 1, 1); }
        else if (c == 'N') { addLine(0, 1, 0, 0); addLine(0, 0, 1, 1); addLine(1, 1, 1, 0); }
        else if (c == 'O') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); addLine(0, 1, 0, 0); }
        else if (c == 'P') { addLine(0, 1, 0, 0); addLine(0, 0, 1, 0); addLine(1, 0, 1, 0.5); addLine(1, 0.5, 0, 0.5); }
        else if (c == 'Q') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); addLine(0, 1, 0, 0); addLine(0.5, 0.5, 1, 1); }
        else if (c == 'R') { addLine(0, 1, 0, 0); addLine(0, 0, 1, 0); addLine(1, 0, 1, 0.5); addLine(1, 0.5, 0, 0.5); addLine(0.5, 0.5, 1, 1); }
        else if (c == 'S') { addLine(1, 0, 0, 0); addLine(0, 0, 0, 0.5); addLine(0, 0.5, 1, 0.5); addLine(1, 0.5, 1, 1); addLine(1, 1, 0, 1); }
        else if (c == 'T') { addLine(0.5, 0, 0.5, 1); addLine(0, 0, 1, 0); }
        else if (c == 'U') { addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); addLine(1, 1, 1, 0); }
        else if (c == 'V') { addLine(0, 0, 0.5, 1); addLine(0.5, 1, 1, 0); }
        else if (c == 'W') { addLine(0, 0, 0.2, 1); addLine(0.2, 1, 0.5, 0.5); addLine(0.5, 0.5, 0.8, 1); addLine(0.8, 1, 1, 0); }
        else if (c == 'X') { addLine(0, 0, 1, 1); addLine(1, 0, 0, 1); }
        else if (c == 'Y') { addLine(0, 0, 0.5, 0.5); addLine(1, 0, 0.5, 0.5); addLine(0.5, 0.5, 0.5, 1); }
        else if (c == 'Z') { addLine(0, 0, 1, 0); addLine(1, 0, 0, 1); addLine(0, 1, 1, 1); }
        else if (c == '0') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); addLine(0, 1, 0, 0); addLine(0, 1, 1, 0); }
        else if (c == '1') { addLine(0.5, 0, 0.5, 1); addLine(0.3, 0.2, 0.5, 0); }
        else if (c == '2') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 0.5); addLine(1, 0.5, 0, 1); addLine(0, 1, 1, 1); }
        else if (c == '3') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); addLine(0, 0.5, 1, 0.5); }
        else if (c == '4') { addLine(0, 0, 0, 0.5); addLine(0, 0.5, 1, 0.5); addLine(1, 0, 1, 1); }
        else if (c == '5') { addLine(1, 0, 0, 0); addLine(0, 0, 0, 0.5); addLine(0, 0.5, 1, 0.5); addLine(1, 0.5, 1, 1); addLine(1, 1, 0, 1); }
        else if (c == '6') { addLine(0, 0, 1, 0); addLine(0, 0, 0, 1); addLine(0, 1, 1, 1); addLine(1, 1, 1, 0.5); addLine(1, 0.5, 0, 0.5); }
        else if (c == '7') { addLine(0, 0, 1, 0); addLine(1, 0, 0.5, 1); }
        else if (c == '8') { addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); addLine(0, 1, 0, 0); addLine(0, 0.5, 1, 0.5); }
        else if (c == '9') { addLine(1, 1, 0, 1); addLine(1, 1, 1, 0); addLine(1, 0, 0, 0); addLine(0, 0, 0, 0.5); addLine(0, 0.5, 1, 0.5); }
        else if (c == '.') { addLine(0.4, 0.9, 0.6, 0.9); addLine(0.6, 0.9, 0.6, 1); addLine(0.6, 1, 0.4, 1); addLine(0.4, 1, 0.4, 0.9); }
        else if (c == ':') { addLine(0.4, 0.3, 0.6, 0.3); addLine(0.4, 0.7, 0.6, 0.7); }
        else if (c == '-') { addLine(0.1, 0.5, 0.9, 0.5); }
        else if (c == '_') { addLine(0.0, 1.0, 1.0, 1.0); }
        // Added for the solver status overlay, which reports percentages,
        // "ITER 3/14" ratios and a ">" active-stage marker.
        else if (c == '/') { addLine(0.1, 1.0, 0.9, 0.0); }
        else if (c == '%') { addLine(0.1, 1.0, 0.9, 0.0); addLine(0.1, 0.1, 0.35, 0.1); addLine(0.35, 0.1, 0.35, 0.35); addLine(0.35, 0.35, 0.1, 0.35); addLine(0.1, 0.35, 0.1, 0.1); addLine(0.65, 0.65, 0.9, 0.65); addLine(0.9, 0.65, 0.9, 0.9); addLine(0.9, 0.9, 0.65, 0.9); addLine(0.65, 0.9, 0.65, 0.65); }
        else if (c == '>') { addLine(0.2, 0.15, 0.75, 0.5); addLine(0.75, 0.5, 0.2, 0.85); }
        cursorX += size * 1.2f;
    }

    if (lines.empty()) return;
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);
    glLineWidth(2.0f); glDrawArrays(GL_LINES, 0, lines.size() / 2); glLineWidth(1.0f);
    glBindVertexArray(0);
}

void SimpleUI::drawRoundedRect(
    const ui_design::Rect& rect, float radius, const glm::vec4& color) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || color.a <= 0.0f) {
        return;
    }
    const float halfWidth = rect.w * 0.5f;
    const float halfHeight = rect.h * 0.5f;
    const float clampedRadius = std::clamp(radius, 0.0f, std::min(halfWidth, halfHeight));
    const float vertices[] = {
        rect.x,          rect.y,          -halfWidth, -halfHeight,
        rect.x + rect.w, rect.y,           halfWidth, -halfHeight,
        rect.x,          rect.y + rect.h, -halfWidth,  halfHeight,
        rect.x + rect.w, rect.y,           halfWidth, -halfHeight,
        rect.x + rect.w, rect.y + rect.h,  halfWidth,  halfHeight,
        rect.x,          rect.y + rect.h, -halfWidth,  halfHeight,
    };

    glUseProgram(roundedProgramID);
    glUniformMatrix4fv(glGetUniformLocation(roundedProgramID, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniform2f(glGetUniformLocation(roundedProgramID, "halfSize"), halfWidth, halfHeight);
    glUniform1f(glGetUniformLocation(roundedProgramID, "radius"), clampedRadius);
    glUniform4fv(glGetUniformLocation(roundedProgramID, "color"), 1, &color[0]);
    glUniform1i(glGetUniformLocation(roundedProgramID, "outlineOnly"), GL_FALSE);
    glBindVertexArray(roundedVAO); glBindBuffer(GL_ARRAY_BUFFER, roundedVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6); glBindVertexArray(0);
}

void SimpleUI::drawRoundedOutline(
    const ui_interaction::FocusRingPresentation& presentation) {
    const auto& outer = presentation.outerBounds;
    const auto& inner = presentation.innerBounds;
    if (outer.w <= 0.0f || outer.h <= 0.0f || presentation.opacity <= 0.0f) {
        return;
    }
    const float outerHalfWidth = outer.w * 0.5f;
    const float outerHalfHeight = outer.h * 0.5f;
    const float innerHalfWidth = std::max(0.0f, inner.w * 0.5f);
    const float innerHalfHeight = std::max(0.0f, inner.h * 0.5f);
    const float outerRadius = std::clamp(
        presentation.outerRadius, 0.0f,
        std::min(outerHalfWidth, outerHalfHeight));
    const float innerRadius = std::clamp(
        presentation.innerRadius, 0.0f,
        std::min(innerHalfWidth, innerHalfHeight));
    const float vertices[] = {
        outer.x,           outer.y,           -outerHalfWidth, -outerHalfHeight,
        outer.x + outer.w, outer.y,            outerHalfWidth, -outerHalfHeight,
        outer.x,           outer.y + outer.h, -outerHalfWidth,  outerHalfHeight,
        outer.x + outer.w, outer.y,            outerHalfWidth, -outerHalfHeight,
        outer.x + outer.w, outer.y + outer.h,  outerHalfWidth,  outerHalfHeight,
        outer.x,           outer.y + outer.h, -outerHalfWidth,  outerHalfHeight,
    };
    const glm::vec4 color = themeColor(
        presentation.color, presentation.opacity);

    glUseProgram(roundedProgramID);
    glUniformMatrix4fv(glGetUniformLocation(roundedProgramID, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniform2f(glGetUniformLocation(roundedProgramID, "halfSize"),
                outerHalfWidth, outerHalfHeight);
    glUniform1f(glGetUniformLocation(roundedProgramID, "radius"), outerRadius);
    glUniform4fv(glGetUniformLocation(roundedProgramID, "color"), 1, &color[0]);
    glUniform1i(glGetUniformLocation(roundedProgramID, "outlineOnly"), GL_TRUE);
    glUniform2f(glGetUniformLocation(roundedProgramID, "innerHalfSize"),
                innerHalfWidth, innerHalfHeight);
    glUniform1f(glGetUniformLocation(roundedProgramID, "innerRadius"), innerRadius);
    glBindVertexArray(roundedVAO);
    glBindBuffer(GL_ARRAY_BUFFER, roundedVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void SimpleUI::drawShadow(const ui_design::Rect& rect, float radius, float opacity) {
    const float alpha = std::clamp(opacity, 0.0f, 1.0f);
    drawRoundedRect(
        {rect.x - 3.0f, rect.y + 5.0f, rect.w + 6.0f, rect.h + 4.0f},
        radius + 3.0f, themeColor(ui_design::ColorToken::PrimaryInk, alpha * 0.08f));
    drawRoundedRect(
        {rect.x - 1.0f, rect.y + 2.0f, rect.w + 2.0f, rect.h + 1.0f},
        radius + 1.0f, themeColor(ui_design::ColorToken::PrimaryInk, alpha * 0.12f));
}

void SimpleUI::drawText(
    std::string_view text, float x, float baselineY, float pixelSize,
    const glm::vec4& color, ui_design::FontRole role) {
    pixelSize = ui_design::essentialTextPixelSize(pixelSize);
    const auto policy = ui_design::resolveTextDrawPolicy(
        fontRenderer.ready(role), role, {color.r, color.g, color.b, color.a});
    const glm::vec4 resolvedColor{
        policy.color.r, policy.color.g, policy.color.b, policy.color.a};
    if (policy.backend == ui_design::TextBackend::FontAtlas) {
        fontRenderer.draw(text, x, baselineY, pixelSize, resolvedColor, policy.role);
        return;
    }
    drawStrokeText(text, x, baselineY - pixelSize, pixelSize, resolvedColor);
}

bool SimpleUI::button(
    ui_design::ControlId id, std::string_view label, const ui_design::Rect& rect,
    ui_design::ControlRole role, bool selected, bool disabled,
    float rightAccessoryWidth) {
    return button(ui_design::WidgetId{id, 0}, label, rect, role, selected, disabled,
                  rightAccessoryWidth);
}

bool SimpleUI::button(
    ui_design::WidgetId id, std::string_view label, const ui_design::Rect& rect,
    ui_design::ControlRole role, bool selected, bool disabled,
    float rightAccessoryWidth) {
    registerFocusable(id, rect);
    disabled = disabled || inputLocked;
    if (!mousePressed) {
        activeWidgetID.reset();
    }
    const bool hovered = ui_design::containsPoint(rect, mouseX, mouseY) &&
                         pointerInsideActiveClip(mouseX, mouseY);
    bool clicked = false;
    if (!disabled && hovered && mousePressed && !prevMousePressed) {
        activeWidgetID = id;
        focusedWidgetID = id;
        clicked = true;
    } else if (!disabled && mouseClickLatch &&
               ui_design::containsPoint(rect, mouseClickLatchX, mouseClickLatchY) &&
               pointerInsideActiveClip(mouseClickLatchX, mouseClickLatchY)) {
        focusedWidgetID = id;
        clicked = true;
    }
    if (keyboardTriggers(id, ui_interaction::KeyIntent::Activate, disabled)) {
        clicked = true;
    }
    if (clicked) {
        mouseClickLatch = false;
    }

    const bool pressed = activeWidgetID && *activeWidgetID == id && mousePressed;
    const ui_design::ControlState state = disabled
        ? ui_design::ControlState::Disabled
        : (selected ? ui_design::ControlState::Selected
                    : (pressed ? ui_design::ControlState::Pressed
                               : (hovered ? ui_design::ControlState::Hover
                                          : ui_design::ControlState::Rest)));
    const auto visual = ui_design::resolveControlVisual(role, state);
    drawFocusRing(id, rect, 8.0f);
    drawRoundedRect(rect, 8.0f, themeColor(visual.fill, visual.fillOpacity));

    const float pixelSize = std::clamp(rect.h * 0.38f, 12.0f, 16.0f);
    float textWidth = fontRenderer.measure(label, pixelSize, ui_design::FontRole::Interface);
    if (textWidth <= 0.0f) {
        textWidth = static_cast<float>(label.size()) * pixelSize * 1.2f;
    }
    const float labelAreaWidth = std::max(0.0f, rect.w - rightAccessoryWidth);
    drawText(label, rect.x + std::max(8.0f, (labelAreaWidth - textWidth) * 0.5f),
             rect.y + (rect.h + pixelSize * 0.7f) * 0.5f, pixelSize,
             themeColor(visual.text, visual.contentOpacity), ui_design::FontRole::Interface);
    return clicked;
}

bool SimpleUI::segmentedControl(
    const std::vector<ui_design::WidgetId>& ids, const ui_design::Rect& rect,
    const std::vector<std::string>& labels, int& selectedIndex, bool disabled,
    const std::vector<bool>& disabledSegments) {
    const std::size_t count = std::min(ids.size(), labels.size());
    if (count == 0) {
        return false;
    }

    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(count) - 1);
    bool changed = false;
    const bool effectiveDisabled = disabled || inputLocked;
    const auto segmentDisabled = [&](std::size_t index) {
        return effectiveDisabled ||
               (index < disabledSegments.size() && disabledSegments[index]);
    };
    const float segmentWidth = rect.w / static_cast<float>(count);
    if (!mousePressed) {
        activeWidgetID.reset();
    }
    for (std::size_t index = 0; index < count; ++index) {
        const ui_design::Rect segment{
            rect.x + segmentWidth * static_cast<float>(index), rect.y,
            segmentWidth, rect.h};
        registerFocusable(ids[index], segment);
        const bool hovered = ui_design::containsPoint(segment, mouseX, mouseY) &&
                             pointerInsideActiveClip(mouseX, mouseY);
        bool activated = false;
        if (!segmentDisabled(index) && hovered && mousePressed && !prevMousePressed) {
            activeWidgetID = ids[index];
            focusedWidgetID = ids[index];
            activated = true;
        } else if (!segmentDisabled(index) && mouseClickLatch &&
                   ui_design::containsPoint(
                       segment, mouseClickLatchX, mouseClickLatchY) &&
                   pointerInsideActiveClip(mouseClickLatchX, mouseClickLatchY)) {
            focusedWidgetID = ids[index];
            activated = true;
        }
        if (keyboardTriggers(
                ids[index], ui_interaction::KeyIntent::Activate,
                segmentDisabled(index))) {
            activated = true;
        }
        if (activated) {
            mouseClickLatch = false;
            selectedIndex = static_cast<int>(index);
            changed = true;
        }
    }
    if (!changed && !keyIntentConsumed && pendingKeyIntent &&
        keyboardTargetWidgetID &&
        (*pendingKeyIntent == ui_interaction::KeyIntent::Decrease ||
         *pendingKeyIntent == ui_interaction::KeyIntent::Increase) &&
        ui_interaction::allowsKeyboardMutation(
            *pendingKeyIntent, false, inputLocked)) {
        const auto focused = std::find(ids.begin(), ids.begin() + count,
                                       *keyboardTargetWidgetID);
        if (focused != ids.begin() + count) {
            const int focusedIndex = static_cast<int>(focused - ids.begin());
            const int direction = *pendingKeyIntent ==
                ui_interaction::KeyIntent::Decrease ? -1 : 1;
            const int next = ui_interaction::adjustSegmentIndex(
                focusedIndex, static_cast<int>(count), direction);
            focusedWidgetID = ids[static_cast<std::size_t>(next)];
            keyIntentConsumed = true;
            if (next != selectedIndex &&
                !segmentDisabled(static_cast<std::size_t>(next))) {
                selectedIndex = next;
                changed = true;
            }
        }
    }


    const double now = monotonicSeconds();
    const auto durations = ui_interaction::motionDurations(reducedMotion);
    auto motion = std::find_if(
        segmentMotions.begin(), segmentMotions.end(), [&](const SegmentMotion& candidate) {
            return candidate.group == ids.front().control;
        });
    if (motion == segmentMotions.end()) {
        segmentMotions.push_back(
            {ids.front().control, static_cast<float>(selectedIndex), selectedIndex, now});
        motion = segmentMotions.end() - 1;
    }
    const auto animatedIndex = [&](const SegmentMotion& state) {
        if (durations.selectionMs == 0) {
            return static_cast<float>(state.targetIndex);
        }
        const float elapsed = static_cast<float>(
            (now - state.startSeconds) * 1000.0 /
            static_cast<double>(durations.selectionMs));
        const float t = std::clamp(elapsed, 0.0f, 1.0f);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        return state.fromIndex +
               (static_cast<float>(state.targetIndex) - state.fromIndex) * eased;
    };
    if (motion->targetIndex != selectedIndex) {
        motion->fromIndex = animatedIndex(*motion);
        motion->targetIndex = selectedIndex;
        motion->startSeconds = now;
    }
    const float thumbIndex = animatedIndex(*motion);

    const float opacity = effectiveDisabled ? 0.38f : 1.0f;
    drawRoundedRect(rect, 10.0f,
                    themeColor(ui_design::ColorToken::PrimaryInk,
                               0.08f * opacity));
    const ui_design::Rect thumb{
        rect.x + thumbIndex * segmentWidth + 2.0f, rect.y + 2.0f,
        segmentWidth - 4.0f, rect.h - 4.0f};
    drawShadow(thumb, 8.0f, opacity);
    drawRoundedRect(thumb, 8.0f,
                    themeColor(ui_design::ColorToken::SnowSurface, opacity));

    for (std::size_t index = 0; index < count; ++index) {
        const ui_design::Rect segment{
            rect.x + segmentWidth * static_cast<float>(index), rect.y,
            segmentWidth, rect.h};
        const bool hovered = ui_design::containsPoint(segment, mouseX, mouseY) &&
                             pointerInsideActiveClip(mouseX, mouseY);
        const bool pressed = activeWidgetID && *activeWidgetID == ids[index] &&
                             mousePressed;
        drawFocusRing(ids[index], segment, 10.0f);
        if (!segmentDisabled(index) && (hovered || pressed)) {
            drawRoundedRect(
                {segment.x + 2.0f, segment.y + 2.0f,
                 segment.w - 4.0f, segment.h - 4.0f},
                8.0f,
                themeColor(ui_design::ColorToken::PrimaryInk,
                           pressed ? 0.12f : 0.06f));
        }
        const float pixelSize = std::clamp(rect.h * 0.36f, 12.0f, 15.0f);
        float textWidth = fontRenderer.measure(
            labels[index], pixelSize, ui_design::FontRole::Interface);
        if (textWidth <= 0.0f) {
            textWidth = static_cast<float>(labels[index].size()) *
                        pixelSize * 1.2f;
        }
        const auto token = selectedIndex == static_cast<int>(index)
            ? ui_design::ColorToken::PrimaryInk
            : ui_design::ColorToken::Graphite;
        const float segmentOpacity = segmentDisabled(index) ? 0.38f : opacity;
        drawText(labels[index],
                 segment.x + std::max(6.0f, (segment.w - textWidth) * 0.5f),
                 segment.y + (segment.h + pixelSize * 0.7f) * 0.5f,
                 pixelSize, themeColor(token, segmentOpacity),
                 ui_design::FontRole::Interface);
    }
    return changed;
}

bool SimpleUI::toggle(
    ui_design::ControlId control, std::string_view label, const ui_design::Rect& rect,
    bool& value, bool disabled) {
    const ui_design::WidgetId id{control, 0};
    registerFocusable(id, rect);
    disabled = disabled || inputLocked;
    if (!mousePressed) {
        activeWidgetID.reset();
    }
    const bool hovered = ui_design::containsPoint(rect, mouseX, mouseY) &&
                         pointerInsideActiveClip(mouseX, mouseY);
    bool changed = false;
    if (!disabled && hovered && mousePressed && !prevMousePressed) {
        activeWidgetID = id;
        focusedWidgetID = id;
        value = !value;
        changed = true;
    } else if (!disabled && mouseClickLatch &&
               ui_design::containsPoint(rect, mouseClickLatchX, mouseClickLatchY) &&
               pointerInsideActiveClip(mouseClickLatchX, mouseClickLatchY)) {
        focusedWidgetID = id;
        mouseClickLatch = false;
        value = !value;
        changed = true;
    }
    if (!changed && keyboardTriggers(
            id, ui_interaction::KeyIntent::Activate, disabled)) {
        value = !value;
        changed = true;
    }

    const float opacity = disabled ? 0.38f : 1.0f;
    drawFocusRing(id, rect, 10.0f);
    const float pixelSize = std::clamp(rect.h * 0.36f, 12.0f, 15.0f);
    drawText(label, rect.x, rect.y + (rect.h + pixelSize * 0.7f) * 0.5f, pixelSize,
             themeColor(ui_design::ColorToken::PrimaryInk, opacity),
             ui_design::FontRole::Interface);
    const ui_design::Rect track{rect.x + rect.w - 38.0f, rect.y + (rect.h - 22.0f) * 0.5f,
                                38.0f, 22.0f};
    drawRoundedRect(track, 11.0f,
                    value ? themeColor(ui_design::ColorToken::SystemBlue, opacity)
                          : themeColor(ui_design::ColorToken::PrimaryInk, 0.14f * opacity));
    const float knobX = value ? track.x + track.w - 20.0f : track.x + 2.0f;
    const ui_design::Rect knob{knobX, track.y + 2.0f, 18.0f, 18.0f};
    drawShadow(knob, 9.0f, opacity);
    drawRoundedRect(knob, 9.0f, themeColor(ui_design::ColorToken::SnowSurface, opacity));
    return changed;
}

bool SimpleUI::sliderField(
    ui_design::ControlId control, std::string_view label, float& value, float min,
    float max, const ui_design::Rect& rect, const ui_design::FormattedValue& display,
    bool exponential, bool disabled,
    ui_interaction::SliderChangeSource* changeSource) {
    if (changeSource) {
        *changeSource = ui_interaction::SliderChangeSource::None;
    }
    const ui_design::WidgetId id{control, 0};
    registerFocusable(id, rect);
    disabled = disabled || inputLocked;
    if (!mousePressed) {
        activeWidgetID.reset();
    }

    const auto initialGeometry = ui_design::horizontalSliderGeometry(rect, 0.0f);
    const ui_design::Rect track = initialGeometry.track;
    const bool hovered = ui_design::containsPoint(rect, mouseX, mouseY) &&
                         pointerInsideActiveClip(mouseX, mouseY);
    if (!disabled && hovered && mousePressed && !prevMousePressed) {
        activeWidgetID = id;
        focusedWidgetID = id;
    }

    bool changed = false;
    const bool active = activeWidgetID && *activeWidgetID == id && mousePressed;
    if (active && max > min) {
        const float position = std::clamp((mouseX - track.x) / track.w, 0.0f, 1.0f);
        if (exponential && min > 0.0f && max > 0.0f) {
            value = min * std::pow(max / min, position);
        } else {
            value = min + position * (max - min);
        }
        if (changeSource) {
            *changeSource = ui_interaction::SliderChangeSource::Pointer;
        }
        changed = true;
    }
    if (!changed && !keyIntentConsumed && pendingKeyIntent &&
        (*pendingKeyIntent == ui_interaction::KeyIntent::Decrease ||
         *pendingKeyIntent == ui_interaction::KeyIntent::Increase) &&
        keyboardTargetWidgetID && *keyboardTargetWidgetID == id &&
        ui_interaction::allowsKeyboardMutation(
            *pendingKeyIntent, disabled, inputLocked)) {
        const int direction = *pendingKeyIntent ==
            ui_interaction::KeyIntent::Decrease ? -1 : 1;
        value = ui_interaction::adjustSlider(
            value, min, max, exponential, direction);
        if (changeSource) {
            *changeSource = ui_interaction::SliderChangeSource::Keyboard;
        }
        keyIntentConsumed = true;
        changed = true;
    }

    float position = 0.0f;
    if (max > min) {
        if (exponential && min > 0.0f && max > 0.0f && value > 0.0f) {
            position = std::log(value / min) / std::log(max / min);
        } else {
            position = (value - min) / (max - min);
        }
    }
    position = std::clamp(position, 0.0f, 1.0f);
    const float opacity = disabled ? 0.38f : 1.0f;
    drawFocusRing(id, rect, 10.0f);
    drawText(label, rect.x, rect.y + 14.0f, 13.0f,
             themeColor(ui_design::ColorToken::Graphite, opacity),
             ui_design::FontRole::Interface);
    float numberWidth = fontRenderer.measure(
        display.number, 13.0f, ui_design::FontRole::Data);
    if (numberWidth <= 0.0f) {
        numberWidth = static_cast<float>(display.number.size()) * 13.0f * 1.2f;
    }
    constexpr float unitColumnWidth = 32.0f;
    constexpr float valueUnitGap = 6.0f;
    const auto valueLayout = ui_design::layoutFormattedValueText(
        display, rect.x + rect.w, numberWidth, unitColumnWidth, valueUnitGap);
    drawText(valueLayout.number.text, valueLayout.number.x, rect.y + 14.0f, 13.0f,
             themeColor(ui_design::ColorToken::PrimaryInk, opacity),
             valueLayout.number.role);
    if (!valueLayout.unit.text.empty()) {
        drawText(valueLayout.unit.text, valueLayout.unit.x, rect.y + 14.0f, 13.0f,
                 themeColor(ui_design::ColorToken::Graphite, opacity),
                 valueLayout.unit.role);
    }
    drawRoundedRect(track, 2.0f,
                    themeColor(ui_design::ColorToken::PrimaryInk, 0.12f * opacity));
    drawRoundedRect({track.x, track.y, track.w * position, track.h}, 2.0f,
                    themeColor(ui_design::ColorToken::SystemBlue, opacity));
    const ui_design::Rect thumb =
        ui_design::horizontalSliderGeometry(rect, position).thumb;
    drawShadow(thumb, 8.0f, opacity);
    drawRoundedRect(thumb, 8.0f,
                    themeColor(ui_design::ColorToken::SnowSurface, opacity));
    return changed;
}

bool SimpleUI::button(std::string label, float x, float y, float w, float h, bool active, bool disabled) {
    if (inputLocked) disabled = true;   // compute in progress: render-only
    bool hovered = (mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h);
    bool clicked = false;
    if (!disabled) {
        if (hovered && mousePressed && !prevMousePressed) {
            clicked = true;
        } else if (mouseClickLatch &&
                   mouseClickLatchX >= x && mouseClickLatchX <= x + w &&
                   mouseClickLatchY >= y && mouseClickLatchY <= y + h) {
            // Press+release were swallowed by one poll (slow frame during a
            // compute job) so no rising edge was ever observable -- honour the
            // latched press instead. This is what makes the progress panel's
            // cancel X reliable while every core is busy solving.
            clicked = true;
        }
        // Whichever path fired, consume the latch so a second overlapping
        // widget cannot claim the same physical click.
        if (clicked) mouseClickLatch = false;
    }

    glm::vec3 fillColor = active ? glm::vec3(0.2f, 0.5f, 0.8f) : (disabled ? glm::vec3(0.25f, 0.25f, 0.25f) : glm::vec3(0.2f, 0.2f, 0.2f));
    if (hovered && !disabled && !active) {
        fillColor = glm::vec3(0.3f, 0.3f, 0.3f);
        if (mousePressed) fillColor = glm::vec3(0.15f, 0.15f, 0.15f);
    }

    drawRect(x, y, w, h, fillColor);
    drawText(label, x + 8, y + h * 0.25f, 8.5f, disabled ? glm::vec3(0.5f) : glm::vec3(0.9f, 0.9f, 0.9f));
    return clicked;
}

bool SimpleUI::slider(std::string label, float& value, float min, float max, float x, float y, float w, float h, bool exponential) {
    bool changed = false;
    if (!mousePressed) activeUIID = "";
    bool hovered = !inputLocked && (mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h);
    if (hovered && mousePressed && activeUIID == "") activeUIID = label;
    if (activeUIID == label && mousePressed) {
        float t = (mouseX - x) / w;
        if (t < 0) t = 0; if (t > 1) t = 1;
        if (exponential)
            value = min * powf(max / min, t);
        else
            value = min + t * (max - min);
        changed = true;
    }
    drawRect(x, y, w, h, glm::vec3(0.2f, 0.2f, 0.2f));
    float t = exponential
        ? logf(value / min) / logf(max / min)
        : (value - min) / (max - min);
    glm::vec3 fillColor = (activeUIID == label) ? glm::vec3(0.5f, 0.8f, 1.0f) : glm::vec3(0.3f, 0.7f, 0.9f);
    drawRect(x, y, w * t, h, fillColor);
    
    char buffer[64];
    if (exponential)
        snprintf(buffer, sizeof(buffer), "%s: %.2e", label.c_str(), value);
    else
        snprintf(buffer, sizeof(buffer), "%s: %.3f", label.c_str(), value);
    drawText(std::string(buffer), x + 8, y + h * 0.25f, 8.5f, glm::vec3(0.9f, 0.9f, 0.9f));

    return changed;
}

// Vertical slider: the containing rect is the hit target, while the visible
// track stays at the approved 4 px and the thumb at 16 px.
bool SimpleUI::vslider(
    ui_design::ControlId control, float& value, float min, float max,
    const ui_design::Rect& rect, bool disabled) {
    const ui_design::WidgetId id{control, 0};
    registerFocusable(id, rect);
    disabled = disabled || inputLocked;
    if (!mousePressed) {
        activeWidgetID.reset();
    }

    const bool hovered = ui_design::containsPoint(rect, mouseX, mouseY) &&
                         pointerInsideActiveClip(mouseX, mouseY);
    if (!disabled && hovered && mousePressed && !prevMousePressed) {
        activeWidgetID = id;
        focusedWidgetID = id;
    }

    bool changed = false;
    const bool active = activeWidgetID && *activeWidgetID == id && mousePressed;
    if (active && max > min) {
        const float position = std::clamp(
            (rect.y + rect.h - mouseY) / rect.h, 0.0f, 1.0f);
        value = min + position * (max - min);
        changed = true;
    }
    if (!changed && !keyIntentConsumed && pendingKeyIntent &&
        (*pendingKeyIntent == ui_interaction::KeyIntent::Decrease ||
         *pendingKeyIntent == ui_interaction::KeyIntent::Increase) &&
        keyboardTargetWidgetID && *keyboardTargetWidgetID == id &&
        ui_interaction::allowsKeyboardMutation(
            *pendingKeyIntent, disabled, inputLocked)) {
        const int direction = *pendingKeyIntent ==
            ui_interaction::KeyIntent::Decrease ? -1 : 1;
        value = ui_interaction::adjustSlider(value, min, max, false, direction);
        keyIntentConsumed = true;
        changed = true;
    }

    float position = max > min ? (value - min) / (max - min) : 0.0f;
    position = std::clamp(position, 0.0f, 1.0f);
    const float opacity = disabled ? 0.38f : 1.0f;
    drawFocusRing(id, rect, 8.0f);
    const ui_design::Rect track{
        rect.x + (rect.w - 4.0f) * 0.5f, rect.y, 4.0f, rect.h};
    drawRoundedRect(track, 2.0f,
                    themeColor(ui_design::ColorToken::PrimaryInk, 0.14f * opacity));
    if (position > 0.0f) {
        drawRoundedRect(
            {track.x, track.y + track.h * (1.0f - position),
             track.w, track.h * position},
            2.0f, themeColor(ui_design::ColorToken::SystemBlue, opacity));
    }

    const ui_design::Rect thumb{
        track.x + track.w * 0.5f - 8.0f,
        track.y + track.h * (1.0f - position) - 8.0f,
        16.0f, 16.0f};
    drawShadow(thumb, 8.0f, opacity);
    drawRoundedRect(thumb, 8.0f,
                    themeColor(ui_design::ColorToken::SnowSurface, opacity));
    return changed;
}

void SimpleUI::pushClip(const ui_design::Rect& rect) {
    ui_design::Rect clipped = rect;
    if (!clipStack.empty()) {
        const auto& parent = clipStack.back();
        const float right = std::min(parent.x + parent.w, clipped.x + clipped.w);
        const float bottom = std::min(parent.y + parent.h, clipped.y + clipped.h);
        clipped.x = std::max(parent.x, clipped.x);
        clipped.y = std::max(parent.y, clipped.y);
        clipped.w = std::max(0.0f, right - clipped.x);
        clipped.h = std::max(0.0f, bottom - clipped.y);
    }
    clipStack.push_back(clipped);
    applyClip();
}

void SimpleUI::popClip() {
    if (!clipStack.empty()) {
        clipStack.pop_back();
    }
    if (clipStack.empty()) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        applyClip();
    }
}

bool SimpleUI::pointerOver(const ui_design::Rect& rect) const {
    return ui_design::containsPoint(rect, mouseX, mouseY) &&
           pointerInsideActiveClip(mouseX, mouseY);
}

void SimpleUI::queueTooltip(
    std::string_view text, const ui_design::Rect& anchor) {
    if (!text.empty()) {
        pendingTooltip = TooltipRequest{std::string(text), anchor};
    }
}

void SimpleUI::drawQueuedTooltip() {
    if (!pendingTooltip) {
        return;
    }
    const float width = std::min(360.0f, std::max(220.0f,
        static_cast<float>(viewportWidth) - 32.0f));
    const auto lines = ui_design::wrapTextToWidth(
        pendingTooltip->text, width - 20.0f, 11.0f);
    if (lines.empty()) {
        return;
    }
    const float height = 20.0f + static_cast<float>(lines.size()) * 15.0f;
    const float x = std::clamp(
        pendingTooltip->anchor.x - width - 8.0f, 8.0f,
        std::max(8.0f, static_cast<float>(viewportWidth) - width - 8.0f));
    const float y = std::clamp(
        pendingTooltip->anchor.y, 8.0f,
        std::max(8.0f, static_cast<float>(viewportHeight) - height - 8.0f));
    const ui_design::Rect surface{x, y, width, height};
    drawShadow(surface, 10.0f, 1.0f);
    drawRoundedRect(surface, 10.0f,
                    themeColor(ui_design::ColorToken::SnowSurface, 0.98f));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        drawText(lines[index], x + 10.0f,
                 y + 18.0f + static_cast<float>(index) * 15.0f,
                 11.0f, themeColor(ui_design::ColorToken::PrimaryInk),
                 ui_design::FontRole::Interface);
    }
}

void SimpleUI::applyClip() {
    if (clipStack.empty()) {
        return;
    }
    const auto& rect = clipStack.back();
    const int x = static_cast<int>(std::lround(rect.x * contentScale));
    const int y = static_cast<int>(std::lround(
        (static_cast<float>(viewportHeight) - rect.y - rect.h) * contentScale));
    const int width = static_cast<int>(std::lround(rect.w * contentScale));
    const int height = static_cast<int>(std::lround(rect.h * contentScale));
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, std::max(0, width), std::max(0, height));
}

bool SimpleUI::pointerInsideActiveClip(float x, float y) const {
    return clipStack.empty() || ui_design::containsPoint(clipStack.back(), x, y);
}

glm::vec4 SimpleUI::themeColor(ui_design::ColorToken token, float opacity) const {
    const auto color = ui_design::rgba(token, opacity);
    return {color.r, color.g, color.b, color.a};
}

void SimpleUI::shutdown() {
    clipStack.clear();
    segmentMotions.clear();
    activeWidgetID.reset();
    focusedWidgetID.reset();
    glDisable(GL_SCISSOR_TEST);
    fontRenderer.shutdown();
    fontInitialized = false;
    if (roundedVBO != 0) {
        glDeleteBuffers(1, &roundedVBO);
        roundedVBO = 0;
    }
    if (roundedVAO != 0) {
        glDeleteVertexArrays(1, &roundedVAO);
        roundedVAO = 0;
    }
    if (roundedProgramID != 0) {
        glDeleteProgram(roundedProgramID);
        roundedProgramID = 0;
    }
    if (VBO != 0) {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (VAO != 0) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    if (programID != 0) {
        glDeleteProgram(programID);
        programID = 0;
    }
}
