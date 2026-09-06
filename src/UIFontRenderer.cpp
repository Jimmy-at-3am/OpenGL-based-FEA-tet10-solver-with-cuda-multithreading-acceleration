#include "UIFontRenderer.h"

#include "ShaderSources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#if defined(HAS_FREETYPE_UI) && !defined(UI_FONT_FORCE_FALLBACK)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace {

constexpr int kAtlasPixelSize = 48;
constexpr int kFirstGlyph = 32;
constexpr int kLastGlyph = 126;

std::size_t roleIndex(ui_design::FontRole role) {
    switch (role) {
    case ui_design::FontRole::Display:
        return 0;
    case ui_design::FontRole::Interface:
        return 1;
    case ui_design::FontRole::Data:
        return 2;
    }
    return 1;
}

unsigned int compileShader(unsigned int type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int compileProgram(const char* vertexSource, const char* fragmentSource) {
    const unsigned int vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        return 0;
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace

struct UIFontRenderer::Impl {
    struct Glyph {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        int width = 0;
        int height = 0;
        int bearingX = 0;
        int bearingY = 0;
        unsigned int advance = 0;
    };

    struct Atlas {
        unsigned int texture = 0;
        std::array<Glyph, 128> glyphs{};
        int pixelSize = kAtlasPixelSize;
        bool loaded = false;
    };

    std::array<Atlas, 3> atlases{};
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    glm::mat4 projection{1.0f};
    float contentScale = 1.0f;
    bool diagnosticEmitted = false;

    void diagnoseOnce() {
        if (!diagnosticEmitted) {
            std::cerr << "UI font initialization failed; using built-in stroke font\n";
            diagnosticEmitted = true;
        }
    }

#if defined(HAS_FREETYPE_UI) && !defined(UI_FONT_FORCE_FALLBACK)
    bool buildAtlas(FT_Library library, ui_design::FontRole role) {
        FT_Face face = nullptr;
        for (const auto& candidate : ui_design::fontCandidates(role)) {
            if (!std::filesystem::exists(candidate)) {
                continue;
            }
            const std::string filename = candidate.string();
            if (FT_New_Face(library, filename.c_str(), 0, &face) == 0) {
                break;
            }
            face = nullptr;
        }
        if (face == nullptr) {
            return false;
        }

        const int atlasPixelSize = std::max(
            1, static_cast<int>(std::lround(kAtlasPixelSize * contentScale)));
        if (FT_Set_Pixel_Sizes(face, 0, atlasPixelSize) != 0) {
            FT_Done_Face(face);
            return false;
        }

        int atlasWidth = 1;
        int atlasHeight = 1;
        for (int code = kFirstGlyph; code <= kLastGlyph; ++code) {
            if (FT_Load_Char(face, static_cast<unsigned long>(code), FT_LOAD_RENDER) == 0) {
                atlasWidth += static_cast<int>(face->glyph->bitmap.width) + 1;
                atlasHeight = std::max(atlasHeight, static_cast<int>(face->glyph->bitmap.rows));
            }
        }

        Atlas& atlas = atlases[roleIndex(role)];
        atlas.pixelSize = atlasPixelSize;
        glGenTextures(1, &atlas.texture);
        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        GLint previousUnpackAlignment = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasWidth, atlasHeight, 0,
                     GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int cursorX = 0;
        for (int code = kFirstGlyph; code <= kLastGlyph; ++code) {
            if (FT_Load_Char(face, static_cast<unsigned long>(code), FT_LOAD_RENDER) != 0) {
                continue;
            }
            const FT_GlyphSlot slot = face->glyph;
            const int width = static_cast<int>(slot->bitmap.width);
            const int height = static_cast<int>(slot->bitmap.rows);
            if (width > 0 && height > 0) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, cursorX, 0, width, height,
                                GL_RED, GL_UNSIGNED_BYTE, slot->bitmap.buffer);
            }

            Glyph& glyph = atlas.glyphs[static_cast<std::size_t>(code)];
            glyph.u0 = static_cast<float>(cursorX) / static_cast<float>(atlasWidth);
            glyph.v0 = 0.0f;
            glyph.u1 = static_cast<float>(cursorX + width) / static_cast<float>(atlasWidth);
            glyph.v1 = static_cast<float>(height) / static_cast<float>(atlasHeight);
            glyph.width = width;
            glyph.height = height;
            glyph.bearingX = slot->bitmap_left;
            glyph.bearingY = slot->bitmap_top;
            glyph.advance = static_cast<unsigned int>(slot->advance.x);
            cursorX += width + 1;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        FT_Done_Face(face);
        atlas.loaded = true;
        return true;
    }
#endif
};

UIFontRenderer::UIFontRenderer() : impl_(std::make_unique<Impl>()) {}

UIFontRenderer::~UIFontRenderer() = default;

bool UIFontRenderer::initialize(int viewportWidth, int viewportHeight, float contentScale) {
    shutdown();
    resize(viewportWidth, viewportHeight, contentScale);

#if defined(HAS_FREETYPE_UI) && !defined(UI_FONT_FORCE_FALLBACK)
    impl_->program = compileProgram(fontVertexShaderSource, fontFragmentShaderSource);
    if (impl_->program == 0) {
        impl_->diagnoseOnce();
        return false;
    }

    glGenVertexArrays(1, &impl_->vao);
    glGenBuffers(1, &impl_->vbo);
    glBindVertexArray(impl_->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        impl_->diagnoseOnce();
        return false;
    }

    bool allLoaded = true;
    allLoaded = impl_->buildAtlas(library, ui_design::FontRole::Display) && allLoaded;
    allLoaded = impl_->buildAtlas(library, ui_design::FontRole::Interface) && allLoaded;
    allLoaded = impl_->buildAtlas(library, ui_design::FontRole::Data) && allLoaded;
    FT_Done_FreeType(library);
    if (!allLoaded) {
        impl_->diagnoseOnce();
    }
    return allLoaded;
#else
    (void)viewportWidth;
    (void)viewportHeight;
    (void)contentScale;
    impl_->diagnoseOnce();
    return false;
#endif
}

void UIFontRenderer::resize(int viewportWidth, int viewportHeight, float contentScale) {
    impl_->contentScale = contentScale > 0.0f ? contentScale : 1.0f;
    impl_->projection = glm::ortho(
        0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0f);
}

bool UIFontRenderer::ready(ui_design::FontRole role) const {
    return impl_->program != 0 && impl_->atlases[roleIndex(role)].loaded;
}

float UIFontRenderer::measure(
    std::string_view text, float pixelSize, ui_design::FontRole role) const {
    if (!ready(role)) {
        return 0.0f;
    }
    const auto& atlas = impl_->atlases[roleIndex(role)];
    const float scale = pixelSize / static_cast<float>(atlas.pixelSize);
    float width = 0.0f;
    const auto& glyphs = atlas.glyphs;
    for (const unsigned char raw : text) {
        const unsigned char code = raw < glyphs.size() ? raw : static_cast<unsigned char>('?');
        width += static_cast<float>(glyphs[code].advance >> 6) * scale;
    }
    return width;
}

void UIFontRenderer::draw(
    std::string_view text, float x, float baselineY, float pixelSize,
    const glm::vec4& color, ui_design::FontRole role) {
    if (!ready(role) || text.empty()) {
        return;
    }

    const auto& atlas = impl_->atlases[roleIndex(role)];
    const float scale = pixelSize / static_cast<float>(atlas.pixelSize);
    std::vector<float> vertices;
    vertices.reserve(text.size() * 6 * 4);

    float cursorX = x;
    for (const unsigned char raw : text) {
        const unsigned char code = raw < atlas.glyphs.size() ? raw : static_cast<unsigned char>('?');
        const auto& glyph = atlas.glyphs[code];
        const float xpos = cursorX + static_cast<float>(glyph.bearingX) * scale;
        const float ypos = baselineY - static_cast<float>(glyph.bearingY) * scale;
        const float width = static_cast<float>(glyph.width) * scale;
        const float height = static_cast<float>(glyph.height) * scale;
        const float quad[] = {
            xpos,         ypos,          glyph.u0, glyph.v0,
            xpos + width, ypos,          glyph.u1, glyph.v0,
            xpos,         ypos + height, glyph.u0, glyph.v1,
            xpos + width, ypos,          glyph.u1, glyph.v0,
            xpos + width, ypos + height, glyph.u1, glyph.v1,
            xpos,         ypos + height, glyph.u0, glyph.v1,
        };
        vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
        cursorX += static_cast<float>(glyph.advance >> 6) * scale;
    }

    glUseProgram(impl_->program);
    glUniformMatrix4fv(glGetUniformLocation(impl_->program, "projection"), 1,
                       GL_FALSE, glm::value_ptr(impl_->projection));
    glUniform4fv(glGetUniformLocation(impl_->program, "textColor"), 1, &color[0]);
    glUniform1i(glGetUniformLocation(impl_->program, "fontAtlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.texture);
    glBindVertexArray(impl_->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices.size() / 4));
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UIFontRenderer::shutdown() {
    for (auto& atlas : impl_->atlases) {
        if (atlas.texture != 0) {
            glDeleteTextures(1, &atlas.texture);
        }
        atlas = {};
    }
    if (impl_->vbo != 0) {
        glDeleteBuffers(1, &impl_->vbo);
        impl_->vbo = 0;
    }
    if (impl_->vao != 0) {
        glDeleteVertexArrays(1, &impl_->vao);
        impl_->vao = 0;
    }
    if (impl_->program != 0) {
        glDeleteProgram(impl_->program);
        impl_->program = 0;
    }
}
