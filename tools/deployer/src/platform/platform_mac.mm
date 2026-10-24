// 校园作业分析 · 桌面部署器 —— macOS 专有平台逻辑（字体 / Cocoa 毛玻璃）。
// 本文件仅由主目标编译（CMake 中 enable_language(OBJCXX)）。
#include "platform/platform.h"

#if defined(__APPLE__)

#define GLFW_EXPOSE_NATIVE_COCOA
#import <QuartzCore/QuartzCore.h>  // CALayer（NSView.layer 的圆角属性）
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace mes::platform {
namespace {

struct FontEntry {
  const char* path;
  int index;
};

// 顺序即优先级：CJK 字体在前，Menlo 等宽字体留给日志面板。
const FontEntry kFontEntries[] = {
    {"/System/Library/Fonts/PingFang.ttc", 0},
    {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0},
    {"/System/Library/Fonts/STHeiti Light.ttc", 0},
    {"/System/Library/Fonts/STHeiti Medium.ttc", 0},
    {"/Library/Fonts/Arial Unicode.ttf", 0},
    {"/System/Library/Fonts/Supplemental/Songti.ttc", 0},
    {"/System/Library/Fonts/SFNS.ttf", 0},
    {"/System/Library/Fonts/Supplemental/Arial.ttf", 0},
    {"/System/Library/Fonts/Menlo.ttc", 0},
};

}  // namespace

std::vector<FontCandidate> fontCandidates() {
  std::vector<FontCandidate> candidates;
  for (const FontEntry& entry : kFontEntries) {
    if (fileExists(entry.path))
      candidates.push_back(FontCandidate{entry.path, entry.index});
  }
  if (candidates.empty()) {
    candidates.push_back(FontCandidate{"/System/Library/Fonts/PingFang.ttc", 0});
  }
  return candidates;
}

std::string preferredUiFont() {
  const std::vector<FontCandidate> candidates = fontCandidates();
  return candidates.empty() ? std::string() : candidates.front().path;
}

void styleWindow(GLFWwindow* window) {
  if (!window) return;
  id cocoaWindow = glfwGetCocoaWindow(window);
  if (!cocoaWindow) return;
  NSWindow* nsWindow = (NSWindow*)cocoaWindow;
  @try {
    // 无边框窗口：透明标题栏 + 全尺寸内容 + 透明背景，圆角与阴影由窗口层提供。
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];
    nsWindow.hasShadow = YES;
    nsWindow.movableByWindowBackground = NO;
    nsWindow.styleMask =
        nsWindow.styleMask | NSWindowStyleMaskFullSizeContentView;

    NSView* contentView = [nsWindow contentView];
    if (!contentView) return;
    contentView.wantsLayer = YES;
    contentView.layer.cornerRadius = 12.0;
    contentView.layer.masksToBounds = YES;

    // 系统级毛玻璃：把 NSVisualEffectView 作为窗口内容容器，原 GL 视图
    // （GLFW 的 contentView）作为其子视图，因此毛玻璃位于 GL 内容后方。
    // 失败时静默降级为纯透明窗口（GL alpha 依然可见桌面）。
    Class effectClass = NSClassFromString(@"NSVisualEffectView");
    if (effectClass) {
      NSVisualEffectView* effect =
          [[effectClass alloc] initWithFrame:contentView.frame];
      if (effect) {
        effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        if (@available(macOS 10.14, *)) {
          effect.material = NSVisualEffectMaterialUnderWindowBackground;
          effect.appearance =
              [NSAppearance appearanceNamed:NSAppearanceNameVibrantLight];
        } else {
          effect.material = NSVisualEffectMaterialLight;
        }
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateActive;
        effect.wantsLayer = YES;
        effect.layer.cornerRadius = 12.0;
        effect.layer.masksToBounds = YES;
        contentView.autoresizingMask =
            NSViewWidthSizable | NSViewHeightSizable;
        [effect addSubview:contentView];
        [nsWindow setContentView:effect];
#if !__has_feature(objc_arc)
        [effect release];
#endif
      }
    }
  } @catch (NSException* exception) {
    (void)exception;
    // 静默降级：保留透明无边框窗口，不影响 GLFW/ImGui 渲染。
  }
}

}  // namespace mes::platform

#endif  // __APPLE__
