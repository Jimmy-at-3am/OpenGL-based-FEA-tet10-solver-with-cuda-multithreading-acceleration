#pragma once

inline const char* modelVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
out vec3 FragPos;
out vec3 Normal;
out vec2 ScalarData;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;  
    ScalarData = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

inline const char* modelFS = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
in vec2 ScalarData;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform int scalarMode;
uniform float scalarMin;
uniform float scalarMax;

vec3 contourColor(float t) {
    t = clamp(t, 0.0, 1.0);
    if (t < 0.25) return mix(vec3(0.0, 0.05, 0.55), vec3(0.0, 0.55, 1.0), t / 0.25);
    if (t < 0.50) return mix(vec3(0.0, 0.55, 1.0), vec3(0.0, 0.90, 0.45), (t - 0.25) / 0.25);
    if (t < 0.75) return mix(vec3(0.0, 0.90, 0.45), vec3(1.0, 0.92, 0.10), (t - 0.50) / 0.25);
    return mix(vec3(1.0, 0.92, 0.10), vec3(0.85, 0.00, 0.00), (t - 0.75) / 0.25);
}

void main() {
    vec3 lightDir = normalize(viewPos - FragPos);
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), dot(-norm, lightDir));
    float diffuse = max(diff, 0.4);
    vec3 baseColor = objectColor;
    if (scalarMode == 1 || scalarMode == 2) {
        float scalarValue = scalarMode == 1 ? ScalarData.x : ScalarData.y;
        float normalizedValue = (scalarValue - scalarMin) / max(scalarMax - scalarMin, 1e-6);
        baseColor = contourColor(normalizedValue);
    }
    float lighting = 0.60 + 0.40 * diffuse;
    FragColor = vec4(baseColor * lighting, 1.0);
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
void main() { FragColor = vec4(color, 1.0); }
)";
