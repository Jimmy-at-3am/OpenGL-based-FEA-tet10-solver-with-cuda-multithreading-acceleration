#pragma once
#include <string>
#include <glm/glm.hpp>

class SimpleUI {
private:
    unsigned int VAO, VBO;
    glm::mat4 projection;
    std::string activeUIID = "";
public:
    unsigned int programID;

    void init(int width, int height);
    void resize(int width, int height);
    void drawRect(float x, float y, float w, float h, glm::vec3 color);
    void drawLine(float x1, float y1, float x2, float y2, glm::vec3 color, float thickness = 1.0f);
    void drawText(std::string text, float x, float y, float size, glm::vec3 color);
    bool button(std::string label, float x, float y, float w, float h, bool active = false, bool disabled = false);
    bool slider(std::string label, float& value, float min, float max, float x, float y, float w, float h, bool exponential = false);
};
