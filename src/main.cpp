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
#include "LayerSlicer.h"   // new_TODO_04: SLICE controls call the same free fns
#include "SlabMesher.h"    // new_TODO_19E: MESH button routes gcode models here
#include "ScenarioRunner.h"

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
int modelListPage = 0;          // current page index for the model file list
static constexpr int kModelsPerPage = 6;

namespace fs = std::filesystem;

// new_TODO_19E: the file panel reads TWO folders — "." (classic design models,
// staged flat) and "gcode_models/" (Bambu sliced exports + showcase defaults).
// Entries keep their relative path (loadFile uses it); the UI displays the
// bare filename with a GCODE badge for *.gcode.3mf.
void scanForModels() {
    modelFiles.clear();
    auto scanDir = [&](const std::string& dir, bool prefixed) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            std::string extLow = entry.path().extension().string();
            std::transform(extLow.begin(), extLow.end(), extLow.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (extLow == ".stl" || extLow == ".3mf" || extLow == ".step" || extLow == ".stp") {
                std::string name = entry.path().filename().string();
                modelFiles.push_back(prefixed ? dir + "/" + name : name);
            }
        }
    };
    scanDir(".", false);
    scanDir("gcode_models", true);
    std::sort(modelFiles.begin(), modelFiles.end());
}

