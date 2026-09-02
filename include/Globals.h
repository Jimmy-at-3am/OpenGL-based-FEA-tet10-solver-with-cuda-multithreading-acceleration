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
// Latched left-button press. GLFW dispatches button callbacks from inside
// glfwPollEvents(), so when a frame takes longer than the click itself (any
// compute job saturating the cores) a whole PRESS+RELEASE pair is consumed by
// one poll and `mousePressed` is already back to false when the widgets run --
// the rising edge never happens and the click is silently dropped. The latch
// records that a press occurred, and where, so exactly one widget can still
// claim it on the next frame. Cleared once per frame by the render loop.
extern bool  mouseClickLatch;
extern float mouseClickLatchX;
extern float mouseClickLatchY;
extern bool showWireframe;
extern float deltaTime;
extern float lastFrame;
extern AppMode currentMode;
extern std::vector<std::string> stlFiles;

void scanForSTLs();
