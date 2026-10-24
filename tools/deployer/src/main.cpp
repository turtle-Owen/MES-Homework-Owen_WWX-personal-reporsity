// 校园作业分析 · 桌面部署器 —— 程序入口
// 单文件桌面应用：Apple 风格 UI + 轻量毛玻璃；启动时选择本地版/服务器版。
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

// MSVC 的 <GL/gl.h> 必须晚于 <windows.h>；Linux/macOS 直接包含系统 GL 头。
#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app.h"
#include "core/core.h"
#include "core/elevated.h"
#include "platform/platform.h"
#include "ui/screens.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

// GUI 子系统下从 CI/终端启动 CLI 模式时，接管父进程控制台以输出信息
// （--version/--self-test/--elevated-action）。双击启动时不会出现控制台窗口。
void attachParentConsole() {
#if defined(_WIN32)
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
  }
#endif
}

#if defined(_WIN32)
void fatalMessageBox(const wchar_t* text) {
  MessageBoxW(nullptr, text, L"校园作业分析 · 桌面部署器",
              MB_OK | MB_ICONERROR);
}
#endif

constexpr int kWindowWidth = 1120;
constexpr int kWindowHeight = 720;
constexpr float kTitleBarHeight = 52.0f;

bool gDragging = false;
double gDragStartX = 0.0, gDragStartY = 0.0;
int gWindowStartX = 0, gWindowStartY = 0;

void glfwErrorCallback(int code, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", code,
               description ? description : "");
}

void drawWindowControls(GLFWwindow* window, float scale, bool& dragging) {
  ImGuiIO& io = ImGui::GetIO();
  const float buttonSize = 26.0f * scale;
  const float right = io.DisplaySize.x - 18.0f * scale;
  const float top = 13.0f * scale;
  ImDrawList* draw = ImGui::GetForegroundDrawList();

  ImGui::SetCursorScreenPos(ImVec2(right - buttonSize, top));
  ImGui::InvisibleButton("##window-close", ImVec2(buttonSize, buttonSize));
  const bool hoverClose = ImGui::IsItemHovered();
  const ImVec2 closeMin = ImGui::GetItemRectMin();
  const ImVec2 closeMax = ImGui::GetItemRectMax();
  draw->AddCircleFilled(
      ImVec2((closeMin.x + closeMax.x) * 0.5f, (closeMin.y + closeMax.y) * 0.5f),
      buttonSize * 0.5f,
      hoverClose ? IM_COL32(255, 95, 87, 255) : IM_COL32(255, 111, 97, 235));
  if (hoverClose) {
    draw->AddLine(
        ImVec2(closeMin.x + 8.0f * scale, closeMin.y + 8.0f * scale),
        ImVec2(closeMax.x - 8.0f * scale, closeMax.y - 8.0f * scale),
        IM_COL32(120, 25, 20, 255), 1.6f * scale);
    draw->AddLine(
        ImVec2(closeMax.x - 8.0f * scale, closeMin.y + 8.0f * scale),
        ImVec2(closeMin.x + 8.0f * scale, closeMax.y - 8.0f * scale),
        IM_COL32(120, 25, 20, 255), 1.6f * scale);
  }
  if (ImGui::IsItemClicked()) glfwSetWindowShouldClose(window, 1);

  ImGui::SetCursorScreenPos(ImVec2(right - buttonSize * 2.4f, top));
  ImGui::InvisibleButton("##window-min", ImVec2(buttonSize, buttonSize));
  const bool hoverMin = ImGui::IsItemHovered();
  const ImVec2 minMin = ImGui::GetItemRectMin();
  const ImVec2 minMax = ImGui::GetItemRectMax();
  draw->AddCircleFilled(
      ImVec2((minMin.x + minMax.x) * 0.5f, (minMin.y + minMax.y) * 0.5f),
      buttonSize * 0.5f,
      hoverMin ? IM_COL32(255, 189, 46, 255) : IM_COL32(254, 188, 46, 235));
  if (hoverMin)
    draw->AddLine(
        ImVec2(minMin.x + 8.0f * scale, (minMin.y + minMax.y) * 0.5f),
        ImVec2(minMax.x - 8.0f * scale, (minMin.y + minMax.y) * 0.5f),
        IM_COL32(130, 85, 10, 255), 1.6f * scale);
  if (ImGui::IsItemClicked()) glfwIconifyWindow(window);

  if (!dragging && io.MouseDown[0] && ImGui::IsMouseClicked(0) &&
      io.MousePos.y < kTitleBarHeight * scale && io.MousePos.y >= 0 &&
      io.MousePos.x > 12.0f * scale &&
      io.MousePos.x < right - buttonSize * 3.2f) {
    dragging = true;
    glfwGetWindowPos(window, &gWindowStartX, &gWindowStartY);
    gDragStartX = io.MousePos.x;
    gDragStartY = io.MousePos.y;
  }
}

}  // namespace

