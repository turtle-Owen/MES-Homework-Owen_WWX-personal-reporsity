// 校园作业分析 · 桌面部署器 —— POSIX 窗口增强（仅主目标编译）。
//
// Linux / macOS 公共部分：窗口 hint 与自定义拖动（纯 GLFW，无原生依赖）。
// 平台特有的毛玻璃在 platform_linux.cpp（无操作）/ platform_mac.mm（Cocoa）。
#include "platform/platform.h"

#if !defined(_WIN32)

#include <GLFW/glfw3.h>

namespace mes::platform {
namespace {

GLFWwindow* gDragWindow = nullptr;
bool gDragging = false;
double gDragCursorX = 0.0;
double gDragCursorY = 0.0;
int gDragWindowX = 0;
int gDragWindowY = 0;

}  // namespace

void configureWindowHints() {
  // 必须在 glfwCreateWindow 之前调用；配合 main.cpp 的 glClearColor(0,0,0,0)
  // 实现无边框透明窗口。
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
#ifdef GLFW_SCALE_TO_MONITOR
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif
}

void setWindowIconIfAvailable(GLFWwindow* /*window*/) {
  // 无边框窗口不显示标题栏图标；设置 GLFW 图标需要额外 RGBA 资源，跳过。
}

void beginWindowDrag(GLFWwindow* window) {
  if (!window) return;
  gDragWindow = window;
  gDragging = true;
  glfwGetCursorPos(window, &gDragCursorX, &gDragCursorY);
  glfwGetWindowPos(window, &gDragWindowX, &gDragWindowY);
}

void updateWindowDrag(GLFWwindow* window) {
  if (!window || !gDragging || window != gDragWindow) return;
  double cursorX = 0.0;
  double cursorY = 0.0;
  glfwGetCursorPos(window, &cursorX, &cursorY);
  glfwSetWindowPos(window, gDragWindowX + static_cast<int>(cursorX - gDragCursorX),
                   gDragWindowY + static_cast<int>(cursorY - gDragCursorY));
  if (!glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) gDragging = false;
}

}  // namespace mes::platform

#endif  // !_WIN32
