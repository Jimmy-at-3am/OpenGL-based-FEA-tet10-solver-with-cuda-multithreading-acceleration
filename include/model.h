#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h> 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <learnopengl/shader_m.h>
#include <learnopengl/mesh.h> 

// -------------------------------------------------------------------------
// STB IMAGE SETUP
// Ensure stb_image.h is in your include path.
// IMPORTANT: The #define STB_IMAGE_IMPLEMENTATION must exist in your main.cpp
// or another .cpp file, not here, to avoid linker errors.
// -------------------------------------------------------------------------
#include <stb_image.h> 

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

using namespace std;

// Forward declaration of the standard file loader
unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

// --------------------------------------------------------------------------------------------
// 1. MEMORY LOADING FUNCTION (For .glb embedded textures)
// --------------------------------------------------------------------------------------------
unsigned int TextureFromMemory(const aiTexture* texture) {
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = nullptr;

    // Logic to handle Assimp's embedded texture data
    // mHeight = 0 means the data is compressed (PNG/JPG buffer)
    if (texture->mHeight == 0) {
        data = stbi_load_from_memory(
            reinterpret_cast<unsigned char*>(texture->pcData), 
            texture->mWidth, 
            &width, &height, &nrComponents, 
            0
        );
    } 
    else {
        // mHeight > 0 means raw pixel data (ARGB8888)
        data = stbi_load_from_memory(
            reinterpret_cast<unsigned char*>(texture->pcData), 
            texture->mWidth * texture->mHeight * 4, 
            &width, &height, &nrComponents, 
            0
        );
    }

    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else
            format = GL_RGB; 

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Texture Parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
        cout << "SUCCESS::MODEL::Loaded embedded texture from memory." << endl;
    }
    else {
        // DEBUG: Print failure reason
        cout << "ERROR::MODEL::Texture failed to load from embedded memory! STB Reason: " << stbi_failure_reason() << endl;
        stbi_image_free(data);
    }

    return textureID;
}

// --------------------------------------------------------------------------------------------
// 2. STANDARD FILE LOADING FUNCTION (For .obj / external files)
// --------------------------------------------------------------------------------------------
unsigned int TextureFromFile(const char *path, const string &directory, bool gamma) {
    string filename = string(path);
    filename = directory + '/' + filename;

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else 
            format = GL_RGB;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
        cout << "SUCCESS::MODEL::Loaded file texture: " << filename << endl;
    }
    else {
        cout << "ERROR::MODEL::Texture failed to load at path: " << path << endl;
        stbi_image_free(data);
    }

    return textureID;
}

class Model {
public:
    // model data 
    vector<Texture> textures_loaded;	
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;

    Model(string const &path, bool gamma = false) : gammaCorrection(gamma) {
        loadModel(path);
    }

    void Draw(Shader &shader) {
        for(unsigned int i = 0; i < meshes.size(); i++)
            meshes[i].Draw(shader);
    }
    
private:
    void loadModel(string const &path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }
        directory = path.substr(0, path.find_last_of('/'));
        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode *node, const aiScene *scene) {
        for(unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        for(unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene) {
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        // Process vertices
        for(unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            glm::vec3 vector; 
            
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;

            if (mesh->HasNormals()) {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }

            if(mesh->mTextureCoords[0]) {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                
                if (mesh->HasTangentsAndBitangents()) {
                    vector.x = mesh->mTangents[i].x;
                    vector.y = mesh->mTangents[i].y;
                    vector.z = mesh->mTangents[i].z;
                    vertex.Tangent = vector;

                    vector.x = mesh->mBitangents[i].x;
                    vector.y = mesh->mBitangents[i].y;
                    vector.z = mesh->mBitangents[i].z;
                    vertex.Bitangent = vector;
                }
            } else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            vertices.push_back(vertex);
        }

        // Process Indices
        for(unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        // Process Materials
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];    

        // NOTE: We map ALL discovered textures to "texture_diffuse" to ensure your shader uses them.
        
        // 1. Diffuse maps
        vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse", scene);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        
        // 2. Base Color maps (PBR) -> Mapped to texture_diffuse
        vector<Texture> baseColorMaps = loadMaterialTextures(material, aiTextureType_BASE_COLOR, "texture_diffuse", scene);
        textures.insert(textures.end(), baseColorMaps.begin(), baseColorMaps.end());

        // 3. Emissive maps -> Mapped to texture_diffuse (Hack to see color if it's stored here)
        vector<Texture> emissiveMaps = loadMaterialTextures(material, aiTextureType_EMISSIVE, "texture_diffuse", scene);
        textures.insert(textures.end(), emissiveMaps.begin(), emissiveMaps.end());
        
        // 4. Unknown maps -> Mapped to texture_diffuse (Catch-all)
        vector<Texture> unknownMaps = loadMaterialTextures(material, aiTextureType_UNKNOWN, "texture_diffuse", scene);
        textures.insert(textures.end(), unknownMaps.begin(), unknownMaps.end());

        return Mesh(vertices, indices, textures);
    }

    vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName, const aiScene* scene) {
        vector<Texture> textures;
        for(unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            mat->GetTexture(type, i, &str);
            
            bool skip = false;
            for(unsigned int j = 0; j < textures_loaded.size(); j++) {
                if(std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0) {
                    textures.push_back(textures_loaded[j]);
                    skip = true; 
                    break;
                }
            }
            if(!skip) {   
                Texture texture;
                
                const char* pathStr = str.C_Str();
                
                // If the path starts with '*', it is an embedded texture (e.g. "*0")
                if (pathStr[0] == '*') {
                    try {
                        int textureIndex = std::stoi(pathStr + 1);
                        if (textureIndex < scene->mNumTextures) {
                            cout << "DEBUG::MODEL::Found embedded texture path: " << pathStr << ". Loading from index " << textureIndex << endl;
                            texture.id = TextureFromMemory(scene->mTextures[textureIndex]);
                        } else {
                            cout << "ERROR::MODEL::Texture index out of bounds: " << textureIndex << endl;
                        }
                    } catch (...) {
                        cout << "ERROR::MODEL::Could not parse texture index: " << pathStr << endl;
                    }
                } 
                else {
                    cout << "DEBUG::MODEL::Found external texture path: " << pathStr << endl;
                    texture.id = TextureFromFile(pathStr, this->directory, false);
                }

                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture);  
            }
        }
        return textures;
    }
};
#endif