int main(int argc, char** argv) {
  bool selfTest = false;
  bool demo = false;
  std::string forcedScreen;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--self-test" || arg == "--selftest")
      selfTest = true;
    else if (arg == "--demo")
      demo = true;
    else if (arg.rfind("--screen=", 0) == 0)
      forcedScreen = arg.substr(9);
    else if (arg == "--version" || arg == "-v") {
      attachParentConsole();
      std::printf("校园作业分析桌面部署器 %s (build %s)\n",
                  mes::core::kVersion, mes::core::kBuild);
      return 0;
    } else if (arg == "--help" || arg == "-h") {
      attachParentConsole();
      std::printf(
          "校园作业分析 · 桌面部署器 %s (build %s)\n"
          "  --self-test    运行核心自检（CI/诊断用）\n"
          "  --demo         演示模式：不联网、不写入真实目录\n"
          "  --screen=NAME  直接打开指定页面（welcome/configure/installing/result）\n"
          "  --version      输出版本\n",
          mes::core::kVersion, mes::core::kBuild);
      return 0;
    }
  }

  // 以管理员/root 权限重新执行本程序时，只执行被请求的系统级动作（服务/防火墙），
  // 不创建窗口；由 engine.cpp 解析 --elevated-action=... 等参数。
  bool elevatedRequest = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg.rfind("--elevated-action=", 0) == 0) {
      elevatedRequest = true;
      break;
    }
  }
  if (elevatedRequest) {
    attachParentConsole();
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
      args.emplace_back(argv[i] ? argv[i] : "");
    return mes::core::runElevatedAction(args);
  }

  if (selfTest) {
    attachParentConsole();
    return mes::core::runSelfTest(true) ? 0 : 1;
  }

  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
#if defined(_WIN32)
    fatalMessageBox(L"无法初始化图形环境（GLFW）。请确认显卡驱动与桌面会话可用。");
#else
    std::fprintf(stderr, "无法初始化图形环境（GLFW）。\n");
#endif
    return 2;
  }
  mes::platform::configureWindowHints();
  GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight,
                                        "MES Homework 部署器", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
#if defined(_WIN32)
    fatalMessageBox(L"无法创建窗口。请确认桌面会话、显卡驱动与 OpenGL 可用。");
#else
    std::fprintf(stderr, "无法创建窗口，请确认桌面与 OpenGL 驱动可用。\n");
#endif
    return 3;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  mes::platform::styleWindow(window);
  mes::platform::setWindowIconIfAvailable(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;

  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale(window, &xscale, &yscale);
  const float scale = (xscale > 0.5f && xscale < 3.0f) ? xscale : 1.0f;

  mes::ui::loadFonts(scale);
  mes::ui::applyStyle();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  mes::AppState state;
  mes::appInit(state, demo);
  if (forcedScreen == "configure")
    state.screen = mes::Screen::Configure;
  else if (forcedScreen == "installing")
    state.screen = mes::Screen::Installing;
  else if (forcedScreen == "result")
    state.screen = mes::Screen::Result;
  else if (forcedScreen == "about")
    state.screen = mes::Screen::About;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
      glfwWaitEventsTimeout(0.2);
      continue;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (fbWidth > 0 && fbHeight > 0) {
      glViewport(0, 0, fbWidth, fbHeight);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
    }

    mes::ui::drawBackground();
    mes::ui::drawTopBar(state);
    switch (state.screen) {
      case mes::Screen::Welcome: mes::ui::drawWelcome(state); break;
      case mes::Screen::Configure: mes::ui::drawConfigure(state); break;
      case mes::Screen::Installing: mes::ui::drawInstalling(state); break;
      case mes::Screen::Result: mes::ui::drawResult(state); break;
      case mes::Screen::About: mes::ui::drawAbout(state, true); break;
    }
    mes::ui::drawFooter(state);
    mes::ui::drawToast(state);
    drawWindowControls(window, scale, gDragging);

    if (gDragging) {
      double x = 0.0, y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      glfwSetWindowPos(window,
                       gWindowStartX + static_cast<int>(x - gDragStartX),
                       gWindowStartY + static_cast<int>(y - gDragStartY));
      if (!ImGui::IsMouseDown(0)) gDragging = false;
    }

    ImGui::Render();
    if (fbWidth > 0 && fbHeight > 0) {
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
    }
    mes::appFrame(state);
  }

  mes::appShutdown(state);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
