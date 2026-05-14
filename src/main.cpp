#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Globals.h"
#include "ShaderSources.h"
#include "BuiltInShader.h"
#include "SimpleUI.h"
#include "FEAModel.h"
#include "FEASolver.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>

// Define globals
unsigned int scrWidth = 1600;
unsigned int scrHeight = 720;
Camera camera(glm::vec3(0.0f, 1.0f, 4.0f));
float lastX = scrWidth / 2.0f;
float lastY = scrHeight / 2.0f;
float mouseX = 0.0f;
float mouseY = 0.0f;
bool mousePressed = false;
bool prevMousePressed = false;
bool showWireframe = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
AppMode currentMode = MODE_CUBE;
std::vector<std::string> modelFiles;  // replaces stlFiles: holds .stl + .3mf
float panelWidth = 600.0f;

namespace fs = std::filesystem;

void scanForModels() {
    modelFiles.clear();
    for (const auto& entry : fs::directory_iterator(".")) {
        auto ext = entry.path().extension().string();
        // Normalise extension to lower-case for comparison
        std::string extLow = ext;
        std::transform(extLow.begin(), extLow.end(), extLow.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (extLow == ".stl" || extLow == ".3mf") {
            modelFiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(modelFiles.begin(), modelFiles.end());
}

struct MaterialProps {
    std::string name          = "Steel";
    double      E             = 2.0e11;
    double      nu            = 0.3;
    double      density       = 7850.0;
    double      fractureStress = 2.5e8; // 250 MPa (steel yield approx)
    // FDM anisotropy (present only in PLA-type .mat files; zero = not provided).
    double      E_z                      = 0.0;
    double      nu_pz                    = 0.0;
    double      G_pz                     = 0.0;
    double      fractureStress_intralayer = 0.0;
    double      fractureStress_interlayer = 0.0;
    double      fractureShear_interlayer  = 0.0;
};

static bool loadMaterialFile(const std::string& path, MaterialProps& props) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto ltrim = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t\r\n")); };
        auto rtrim = [](std::string& s) { auto p = s.find_last_not_of(" \t\r\n"); if (p != std::string::npos) s.erase(p + 1); else s.clear(); };
        ltrim(key); rtrim(key); ltrim(val); rtrim(val);
        try {
            if      (key == "name")          props.name          = val;
            else if (key == "E")             props.E             = std::stod(val);
            else if (key == "nu")            props.nu            = std::stod(val);
            else if (key == "density")       props.density       = std::stod(val);
            else if (key == "fractureStress")              props.fractureStress              = std::stod(val);
            else if (key == "E_z")                         props.E_z                         = std::stod(val);
            else if (key == "nu_pz")                       props.nu_pz                       = std::stod(val);
            else if (key == "G_pz")                        props.G_pz                        = std::stod(val);
            else if (key == "fractureStress_intralayer")   props.fractureStress_intralayer   = std::stod(val);
            else if (key == "fractureStress_interlayer")   props.fractureStress_interlayer   = std::stod(val);
            else if (key == "fractureShear_interlayer")    props.fractureShear_interlayer    = std::stod(val);
        } catch (...) {}
    }
    return true;
}

static std::string matStemToLabel(const std::string& filename) {
    std::string s = filename;
    auto dot = s.rfind('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    for (auto& c : s) c = (c == '_') ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> matFiles;
std::string activeMaterialFile;
MaterialProps currentMaterial;

void scanForMaterials() {
    matFiles.clear();
    std::string matDir = "materials";
    if (!fs::exists(matDir)) return;
    for (const auto& entry : fs::directory_iterator(matDir)) {
        if (entry.path().extension() == ".mat") {
            matFiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(matFiles.begin(), matFiles.end());
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void processInput(GLFWwindow* window);

glm::vec3 contourColor(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.25f) return glm::mix(glm::vec3(0.0f, 0.05f, 0.55f), glm::vec3(0.0f, 0.55f, 1.0f), t / 0.25f);
    if (t < 0.50f) return glm::mix(glm::vec3(0.0f, 0.55f, 1.0f), glm::vec3(0.0f, 0.90f, 0.45f), (t - 0.25f) / 0.25f);
    if (t < 0.75f) return glm::mix(glm::vec3(0.0f, 0.90f, 0.45f), glm::vec3(1.0f, 0.92f, 0.10f), (t - 0.50f) / 0.25f);
    return glm::mix(glm::vec3(1.0f, 0.92f, 0.10f), glm::vec3(0.85f, 0.0f, 0.0f), (t - 0.75f) / 0.25f);
}

bool projectToScreen(const glm::vec3& point, const glm::mat4& view, const glm::mat4& projection, float& outX, float& outY) {
    glm::vec4 clip = projection * view * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
    outX = ((ndc.x * 0.5f) + 0.5f) * static_cast<float>(scrWidth);
    outY = ((-ndc.y * 0.5f) + 0.5f) * static_cast<float>(scrHeight);
    return true;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(scrWidth, scrHeight, "FEA Pre-Processor (M: Toggle Mesh, E: Config Mode)", NULL, NULL);
    if (window == NULL) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { return -1; }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);

    BuiltInShader schematicShader(modelVS, modelFS);
    BuiltInShader axisShaderObj(axisVS, axisFS);

    FEAModel model;
    SimpleUI ui;
    ui.init(scrWidth, scrHeight);
    scanForModels();
    scanForMaterials();
    for (const auto& mf : matFiles) {
        if (mf == "steel.mat") { activeMaterialFile = mf; loadMaterialFile("materials/" + mf, currentMaterial); break; }
    }
    if (activeMaterialFile.empty() && !matFiles.empty()) {
        activeMaterialFile = matFiles[0];
        loadMaterialFile("materials/" + activeMaterialFile, currentMaterial);
    }

    float axisVertices[] = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
    };
    unsigned int axisVAO, axisVBO;
    glGenVertexArrays(1, &axisVAO); glGenBuffers(1, &axisVBO);
    glBindVertexArray(axisVAO); glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axisVertices), axisVertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame; lastFrame = currentFrame;
        int w, h; glfwGetWindowSize(window, &w, &h);
        if (w == 0 || h == 0) {
            glfwWaitEvents();
            continue;
        }
        scrWidth = w; scrHeight = h; ui.resize(scrWidth, scrHeight);
        processInput(window);

        if (currentMode == MODE_CUBE && model.needsUpdate) { model.generateCube(); }

        glClearColor(0.9f, 0.92f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)scrWidth / (float)scrHeight, 0.1f, 1000.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::vec3 axisExtents = glm::max(glm::abs(model.currentMinBounds), glm::abs(model.currentMaxBounds));
        glm::vec3 axisLengths = glm::max(axisExtents * 1.15f, glm::vec3(1.0f));
        float dynamicAxisVertices[] = {
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, axisLengths.x, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, axisLengths.y, 0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, axisLengths.z, 0.0f, 0.0f, 1.0f
        };
        glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(dynamicAxisVertices), dynamicAxisVertices);

        axisShaderObj.use();
        axisShaderObj.setMat4("projection", projection);
        axisShaderObj.setMat4("view", view);
        axisShaderObj.setMat4("model", glm::mat4(1.0f));
        glBindVertexArray(axisVAO);
        glLineWidth(3.0f);
        glDrawArrays(GL_LINES, 0, 6);

        schematicShader.use();
        schematicShader.setMat4("projection", projection);
        schematicShader.setMat4("view", view);
        model.draw(schematicShader, camera.Position);

        glDisable(GL_DEPTH_TEST);
        float panelW = panelWidth; float panelX = scrWidth - panelW;
        float halfW  = panelW * 0.5f;
        float divX   = panelX + halfW;

        auto drawAxisLabel = [&](const glm::vec3& point, const char* axisName, float axisValue, const glm::vec3& color) {
            float sx = 0.0f;
            float sy = 0.0f;
            if (!projectToScreen(point, view, projection, sx, sy)) return;
            if (sx >= panelX - 70.0f) return;
            char label[64];
            snprintf(label, sizeof(label), "%s %.3f", axisName, axisValue);
            ui.drawText(label, sx + 6.0f, sy - 6.0f, 8.0f, color);
        };

        drawAxisLabel(glm::vec3(axisLengths.x, 0.0f, 0.0f), "X", axisLengths.x, glm::vec3(1.0f, 0.35f, 0.35f));
        drawAxisLabel(glm::vec3(0.0f, axisLengths.y, 0.0f), "Y", axisLengths.y, glm::vec3(0.35f, 1.0f, 0.35f));
        drawAxisLabel(glm::vec3(0.0f, 0.0f, axisLengths.z), "Z", axisLengths.z, glm::vec3(0.35f, 0.7f, 1.0f));

        ui.drawRect(panelX, 0, panelW, scrHeight, glm::vec3(0.1f, 0.1f, 0.1f));
        ui.drawRect(panelX, 0, 2, scrHeight, glm::vec3(0.3f, 0.3f, 0.3f));
        ui.drawRect(divX, 0, 2, scrHeight, glm::vec3(0.22f, 0.22f, 0.22f));

        // ===== LEFT COLUMN: LOADING =====
        float lX = panelX + 15.0f;
        float lW = halfW - 25.0f;
        float lY = 18.0f;

        ui.drawText("FEA PRE-PROCESSOR", lX, lY, 13.0f, glm::vec3(0.5f, 0.8f, 1.0f)); lY += 22.0f;
        ui.drawText("MODE: CAD ORBIT", lX, lY, 9.0f, glm::vec3(0.3f, 1.0f, 0.3f)); lY += 20.0f;
        ui.drawRect(lX, lY, lW, 1.5f, glm::vec3(0.3f)); lY += 12.0f;

        ui.drawText("LOAD MODEL", lX, lY, 9.5f, glm::vec3(0.65f, 0.65f, 0.65f)); lY += 20.0f;
        float lBtnH = lW * 0.5f - 3.0f;
        if (ui.button("CUBE MODE", lX, lY, lBtnH, 22.0f, currentMode == MODE_CUBE)) {
            currentMode = MODE_CUBE;
            model.needsUpdate = true;
            camera.OrbitTarget = glm::vec3(0.0f);
            camera.OrbitRadius = 5.0f;
            camera.UpdatePosition();
        }
        if (ui.button("IMPORT FILE", lX + lBtnH + 6.0f, lY, lBtnH, 22.0f, currentMode == MODE_IMPORT)) {
            currentMode = MODE_IMPORT;
            scanForModels();
        }
        lY += 30.0f;

        if (currentMode == MODE_IMPORT) {
            if (modelFiles.empty()) {
                ui.drawText("NO MODELS FOUND", lX, lY, 9.0f, glm::vec3(1.0f, 0.2f, 0.2f)); lY += 15.0f;
                ui.drawText("(place .stl / .3mf here)", lX, lY, 7.5f, glm::vec3(0.5f)); lY += 20.0f;
            } else {
                float fileListMax = (float)scrHeight * 0.52f;
                for (const auto& file : modelFiles) {
                    if (lY + 22.0f > fileListMax) break;
                    bool isActive = (file == model.loadedFileName);

                    // Determine format for badge colour
                    std::string extLow = fs::path(file).extension().string();
                    std::transform(extLow.begin(), extLow.end(), extLow.begin(),
                                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    bool is3MF = (extLow == ".3mf");

                    if (ui.button(file, lX, lY, lW - (is3MF ? 38.0f : 0.0f), 22.0f, isActive)) {
                        if (model.loadFile(file)) {
                            camera.OrbitTarget = glm::vec3(0.0f);
                            camera.OrbitRadius = 5.0f;
                        }
                    }
                    // [3MF] badge in teal accent
                    if (is3MF) {
                        ui.drawRect(lX + lW - 36.0f, lY + 2.0f, 34.0f, 18.0f,
                                    glm::vec3(0.05f, 0.45f, 0.45f));
                        ui.drawText("3MF", lX + lW - 31.0f, lY + 6.0f, 7.5f,
                                    glm::vec3(0.4f, 1.0f, 0.95f));
                    }
                    lY += 30.0f;
                }
            }

            // --- File metadata line ---
            if (!model.loadedFileName.empty()) {
                lY += 4.0f;
                ui.drawRect(lX, lY, lW, 1.0f, glm::vec3(0.25f)); lY += 8.0f;
                char metaBuf[128];
                if (!model.lastLoadedFormat.empty()) {
                    snprintf(metaBuf, sizeof(metaBuf),
                             "FORMAT: %s", model.lastLoadedFormat.c_str());
                    ui.drawText(metaBuf, lX, lY, 8.0f, glm::vec3(0.4f, 0.9f, 0.85f)); lY += 14.0f;
                    if (model.lastLoadedObjectCount > 1) {
                        snprintf(metaBuf, sizeof(metaBuf),
                                 "OBJECTS: %d", model.lastLoadedObjectCount);
                        ui.drawText(metaBuf, lX, lY, 8.0f, glm::vec3(0.65f, 0.85f, 0.65f)); lY += 14.0f;
                    }
                }
            }
        } // end MODE_IMPORT

        lY += 5.0f;
        ui.drawRect(lX, lY, lW, 1.5f, glm::vec3(0.3f)); lY += 12.0f;
        ui.drawText("LOAD MATERIAL", lX, lY, 9.5f, glm::vec3(0.65f, 0.65f, 0.65f)); lY += 20.0f;

        if (matFiles.empty()) {
            ui.drawText("NO .MAT FILES FOUND", lX, lY, 8.5f, glm::vec3(1.0f, 0.4f, 0.4f)); lY += 20.0f;
            ui.drawText("(place in ./materials/)", lX, lY, 7.5f, glm::vec3(0.55f)); lY += 18.0f;
        } else {
            for (const auto& mf : matFiles) {
                bool isActive = (mf == activeMaterialFile);
                if (ui.button(matStemToLabel(mf), lX, lY, lW, 22.0f, isActive)) {
                    activeMaterialFile = mf;
                    loadMaterialFile("materials/" + mf, currentMaterial);
                }
                lY += 30.0f;
            }
        }

        lY += 3.0f;
        ui.drawRect(lX, lY, lW, 1.5f, glm::vec3(0.25f)); lY += 10.0f;
        char matBuf[128];
        snprintf(matBuf, sizeof(matBuf), "ACTIVE: %s", currentMaterial.name.c_str());
        ui.drawText(matBuf, lX, lY, 9.0f, glm::vec3(0.85f, 0.95f, 0.75f)); lY += 17.0f;
        snprintf(matBuf, sizeof(matBuf), "E: %.3g GPa   nu: %.2f", currentMaterial.E * 1e-9, currentMaterial.nu);
        ui.drawText(matBuf, lX, lY, 8.5f, glm::vec3(0.7f, 0.85f, 0.7f)); lY += 15.0f;
        snprintf(matBuf, sizeof(matBuf), "rho: %.0f kg/m3", currentMaterial.density);
        ui.drawText(matBuf, lX, lY, 8.5f, glm::vec3(0.6f, 0.75f, 0.6f));

        // ===== RIGHT COLUMN: CONTROLS =====
        float rX = divX + 15.0f;
        float rW = halfW - 25.0f;
        float rY = 18.0f;

        char axisBuffer[64];
        snprintf(axisBuffer, sizeof(axisBuffer), "X AXIS: %.3f m", axisLengths.x);
        ui.drawText(axisBuffer, rX, rY, 8.5f, glm::vec3(0.95f, 0.55f, 0.55f)); rY += 18.0f;
        snprintf(axisBuffer, sizeof(axisBuffer), "Y AXIS: %.3f m", axisLengths.y);
        ui.drawText(axisBuffer, rX, rY, 8.5f, glm::vec3(0.55f, 0.95f, 0.55f)); rY += 18.0f;
        snprintf(axisBuffer, sizeof(axisBuffer), "Z AXIS: %.3f m", axisLengths.z);
        ui.drawText(axisBuffer, rX, rY, 8.5f, glm::vec3(0.55f, 0.75f, 0.95f)); rY += 22.0f;
        ui.drawRect(rX, rY, rW, 1.5f, glm::vec3(0.3f)); rY += 14.0f;

        static bool useMultithreading = false;
        static float forceMagnitudeMN = 100.0f; // 100 MN force for benchmark (gives 0.25m deflection on 5x1x1m steel beam)
        static float curvAngleThreshold = 15.0f;
        static float curvFracLimit = 0.25f;

        if (currentMode == MODE_CUBE) {
            if (ui.slider("X LENGTH (m)", model.params.sizeX, 0.5f, 50.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("Y LENGTH (m)", model.params.sizeY, 0.1f, 50.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("Z LENGTH (m)", model.params.sizeZ, 0.1f, 50.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("SUBDIVISIONS", model.params.subdivisions, 1.0f, 20.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
        }
        else if (currentMode == MODE_IMPORT) {
            std::string prLabel = model.params.enablePolarRemoval ? "VERTEX SMOOTHING: ON" : "VERTEX SMOOTHING: OFF";
            if (ui.button(prLabel, rX, rY, rW, 20.0f, model.params.enablePolarRemoval)) {
                model.params.enablePolarRemoval = !model.params.enablePolarRemoval;
                if (!model.loadedFileName.empty()) model.loadSTL(model.loadedFileName);
            }
            rY += 25.0f;

            ui.slider("MESH QUALITY (p)", model.params.tetQuality, 1.1f, 3.0f, rX, rY, rW, 15.0f); rY += 20.0f;
            ui.slider("MAX VOLUME (%)", model.params.maxVolPercent, 0.001f, 0.5f, rX, rY, rW, 15.0f); rY += 20.0f;

            float btnW = rW * 0.5f - 2.0f;
            if (ui.button("SURFACE MESH", rX, rY, btnW, 25.0f, !model.showVolumetricMesh, false)) {
                model.showVolumetricMesh = false;
                model.buildBuffers();
            }
            if (ui.button("VOLUME MESH", rX + btnW + 4.0f, rY, btnW, 25.0f, model.showVolumetricMesh, !model.hasVolumetricMesh)) {
                if (model.hasVolumetricMesh) {
                    model.showVolumetricMesh = true;
                    model.buildBuffers();
                }
            }
            rY += 30.0f;

            if (ui.button("GENERATE 3D MESH", rX, rY, rW, 35.0f)) {
                std::cout << "Button Clicked: Launching TetGen..." << std::endl;
                model.generateVolumetricMesh();
            }
            rY += 40.0f;

            if (model.hasVolumetricMesh) {
                std::string mtLabel = useMultithreading ? "MULTITHREADING: ON" : "MULTITHREADING: OFF";
                if (ui.button(mtLabel, rX, rY, rW, 25.0f, useMultithreading)) {
                    useMultithreading = !useMultithreading;
                }
                rY += 30.0f;

                static bool useGPU = false;
                static bool useFdmAnisotropy = false;

                // Build-axis and force-type selections (shared by all solvers).
                static int buildAxis = 1; // 0=X weak, 1=Y weak (default), 2=Z weak
                static int loadTypeSel = 0;
                static const FEASolver::LoadType loadTypeMap[] = {
                    FEASolver::LoadType::CantileverBendingZ,
                    FEASolver::LoadType::PointForceZ,
                    FEASolver::LoadType::SurfaceCompressionY,
                    FEASolver::LoadType::TensionX,
                    FEASolver::LoadType::TensionY,
                    FEASolver::LoadType::TensionZ,
                };
                static const char* loadTypeNames[] = {
                    "FORCE: CANTILEVER Z",
                    "FORCE: POINT Z",
                    "FORCE: SURFACE COMP Y",
                    "FORCE: TENSION X",
                    "FORCE: TENSION Y",
                    "FORCE: TENSION Z",
                };
                static const char* buildAxisNames[] = {
                    "LAYER: X (build top)",
                    "LAYER: Y (build top)",
                    "LAYER: Z (build top)",
                };

                std::string gpuLabel = useGPU ? "GPU ACCEL (CUDA): ON" : "GPU ACCEL (CUDA): OFF";
                if (ui.button(gpuLabel, rX, rY, rW, 25.0f, useGPU)) {
                    useGPU = !useGPU;
                }
                rY += 30.0f;

                if (ui.button(buildAxisNames[buildAxis], rX, rY, rW, 22.0f)) {
                    buildAxis = (buildAxis + 1) % 3;
                }
                rY += 27.0f;

                if (ui.button(loadTypeNames[loadTypeSel], rX, rY, rW, 22.0f)) {
                    loadTypeSel = (loadTypeSel + 1) % 6;
                }
                rY += 27.0f;

                ui.slider("POINT FORCE (MN)", forceMagnitudeMN, 1.0f, 1000.0f, rX, rY, rW, 15.0f);
                rY += 20.0f;

                if (ui.button("LINEAR STATIC FEA", rX, rY, rW, 25.0f)) {
                    std::cout << "Launching Static Solver..." << std::endl;
                    FEASolver solver;
                    solver.loadType             = loadTypeMap[loadTypeSel];
                    solver.buildAxis            = buildAxis;
                    solver.useQuadraticElements = true;
                    solver.useMultithreading    = useMultithreading;
                    solver.useGPU              = useGPU;
                    solver.forceMagnitude       = static_cast<double>(forceMagnitudeMN) * 1.0e6;
                    solver.youngsModulus        = currentMaterial.E;
                    solver.poissonRatio         = currentMaterial.nu;
                    solver.solveLinearStatic(model, 10.0f);
                }
                rY += 30.0f;
                if (ui.button("NONLINEAR FEA (NR)", rX, rY, rW, 25.0f)) {
                    std::cout << "Launching Newton-Raphson Solver..." << std::endl;
                    FEASolver solver;
                    solver.loadType             = loadTypeMap[loadTypeSel];
                    solver.buildAxis            = buildAxis;
                    solver.useQuadraticElements = true;
                    solver.verboseDiagnostics   = true;
                    solver.useMultithreading    = useMultithreading;
                    solver.useGPU              = useGPU;
                    solver.forceMagnitude       = static_cast<double>(forceMagnitudeMN) * 1.0e6;
                    solver.youngsModulus        = currentMaterial.E;
                    solver.poissonRatio         = currentMaterial.nu;
                    solver.loadSymmetry = FEASolver::LoadSymmetry::None;
                    NRParams  nrp;
                    solver.solveNonlinearStatic(model, 10.0f, nrp);
                }
                rY += 30.0f;

                ui.drawRect(rX, rY, rW, 1.5f, glm::vec3(0.25f)); rY += 10.0f;
                ui.drawText("ADAPTIVE MESHING", rX, rY, 9.0f, glm::vec3(0.75f, 0.95f, 0.85f)); rY += 17.0f;
                ui.slider("CURV ANGLE (DEG)", curvAngleThreshold, 1.0f, 45.0f, rX, rY, rW, 15.0f); rY += 20.0f;
                ui.slider("CURV FRAC LIMIT", curvFracLimit, 0.05f, 0.75f, rX, rY, rW, 15.0f); rY += 20.0f;

                if (ui.button("RUN ADAPTIVE FEA", rX, rY, rW, 25.0f)) {
                    std::cout << "Launching Adaptive Solver..." << std::endl;
                    FEASolver solver;
                    solver.loadType          = loadTypeMap[loadTypeSel];
                    solver.buildAxis         = buildAxis;
                    solver.useMultithreading = useMultithreading;
                    solver.useGPU           = useGPU;
                    solver.forceMagnitude    = static_cast<double>(forceMagnitudeMN) * 1.0e6;
                    solver.youngsModulus     = currentMaterial.E;
                    solver.poissonRatio      = currentMaterial.nu;
                    solver.geoParams.curvatureAngleThreshold = curvAngleThreshold;
                    solver.geoParams.highCurvatureFracLimit  = curvFracLimit;
                    solver.solveAdaptive(model, 10.0f);
                }
                rY += 30.0f;

                // FDM anisotropy toggle — only meaningful if the loaded material has E_z data.
                const bool hasFdmData = (currentMaterial.E_z > 0.0);
                {
                    std::string fdmLabel = useFdmAnisotropy
                        ? "FDM ANISOTROPY: ON " : "FDM ANISOTROPY: OFF";
                    if (!hasFdmData) fdmLabel += " [no data]";
                    if (ui.button(fdmLabel, rX, rY, rW, 22.0f, useFdmAnisotropy)) {
                        if (!hasFdmData) {
                            std::cout << "[FDM-ANISO] Current material has no E_z/G_pz data — "
                                      << "load a 3D-print material (e.g. pla.mat) first." << std::endl;
                            useFdmAnisotropy = false;
                        } else {
                            useFdmAnisotropy = !useFdmAnisotropy;
                        }
                    }
                }
                rY += 27.0f;

                if (ui.button("BRITTLE FRACTURE", rX, rY, rW, 25.0f)) {
                    // Reset any previous fracture state so we start fresh.
                    model.elementAlive.clear();
                    model.elementFailureIter.clear();
                    model.elementFailureMode.clear();

                    const bool fdmOn = useFdmAnisotropy && hasFdmData;
                    if (fdmOn) {
                        std::cout << "Launching FDM-Aware Brittle Fracture Solver..." << std::endl;
                    } else {
                        std::cout << "Launching Brittle Fracture Solver (sigma_f="
                                  << currentMaterial.fractureStress * 1e-6 << " MPa)..." << std::endl;
                    }

                    FEASolver solver;
                    solver.useMultithreading    = useMultithreading;
                    solver.useGPU               = useGPU;
                    solver.useQuadraticElements = model.hasQuadraticMesh;
                    solver.loadType             = loadTypeMap[loadTypeSel];
                    solver.buildAxis            = buildAxis;
                    solver.forceMagnitude       = static_cast<double>(forceMagnitudeMN) * 1.0e6;
                    solver.youngsModulus        = currentMaterial.E;
                    solver.poissonRatio         = currentMaterial.nu;
                    solver.fractureStress       = currentMaterial.fractureStress;
                    // FDM anisotropic parameters (no-ops when useFdmAnisotropy=false).
                    solver.useFdmAnisotropy             = fdmOn;
                    solver.E_z                           = currentMaterial.E_z;
                    solver.nu_pz                         = currentMaterial.nu_pz;
                    solver.G_pz                          = currentMaterial.G_pz;
                    solver.fractureStress_intralayer     = currentMaterial.fractureStress_intralayer;
                    solver.fractureStress_interlayer     = currentMaterial.fractureStress_interlayer;
                    solver.fractureShear_interlayer      = currentMaterial.fractureShear_interlayer;
                    solver.solveBrittleFracture(model, 10.0f, 50);
                }
                rY += 30.0f;
            }

            if (model.hasDeformation) {
                std::string btnLabel = model.showDeformedMesh ? "SHOWING: DEFORMED" : "SHOWING: ORIGINAL";
                if (ui.button(btnLabel, rX, rY, rW, 25.0f, model.showDeformedMesh)) {
                    model.showDeformedMesh = !model.showDeformedMesh;
                    model.buildBuffers();
                }
                rY += 30.0f;

                std::string forceMapLabel = model.showAppliedForceField ? "FORCE MAP: ON" : "FORCE MAP: OFF";
                if (ui.button(forceMapLabel, rX, rY, rW, 25.0f, model.showAppliedForceField)) {
                    model.showAppliedForceField = !model.showAppliedForceField;
                }
                rY += 30.0f;

                char forceBuffer[64];
                snprintf(forceBuffer, sizeof(forceBuffer), "TOTAL FORCE: %.3g N", model.totalAppliedForce);
                ui.drawText(forceBuffer, rX, rY, 8.5f, glm::vec3(0.95f));
                rY += 18.0f;
                snprintf(forceBuffer, sizeof(forceBuffer), "NODE FORCE: %.3g N", model.appliedForcePerNode);
                ui.drawText(forceBuffer, rX, rY, 8.5f, glm::vec3(0.95f));
                rY += 22.0f;

                int activeScalarMode = model.getActiveScalarMode();
                if (activeScalarMode != 0) {
                    std::string legendTitle = activeScalarMode == 2 ? "FORCE MAG (N)" : "DEFORM MAG (m)";
                    float scalarMin = model.getActiveScalarMin();
                    float scalarMax = model.getActiveScalarMax();
                    float legendBarX = rX + rW - 24.0f;
                    float legendBarY = rY + 12.0f;
                    float legendBarW = 18.0f;
                    float legendBarH = 108.0f;
                    ui.drawText(legendTitle, rX, rY, 8.5f, glm::vec3(0.65f, 0.85f, 1.0f));

                    for (int i = 0; i < 48; ++i) {
                        float t0 = (float)i / 48.0f;
                        float t1 = (float)(i + 1) / 48.0f;
                        float segY = legendBarY + legendBarH * (1.0f - t1);
                        ui.drawRect(legendBarX, segY, legendBarW, legendBarH / 48.0f + 1.0f, contourColor(t0));
                    }

                    ui.drawLine(legendBarX, legendBarY, legendBarX + legendBarW, legendBarY, glm::vec3(0.9f), 1.0f);
                    ui.drawLine(legendBarX + legendBarW, legendBarY, legendBarX + legendBarW, legendBarY + legendBarH, glm::vec3(0.9f), 1.0f);
                    ui.drawLine(legendBarX + legendBarW, legendBarY + legendBarH, legendBarX, legendBarY + legendBarH, glm::vec3(0.9f), 1.0f);
                    ui.drawLine(legendBarX, legendBarY + legendBarH, legendBarX, legendBarY, glm::vec3(0.9f), 1.0f);

                    float cubeX = rX + 18.0f;
                    float cubeY = legendBarY + 10.0f;
                    float cubeSize = 42.0f;
                    float cubeDepth = 12.0f;
                    int cubeSlices = 14;
                    for (int i = 0; i < cubeSlices; ++i) {
                        float t = (float)i / (float)(cubeSlices - 1);
                        float offset = (1.0f - t) * cubeDepth;
                        float sliceH = cubeSize / (float)cubeSlices;
                        float sliceY = cubeY + (cubeSlices - 1 - i) * sliceH;
                        ui.drawRect(cubeX + offset, sliceY - offset * 0.45f, cubeSize, sliceH + 1.0f, contourColor(1.0f - t));
                    }

                    float bx = cubeX;
                    float by = cubeY;
                    float fx = cubeX + cubeDepth;
                    float fy = cubeY - cubeDepth * 0.45f;
                    float s = cubeSize;
                    glm::vec3 cubeLineColor(0.92f, 0.92f, 0.92f);
                    ui.drawLine(bx, by, bx + s, by, cubeLineColor, 1.0f);
                    ui.drawLine(bx + s, by, bx + s, by + s, cubeLineColor, 1.0f);
                    ui.drawLine(bx + s, by + s, bx, by + s, cubeLineColor, 1.0f);
                    ui.drawLine(bx, by + s, bx, by, cubeLineColor, 1.0f);
                    ui.drawLine(fx, fy, fx + s, fy, cubeLineColor, 1.0f);
                    ui.drawLine(fx + s, fy, fx + s, fy + s, cubeLineColor, 1.0f);
                    ui.drawLine(fx + s, fy + s, fx, fy + s, cubeLineColor, 1.0f);
                    ui.drawLine(fx, fy + s, fx, fy, cubeLineColor, 1.0f);
                    ui.drawLine(bx, by, fx, fy, cubeLineColor, 1.0f);
                    ui.drawLine(bx + s, by, fx + s, fy, cubeLineColor, 1.0f);
                    ui.drawLine(bx + s, by + s, fx + s, fy + s, cubeLineColor, 1.0f);
                    ui.drawLine(bx, by + s, fx, fy + s, cubeLineColor, 1.0f);
                    ui.drawText("REF CUBE", rX, legendBarY + legendBarH + 22.0f, 7.5f, glm::vec3(0.85f));

                    char legendValue[64];
                    float scalarMid = 0.5f * (scalarMin + scalarMax);
                    snprintf(legendValue, sizeof(legendValue), "%.3f", scalarMax);
                    ui.drawText(legendValue, legendBarX - 72.0f, legendBarY - 2.0f, 7.8f, glm::vec3(0.9f));
                    snprintf(legendValue, sizeof(legendValue), "%.3f", scalarMid);
                    ui.drawText(legendValue, legendBarX - 72.0f, legendBarY + legendBarH * 0.5f - 4.0f, 7.8f, glm::vec3(0.9f));
                    snprintf(legendValue, sizeof(legendValue), "%.3f", scalarMin);
                    ui.drawText(legendValue, legendBarX - 72.0f, legendBarY + legendBarH - 6.0f, 7.8f, glm::vec3(0.9f));
                }
            }
        }

        static bool showReadme = false;
        if (ui.button("README", 10.0f, 10.0f, 80.0f, 30.0f, showReadme)) {
            showReadme = !showReadme;
        }

        if (showReadme) {
            float rw = 520.0f;
            float rh = 540.0f;
            float rx = 10.0f;
            float ry = 50.0f;
            ui.drawRect(rx, ry, rw, rh, glm::vec3(0.12f, 0.12f, 0.15f));
            ui.drawRect(rx, ry, 2.0f, rh, glm::vec3(0.3f, 0.6f, 0.9f));

            float textY = ry + 20.0f;
            float textX = rx + 20.0f;
            ui.drawText("FUNCTIONALITY README", textX, textY, 12.0f, glm::vec3(0.5f, 0.8f, 1.0f)); textY += 30.0f;

            auto drawHelp = [&](const std::string& name, const std::string& desc) {
                ui.drawText(name, textX, textY, 9.5f, glm::vec3(0.9f, 0.9f, 0.6f)); textY += 18.0f;
                
                std::string currentLine = "";
                float maxW = rw - 40.0f;
                float charW = 8.5f * 1.2f;
                int charsPerLine = static_cast<int>(maxW / charW);
                
                std::vector<std::string> words;
                size_t pos = 0, found;
                while((found = desc.find_first_of(' ', pos)) != std::string::npos) {
                    words.push_back(desc.substr(pos, found - pos));
                    pos = found + 1;
                }
                words.push_back(desc.substr(pos));

                for (const auto& w : words) {
                    if (currentLine.empty()) {
                        currentLine = w;
                    } else if ((currentLine.length() + 1 + w.length()) <= charsPerLine) {
                        currentLine += " " + w;
                    } else {
                        ui.drawText(currentLine, textX + 12.0f, textY, 8.5f, glm::vec3(0.8f, 0.8f, 0.8f));
                        textY += 15.0f;
                        currentLine = w;
                    }
                }
                if (!currentLine.empty()) {
                    ui.drawText(currentLine, textX + 12.0f, textY, 8.5f, glm::vec3(0.8f, 0.8f, 0.8f));
                    textY += 22.0f;
                }
            };

            drawHelp("VERTEX SMOOTHING", "Smooths problematic curved surface topologies before 3D meshing.");
            drawHelp("MESH QUALITY / MAX VOL", "Controls tetrahedral aspect ratio and max volume constraints.");
            drawHelp("SURFACE / VOLUME MESH", "Toggles between 2D boundary and 3D volumetric mesh views.");
            drawHelp("MULTITHREADING / GPU ACCEL", "Enables CPU OpenMP or CUDA GPU acceleration for solvers.");
            drawHelp("LINEAR STATIC FEA", "Runs small-strain linear structural analysis.");
            drawHelp("NONLINEAR FEA (NR)", "Runs large-deformation analysis using Newton-Raphson iteration.");
            drawHelp("ADAPTIVE FEA", "Uses quadratic elements in high-curvature regions for accuracy.");
            drawHelp("SHOWING: DEFORMED", "Toggles visualization of the post-simulation deformed structure.");
            drawHelp("FORCE MAP / REF CUBE", "Visualizes applied external force vectors and value contours.");
        }

        prevMousePressed = mousePressed;
        glEnable(GL_DEPTH_TEST);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwTerminate(); return 0;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

    static bool mPressed = false;
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        if (!mPressed) { showWireframe = !showWireframe; mPressed = true; }
    }
    else mPressed = false;
}

bool rightMousePressed = false;
bool middleMousePressed = false;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) mousePressed = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) rightMousePressed = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) middleMousePressed = (action == GLFW_PRESS);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn); float ypos = static_cast<float>(yposIn);
    mouseX = xpos; mouseY = ypos;
    
    // UI Panel override: if hovering over panel, consume drag inputs
    if (mouseX >= scrWidth - panelWidth) {
        lastX = xpos; lastY = ypos;
        return;
    }

    float xoffset = xpos - lastX; float yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;

    if (rightMousePressed) camera.ProcessMouseOrbit(xoffset, yoffset);
    if (middleMousePressed) camera.ProcessMousePan(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (mouseX >= scrWidth - panelWidth) return;
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}
