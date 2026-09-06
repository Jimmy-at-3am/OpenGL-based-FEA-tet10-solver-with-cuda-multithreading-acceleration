#pragma once

#include "UIDesign.h"

#include <memory>
#include <string_view>

#include <glm/glm.hpp>

class UIFontRenderer {
public:
    UIFontRenderer();
    ~UIFontRenderer();
    UIFontRenderer(const UIFontRenderer&) = delete;
    UIFontRenderer& operator=(const UIFontRenderer&) = delete;

    bool initialize(int viewportWidth, int viewportHeight, float contentScale = 1.0f);
    void resize(int viewportWidth, int viewportHeight, float contentScale = 1.0f);
    bool ready(ui_design::FontRole role) const;
    float measure(std::string_view text, float pixelSize, ui_design::FontRole role) const;
    void draw(std::string_view text, float x, float baselineY, float pixelSize,
              const glm::vec4& color, ui_design::FontRole role);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
