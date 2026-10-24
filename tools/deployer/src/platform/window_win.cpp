// 校园作业分析 · 桌面部署器 —— Windows 窗口增强（圆角 / Mica / Acrylic 毛玻璃）。
//
// 仅由主目标 mes-deployer 编译（mes-deployer-test 不链接 GLFW）。
#include "platform/platform.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// dwmapi.h 的 DWMWA_* 常量随 SDK 版本变化，直接用数值并自行声明入口，
// 保证 MinGW / 老 SDK 也能编译（CMake 已链接 dwmapi）。
extern "C" HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD attribute,
                                                LPCVOID value, DWORD size);

namespace mes::platform {
namespace {

// DWM 属性号（Windows 11 SDK：33 / 38 / 20）
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
// DWMWCP_ROUND
constexpr int kDwmCornerRound = 2;
// DWMSBT_TRANSIENTWINDOW（Acrylic）/ DWMSBT_MAINWINDOW（Mica）
constexpr int kDwmBackdropAcrylic = 3;
constexpr int kDwmBackdropMica = 2;

// SetWindowCompositionAttribute 未公开，动态解析（Windows 10 回退方案）。
struct AccentPolicy {
  int state;           // ACCENT_STATE
  DWORD flags;         // ACCENT_FLAGS
  DWORD gradientColor; // AABBGGRR
  DWORD animationId;
};
struct WindowCompositionAttribData {
  DWORD attribute;
  PVOID data;
  SIZE_T size;
};
constexpr DWORD kWcaAccentPolicy = 19;  // WCA_ACCENT_POLICY
constexpr int kAccentEnableBlurBehind = 3;
constexpr int kAccentEnableAcrylicBlurBehind = 4;

using SetWindowCompositionAttributeFn =
    BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);

void applyLegacyAcrylic(HWND hwnd) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32) return;
  const FARPROC address =
      GetProcAddress(user32, "SetWindowCompositionAttribute");
  if (!address) return;
  auto setComposition = reinterpret_cast<SetWindowCompositionAttributeFn>(
      reinterpret_cast<void*>(address));
  // 浅色近白玻璃：AABBGGRR，alpha=0xB4。
  constexpr DWORD kGradient = 0xB4F3F3F3;
  AccentPolicy policy{};
  policy.state = kAccentEnableAcrylicBlurBehind;
  policy.flags = 0;
  policy.gradientColor = kGradient;
  WindowCompositionAttribData data{kWcaAccentPolicy, &policy, sizeof(policy)};
  if (setComposition(hwnd, &data)) return;
  // 1803 之前不支持 Acrylic：退化为普通模糊。
  policy.state = kAccentEnableBlurBehind;
  setComposition(hwnd, &data);
}

void applyDwmAttributes(HWND hwnd) {
  HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
  if (!dwmapi) return;
  // 圆角（Windows 11；旧系统直接失败返回，静默忽略）
  const int corner = kDwmCornerRound;
  const HRESULT cornerResult = DwmSetWindowAttribute(
      hwnd, kDwmWindowCornerPreference, &corner, sizeof(corner));
  // 系统背景材质：优先 Acrylic，再试 Mica。
  int backdrop = kDwmBackdropAcrylic;
  HRESULT backdropResult = DwmSetWindowAttribute(
      hwnd, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
  if (FAILED(backdropResult)) {
    backdrop = kDwmBackdropMica;
    backdropResult = DwmSetWindowAttribute(
        hwnd, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
  }
  // 固定浅色外观（本项目为浅色 UI）。
  const BOOL lightMode = FALSE;
  DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &lightMode,
                        sizeof(lightMode));
  FreeLibrary(dwmapi);
  // 不支持 Win11 系统背景材质时用 Win10 非公开接口做毛玻璃。
  if (FAILED(cornerResult) && FAILED(backdropResult)) applyLegacyAcrylic(hwnd);
}

// 无边框窗口的自定义拖动：用 GLFW 的窗口位置 + 光标位置计算，跨平台语义一致。
GLFWwindow* gDragWindow = nullptr;
bool gDragging = false;
double gDragCursorX = 0.0;
double gDragCursorY = 0.0;
int gDragWindowX = 0;
int gDragWindowY = 0;

}  // namespace

void configureWindowHints() {
  // 必须在 glfwCreateWindow 之前调用；配合 main.cpp 里 glClearColor(0,0,0,0)
  // 实现无边框透明窗口。
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
#ifdef GLFW_SCALE_TO_MONITOR
  // GLFW 3.4 起该 hint 标记为 deprecated，但仍可用；高 DPI 下按内容缩放。
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif
}

void styleWindow(GLFWwindow* window) {
  if (!window) return;
  applyDwmAttributes(glfwGetWin32Window(window));
}

void setWindowIconIfAvailable(GLFWwindow* /*window*/) {
  // 无边框窗口不在任务栏显示标题栏图标；GLFW 设置图标需要自行提供 RGBA
  // 像素数据，这里保持无操作（避免额外资源依赖）。
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
  if (!glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT))
    gDragging = false;
}

}  // namespace mes::platform

#endif  // _WIN32
