// 校园作业分析 · 桌面部署器 —— Linux 专有平台逻辑（字体 / 窗口外观）。
// 本文件仅由主目标编译（mes-deployer-test 不引用这里的符号）。
#include "platform/platform.h"

#if defined(__linux__)

namespace mes::platform {
namespace {

struct FontEntry {
  const char* path;
  int index;
};

// 顺序即优先级：CJK 字体在前，最后是 DejaVu 兜底。
const FontEntry kFontEntries[] = {
    // Noto Sans CJK（Debian/Ubuntu/Fedora/Arch 的常见位置与文件名）
    {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", 0},
    {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0},
    {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0},
    {"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf", 0},
    {"/usr/share/fonts/truetype/arphic/uming.ttc", 0},
    // 文泉驿
    {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0},
    {"/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc", 0},
    {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 0},
    // 其它常见中文回退
    {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0},
    {"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc", 0},
    // 无 CJK 时的通用拉丁字体（保证 UI 至少能加载字体）
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0},
    {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 0},
};

}  // namespace

std::vector<FontCandidate> fontCandidates() {
  std::vector<FontCandidate> candidates;
  for (const FontEntry& entry : kFontEntries) {
    if (fileExists(entry.path))
      candidates.push_back(FontCandidate{entry.path, entry.index});
  }
  if (candidates.empty()) {
    // 约定路径：UI 尝试加载失败后会回退 ImGui 默认字体，不会崩溃。
    candidates.push_back(FontCandidate{
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0});
    candidates.push_back(
        FontCandidate{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0});
  }
  return candidates;
}

std::string preferredUiFont() {
  const std::vector<FontCandidate> candidates = fontCandidates();
  return candidates.empty() ? std::string() : candidates.front().path;
}

void styleWindow(GLFWwindow* /*window*/) {
  // Linux 下毛玻璃由「GLFW 透明 framebuffer + GL alpha 清屏 + 桌面合成器」
  // 共同实现；ImGui 自绘半透明卡片提供玻璃观感。不需要额外的原生调用，
  // 在没有合成器的环境（如纯 X11 无 compositor）也保持可用。
}

}  // namespace mes::platform

#endif  // __linux__
