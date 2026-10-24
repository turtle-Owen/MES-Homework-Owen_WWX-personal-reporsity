// 轻量组件库：所有控件用 ImDrawList 自绘，保持 Apple 风格一致。
#pragma once

#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace mes::ui {

struct Rect {
  ImVec2 min;
  ImVec2 max;
  ImVec2 size() const { return ImVec2(max.x - min.x, max.y - min.y); }
  ImVec2 center() const {
    return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  }
};

// ---------------------------------------------------------------- 布局辅助
// 绝对定位布局：控件以当前 ImGui 光标位置为左上角，绘制后手动推进光标。
void setLayoutWidth(float width);
float layoutWidth();
// 阻止页面控件接收输入（模态弹层打开时使用），仍会绘制。
void setInteractionBlocked(bool blocked);
bool interactionBlocked();
void modalScrim(float alpha = 0.26f);
std::string elideText(const std::string& text, float maxWidth,
                      ImFont* font = nullptr);

// ---------------------------------------------------------------- 矢量图标
enum class Icon {
  None,
  House,
  Server,
  Check,
  Cross,
  ArrowRight,
  ChevronLeft,
  Folder,
  Globe,
  Info,
  Warning,
  Download,
  Refresh,
  Play,
  Stop,
  Trash,
  Copy,
  External,
  Sparkle,
  Shield,
  Clock,
  Lock,
  Terminal,
  Cpu,
  Minus,
};
void paintIcon(ImDrawList* drawList, const ImVec2& center, float size, Icon icon,
               ImU32 color, float thickness = 1.6f);
// 渐变圆形图标底座（卡片/结果页大图标）。
void iconBadge(ImDrawList* drawList, const ImVec2& center, float radius,
               Icon icon, ImU32 from, ImU32 to, float iconScale = 0.52f);

// ---------------------------------------------------------------- 基础绘制
// 在当前位置开始一块玻璃卡片，返回内容区域（已扣除 padding）。
Rect card(const char* id, const ImVec2& size, float rounding = 20.0f,
          bool strong = false);
void cardHeader(const char* eyebrow, const char* title, const char* desc = nullptr);

bool primaryButton(const char* label, const ImVec2& size = ImVec2(0, 44),
                   bool disabled = false);
bool secondaryButton(const char* label, const ImVec2& size = ImVec2(0, 40),
                     bool disabled = false);
bool quietButton(const char* label, const ImVec2& size = ImVec2(0, 34),
                 bool disabled = false);
bool dangerButton(const char* label, const ImVec2& size = ImVec2(0, 40),
                  bool disabled = false);
// 带图标的按钮；primary=true 为主按钮，danger=true 为危险样式。
bool actionButton(const char* id, Icon icon, const char* label,
                  const ImVec2& size, bool primary = false,
                  bool danger = false);
bool iconButton(const char* id, const char* glyph, const ImVec2& size,
                const char* tooltip = nullptr);
// 文字链接（可点击），自动省略过长文本。
bool linkText(const char* id, const std::string& text, float maxWidth,
              const char* tooltip = nullptr);

// 大卡片选择（本地版 / 服务器版）。glyph 取 "local" / "server" 等语义值。
bool selectCard(const char* id, const char* title, const char* tagline,
                const char* description, bool selected, float height,
                const char* glyph);

// 开关；返回是否被点击。
bool toggle(const char* id, bool* value, bool disabled = false);
// iOS 风格分段控件；返回是否改变。
bool segmented(const char* id, const char* const* labels, int count,
               int* index);
bool checkbox(const char* id, bool* value, const char* label);

void progressBar(float fraction, const ImVec2& size);
void statusDot(ImU32 color, float radius = 4.0f);
void badge(const char* text, ImU32 background, ImU32 foreground);
void sectionTitle(const char* text, float topGap = 0.0f);
void bodyText(const char* text, ImU32 color = 0, bool wrap = true);
void hint(const char* text);
void divider(float topGap = 8.0f, float bottomGap = 8.0f);
void spacer(float height);
void labelValue(const char* label, const std::string& value,
                const char* monoPrefix = nullptr);

// 输入框，返回是否改变。
bool textField(const char* id, std::string* value, const char* hint,
               float width = 0.0f, bool numeric = false);

// 打开系统文件对话框（zip 安装包 / 目录）；返回空串表示取消。
std::string pickFile(const char* title, const std::vector<std::string>& extensions);
std::string pickFolder(const std::string& initialDir);

// 顶部提示条（失败/成功），带关闭按钮。
bool banner(const char* id, const std::string& text, bool error);

// 自绘顶部标题栏（无边框窗口）：返回点击的动作。
enum class TitleBarAction { None, Minimize, Close, Drag };
TitleBarAction titleBar(const std::string& title, const std::string& subtitle);

// 绘制旋转/呼吸的等待指示点。
void spinner(float radius, float thickness, float phase);

// 确认弹层：返回 0 = 仍在显示 / 无操作，1 = 确认，2 = 取消。
// open 由调用方持有；toggle 非空时附带一个勾选项。
int confirmDialog(const char* id, const char* title, const std::string& message,
                  const char* confirmLabel, const char* cancelLabel, bool danger,
                  bool* toggle = nullptr, const char* toggleLabel = nullptr);

}  // namespace mes::ui
