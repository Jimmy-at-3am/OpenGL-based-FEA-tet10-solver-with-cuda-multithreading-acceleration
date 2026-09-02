#include "SimpleUI.h"
#include <cmath>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include "ShaderSources.h"
#include "Globals.h"
#include "BuiltInShader.h"

void SimpleUI::init(int width, int height) {
    BuiltInShader uiShader(uiVertexShaderSource, uiFragmentShaderSource);
    programID = uiShader.ID;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0); glBindVertexArray(0);
    resize(width, height);
}

void SimpleUI::resize(int width, int height) {
    projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f);
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
    glUseProgram(programID);
    glUniformMatrix4fv(glGetUniformLocation(programID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(programID, "color"), 1, &color[0]);
    glUniform1f(glGetUniformLocation(programID, "uiAlpha"), 1.0f);

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
        c = toupper(c);
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

// Vertical slider: track runs from y (top, value = max) down to y+h (bottom,
// value = min). Fill grows upward from the bottom; a bright thumb bar marks
// the current level. Same activeUIID capture pattern as the horizontal slider
// so a drag that leaves the track keeps control until the button is released.
bool SimpleUI::vslider(std::string id, float& value, float min, float max, float x, float y, float w, float h) {
    bool changed = false;
    if (!mousePressed) activeUIID = "";
    // A few px of horizontal padding makes the thin track easy to grab.
    bool hovered = !inputLocked &&
                   (mouseX >= x - 6.0f && mouseX <= x + w + 6.0f &&
                    mouseY >= y && mouseY <= y + h);
    if (hovered && mousePressed && activeUIID == "") activeUIID = id;
    if (activeUIID == id && mousePressed) {
        float t = (y + h - mouseY) / h;          // 0 at bottom, 1 at top
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        value = min + t * (max - min);
        changed = true;
    }
    drawRect(x, y, w, h, glm::vec3(0.16f, 0.17f, 0.19f));
    drawRect(x + 1, y + 1, w - 2, h - 2, glm::vec3(0.22f, 0.23f, 0.26f));
    float t = (max > min) ? (value - min) / (max - min) : 0.0f;
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    glm::vec3 fillColor = (activeUIID == id) ? glm::vec3(0.5f, 0.8f, 1.0f) : glm::vec3(0.3f, 0.7f, 0.9f);
    if (t > 0.0f) drawRect(x + 2, y + h * (1.0f - t), w - 4, h * t - 2.0f > 0.0f ? h * t - 2.0f : h * t, fillColor);
    // Thumb bar
    drawRect(x - 4, y + h * (1.0f - t) - 2.0f, w + 8, 4.0f, glm::vec3(0.9f, 0.95f, 1.0f));
    return changed;
}
