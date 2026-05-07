#pragma once
#include <string>
#include <glm/glm.hpp>

class BuiltInShader {
public:
    unsigned int ID;
    BuiltInShader(const char* vCode, const char* fCode);
    void use();
    void setMat4(const std::string& name, const glm::mat4& mat) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;
};