// new_TODO_19E: showcase defaults (per-gcode-model calibrated load magnitude
// from the 19D acceptance runs; 0 for uncalibrated models). Loaded once from
// gcode_models/showcase_defaults.json (tiny hand parser — same key=value
// robustness philosophy as the .mat loader; the file is machine-written).
struct ShowcaseDefaults {
    std::string load = "pullZ";     // pull axis preset name
    double magN = 0.0;              // calibrated force (N); 0 = none stored
    int    maxSlabs = 12;
    float  targetEdgeMM = -1.0f;
    bool   found = false;
};
static ShowcaseDefaults showcaseDefaultsFor(const std::string& fileName) {
    ShowcaseDefaults d;
    std::ifstream f("gcode_models/showcase_defaults.json");
    if (!f.is_open()) return d;
    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    std::string base = fs::path(fileName).filename().string();
    size_t k = all.find("\"" + base + "\"");
    if (k == std::string::npos) return d;
    size_t end = all.find('}', k);
    std::string blk = all.substr(k, end == std::string::npos ? std::string::npos
                                                             : end - k);
    auto grabNum = [&](const char* key, double fallback) -> double {
        size_t p = blk.find(std::string("\"") + key + "\"");
        if (p == std::string::npos) return fallback;
        p = blk.find(':', p);
        if (p == std::string::npos) return fallback;
        try { return std::stod(blk.substr(p + 1)); } catch (...) { return fallback; }
    };
    size_t lp = blk.find("\"load\"");
    if (lp != std::string::npos) {
        size_t q1 = blk.find('"', blk.find(':', lp));
        size_t q2 = (q1 == std::string::npos) ? q1 : blk.find('"', q1 + 1);
        if (q2 != std::string::npos) d.load = blk.substr(q1 + 1, q2 - q1 - 1);
    }
    d.magN         = grabNum("magN", 0.0);
    d.maxSlabs     = static_cast<int>(grabNum("maxSlabs", 12.0));
    d.targetEdgeMM = static_cast<float>(grabNum("targetEdgeMM", -1.0));
    d.found = true;
    return d;
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

// new_TODO_19E: showcase state — magnitude typed by the user (N / N*m), the
// defaults record for the loaded gcode model, and the keyboard input queue
// (filled by the GLFW char/key callbacks below main()).
static ShowcaseDefaults showcaseCfg;
static std::string showcaseMagText   = "0";
static bool        showcaseMagFocused = false;
static std::string g_charInput;
static bool        g_backspace = false, g_enter = false;

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
void char_callback(GLFWwindow* window, unsigned int codepoint);  // new_TODO_19E
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
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

int runInteractive() {
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
    glfwSetCharCallback(window, char_callback);   // new_TODO_19E magnitude field
    glfwSetKeyCallback(window, key_callback);
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
        model.drawSlicePreview(schematicShader); // new_TODO_04: section overlay
        model.drawForceArrows(schematicShader);  // new_TODO_19E: load arrows

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
            modelListPage = 0;
        }
        lY += 30.0f;

        if (currentMode == MODE_IMPORT) {
            if (modelFiles.empty()) {
                ui.drawText("NO MODELS FOUND", lX, lY, 9.0f, glm::vec3(1.0f, 0.2f, 0.2f)); lY += 15.0f;
                ui.drawText("(place .stl / .3mf / .step here)", lX, lY, 7.5f, glm::vec3(0.5f)); lY += 20.0f;
            } else {
                int totalPages = ((int)modelFiles.size() + kModelsPerPage - 1) / kModelsPerPage;
                // Clamp page in case the file list shrank after a rescan
                if (modelListPage >= totalPages) modelListPage = totalPages - 1;
                if (modelListPage < 0)           modelListPage = 0;

                int firstIdx = modelListPage * kModelsPerPage;
                int lastIdx  = std::min(firstIdx + kModelsPerPage, (int)modelFiles.size());

                for (int i = firstIdx; i < lastIdx; ++i) {
                    const std::string& file = modelFiles[i];
                    std::string display = fs::path(file).filename().string();
                    bool isActive = !model.loadedFileName.empty() &&
                                    (file == model.loadedFileName ||
                                     display == fs::path(model.loadedFileName).filename().string());

                    std::string nameLow = display;
                    std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(),
                                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    std::string extLow = fs::path(display).extension().string();
                    std::transform(extLow.begin(), extLow.end(), extLow.begin(),
                                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    bool isGcode = nameLow.size() > 10 &&
                                   nameLow.compare(nameLow.size() - 10, 10, ".gcode.3mf") == 0;
                    bool is3MF  = !isGcode && (extLow == ".3mf");
                    bool isSTEP = (extLow == ".step" || extLow == ".stp");
                    bool hasBadge = is3MF || isSTEP || isGcode;

                    if (ui.button(display, lX, lY, lW - (hasBadge ? 46.0f : 0.0f), 22.0f, isActive)) {
                        if (model.loadFile(file)) {
                            camera.OrbitTarget = glm::vec3(0.0f);
                            camera.OrbitRadius = 5.0f;
                            if (isGcode) {
                                // new_TODO_19E: pull the stored showcase default
                                // (calibrated magnitude etc.) and auto-select the
                                // PLA card — the gcode showcase is FDM physics.
                                showcaseCfg = showcaseDefaultsFor(display);
                                char b[32];
                                snprintf(b, sizeof(b), "%.0f", showcaseCfg.magN);
                                showcaseMagText = b;
                                showcaseMagFocused = false;
                                if (loadMaterialFile("materials/pla.mat", currentMaterial))
                                    activeMaterialFile = "materials/pla.mat";
                            }
                        }
                    }
                    if (isGcode) {
                        ui.drawRect(lX + lW - 44.0f, lY + 2.0f, 42.0f, 18.0f,
                                    glm::vec3(0.35f, 0.15f, 0.45f));
                        ui.drawText("GCODE", lX + lW - 42.0f, lY + 6.0f, 6.5f,
                                    glm::vec3(0.95f, 0.6f, 1.0f));
                    }
                    if (is3MF) {
                        ui.drawRect(lX + lW - 44.0f, lY + 2.0f, 42.0f, 18.0f,
                                    glm::vec3(0.05f, 0.45f, 0.45f));
                        ui.drawText("3MF", lX + lW - 39.0f, lY + 6.0f, 7.5f,
                                    glm::vec3(0.4f, 1.0f, 0.95f));
                    }
                    if (isSTEP) {
                        ui.drawRect(lX + lW - 44.0f, lY + 2.0f, 42.0f, 18.0f,
                                    glm::vec3(0.45f, 0.35f, 0.05f));
                        ui.drawText("STEP", lX + lW - 40.0f, lY + 6.0f, 7.5f,
                                    glm::vec3(1.0f, 0.85f, 0.3f));
                    }
                    lY += 30.0f;
                }

                // Pagination controls (only shown when there is more than one page)
                if (totalPages > 1) {
                    lY += 4.0f;
                    float navBtnW = (lW - 8.0f) / 3.0f;
                    if (ui.button("<", lX, lY, navBtnW, 22.0f, false, modelListPage == 0)) {
                        --modelListPage;
                    }
                    // Centre page counter text
                    char pageLabel[32];
                    snprintf(pageLabel, sizeof(pageLabel), "%d / %d", modelListPage + 1, totalPages);
                    ui.drawText(pageLabel, lX + navBtnW + 4.0f + 4.0f, lY + 6.0f,
                                8.5f, glm::vec3(0.7f, 0.7f, 0.7f));
                    if (ui.button(">", lX + 2.0f * navBtnW + 8.0f, lY, navBtnW, 22.0f,
                                  false, modelListPage == totalPages - 1)) {
                        ++modelListPage;
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
                    if (model.lastLoadedFormat == "STEP" && model.hasBRep()) {
                        snprintf(metaBuf, sizeof(metaBuf), "Source: STEP (B-rep retained)");
                        ui.drawText(metaBuf, lX, lY, 8.0f, glm::vec3(1.0f, 0.85f, 0.3f)); lY += 14.0f;
                    } else {
                        snprintf(metaBuf, sizeof(metaBuf),
                                 "FORMAT: %s", model.lastLoadedFormat.c_str());
                        ui.drawText(metaBuf, lX, lY, 8.0f, glm::vec3(0.4f, 0.9f, 0.85f)); lY += 14.0f;
                    }
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

        // --- new_TODO_04: SLICE controls (shared by CUBE + IMPORT, handoff 5.4) ---
        // The SLICE PREVIEW button calls the EXACT free functions the headless
        // harness calls in ScenarioRunner::runSlice (LayerSlicer::computeSlices +
        // model.setLayerStack + sectionToSegments + model.buildSlicePreview).
        static LayerSlicer::SliceResult sliceResult; // UI-local cache (decoupled)
        static LayerSlicer::SliceGrouping sliceGrp;   // new_TODO_04C: physical readout
        static int   slicePreviewLayer = 0;
        static float sliceMaxSlabsF    = 40.0f;
        auto rebuildSlicePreview = [&](int layer) {
            int nL = static_cast<int>(sliceResult.sections.size());
            if (nL == 0) { model.showSlicePreview = false; return; }
            slicePreviewLayer = std::max(0, std::min(layer, nL - 1));
            auto segs = LayerSlicer::sectionToSegments(
                sliceResult.sections[slicePreviewLayer], sliceResult.buildAxis,
                sliceResult.planeCoords[slicePreviewLayer]);
            model.buildSlicePreview(segs);     // [same-path: harness buildSlicePreview]
            model.showSlicePreview = true;
        };
        auto drawSliceControls = [&](float x, float& y, float w) {
            ui.drawRect(x, y, w, 1.5f, glm::vec3(0.3f)); y += 10.0f;
            std::string sl = model.params.enableLayerSlicing ? "SLICE: ON" : "SLICE: OFF";
            if (ui.button(sl, x, y, w, 22.0f, model.params.enableLayerSlicing)) {
                model.params.enableLayerSlicing = !model.params.enableLayerSlicing;
                if (!model.params.enableLayerSlicing) model.showSlicePreview = false;
            }
            y += 28.0f;
            if (!model.params.enableLayerSlicing) return;

            ui.slider("LAYER THICK", model.params.layerThickness, 0.01f, 1.0f, x, y, w, 15.0f); y += 20.0f;
            float bw = w / 3.0f - 3.0f;
            if (ui.button("AXIS X", x, y, bw, 20.0f, model.params.buildAxisSel == 0)) model.params.buildAxisSel = 0;
            if (ui.button("AXIS Y", x + bw + 4.0f, y, bw, 20.0f, model.params.buildAxisSel == 1)) model.params.buildAxisSel = 1;
            if (ui.button("AXIS Z", x + 2.0f * (bw + 4.0f), y, bw, 20.0f, model.params.buildAxisSel == 2)) model.params.buildAxisSel = 2;
            y += 26.0f;
            ui.slider("MAX SLABS", sliceMaxSlabsF, 2.0f, 200.0f, x, y, w, 15.0f);
            model.params.maxSlabs = static_cast<int>(sliceMaxSlabsF + 0.5f); y += 20.0f;
            ui.slider("WALL WIDTH", model.params.wallWidth, 0.05f, 2.0f, x, y, w, 15.0f); y += 20.0f;

            if (ui.button("SLICE PREVIEW", x, y, w, 28.0f)) {
                LayerSlicer::SliceGrouping grp;
                std::vector<LayerSlicer::PlaneStats> stats;
                const BRepHandle* brep = model.hasBRep() ? model.brep.get() : nullptr;
                // [same-path: harness LayerSlicer::computeSlices]
                sliceResult = LayerSlicer::computeSlices(
                    model.surfaceVertices, model.surfaceIndices,
                    model.currentMinBounds, model.currentMaxBounds,
                    model.params, model.modelToMM, brep, grp, stats);
                sliceGrp = grp; // new_TODO_04C: cache for the physical readout
                // [same-path: harness model.setLayerStack]
                model.setLayerStack(LayerSlicer::axisFromParams(model.params),
                                    grp.physicalLayerThickness, grp.layersPerSlab,
                                    grp.slabBoundaries);
                rebuildSlicePreview(static_cast<int>(sliceResult.sections.size()) / 2);
            }
            y += 32.0f;

            // new_TODO_04C: physical (real-world) readout — real mm dimensions and
            // the true layer count, so the print is physically readable.
            {
                glm::vec3 mm = model.physicalSizeMM();
                char dbuf[128];
                snprintf(dbuf, sizeof(dbuf), "PART %.1f x %.1f x %.1f mm", mm.x, mm.y, mm.z);
                ui.drawText(dbuf, x, y, 8.0f, glm::vec3(0.7f, 0.9f, 1.0f)); y += 14.0f;
                if (sliceGrp.nPhysical > 0) {
                    char lbuf[128];
                    snprintf(lbuf, sizeof(lbuf), "%d layers @ %.2f mm  (%d slabs, k=%d)",
                             sliceGrp.nPhysical, sliceGrp.physThickMM, sliceGrp.nSlabs,
                             sliceGrp.layersPerSlab);
                    ui.drawText(lbuf, x, y, 8.0f, glm::vec3(0.7f, 0.9f, 1.0f)); y += 16.0f;
                }
            }

            int nL = static_cast<int>(sliceResult.sections.size());
            if (model.showSlicePreview && nL > 0) {
                float layerF = static_cast<float>(slicePreviewLayer);
                if (ui.slider("LAYER", layerF, 0.0f, static_cast<float>(nL - 1), x, y, w, 15.0f))
                    rebuildSlicePreview(static_cast<int>(layerF + 0.5f));
                y += 20.0f;
                char sbuf[96];
                snprintf(sbuf, sizeof(sbuf), "SLAB %d/%d  loops:%d", slicePreviewLayer + 1, nL,
                         static_cast<int>(sliceResult.sections[slicePreviewLayer].size()));
                ui.drawText(sbuf, x, y, 8.0f, glm::vec3(1.0f, 0.7f, 0.3f)); y += 16.0f;
            }
        };

        if (currentMode == MODE_CUBE) {
            if (ui.slider("X LENGTH (m)", model.params.sizeX, 0.1f, 10.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("Y LENGTH (m)", model.params.sizeY, 0.1f, 5.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("Z LENGTH (m)", model.params.sizeZ, 0.1f, 5.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            if (ui.slider("SUBDIVISIONS", model.params.subdivisions, 1.0f, 20.0f, rX, rY, rW, 20.0f)) model.needsUpdate = true; rY += 40.0f;
            ui.slider("MESH QUALITY (p)", model.params.tetRadiusEdge, 1.1f, 3.0f, rX, rY, rW, 15.0f); rY += 20.0f;
            ui.slider("MAX VOLUME (%)", model.params.maxVolPercent, 0.00001f, 0.2f, rX, rY, rW, 15.0f, true); rY += 20.0f;
            if (ui.button("GENERATE 3D MESH", rX, rY, rW, 35.0f)) {
                std::cout << "Button Clicked: Launching TetGen..." << std::endl;
                model.generateVolumetricMesh();
            }
            rY += 40.0f;
            drawSliceControls(rX, rY, rW); // new_TODO_04: SLICE block (CUBE)
        }
        else if (currentMode == MODE_IMPORT) {
            std::string prLabel = model.params.enablePolarRemoval ? "VERTEX SMOOTHING: ON" : "VERTEX SMOOTHING: OFF";
            if (ui.button(prLabel, rX, rY, rW, 20.0f, model.params.enablePolarRemoval)) {
                model.params.enablePolarRemoval = !model.params.enablePolarRemoval;
                if (!model.loadedFileName.empty()) model.loadFile(model.loadedFileName);
            }
            rY += 25.0f;

            ui.slider("MESH QUALITY (p)", model.params.tetRadiusEdge, 1.1f, 3.0f, rX, rY, rW, 15.0f); rY += 20.0f;
            ui.slider("MAX VOLUME (%)", model.params.maxVolPercent, 0.00001f, 0.2f, rX, rY, rW, 15.0f, true); rY += 20.0f;

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
                if (model.hasToolpath()) {
                    // new_TODO_19E: gcode models mesh through the toolpath lane
                    // — the EXACT functions the harness calls.
                    // [same-path: mesh.method="toolpath" in ScenarioRunner]
                    std::cout << "Button Clicked: toolpath sections + slab mesh..." << std::endl;
                    ToolpathSections::Options topts;
                    ToolpathSections::LayerSections secs;
                    std::string terr;
                    if (ToolpathSections::build(*model.toolpath, topts, secs, terr)) {
                        if (!model.hasLayerStack())
                            model.layers = std::make_unique<LayerStack>();
                        SlabMesher::ToolpathMeshOptions mo;
                        mo.maxSlabs     = showcaseCfg.found ? showcaseCfg.maxSlabs : 12;
                        mo.targetEdgeMM = showcaseCfg.found ? showcaseCfg.targetEdgeMM : -1.0f;
                        SlabMesher::meshToolpathSlabs(secs, mo, model);
                    } else {
                        std::cout << "[GCODE] section build failed: " << terr << std::endl;
                    }
                } else {
                    std::cout << "Button Clicked: Launching TetGen..." << std::endl;
                    model.generateVolumetricMesh();
                }
            }
            rY += 40.0f;

            // ===== new_TODO_19E: GCODE SHOWCASE panel =====
            // Workflow: pick gcode model -> GENERATE 3D MESH -> type/accept the
            // magnitude -> RUN -> color-spectrum result + 3-D load arrows.
            if (model.hasToolpath()) {
                ui.drawRect(rX, rY, rW, 1.5f, glm::vec3(0.35f, 0.2f, 0.4f)); rY += 8.0f;
                ui.drawText("GCODE SHOWCASE", rX, rY, 9.5f, glm::vec3(0.95f, 0.6f, 1.0f)); rY += 16.0f;
                char lbuf[96];
                snprintf(lbuf, sizeof(lbuf), "LOAD: %s  (force in N)",
                         showcaseCfg.load.c_str());
                ui.drawText(lbuf, rX, rY, 8.0f, glm::vec3(0.8f)); rY += 14.0f;
                if (!showcaseCfg.found || showcaseCfg.magN <= 0.0)
                    { ui.drawText("no stored default for this model (enter a value)",
                                  rX, rY, 7.0f, glm::vec3(0.6f)); rY += 12.0f; }

                // Typed magnitude field: click to focus, digits/'.'/'-' typed,
                // BACKSPACE deletes, ENTER commits.
                std::string fieldLabel = "MAG: " + showcaseMagText +
                                         (showcaseMagFocused ? "_" : "") + " N";
                if (ui.button(fieldLabel, rX, rY, rW * 0.62f, 24.0f, showcaseMagFocused)) {
                    showcaseMagFocused = !showcaseMagFocused;
                    g_charInput.clear(); g_backspace = false; g_enter = false;
                }
                if (ui.button("DEFAULT", rX + rW * 0.65f, rY, rW * 0.35f, 24.0f)) {
                    char b[32]; snprintf(b, sizeof(b), "%.0f", showcaseCfg.magN);
                    showcaseMagText = b;
                }
                rY += 30.0f;
                if (showcaseMagFocused) {
                    for (char c : g_charInput)
                        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-')
                            if (showcaseMagText.size() < 12) showcaseMagText.push_back(c);
                    g_charInput.clear();
                    if (g_backspace) {
                        if (!showcaseMagText.empty()) showcaseMagText.pop_back();
                        g_backspace = false;
                    }
                    if (g_enter) { showcaseMagFocused = false; g_enter = false; }
                } else {
                    g_charInput.clear(); g_backspace = false; g_enter = false;
                }

                if (ui.button("RUN SHOWCASE FEA (FRACTURE)", rX, rY, rW, 30.0f,
                              false, !model.hasVolumetricMesh)) {
                    double mag = 0.0;
                    try { mag = std::stod(showcaseMagText); } catch (...) {}
                    if (mag > 0.0 && model.hasVolumetricMesh) {
                        FEASolver solver;
                        // Map "pullX|pullY|pullZ" -> FacePull + axis (the same
                        // preset the frozen 19D scenarios use).
                        char ax = showcaseCfg.load.empty() ? 'Z' : showcaseCfg.load.back();
                        solver.loadType = FEASolver::LoadType::FacePull;
                        solver.faceAxis = (ax=='X'||ax=='x') ? 0 : (ax=='Y'||ax=='y') ? 1 : 2;
                        solver.buildAxis = 2;                 // gcode layers are always +Z
                        solver.useQuadraticElements = false;  // fracture path = Tet4
                        solver.useMultithreading = useMultithreading;
                        solver.forceMagnitude = mag;
                        solver.youngsModulus  = currentMaterial.E;
                        solver.poissonRatio   = currentMaterial.nu;
                        solver.fractureStress = currentMaterial.fractureStress;
                        if (currentMaterial.E_z > 0.0) {
                            solver.useFdmAnisotropy = true;
                            solver.E_z   = currentMaterial.E_z;
                            solver.nu_pz = currentMaterial.nu_pz;
                            solver.G_pz  = currentMaterial.G_pz;
                            solver.fractureStress_intralayer = currentMaterial.fractureStress_intralayer;
                            solver.fractureStress_interlayer = currentMaterial.fractureStress_interlayer;
                            solver.fractureShear_interlayer  = currentMaterial.fractureShear_interlayer;
                        }
                        model.elementAlive.clear();
                        model.elementFailureIter.clear();
                        model.elementFailureMode.clear();
                        solver.solveBrittleFracture(model, 10.0f, 14); // [same-path: BRITTLE FRACTURE]
                        model.showAppliedForceField = true;  // 3-D load arrows on
                    } else {
                        std::cout << "[SHOWCASE] enter a positive magnitude (N) first"
                                  << std::endl;
                    }
                }
                rY += 36.0f;
            }

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

                // new_TODO_03: fracture result controls (only when a fracture run
                // has produced per-element failure data).
                const bool hasFracture = !model.elementFailureMode.empty();
                if (hasFracture) {
                    const char* fvName =
                        (model.fractureViewMode == 3) ? "MODE" :
                        (model.fractureViewMode == 4) ? "CRACK ORDER" :
                        (model.fractureViewMode == 5) ? "STRESS" : "DEFORM";
                    std::string fvLabel = std::string("FRACTURE VIEW: ") + fvName;
                    if (ui.button(fvLabel, rX, rY, rW, 25.0f, model.fractureViewMode != 1)) {
                        model.fractureViewMode =
                            (model.fractureViewMode == 1) ? 3 :
                            (model.fractureViewMode == 3) ? 4 :
                            (model.fractureViewMode == 4) ? 5 : 1;
                        model.buildBuffers();
                    }
                    rY += 30.0f;

                    const char* dvNames[3] = {"HIDDEN", "GHOST", "COLORED"};
                    std::string dvLabel = std::string("DEAD ELEMS: ") + dvNames[(int)model.fractureDeadView];
                    if (ui.button(dvLabel, rX, rY, rW, 25.0f, model.fractureDeadView != FEAModel::DEAD_HIDDEN)) {
                        model.fractureDeadView =
                            (FEAModel::FractureDeadView)(((int)model.fractureDeadView + 1) % 3);
                        model.buildBuffers();
                    }
                    rY += 30.0f;
                }

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

                // new_TODO_03: fracture legends for the per-element views. The
                // categorical legend mirrors the shader's categoricalColor() exactly.
                if (hasFracture && model.fractureDeadView == FEAModel::DEAD_COLORED) {
                    if (model.fractureViewMode == 3 || model.fractureViewMode == 1) {
                        ui.drawText("FAILURE MODE", rX, rY, 8.5f, glm::vec3(0.65f, 0.85f, 1.0f));
                        rY += 16.0f;
                        struct LegItem { glm::vec3 c; const char* t; };
                        const LegItem items[3] = {
                            { glm::vec3(0.85f, 0.05f, 0.05f), "INTERLAYER TENSION" },
                            { glm::vec3(1.00f, 0.55f, 0.00f), "INTERLAYER SHEAR" },
                            { glm::vec3(1.00f, 0.92f, 0.10f), "INTRALAYER" } };
                        for (int i = 0; i < 3; ++i) {
                            ui.drawRect(rX, rY, 16.0f, 12.0f, items[i].c);
                            ui.drawText(items[i].t, rX + 22.0f, rY + 2.0f, 7.8f, glm::vec3(0.9f));
                            rY += 16.0f;
                        }
                    } else if (model.fractureViewMode == 4 || model.fractureViewMode == 5) {
                        const char* title = (model.fractureViewMode == 4)
                            ? "CRACK ORDER (iter)" : "STRESS AT DEATH (Pa)";
                        ui.drawText(title, rX, rY, 8.5f, glm::vec3(0.65f, 0.85f, 1.0f));
                        rY += 16.0f;
                        float barW = rW - 12.0f, barH = 14.0f;
                        for (int i = 0; i < 48; ++i) {
                            float t = (float)i / 48.0f;
                            ui.drawRect(rX + barW * t, rY, barW / 48.0f + 1.0f, barH, contourColor(t));
                        }
                        rY += barH + 4.0f;
                        char buf[80];
                        snprintf(buf, sizeof(buf), "%.3g  ..  %.3g",
                                 model.deadScalarMin, model.deadScalarMax);
                        ui.drawText(buf, rX, rY, 7.8f, glm::vec3(0.9f));
                        rY += 16.0f;
                    }
                }
            }
            drawSliceControls(rX, rY, rW); // new_TODO_04: SLICE block (IMPORT)
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

// new_TODO_19E: keyboard input for the showcase magnitude field. Characters
// queue up in g_charInput; the field consumes them only while focused, so
// typing never leaks into camera controls.
void char_callback(GLFWwindow*, unsigned int codepoint) {
    if (codepoint < 128) g_charInput.push_back(static_cast<char>(codepoint));
}
void key_callback(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_BACKSPACE) g_backspace = true;
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_enter = true;
    }
}

// =============================================================================
//  CLI dispatch (new_TODO_02). No args -> interactive UI. Otherwise headless
//  scenario harness. Same pipeline functions as the UI buttons (TOP RULE).
//    FEAPreProcessor --run scenarios/x.json --out report.json --shots shots/
//    FEAPreProcessor --regress all
//    FEAPreProcessor --dump-ui            (stub: prints "{}" until new_TODO_16)
// =============================================================================
// -----------------------------------------------------------------------------
// The harness resolves scenarios/, materials/, assets and STL fixtures relative
// to the working directory, and the build stages all of them next to the exe.
// Launched from anywhere else (e.g. the repo root) the run died with exit 2 on
// missing files, which made "is this TODO actually green?" fail for the wrong
// reason. If the CWD lacks the harness data but the exe's directory has it,
// switch there. Caller-supplied paths are pinned to the original CWD first so
// reports/screenshots still land where the caller expects.
// Self-check oracle (2026-07-01): `--regress all` from the repo root must print
// the same PASS table and exit code as a run started inside build/.
// -----------------------------------------------------------------------------
static void ensureHarnessCwd(const char* argv0,
                             const std::vector<std::string*>& callerPaths) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists("scenarios", ec) && fs::exists("materials", ec)) return;
    fs::path exe = fs::absolute(fs::path(argv0), ec);
    if (ec) return;
    fs::path dir = exe.parent_path();
    if (!(fs::exists(dir / "scenarios", ec) && fs::exists(dir / "materials", ec)))
        return;  // exe dir is no better -- leave CWD alone, let errors surface
    for (std::string* p : callerPaths) {
        if (!p || p->empty()) continue;
        fs::path abs = fs::absolute(*p, ec);
        if (!ec) *p = abs.string();
    }
    fs::current_path(dir, ec);
    if (!ec)
        std::cout << "[harness] data not in CWD; running from exe dir: "
                  << dir.string() << "\n";
}

static void printUsage() {
    std::cout <<
        "Usage:\n"
        "  FEAPreProcessor                       run interactive UI\n"
        "  FEAPreProcessor --run <scenario.json> [--out <report.json>] [--shots <dir>]\n"
        "  FEAPreProcessor --regress all\n"
        "  FEAPreProcessor --dump-ui\n";
}

int main(int argc, char** argv) {
    if (argc <= 1) return runInteractive();

    std::string arg1 = argv[1];

    if (arg1 == "--dump-ui") {
        // Real widget tree lands in new_TODO_16; until then emit empty object.
        std::cout << "{}" << std::endl;
        return 0;
    }

    if (arg1 == "--regress") {
        std::string which = (argc > 2) ? argv[2] : "all";
        if (which != "all") {
            std::cerr << "--regress expects 'all'\n";
            return 2;
        }
        ensureHarnessCwd(argv[0], {});
        return ScenarioRunner::runRegress();
    }

    if (arg1 == "--run") {
        std::string scenario, out = "report.json", shots = "shots";
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&](const char* flag) -> std::string {
                if (i + 1 >= argc) {
                    std::cerr << flag << " requires an argument\n";
                    std::exit(2);
                }
                return argv[++i];
            };
            if (a == "--out")        out   = next("--out");
            else if (a == "--shots") shots = next("--shots");
            else if (scenario.empty() && !a.empty() && a[0] != '-') scenario = a;
            else if (a == "--run")   scenario = next("--run");
            else { std::cerr << "unknown argument: " << a << "\n"; return 2; }
        }
        if (scenario.empty()) {
            // Allow "--run <file>" where file directly follows.
            if (argc > 2 && argv[2][0] != '-') scenario = argv[2];
        }
        if (scenario.empty()) {
            std::cerr << "--run requires a scenario path\n";
            return 2;
        }
        // Pin outputs (and the scenario, when it resolves from here) to the
        // caller's CWD before any fallback chdir.
        std::vector<std::string*> pin = { &out, &shots };
        {
            std::error_code ec;
            if (std::filesystem::exists(scenario, ec)) pin.push_back(&scenario);
        }
        ensureHarnessCwd(argv[0], pin);
        return ScenarioRunner::runScenario(scenario, out, shots);
    }

    printUsage();
    return 2;
}
