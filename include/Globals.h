#pragma once
#include <vector>
#include <string>
#include "camera.h"

enum AppMode { MODE_CUBE, MODE_IMPORT };

extern unsigned int scrWidth;
extern unsigned int scrHeight;
extern Camera camera;
extern float lastX;
extern float lastY;
extern bool firstMouse;
extern float mouseX;
extern float mouseY;
extern bool mousePressed;
extern bool prevMousePressed;
extern bool showWireframe;
extern float deltaTime;
extern float lastFrame;
extern AppMode currentMode;
extern std::vector<std::string> stlFiles;

void scanForSTLs();
