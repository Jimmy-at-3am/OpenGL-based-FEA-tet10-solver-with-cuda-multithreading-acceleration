#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // 在世界空间中计算片段的位置
    FragPos = vec3(model * vec4(aPos, 1.0));
    
    // 计算法线向量 (处理非均匀缩放)
    Normal = mat3(transpose(inverse(model))) * aNormal;  
    
    // 传递纹理坐标
    TexCoords = aTexCoords;
    
    // 计算最终裁剪空间位置
    gl_Position = projection * view * vec4(FragPos, 1.0);
}