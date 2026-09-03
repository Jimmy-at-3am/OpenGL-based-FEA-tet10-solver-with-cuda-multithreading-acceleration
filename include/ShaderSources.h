#pragma once

inline const char* modelVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in float aElementScalar;   // per-element value
out vec3 FragPos;
out vec3 Normal;
out vec2 ScalarData;
flat out float ElementScalar;                     // flat: no interpolation across an element
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    ScalarData = aTexCoords;
    ElementScalar = aElementScalar;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

inline const char* modelFS = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
in vec2 ScalarData;
flat in float ElementScalar;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform int scalarMode;
uniform float scalarMin;
uniform float scalarMax;
uniform float fragAlpha;          // 1.0 normally, <1 for GHOST dead pass
// Sectional view: when sectionOn == 1, fragments below the world-space cut
// height (Z) are discarded so the interior above the cut plane is exposed.
// Uniforms default to 0 -> feature off for every path that never sets them.
uniform int   sectionOn;
uniform float sectionZ;

vec3 contourColor(float t) {
    t = clamp(t, 0.0, 1.0);
    if (t < 0.25) return mix(vec3(0.0, 0.05, 0.55), vec3(0.0, 0.55, 1.0), t / 0.25);
    if (t < 0.50) return mix(vec3(0.0, 0.55, 1.0), vec3(0.0, 0.90, 0.45), (t - 0.25) / 0.25);
    if (t < 0.75) return mix(vec3(0.0, 0.90, 0.45), vec3(1.0, 0.92, 0.10), (t - 0.50) / 0.25);
    return mix(vec3(1.0, 0.92, 0.10), vec3(0.85, 0.00, 0.00), (t - 0.75) / 0.25);
}

// categorical failure-mode colour (matches the UI legend).
vec3 categoricalColor(float m) {
    int mi = int(m + 0.5);
    if (mi == 1) return vec3(0.85, 0.05, 0.05); // interlayer tension - red
    if (mi == 2) return vec3(1.00, 0.55, 0.00); // interlayer shear  - orange
    if (mi == 3) return vec3(1.00, 0.92, 0.10); // intralayer        - yellow
    return vec3(0.55, 0.55, 0.55);              // alive / unknown    - grey
}

void main() {
    if (sectionOn == 1 && FragPos.z < sectionZ) discard;
    vec3 lightDir = normalize(viewPos - FragPos);
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), dot(-norm, lightDir));
    float diffuse = max(diff, 0.4);
    vec3 baseColor = objectColor;
    if (scalarMode == 1 || scalarMode == 2) {
        // Per-vertex scalar (displacement / force) -> smooth contour.
        float scalarValue = scalarMode == 1 ? ScalarData.x : ScalarData.y;
        float normalizedValue = (scalarValue - scalarMin) / max(scalarMax - scalarMin, 1e-6);
        baseColor = contourColor(normalizedValue);
    } else if (scalarMode == 3) {
        // Per-element categorical failure mode (flat).
        baseColor = categoricalColor(ElementScalar);
    } else if (scalarMode == 4 || scalarMode == 5) {
        // Per-element heatmap: 4 = failure iteration, 5 = von Mises at death.
        float normalizedValue = (ElementScalar - scalarMin) / max(scalarMax - scalarMin, 1e-6);
        baseColor = contourColor(normalizedValue);
    }
    float lighting = 0.60 + 0.40 * diffuse;
    FragColor = vec4(baseColor * lighting, fragAlpha);
}
)";

inline const char* axisVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 ourColor;
uniform mat4 model; uniform mat4 view; uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

inline const char* axisFS = R"(
#version 330 core
out vec4 FragColor;
in vec3 ourColor;
void main() { FragColor = vec4(ourColor, 1.0); }
)";

inline const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); }
)";

inline const char* uiFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 color;
uniform float uiAlpha;   // set to 1.0 by every SimpleUI draw call; <1 for overlays
void main() { FragColor = vec4(color, uiAlpha); }
)";

inline const char* roundedRectVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aLocalPos;
out vec2 localPos;
uniform mat4 projection;
void main() {
    localPos = aLocalPos;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

inline const char* roundedRectFragmentShaderSource = R"(
#version 330 core
in vec2 localPos;
out vec4 FragColor;
uniform vec2 halfSize;
uniform float radius;
uniform vec4 color;
uniform bool outlineOnly;
uniform vec2 innerHalfSize;
uniform float innerRadius;

float roundedBoxSdf(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    float distanceToEdge = roundedBoxSdf(localPos, halfSize, radius);
    float coverage = 1.0 - smoothstep(-1.0, 1.0, distanceToEdge);
    if (outlineOnly) {
        float distanceToInnerEdge = roundedBoxSdf(
            localPos, innerHalfSize, innerRadius);
        float innerCoverage = 1.0 - smoothstep(
            -1.0, 1.0, distanceToInnerEdge);
        coverage = max(0.0, coverage - innerCoverage);
    }
    FragColor = vec4(color.rgb, color.a * coverage);
}
)";

inline const char* fontVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 texCoord;
uniform mat4 projection;
void main() {
    texCoord = aTexCoord;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

inline const char* fontFragmentShaderSource = R"(
#version 330 core
in vec2 texCoord;
out vec4 FragColor;
uniform sampler2D fontAtlas;
uniform vec4 textColor;
void main() {
    float coverage = texture(fontAtlas, texCoord).r;
    FragColor = vec4(textColor.rgb, textColor.a * coverage);
}
)";
